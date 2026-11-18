#ifndef TELEMETRY_PAYLOAD_H
#define TELEMETRY_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry.h"

/*
 * BLE 实时数据 Characteristic 的固定长度载荷定义。
 * 多字节整数统一按小端序编码，温度/湿度仍使用 0.01 单位，避免浮点传输。
 */
#define TELEMETRY_BLE_PAYLOAD_VERSION       (1U)
#define TELEMETRY_BLE_PAYLOAD_SIZE          (12U)

/* Wi-Fi/MQTT 上传 JSON 的调用方输出缓冲区建议最小长度。 */
#define TELEMETRY_MQTT_JSON_BUFFER_SIZE     (160U)

/*
 * 将有效测量数据编码为 BLE Notify 使用的 12 字节载荷。
 * 数据格式：version(1) + flags(1) + temperature(2) + humidity(2)
 *         + battery(2) + sample_sequence(4)。
 */
bool telemetry_encode_ble_payload(telemetry_snapshot_t const * p_snapshot,
                                  uint8_t *                    p_payload,
                                  size_t                       payload_size);

/*
 * 将有效测量数据编码为 MQTT 消息体 JSON。
 * 设备 ID 应放在 MQTT Topic 中，服务器接收时间由云端记录，因此此函数不写入两者。
 * p_written_length 返回 JSON 正文长度，不包含字符串结尾的 '\0'。
 */
bool telemetry_encode_mqtt_json(telemetry_snapshot_t const * p_snapshot,
                                 char *                       p_json,
                                 size_t                       json_size,
                                 size_t *                     p_written_length);

/* 无板阶段使用固定输入验证 BLE 和 MQTT 编码结果。 */
bool telemetry_payload_self_test(void);

#endif /* TELEMETRY_PAYLOAD_H */
