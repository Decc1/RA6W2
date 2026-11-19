#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * HYGRO-RA6W2 的自定义BLE GATT 服务定义。
 *
 * UUID 目前是项目草案。App、云端和嵌入式三方联调前，必须将它们写入
 * 《通信接口控制文档》的正式版本；一旦量产，不可随意更改。
 */
#define BLE_PROTOCOL_SERVICE_UUID                 "A0E0F010-6D49-4D80-8F32-AB0D7D000000"
#define BLE_PROTOCOL_MEASUREMENT_UUID             "A0E0F011-6D49-4D80-8F32-AB0D7D000000"
#define BLE_PROTOCOL_CONTROL_UUID                 "A0E0F012-6D49-4D80-8F32-AB0D7D000000"
#define BLE_PROTOCOL_REPLY_UUID                   "A0E0F013-6D49-4D80-8F32-AB0D7D000000"

/* 协议版本和单帧限制，所有多字节整数使用小端序。 */
#define BLE_PROTOCOL_VERSION                      (1U)
#define BLE_PROTOCOL_MAX_CONTROL_PAYLOAD_SIZE     (128U)
#define BLE_PROTOCOL_CONTROL_HEADER_SIZE          (5U)
#define BLE_PROTOCOL_REPLY_HEADER_SIZE            (6U)

/* 手机写入“控制命令 Characteristic”时使用的命令码。 */
typedef enum e_ble_protocol_command_code
{
    BLE_PROTOCOL_COMMAND_GET_DEVICE_INFO        = 0x01U,
    BLE_PROTOCOL_COMMAND_GET_LATEST_MEASUREMENT = 0x02U,
    BLE_PROTOCOL_COMMAND_GET_HISTORY_PAGE       = 0x03U,

    /* BLE 配网命令：需要已加密、已绑定的 BLE 链路。 */
    BLE_PROTOCOL_COMMAND_WIFI_PROVISION_BEGIN  = 0x10U,
    BLE_PROTOCOL_COMMAND_WIFI_PROVISION_CHUNK  = 0x11U,
    BLE_PROTOCOL_COMMAND_WIFI_PROVISION_COMMIT = 0x12U,
    BLE_PROTOCOL_COMMAND_WIFI_PROVISION_ABORT  = 0x13U,
    BLE_PROTOCOL_COMMAND_CLEAR_WIFI_CREDENTIALS = 0x14U,

    /* 以下命令为第一版预留；没有接入相应模块前不得返回成功。 */
    BLE_PROTOCOL_COMMAND_START_OTA              = 0x20U,
    BLE_PROTOCOL_COMMAND_FACTORY_RESET          = 0x30U,
} ble_protocol_command_code_t;

/* 设备通过“命令结果 Characteristic”反馈给手机的执行状态。 */
typedef enum e_ble_protocol_reply_status
{
    BLE_PROTOCOL_REPLY_OK                  = 0x00U,
    BLE_PROTOCOL_REPLY_INVALID_FRAME       = 0x01U,
    BLE_PROTOCOL_REPLY_UNSUPPORTED_COMMAND = 0x02U,
    BLE_PROTOCOL_REPLY_INVALID_PAYLOAD     = 0x03U,
    BLE_PROTOCOL_REPLY_BUSY                = 0x04U,
    BLE_PROTOCOL_REPLY_NOT_READY           = 0x05U,
    BLE_PROTOCOL_REPLY_FAILED              = 0x06U,
    BLE_PROTOCOL_REPLY_NOT_AUTHORIZED      = 0x07U,
    BLE_PROTOCOL_REPLY_TIMEOUT             = 0x08U,
} ble_protocol_reply_status_t;

/* 对控制命令帧完成长度检查后的视图；payload 指向调用者持有的原始数据。 */
typedef struct st_ble_protocol_command
{
    uint8_t const * p_payload;
    uint16_t        command_id;
    uint8_t         command_code;
    uint8_t         payload_length;
} ble_protocol_command_t;

/*
 * 解析手机写入控制 Characteristic 的帧：
 * [0] version，[1] command_code，[2..3] command_id LE，[4] payload_length，[5..] payload。
 */
ble_protocol_reply_status_t ble_protocol_parse_control_write(uint8_t const *         p_data,
                                                              size_t                  data_length,
                                                              ble_protocol_command_t * p_command);

/*
 * 编码设备回复帧：
 * [0] version，[1] command_code，[2..3] command_id LE，[4] status，[5] payload_length，[6..] payload。
 * 返回 false 表示输出缓冲区不足或参数不合法。
 */
bool ble_protocol_encode_reply(uint8_t                    command_code,
                               uint16_t                   command_id,
                               ble_protocol_reply_status_t status,
                               uint8_t const *            p_payload,
                               uint8_t                     payload_length,
                               uint8_t *                   p_output,
                               size_t                      output_size,
                               size_t *                    p_written_length);

/* 固定输入的协议自检；无硬件时可验证帧结构和端序。 */
bool ble_protocol_self_test(void);

#endif /* BLE_PROTOCOL_H */
