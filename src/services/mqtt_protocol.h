#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#include "ble_protocol.h"
#include "telemetry.h"
#include "history_service.h"

/* MQTT Topic 的固定根路径；量产前由云端、App 与固件共同确认。 */
#define MQTT_PROTOCOL_TOPIC_ROOT              "hygro/v1"
#define MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH    (32U)
#define MQTT_PROTOCOL_TOPIC_BUFFER_SIZE       (96U)
#define MQTT_PROTOCOL_JSON_BUFFER_SIZE        (192U)

typedef enum e_mqtt_protocol_topic_type
{
    MQTT_PROTOCOL_TOPIC_TELEMETRY = 0,
    MQTT_PROTOCOL_TOPIC_HISTORY,
    MQTT_PROTOCOL_TOPIC_COMMAND,
    MQTT_PROTOCOL_TOPIC_REPLY,
} mqtt_protocol_topic_type_t;

/*
 * 生成 MQTT Topic：
 * hygro/v1/{device_id}/telemetry、command 或 reply。
 * device_id 仅允许字母、数字、下划线和连字符，避免 Topic 注入或 JSON 转义问题。
 */
bool mqtt_protocol_build_topic(char *                     p_topic,
                               size_t                     topic_size,
                               char const *               p_device_id,
                               mqtt_protocol_topic_type_t topic_type);

/* 温湿度上报 JSON，复用 telemetry 模块的统一字段定义。 */
bool mqtt_protocol_encode_telemetry(telemetry_snapshot_t const * p_snapshot,
                                    char *                       p_json,
                                    size_t                       json_size,
                                    size_t *                     p_written_length);

/* 上传一条已聚合的 15 分钟历史曲线记录。 */
bool mqtt_protocol_encode_history(history_record_t const * p_record,
                                  char *                   p_json,
                                  size_t                   json_size,
                                  size_t *                 p_written_length);

/* 云端命令执行后的 MQTT Reply JSON。command_id 与 BLE 侧使用同一安全字符限制。 */
bool mqtt_protocol_encode_reply(char const *                 p_command_id,
                                ble_protocol_reply_status_t status,
                                char *                       p_json,
                                size_t                       json_size,
                                size_t *                     p_written_length);

/* 无硬件自检：验证 Topic 生成、温湿度 JSON 和命令回复 JSON。 */
bool mqtt_protocol_self_test(void);

#endif /* MQTT_PROTOCOL_H */
