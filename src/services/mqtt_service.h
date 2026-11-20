#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../app/hygro_status.h"
#include "mqtt_protocol.h"

/* 作用: 定义 MQTT/TLS 传输服务对应用层公开的连接状态。 */
typedef enum e_mqtt_service_state
{
    MQTT_SERVICE_STATE_UNINITIALIZED = 0,
    MQTT_SERVICE_STATE_WAITING_FOR_WIFI,
    MQTT_SERVICE_STATE_CONNECTING_TLS,
    MQTT_SERVICE_STATE_SUBSCRIBING,
    MQTT_SERVICE_STATE_CONNECTED,
    MQTT_SERVICE_STATE_BACKOFF,
} mqtt_service_state_t;

/* 作用: 抽象 TLS 连接、订阅、发布和断开操作，真实库与 PC mock 共用服务层。 */
typedef struct st_mqtt_service_platform_ops
{
    bool (* connect_tls)(void);
    bool (* subscribe)(char const * p_topic, uint8_t qos);
    bool (* publish)(char const * p_topic, char const * p_payload, size_t payload_length, uint8_t qos);
    void (* disconnect)(void);
} mqtt_service_platform_ops_t;

/* 作用: 接收已订阅的云端命令原始载荷，命令 JSON 格式由云端接口协议后续确定。 */
typedef hygro_status_t (* mqtt_service_command_callback_t)(uint8_t const * p_payload,
                                                            size_t          payload_length);

/* 作用: 初始化 MQTT 服务并复制设备 ID、平台回调及可选云端命令回调。 */
void mqtt_service_init(char const *                         p_device_id,
                       mqtt_service_platform_ops_t const * p_platform_ops,
                       mqtt_service_command_callback_t      command_callback);

/* 作用: 由 Wi-Fi/MQTT 调度任务周期调用，驱动 TLS 连接、订阅或断线退避状态迁移。 */
hygro_status_t mqtt_service_process(bool wifi_connected);

/* 作用: 发布最新遥测 JSON，使用 QoS 1 以降低一次上报丢失概率。 */
hygro_status_t mqtt_service_publish_telemetry(telemetry_snapshot_t const * p_snapshot);

/* 作用: 转交底层 MQTT 收到的命令 Topic 和原始载荷，并校验 Topic 必须属于本设备。 */
hygro_status_t mqtt_service_handle_incoming(char const *   p_topic,
                                            uint8_t const * p_payload,
                                            size_t          payload_length);

/* 作用: 返回当前 MQTT 连接状态。 */
mqtt_service_state_t mqtt_service_get_state(void);

/* 作用: 主动断开 MQTT/TLS 会话并回到等待 Wi-Fi 状态。 */
void mqtt_service_disconnect(void);

/* 作用: 验证 TLS 连接、订阅、上传、断网和命令回调的 mock 状态机。 */
bool mqtt_service_self_test(void);

#endif /* MQTT_SERVICE_H */
