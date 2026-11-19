#include "wifi_provisioning.h"

#include <string.h>

/* BEGIN 载荷：[transaction_id LE(2)] [ssid_length(1)] [password_length(1)]。 */
#define WIFI_PROVISIONING_BEGIN_PAYLOAD_SIZE      (4U)

/* CHUNK 固定头：[transaction_id LE(2)] [offset(1)] [chunk_length(1)] [data...]。 */
#define WIFI_PROVISIONING_CHUNK_HEADER_SIZE       (4U)

/* COMMIT/ABORT 载荷仅携带 transaction_id。 */
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

/* 作用：从 BLE 配网命令的载荷中读取小端事务 ID。 */
static uint16_t wifi_provisioning_read_u16_le(uint8_t const * p_data)
{
    return (uint16_t) p_data[0] | ((uint16_t) p_data[1] << 8U);
}

/* 即使编译器优化，也要逐字节清除暂存的敏感凭据。 */
/* 作用：逐字节清除 RAM 中暂存的 Wi-Fi 密码，避免敏感信息残留。 */
static void wifi_provisioning_clear_sensitive_data(void)
{
    volatile uint8_t * p_data = (volatile uint8_t *) &g_wifi_provisioning.credentials;
    size_t             index;

    for (index = 0U; index < sizeof(g_wifi_provisioning.credentials); ++index)
    {
        p_data[index] = 0U;
    }
}

/* 作用：结束当前配网事务并清除事务号、长度计数和暂存凭据。 */
static void wifi_provisioning_reset_transaction(void)
{
    wifi_provisioning_clear_sensitive_data();
    g_wifi_provisioning.transaction_id = 0U;
    g_wifi_provisioning.received_length = 0U;
    g_wifi_provisioning.expected_length = 0U;
    g_wifi_provisioning.active = false;
}

/* 作用：校验仅携带事务 ID 的 COMMIT 或 ABORT 命令载荷。 */
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

/* 作用：处理配网 BEGIN 命令，校验长度并创建新的凭据接收事务。 */
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

    wifi_provisioning_reset_transaction();
    g_wifi_provisioning.transaction_id = transaction_id;
    g_wifi_provisioning.credentials.ssid_length = ssid_length;
    g_wifi_provisioning.credentials.password_length = password_length;
    g_wifi_provisioning.expected_length = (uint8_t) (ssid_length + password_length);
    g_wifi_provisioning.active = true;

    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用：按顺序接收 SSID 和密码分片，防止乱序、越界或跨事务写入。 */
static ble_protocol_reply_status_t wifi_provisioning_write_chunk(ble_protocol_command_t const * p_command)
{
    uint16_t transaction_id;
    uint8_t  offset;
    uint8_t  chunk_length;
    uint8_t  index;

    if ((!g_wifi_provisioning.active) || (NULL == p_command->p_payload) ||
        (p_command->payload_length < WIFI_PROVISIONING_CHUNK_HEADER_SIZE))
    {
        return BLE_PROTOCOL_REPLY_NOT_READY;
    }

    transaction_id = wifi_provisioning_read_u16_le(p_command->p_payload);
    offset = p_command->p_payload[2];
    chunk_length = p_command->p_payload[3];

    if ((transaction_id != g_wifi_provisioning.transaction_id) || (0U == chunk_length) ||
        (p_command->payload_length != (WIFI_PROVISIONING_CHUNK_HEADER_SIZE + chunk_length)) ||
        (offset != g_wifi_provisioning.received_length) ||
        (chunk_length > (g_wifi_provisioning.expected_length - g_wifi_provisioning.received_length)))
    {
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
    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用：确认凭据已完整接收，并调用 Wi-Fi 服务安全保存和发起连接。 */
static ble_protocol_reply_status_t wifi_provisioning_commit(ble_protocol_command_t const * p_command)
{
    uint16_t                     transaction_id;
    ble_protocol_reply_status_t  result;

    if (!wifi_provisioning_has_transaction(p_command->p_payload,
                                           p_command->payload_length,
                                           &transaction_id) ||
        !g_wifi_provisioning.active ||
        (transaction_id != g_wifi_provisioning.transaction_id) ||
        (g_wifi_provisioning.received_length != g_wifi_provisioning.expected_length))
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    if (NULL == g_wifi_provisioning.commit_callback)
    {
        return BLE_PROTOCOL_REPLY_NOT_READY;
    }

    result = g_wifi_provisioning.commit_callback(&g_wifi_provisioning.credentials);
    wifi_provisioning_reset_transaction();
    return result;
}

/* 作用：初始化 BLE 配网事务服务，并注册凭据提交回调。 */
void wifi_provisioning_init(wifi_provisioning_commit_callback_t commit_callback)
{
    g_wifi_provisioning.commit_callback = commit_callback;
    wifi_provisioning_reset_transaction();
}

/* 作用：仅在已加密、已绑定的 BLE 链路上分发配网相关命令。 */
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
                                                       &transaction_id) ||
                    !g_wifi_provisioning.active ||
                    (transaction_id != g_wifi_provisioning.transaction_id))
                {
                    return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
                }

                wifi_provisioning_reset_transaction();
                return BLE_PROTOCOL_REPLY_OK;
            }

        default:
            return BLE_PROTOCOL_REPLY_UNSUPPORTED_COMMAND;
    }
}

/* 作用：主动取消当前配网事务，并从内存中擦除未提交的凭据。 */
void wifi_provisioning_abort(void)
{
    wifi_provisioning_reset_transaction();
}

/* 作用：配网自检使用的模拟提交回调，验证收到的 SSID 和密码内容。 */
static ble_protocol_reply_status_t wifi_provisioning_test_commit(wifi_credentials_t const * p_credentials)
{
    static uint8_t const expected_ssid[] = "HomeWiFi";
    static uint8_t const expected_password[] = "12345678";

    if ((NULL == p_credentials) ||
        (sizeof(expected_ssid) - 1U != p_credentials->ssid_length) ||
        (sizeof(expected_password) - 1U != p_credentials->password_length) ||
        (0 != memcmp(expected_ssid, p_credentials->ssid, p_credentials->ssid_length)) ||
        (0 != memcmp(expected_password, p_credentials->password, p_credentials->password_length)))
    {
        return BLE_PROTOCOL_REPLY_FAILED;
    }

    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用：验证配网权限控制、BEGIN/CHUNK/COMMIT 事务和凭据拼接流程。 */
bool wifi_provisioning_self_test(void)
{
    static uint8_t const begin_payload[] = {0x34U, 0x12U, 0x08U, 0x08U};
    static uint8_t const chunk_payload[] =
    {
        0x34U, 0x12U, 0x00U, 0x10U,
        'H', 'o', 'm', 'e', 'W', 'i', 'F', 'i', '1', '2', '3', '4', '5', '6', '7', '8',
    };
    static uint8_t const commit_payload[] = {0x34U, 0x12U};
    ble_protocol_command_t command;

    wifi_provisioning_init(wifi_provisioning_test_commit);

    command.command_code = BLE_PROTOCOL_COMMAND_WIFI_PROVISION_BEGIN;
    command.payload_length = sizeof(begin_payload);
    command.p_payload = begin_payload;
    if (BLE_PROTOCOL_REPLY_NOT_AUTHORIZED == wifi_provisioning_handle_command(&command, false))
    {
        /* 继续验证已授权路径。 */
    }
    else
    {
        return false;
    }

    if (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_handle_command(&command, true))
    {
        return false;
    }

    command.command_code = BLE_PROTOCOL_COMMAND_WIFI_PROVISION_CHUNK;
    command.payload_length = sizeof(chunk_payload);
    command.p_payload = chunk_payload;
    if (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_handle_command(&command, true))
    {
        return false;
    }

    command.command_code = BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT;
    command.payload_length = sizeof(commit_payload);
    command.p_payload = commit_payload;
    if (BLE_PROTOCOL_REPLY_OK != wifi_provisioning_handle_command(&command, true))
    {
        return false;
    }

    wifi_provisioning_abort();
    return true;
}
