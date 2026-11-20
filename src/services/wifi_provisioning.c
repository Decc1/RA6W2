#include "wifi_provisioning.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define WIFI_PROVISIONING_BEGIN_PAYLOAD_SIZE      (4U)
#define WIFI_PROVISIONING_CHUNK_HEADER_SIZE       (4U)
#define WIFI_PROVISIONING_TRANSACTION_ID_SIZE     (2U)

typedef struct st_wifi_provisioning_context
{
    wifi_provisioning_commit_callback_t commit_callback;
    wifi_credentials_t                   credentials;
    uint16_t                             transaction_id;
    uint8_t                              received_length;
    uint8_t                              expected_length;
    bool                                 active;
} wifi_provisioning_context_t;

static wifi_provisioning_context_t g_wifi_provisioning;

/* 作用: 从 BLE 配网命令载荷中读取小端事务 ID。 */
static uint16_t wifi_provisioning_read_u16_le(uint8_t const * p_data)
{
    return (uint16_t) p_data[0] | ((uint16_t) p_data[1] << 8U);
}

/* 作用: 使用 volatile 逐字节清除 Wi-Fi 凭据，避免编译器优化掉敏感数据擦除。 */
static void wifi_provisioning_clear_credentials(wifi_credentials_t * p_credentials)
{
    volatile uint8_t * p_data;
    size_t             index;

    if (NULL == p_credentials)
    {
        return;
    }

    p_data = (volatile uint8_t *) p_credentials;
    for (index = 0U; index < sizeof(*p_credentials); ++index)
    {
        p_data[index] = 0U;
    }
}

/* 作用: 在持有临界区时结束配网事务，并清除事务号、长度计数和暂存凭据。 */
static void wifi_provisioning_reset_transaction(void)
{
    wifi_provisioning_clear_credentials(&g_wifi_provisioning.credentials);
    g_wifi_provisioning.transaction_id = 0U;
    g_wifi_provisioning.received_length = 0U;
    g_wifi_provisioning.expected_length = 0U;
    g_wifi_provisioning.active = false;
}

/* 作用: 校验仅携带事务 ID 的 COMMIT 或 ABORT 命令载荷。 */
static bool wifi_provisioning_has_transaction(uint8_t const * p_payload,
                                              uint8_t         payload_length,
                                              uint16_t *      p_transaction_id)
{
    if ((NULL == p_payload) || (NULL == p_transaction_id) ||
        (WIFI_PROVISIONING_TRANSACTION_ID_SIZE != payload_length))
    {
        return false;
    }

    *p_transaction_id = wifi_provisioning_read_u16_le(p_payload);
    return true;
}

/* 作用: 处理 BEGIN 命令，校验长度并创建新的凭据接收事务。 */
static ble_protocol_reply_status_t wifi_provisioning_begin(ble_protocol_command_t const * p_command)
{
    uint16_t transaction_id;
    uint8_t  ssid_length;
    uint8_t  password_length;

    if ((NULL == p_command->p_payload) ||
        (WIFI_PROVISIONING_BEGIN_PAYLOAD_SIZE != p_command->payload_length))
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    transaction_id = wifi_provisioning_read_u16_le(p_command->p_payload);
    ssid_length = p_command->p_payload[2];
    password_length = p_command->p_payload[3];
    if ((0U == transaction_id) || (0U == ssid_length) ||
        (ssid_length > WIFI_PROVISIONING_MAX_SSID_LENGTH) ||
        (password_length < WIFI_PROVISIONING_MIN_PASSWORD_LENGTH) ||
        (password_length > WIFI_PROVISIONING_MAX_PASSWORD_LENGTH))
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    taskENTER_CRITICAL();
    wifi_provisioning_reset_transaction();
    g_wifi_provisioning.transaction_id = transaction_id;
    g_wifi_provisioning.credentials.ssid_length = ssid_length;
    g_wifi_provisioning.credentials.password_length = password_length;
    g_wifi_provisioning.expected_length = (uint8_t) (ssid_length + password_length);
    g_wifi_provisioning.active = true;
    taskEXIT_CRITICAL();
    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用: 按顺序接收 SSID 和密码分片，拒绝乱序、越界或跨事务写入。 */
static ble_protocol_reply_status_t wifi_provisioning_write_chunk(ble_protocol_command_t const * p_command)
{
    uint16_t transaction_id;
    uint8_t  offset;
    uint8_t  chunk_length;
    uint8_t  index;

    if ((NULL == p_command->p_payload) ||
        (p_command->payload_length < WIFI_PROVISIONING_CHUNK_HEADER_SIZE))
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    transaction_id = wifi_provisioning_read_u16_le(p_command->p_payload);
    offset = p_command->p_payload[2];
    chunk_length = p_command->p_payload[3];

    taskENTER_CRITICAL();
    if (!g_wifi_provisioning.active)
    {
        taskEXIT_CRITICAL();
        return BLE_PROTOCOL_REPLY_NOT_READY;
    }
    if ((transaction_id != g_wifi_provisioning.transaction_id) || (0U == chunk_length) ||
        (p_command->payload_length != (WIFI_PROVISIONING_CHUNK_HEADER_SIZE + chunk_length)) ||
        (offset != g_wifi_provisioning.received_length) ||
        (chunk_length > (g_wifi_provisioning.expected_length - g_wifi_provisioning.received_length)))
    {
        taskEXIT_CRITICAL();
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    for (index = 0U; index < chunk_length; ++index)
    {
        uint8_t const destination = (uint8_t) (offset + index);
        uint8_t const value = p_command->p_payload[WIFI_PROVISIONING_CHUNK_HEADER_SIZE + index];

        if (destination < g_wifi_provisioning.credentials.ssid_length)
        {
            g_wifi_provisioning.credentials.ssid[destination] = value;
        }
        else
        {
            g_wifi_provisioning.credentials.password[destination -
                                                      g_wifi_provisioning.credentials.ssid_length] = value;
        }
    }
    g_wifi_provisioning.received_length = (uint8_t) (g_wifi_provisioning.received_length + chunk_length);
    taskEXIT_CRITICAL();
    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用: 确认凭据完整接收后，在临界区外调用 Wi-Fi 服务保存并发起连接。 */
static ble_protocol_reply_status_t wifi_provisioning_commit(ble_protocol_command_t const * p_command)
{
    uint16_t                            transaction_id;
    wifi_provisioning_commit_callback_t commit_callback;
    wifi_credentials_t                  credentials;
    ble_protocol_reply_status_t         result;

    if (!wifi_provisioning_has_transaction(p_command->p_payload,
                                           p_command->payload_length,
                                           &transaction_id))
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    taskENTER_CRITICAL();
    if (!g_wifi_provisioning.active ||
        (transaction_id != g_wifi_provisioning.transaction_id) ||
        (g_wifi_provisioning.received_length != g_wifi_provisioning.expected_length))
    {
        taskEXIT_CRITICAL();
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }
    commit_callback = g_wifi_provisioning.commit_callback;
    credentials = g_wifi_provisioning.credentials;
    wifi_provisioning_reset_transaction();
    taskEXIT_CRITICAL();

    if (NULL == commit_callback)
    {
        wifi_provisioning_clear_credentials(&credentials);
        return BLE_PROTOCOL_REPLY_NOT_READY;
    }

    result = commit_callback(&credentials);
    wifi_provisioning_clear_credentials(&credentials);
    return result;
}

/* 作用: 初始化 BLE 配网事务服务，并注册凭据提交回调。 */
void wifi_provisioning_init(wifi_provisioning_commit_callback_t commit_callback)
{
    taskENTER_CRITICAL();
    (void) memset(&g_wifi_provisioning, 0, sizeof(g_wifi_provisioning));
    g_wifi_provisioning.commit_callback = commit_callback;
    taskEXIT_CRITICAL();
}

/* 作用: 仅在已加密、已绑定的 BLE 链路上分发配网相关命令。 */
ble_protocol_reply_status_t wifi_provisioning_handle_command(ble_protocol_command_t const * p_command,
                                                              bool                         encrypted_and_bonded)
{
    if (NULL == p_command)
    {
        return BLE_PROTOCOL_REPLY_INVALID_FRAME;
    }
    if (!encrypted_and_bonded)
    {
        return BLE_PROTOCOL_REPLY_NOT_AUTHORIZED;
    }

    switch (p_command->command_code)
    {
        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_BEGIN:
            return wifi_provisioning_begin(p_command);

        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_CHUNK:
            return wifi_provisioning_write_chunk(p_command);

        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT:
            return wifi_provisioning_commit(p_command);

        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_ABORT:
        {
            uint16_t transaction_id;

            if (!wifi_provisioning_has_transaction(p_command->p_payload,
                                                   p_command->payload_length,
                                                   &transaction_id))
            {
                return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
            }

            taskENTER_CRITICAL();
            if (!g_wifi_provisioning.active ||
                (transaction_id != g_wifi_provisioning.transaction_id))
            {
                taskEXIT_CRITICAL();
                return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
            }
            wifi_provisioning_reset_transaction();
            taskEXIT_CRITICAL();
            return BLE_PROTOCOL_REPLY_OK;
        }

        default:
            return BLE_PROTOCOL_REPLY_UNSUPPORTED_COMMAND;
    }
}

/* 作用: 主动取消当前配网事务，并从 RAM 中擦除未提交的凭据。 */
void wifi_provisioning_abort(void)
{
    taskENTER_CRITICAL();
    wifi_provisioning_reset_transaction();
    taskEXIT_CRITICAL();
}

static wifi_credentials_t g_wifi_provisioning_test_expected;
static uint32_t           g_wifi_provisioning_test_commit_count;

/* 作用: 配置配网自检期望收到的 SSID 和密码。 */
static void wifi_provisioning_test_set_expected(uint8_t const * p_ssid,
                                                uint8_t         ssid_length,
                                                uint8_t const * p_password,
                                                uint8_t         password_length)
{
    (void) memset(&g_wifi_provisioning_test_expected, 0, sizeof(g_wifi_provisioning_test_expected));
    g_wifi_provisioning_test_expected.ssid_length = ssid_length;
    g_wifi_provisioning_test_expected.password_length = password_length;
    (void) memcpy(g_wifi_provisioning_test_expected.ssid, p_ssid, ssid_length);
    (void) memcpy(g_wifi_provisioning_test_expected.password, p_password, password_length);
}

/* 作用: 模拟凭据提交，并核对状态机拼接出的 SSID 和密码。 */
static ble_protocol_reply_status_t wifi_provisioning_test_commit(wifi_credentials_t const * p_credentials)
{
    g_wifi_provisioning_test_commit_count++;

    if ((NULL == p_credentials) ||
        (g_wifi_provisioning_test_expected.ssid_length != p_credentials->ssid_length) ||
        (g_wifi_provisioning_test_expected.password_length != p_credentials->password_length) ||
        (0 != memcmp(g_wifi_provisioning_test_expected.ssid,
                     p_credentials->ssid,
                     p_credentials->ssid_length)) ||
        (0 != memcmp(g_wifi_provisioning_test_expected.password,
                     p_credentials->password,
                     p_credentials->password_length)))
    {
        return BLE_PROTOCOL_REPLY_FAILED;
    }

    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用: 构造并执行一条配网命令，减少异常路径测试中的重复代码。 */
static ble_protocol_reply_status_t wifi_provisioning_test_send(uint8_t         command_code,
                                                               uint8_t const * p_payload,
                                                               uint8_t         payload_length,
                                                               bool            secure_link)
{
    ble_protocol_command_t command =
    {
        .command_code = command_code,
        .command_id = 1U,
        .payload_length = payload_length,
        .p_payload = p_payload,
    };

    return wifi_provisioning_handle_command(&command, secure_link);
}

/* 作用: 构造并执行 BEGIN 命令。 */
static ble_protocol_reply_status_t wifi_provisioning_test_begin(uint16_t transaction_id,
                                                                uint8_t  ssid_length,
                                                                uint8_t  password_length,
                                                                bool     secure_link)
{
    uint8_t const payload[WIFI_PROVISIONING_BEGIN_PAYLOAD_SIZE] =
    {
        (uint8_t) transaction_id,
        (uint8_t) (transaction_id >> 8U),
        ssid_length,
        password_length,
    };

    return wifi_provisioning_test_send(BLE_PROTOCOL_COMMAND_WIFI_PROVISION_BEGIN,
                                       payload,
                                       sizeof(payload),
                                       secure_link);
}

/* 作用: 构造并执行 CHUNK 命令。 */
static ble_protocol_reply_status_t wifi_provisioning_test_chunk(uint16_t        transaction_id,
                                                                uint8_t         offset,
                                                                uint8_t const * p_data,
                                                                uint8_t         data_length)
{
    uint8_t payload[WIFI_PROVISIONING_CHUNK_HEADER_SIZE +
                    WIFI_PROVISIONING_MAX_SSID_LENGTH +
                    WIFI_PROVISIONING_MAX_PASSWORD_LENGTH];

    if ((NULL == p_data) ||
        (data_length > (WIFI_PROVISIONING_MAX_SSID_LENGTH +
                        WIFI_PROVISIONING_MAX_PASSWORD_LENGTH)))
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    payload[0] = (uint8_t) transaction_id;
    payload[1] = (uint8_t) (transaction_id >> 8U);
    payload[2] = offset;
    payload[3] = data_length;
    (void) memcpy(&payload[WIFI_PROVISIONING_CHUNK_HEADER_SIZE], p_data, data_length);

    return wifi_provisioning_test_send(BLE_PROTOCOL_COMMAND_WIFI_PROVISION_CHUNK,
                                       payload,
                                       (uint8_t) (WIFI_PROVISIONING_CHUNK_HEADER_SIZE + data_length),
                                       true);
}

/* 作用: 构造并执行只携带事务号的 COMMIT 或 ABORT 命令。 */
static ble_protocol_reply_status_t wifi_provisioning_test_transaction_command(uint8_t  command_code,
                                                                               uint16_t transaction_id)
{
    uint8_t const payload[WIFI_PROVISIONING_TRANSACTION_ID_SIZE] =
    {
        (uint8_t) transaction_id,
        (uint8_t) (transaction_id >> 8U),
    };

    return wifi_provisioning_test_send(command_code, payload, sizeof(payload), true);
}

/* 作用: 验证未加密拒配及 SSID、密码长度边界。 */
static bool wifi_provisioning_test_security_and_lengths(void)
{
    if ((BLE_PROTOCOL_REPLY_INVALID_FRAME != wifi_provisioning_handle_command(NULL, true)) ||
        (BLE_PROTOCOL_REPLY_NOT_AUTHORIZED != wifi_provisioning_test_begin(1U, 1U, 8U, false)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_begin(0U, 1U, 8U, true)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_begin(1U, 0U, 8U, true)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_begin(
            1U, WIFI_PROVISIONING_MAX_SSID_LENGTH + 1U, 8U, true)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_begin(
            1U, 1U, WIFI_PROVISIONING_MIN_PASSWORD_LENGTH - 1U, true)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_begin(
            1U, 1U, WIFI_PROVISIONING_MAX_PASSWORD_LENGTH + 1U, true)))
    {
        return false;
    }

    return 0U == g_wifi_provisioning_test_commit_count;
}

/* 作用: 验证分片乱序、重复、跨事务、超长和不完整提交均被拒绝。 */
static bool wifi_provisioning_test_transaction_errors(void)
{
    static uint8_t const ssid[] = {'H', 'o', 'm', 'e', 'W', 'i', 'F', 'i'};
    static uint8_t const password[] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    uint8_t oversized_data[17U] = {0};

    wifi_provisioning_test_set_expected(ssid, sizeof(ssid), password, sizeof(password));
    if ((BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_begin(0x1234U,
                                                                sizeof(ssid),
                                                                sizeof(password),
                                                                true)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_chunk(
            0x1234U, 1U, ssid, sizeof(ssid))) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_chunk(
            0x5678U, 0U, ssid, sizeof(ssid))) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_chunk(
            0x1234U, 0U, oversized_data, sizeof(oversized_data))) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_chunk(
            0x1234U, 0U, ssid, sizeof(ssid))) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_chunk(
            0x1234U, 0U, ssid, sizeof(ssid))) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT, 0x1234U)) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_chunk(
            0x1234U, sizeof(ssid), password, sizeof(password))) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT, 0x5678U)) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT, 0x1234U)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT, 0x1234U)))
    {
        return false;
    }

    return 1U == g_wifi_provisioning_test_commit_count;
}

/* 作用: 验证最大合法 SSID、密码可分片接收，并验证 ABORT 清理事务。 */
static bool wifi_provisioning_test_maximum_and_abort(void)
{
    uint8_t credentials[WIFI_PROVISIONING_MAX_SSID_LENGTH +
                        WIFI_PROVISIONING_MAX_PASSWORD_LENGTH];
    uint8_t index;

    for (index = 0U; index < WIFI_PROVISIONING_MAX_SSID_LENGTH; ++index)
    {
        credentials[index] = 'S';
    }
    for (; index < sizeof(credentials); ++index)
    {
        credentials[index] = 'P';
    }

    wifi_provisioning_test_set_expected(credentials,
                                        WIFI_PROVISIONING_MAX_SSID_LENGTH,
                                        &credentials[WIFI_PROVISIONING_MAX_SSID_LENGTH],
                                        WIFI_PROVISIONING_MAX_PASSWORD_LENGTH);
    if ((BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_begin(
            0x2468U,
            WIFI_PROVISIONING_MAX_SSID_LENGTH,
            WIFI_PROVISIONING_MAX_PASSWORD_LENGTH,
            true)) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_chunk(0x2468U, 0U, credentials, 60U)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_chunk(
            0x2468U, 59U, &credentials[59U], 36U)) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_chunk(
            0x2468U, 60U, &credentials[60U], (uint8_t) (sizeof(credentials) - 60U))) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT, 0x2468U)) ||
        (2U != g_wifi_provisioning_test_commit_count) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_begin(0x1357U, 1U, 8U, true)) ||
        (BLE_PROTOCOL_REPLY_INVALID_PAYLOAD != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_ABORT, 0x2468U)) ||
        (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_test_transaction_command(
            BLE_PROTOCOL_COMMAND_WIFI_PROVISION_ABORT, 0x1357U)) ||
        (BLE_PROTOCOL_REPLY_NOT_READY != wifi_provisioning_test_chunk(
            0x1357U, 0U, credentials, 1U)))
    {
        return false;
    }

    return true;
}

/* 作用: 验证配网权限、长度边界、事务隔离、分片顺序和取消处理。 */
bool wifi_provisioning_self_test(void)
{
    g_wifi_provisioning_test_commit_count = 0U;
    wifi_provisioning_init(wifi_provisioning_test_commit);

    if (!wifi_provisioning_test_security_and_lengths() ||
        !wifi_provisioning_test_transaction_errors() ||
        !wifi_provisioning_test_maximum_and_abort())
    {
        wifi_provisioning_abort();
        return false;
    }

    wifi_provisioning_abort();
    return true;
}
