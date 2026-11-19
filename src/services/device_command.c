#include "device_command.h"

#include <string.h>

#include "ble_protocol.h"
#include "telemetry.h"
#include "telemetry_payload.h"
#include "history_payload.h"
#include "history_storage.h"
#include "wifi_provisioning.h"
#include "wifi_service.h"

/* 设备信息载荷：[协议版本(1)] [固件主/次/修订(3)] [型号字符串长度(1)] [型号 ASCII]。 */
#define DEVICE_COMMAND_FIRMWARE_MAJOR      (0U)
#define DEVICE_COMMAND_FIRMWARE_MINOR      (1U)
#define DEVICE_COMMAND_FIRMWARE_PATCH      (0U)

static uint8_t const g_device_model[] = "HYGRO-RA6W2";

static ble_protocol_reply_status_t device_command_execute(ble_protocol_command_t const * p_command,
                                                           bool                          encrypted_and_bonded,
                                                           uint8_t *                     p_payload,
                                                           uint8_t *                     p_payload_length)
{
    telemetry_snapshot_t snapshot;

    if ((NULL == p_command) || (NULL == p_payload) || (NULL == p_payload_length))
    {
        return BLE_PROTOCOL_REPLY_INVALID_FRAME;
    }

    *p_payload_length = 0U;

    switch (p_command->command_code)
    {
        case BLE_PROTOCOL_COMMAND_GET_DEVICE_INFO:
            if (0U != p_command->payload_length)
            {
                return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
            }

            p_payload[0] = BLE_PROTOCOL_VERSION;
            p_payload[1] = DEVICE_COMMAND_FIRMWARE_MAJOR;
            p_payload[2] = DEVICE_COMMAND_FIRMWARE_MINOR;
            p_payload[3] = DEVICE_COMMAND_FIRMWARE_PATCH;
            p_payload[4] = (uint8_t) (sizeof(g_device_model) - 1U);
            (void) memcpy(&p_payload[5], g_device_model, sizeof(g_device_model) - 1U);
            *p_payload_length = (uint8_t) (5U + sizeof(g_device_model) - 1U);
            return BLE_PROTOCOL_REPLY_OK;

        case BLE_PROTOCOL_COMMAND_GET_LATEST_MEASUREMENT:
            if (0U != p_command->payload_length)
            {
                return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
            }

            if (!telemetry_get_snapshot(&snapshot) ||
                !telemetry_encode_ble_payload(&snapshot,
                                              p_payload,
                                              TELEMETRY_BLE_PAYLOAD_SIZE))
            {
                return BLE_PROTOCOL_REPLY_NOT_READY;
            }

            *p_payload_length = TELEMETRY_BLE_PAYLOAD_SIZE;
            return BLE_PROTOCOL_REPLY_OK;

        case BLE_PROTOCOL_COMMAND_GET_HISTORY_PAGE:
            if (3U != p_command->payload_length)
            {
                return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
            }

            {
                uint16_t const first_index = (uint16_t) p_command->p_payload[0] |
                                             ((uint16_t) p_command->p_payload[1] << 8U);
                size_t          page_length = 0U;

                if (!history_payload_encode_ble_page(first_index,
                                                     p_command->p_payload[2],
                                                     p_payload,
                                                     DEVICE_COMMAND_MAX_REPLY_SIZE - BLE_PROTOCOL_REPLY_HEADER_SIZE,
                                                     &page_length))
                {
                    return BLE_PROTOCOL_REPLY_FAILED;
                }

                *p_payload_length = (uint8_t) page_length;
                return BLE_PROTOCOL_REPLY_OK;
            }

        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_BEGIN:
        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_CHUNK:
        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT:
        case BLE_PROTOCOL_COMMAND_WIFI_PROVISION_ABORT:
            return wifi_provisioning_handle_command(p_command, encrypted_and_bonded);

        case BLE_PROTOCOL_COMMAND_CLEAR_WIFI_CREDENTIALS:
            /* 等加密存储模块落地后实现，当前绝不伪造“已清除”。 */
            return BLE_PROTOCOL_REPLY_NOT_READY;

        case BLE_PROTOCOL_COMMAND_START_OTA:
        case BLE_PROTOCOL_COMMAND_FACTORY_RESET:
        default:
            return BLE_PROTOCOL_REPLY_UNSUPPORTED_COMMAND;
    }
}

void device_command_init(void)
{
    /* 真实 Wi-Fi 服务准备好后，在此传入对应的硬件适配回调。 */
    wifi_service_init(NULL);
    wifi_provisioning_init(wifi_service_commit_credentials);
}

bool device_command_handle_ble_write(uint8_t const * p_request,
                                     size_t          request_length,
                                     bool            encrypted_and_bonded,
                                     uint8_t *       p_reply,
                                     size_t          reply_size,
                                     size_t *        p_reply_length)
{
    ble_protocol_command_t      command;
    ble_protocol_reply_status_t status;
    uint8_t                     payload[DEVICE_COMMAND_MAX_REPLY_SIZE - BLE_PROTOCOL_REPLY_HEADER_SIZE];
    uint8_t                     payload_length = 0U;
    uint8_t                     command_code = 0U;
    uint16_t                    command_id = 0U;

    status = ble_protocol_parse_control_write(p_request, request_length, &command);
    if (BLE_PROTOCOL_REPLY_OK == status)
    {
        command_code = command.command_code;
        command_id = command.command_id;
        status = device_command_execute(&command,
                                        encrypted_and_bonded,
                                        payload,
                                        &payload_length);
    }

    return ble_protocol_encode_reply(command_code,
                                     command_id,
                                     status,
                                     payload,
                                     payload_length,
                                     p_reply,
                                     reply_size,
                                     p_reply_length);
}

bool device_command_self_test(void)
{
    static uint8_t const request[] =
    {
        BLE_PROTOCOL_VERSION,
        BLE_PROTOCOL_COMMAND_GET_LATEST_MEASUREMENT,
        0x01U, 0x00U,
        0x00U,
    };
    static uint8_t const invalid_request[] = {BLE_PROTOCOL_VERSION, 0x02U};
    static uint8_t const history_request[] =
    {
        BLE_PROTOCOL_VERSION,
        BLE_PROTOCOL_COMMAND_GET_HISTORY_PAGE,
        0x02U, 0x00U,
        0x03U,
        0x00U, 0x00U, 0x01U,
    };
    hygro_measurement_t measurement = {0};
    history_record_t    history_record = {0};
    uint8_t             reply[DEVICE_COMMAND_MAX_REPLY_SIZE];
    size_t              reply_length = 0U;

    telemetry_init();
    measurement.temperature_centi_c = 2350;
    measurement.humidity_centi_rh = 4550U;
    measurement.battery_mv = 3850U;
    measurement.sample_sequence = 1U;
    measurement.valid = true;
    telemetry_record_success(&measurement, 10000U);

    if (!device_command_handle_ble_write(request,
                                         sizeof(request),
                                         false,
                                         reply,
                                         sizeof(reply),
                                         &reply_length) ||
        (BLE_PROTOCOL_REPLY_HEADER_SIZE + TELEMETRY_BLE_PAYLOAD_SIZE != reply_length) ||
        (BLE_PROTOCOL_REPLY_OK != reply[4]))
    {
        return false;
    }

    if (!device_command_handle_ble_write(invalid_request,
                                         sizeof(invalid_request),
                                         false,
                                         reply,
                                         sizeof(reply),
                                         &reply_length) ||
        (BLE_PROTOCOL_REPLY_INVALID_FRAME != reply[4]))
    {
        return false;
    }

    /*
     * 验证 App 读取一页历史曲线的完整链路：
     * 控制 Characteristic Write → 命令分发 → RAM 历史库 → 回复 Characteristic。
     */
    history_storage_init();
    history_record.window_start_uptime_s       = 900U;
    history_record.first_sample_sequence       = 1U;
    history_record.temperature_average_centi_c = 2350;
    history_record.temperature_min_centi_c     = 2300;
    history_record.temperature_max_centi_c     = 2400;
    history_record.humidity_average_centi_rh   = 4550U;
    history_record.humidity_min_centi_rh       = 4400U;
    history_record.humidity_max_centi_rh       = 4600U;
    history_record.sample_count                = 90U;

    if (!history_storage_append(&history_record) ||
        !device_command_handle_ble_write(history_request,
                                         sizeof(history_request),
                                         false,
                                         reply,
                                         sizeof(reply),
                                         &reply_length) ||
        (BLE_PROTOCOL_REPLY_HEADER_SIZE + HISTORY_BLE_PAGE_HEADER_SIZE +
         HISTORY_BLE_RECORD_SIZE != reply_length) ||
        (BLE_PROTOCOL_COMMAND_GET_HISTORY_PAGE != reply[1]) ||
        (BLE_PROTOCOL_REPLY_OK != reply[4]) ||
        ((HISTORY_BLE_PAGE_HEADER_SIZE + HISTORY_BLE_RECORD_SIZE) != reply[5]) ||
        (1U != reply[6]) ||      /* 历史载荷版本 */
        (1U != reply[7]) ||      /* 当前历史总条数：低字节 */
        (0U != reply[8]) ||      /* 当前历史总条数：高字节 */
        (1U != reply[9]))        /* 本次返回一条 */
    {
        return false;
    }

    history_storage_clear();

    return true;
}
