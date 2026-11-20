#include "mqtt_service.h"

#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "hygro_log.h"

#define MQTT_SERVICE_QOS_AT_MOST_ONCE    (0U)
#define MQTT_SERVICE_QOS_AT_LEAST_ONCE   (1U)

typedef struct st_mqtt_service_context
{
    mqtt_service_platform_ops_t  platform_ops;
    mqtt_service_command_callback_t command_callback;
    mqtt_service_state_t         state;
    char                         device_id[MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH + 1U];
    char                         command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    bool                         ready;
} mqtt_service_context_t;

static mqtt_service_context_t g_mqtt_service;

/* 作用: 判断平台适配层是否提供 MQTT/TLS 业务所需的完整回调。 */
static bool mqtt_service_platform_is_ready(mqtt_service_platform_ops_t const * p_platform_ops)
{
    return (NULL != p_platform_ops) && (NULL != p_platform_ops->connect_tls) &&
           (NULL != p_platform_ops->subscribe) && (NULL != p_platform_ops->publish);
}

/* 作用: 复制当前平台回调和设备 Topic，避免在临界区内调用底层网络库。 */
static void mqtt_service_copy_context(mqtt_service_platform_ops_t * p_platform_ops,
                                      char *                        p_device_id,
                                      size_t                        device_id_size,
                                      char *                        p_command_topic,
                                      size_t                        command_topic_size,
                                      mqtt_service_command_callback_t * p_command_callback,
                                      mqtt_service_state_t *        p_state,
                                      bool *                        p_ready)
{
    taskENTER_CRITICAL();
    *p_platform_ops = g_mqtt_service.platform_ops;
    (void) memcpy(p_device_id, g_mqtt_service.device_id, device_id_size);
    (void) memcpy(p_command_topic, g_mqtt_service.command_topic, command_topic_size);
    *p_command_callback = g_mqtt_service.command_callback;
    *p_state = g_mqtt_service.state;
    *p_ready = g_mqtt_service.ready;
    taskEXIT_CRITICAL();
}

/* 作用: 初始化 MQTT 服务并复制设备 ID、平台回调及可选云端命令回调。 */
void mqtt_service_init(char const *                         p_device_id,
                       mqtt_service_platform_ops_t const * p_platform_ops,
                       mqtt_service_command_callback_t      command_callback)
{
    char command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE] = {0};
    size_t device_id_length = 0U;
    bool ready = mqtt_service_platform_is_ready(p_platform_ops) &&
                 mqtt_protocol_build_topic(command_topic,
                                           sizeof(command_topic),
                                           p_device_id,
                                           MQTT_PROTOCOL_TOPIC_COMMAND);

    if (ready)
    {
        device_id_length = strlen(p_device_id);
    }

    taskENTER_CRITICAL();
    (void) memset(&g_mqtt_service, 0, sizeof(g_mqtt_service));
    if (ready)
    {
        g_mqtt_service.platform_ops = *p_platform_ops;
        (void) memcpy(g_mqtt_service.device_id, p_device_id, device_id_length + 1U);
        (void) memcpy(g_mqtt_service.command_topic, command_topic, sizeof(g_mqtt_service.command_topic));
        g_mqtt_service.command_callback = command_callback;
        g_mqtt_service.ready = true;
    }
    g_mqtt_service.state = MQTT_SERVICE_STATE_WAITING_FOR_WIFI;
    taskEXIT_CRITICAL();
}

/* 作用: 由 Wi-Fi/MQTT 调度任务周期调用，驱动 TLS 连接、订阅或断线退避状态迁移。 */
hygro_status_t mqtt_service_process(bool wifi_connected)
{
    mqtt_service_platform_ops_t  platform_ops;
    mqtt_service_command_callback_t command_callback;
    mqtt_service_state_t         state;
    char                         device_id[MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH + 1U];
    char                         command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    bool                         ready;

    mqtt_service_copy_context(&platform_ops,
                              device_id,
                              sizeof(device_id),
                              command_topic,
                              sizeof(command_topic),
                              &command_callback,
                              &state,
                              &ready);
    (void) device_id;
    (void) command_callback;

    if (!ready)
    {
        return HYGRO_STATUS_NOT_READY;
    }

    if (!wifi_connected)
    {
        if ((MQTT_SERVICE_STATE_CONNECTED == state) && (NULL != platform_ops.disconnect))
        {
            platform_ops.disconnect();
        }
        taskENTER_CRITICAL();
        g_mqtt_service.state = MQTT_SERVICE_STATE_WAITING_FOR_WIFI;
        taskEXIT_CRITICAL();
        return HYGRO_STATUS_NETWORK_ERROR;
    }

    if (MQTT_SERVICE_STATE_CONNECTED == state)
    {
        return HYGRO_STATUS_OK;
    }

    taskENTER_CRITICAL();
    g_mqtt_service.state = MQTT_SERVICE_STATE_CONNECTING_TLS;
    taskEXIT_CRITICAL();
    if (!platform_ops.connect_tls())
    {
        taskENTER_CRITICAL();
        g_mqtt_service.state = MQTT_SERVICE_STATE_BACKOFF;
        taskEXIT_CRITICAL();
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "MQTT", HYGRO_STATUS_TLS_ERROR);
        return HYGRO_STATUS_TLS_ERROR;
    }

    taskENTER_CRITICAL();
    g_mqtt_service.state = MQTT_SERVICE_STATE_SUBSCRIBING;
    taskEXIT_CRITICAL();
    if (!platform_ops.subscribe(command_topic, MQTT_SERVICE_QOS_AT_LEAST_ONCE))
    {
        if (NULL != platform_ops.disconnect)
        {
            platform_ops.disconnect();
        }
        taskENTER_CRITICAL();
        g_mqtt_service.state = MQTT_SERVICE_STATE_BACKOFF;
        taskEXIT_CRITICAL();
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "MQTT", HYGRO_STATUS_NETWORK_ERROR);
        return HYGRO_STATUS_NETWORK_ERROR;
    }

    taskENTER_CRITICAL();
    g_mqtt_service.state = MQTT_SERVICE_STATE_CONNECTED;
    taskEXIT_CRITICAL();
    return HYGRO_STATUS_OK;
}

/* 作用: 发布最新遥测 JSON，使用 QoS 1 以降低一次上报丢失概率。 */
hygro_status_t mqtt_service_publish_telemetry(telemetry_snapshot_t const * p_snapshot)
{
    mqtt_service_platform_ops_t  platform_ops;
    mqtt_service_command_callback_t command_callback;
    mqtt_service_state_t         state;
    char                         device_id[MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH + 1U];
    char                         command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    char                         telemetry_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    char                         json[MQTT_PROTOCOL_JSON_BUFFER_SIZE];
    size_t                       json_length = 0U;
    bool                         ready;

    if (NULL == p_snapshot)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    mqtt_service_copy_context(&platform_ops,
                              device_id,
                              sizeof(device_id),
                              command_topic,
                              sizeof(command_topic),
                              &command_callback,
                              &state,
                              &ready);
    (void) command_topic;
    (void) command_callback;

    if (!ready || (MQTT_SERVICE_STATE_CONNECTED != state))
    {
        return HYGRO_STATUS_NOT_READY;
    }
    if (!mqtt_protocol_build_topic(telemetry_topic,
                                   sizeof(telemetry_topic),
                                   device_id,
                                   MQTT_PROTOCOL_TOPIC_TELEMETRY) ||
        !mqtt_protocol_encode_telemetry(p_snapshot, json, sizeof(json), &json_length))
    {
        return HYGRO_STATUS_PROTOCOL_ERROR;
    }
    if (!platform_ops.publish(telemetry_topic, json, json_length, MQTT_SERVICE_QOS_AT_LEAST_ONCE))
    {
        taskENTER_CRITICAL();
        g_mqtt_service.state = MQTT_SERVICE_STATE_BACKOFF;
        taskEXIT_CRITICAL();
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "MQTT", HYGRO_STATUS_NETWORK_ERROR);
        return HYGRO_STATUS_NETWORK_ERROR;
    }

    return HYGRO_STATUS_OK;
}

/* 作用: 转交底层 MQTT 收到的命令 Topic 和原始载荷，并校验 Topic 必须属于本设备。 */
hygro_status_t mqtt_service_handle_incoming(char const *   p_topic,
                                            uint8_t const * p_payload,
                                            size_t          payload_length)
{
    mqtt_service_platform_ops_t  platform_ops;
    mqtt_service_command_callback_t command_callback;
    mqtt_service_state_t         state;
    char                         device_id[MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH + 1U];
    char                         command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    bool                         ready;

    if ((NULL == p_topic) || (NULL == p_payload) || (0U == payload_length))
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    mqtt_service_copy_context(&platform_ops,
                              device_id,
                              sizeof(device_id),
                              command_topic,
                              sizeof(command_topic),
                              &command_callback,
                              &state,
                              &ready);
    (void) platform_ops;
    (void) device_id;

    if (!ready || (MQTT_SERVICE_STATE_CONNECTED != state) || (NULL == command_callback))
    {
        return HYGRO_STATUS_NOT_READY;
    }
    if (0 != strcmp(p_topic, command_topic))
    {
        return HYGRO_STATUS_PROTOCOL_ERROR;
    }

    return command_callback(p_payload, payload_length);
}

/* 作用: 返回当前 MQTT 连接状态。 */
mqtt_service_state_t mqtt_service_get_state(void)
{
    mqtt_service_state_t state;

    taskENTER_CRITICAL();
    state = g_mqtt_service.state;
    taskEXIT_CRITICAL();
    return state;
}

/* 作用: 主动断开 MQTT/TLS 会话并回到等待 Wi-Fi 状态。 */
void mqtt_service_disconnect(void)
{
    mqtt_service_platform_ops_t  platform_ops;
    mqtt_service_command_callback_t command_callback;
    mqtt_service_state_t         state;
    char                         device_id[MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH + 1U];
    char                         command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    bool                         ready;

    mqtt_service_copy_context(&platform_ops,
                              device_id,
                              sizeof(device_id),
                              command_topic,
                              sizeof(command_topic),
                              &command_callback,
                              &state,
                              &ready);
    (void) device_id;
    (void) command_topic;
    (void) command_callback;

    if (ready && (MQTT_SERVICE_STATE_CONNECTED == state) && (NULL != platform_ops.disconnect))
    {
        platform_ops.disconnect();
    }

    taskENTER_CRITICAL();
    g_mqtt_service.state = MQTT_SERVICE_STATE_WAITING_FOR_WIFI;
    taskEXIT_CRITICAL();
}

static bool     g_mqtt_test_connect_result;
static bool     g_mqtt_test_subscribe_result;
static bool     g_mqtt_test_publish_result;
static uint32_t g_mqtt_test_connect_count;
static uint32_t g_mqtt_test_subscribe_count;
static uint32_t g_mqtt_test_publish_count;
static uint32_t g_mqtt_test_disconnect_count;
static uint32_t g_mqtt_test_command_count;

/* 作用: 模拟 TLS 连接回调。 */
static bool mqtt_test_connect_tls(void)
{
    g_mqtt_test_connect_count++;
    return g_mqtt_test_connect_result;
}

/* 作用: 模拟 MQTT 订阅回调并校验 QoS。 */
static bool mqtt_test_subscribe(char const * p_topic, uint8_t qos)
{
    g_mqtt_test_subscribe_count++;
    return g_mqtt_test_subscribe_result && (NULL != p_topic) &&
           (MQTT_SERVICE_QOS_AT_LEAST_ONCE == qos);
}

/* 作用: 模拟 MQTT 发布回调并校验 Topic、载荷和 QoS。 */
static bool mqtt_test_publish(char const * p_topic, char const * p_payload, size_t payload_length, uint8_t qos)
{
    g_mqtt_test_publish_count++;
    return g_mqtt_test_publish_result && (NULL != p_topic) && (NULL != p_payload) &&
           (0U != payload_length) && (MQTT_SERVICE_QOS_AT_LEAST_ONCE == qos);
}

/* 作用: 模拟 MQTT/TLS 断开回调。 */
static void mqtt_test_disconnect(void)
{
    g_mqtt_test_disconnect_count++;
}

/* 作用: 模拟云端命令处理回调。 */
static hygro_status_t mqtt_test_command(uint8_t const * p_payload, size_t payload_length)
{
    g_mqtt_test_command_count++;
    return ((NULL != p_payload) && (3U == payload_length)) ? HYGRO_STATUS_OK : HYGRO_STATUS_PROTOCOL_ERROR;
}

/* 作用: 重置 MQTT mock 回调结果与调用计数。 */
static void mqtt_test_reset(void)
{
    g_mqtt_test_connect_result = true;
    g_mqtt_test_subscribe_result = true;
    g_mqtt_test_publish_result = true;
    g_mqtt_test_connect_count = 0U;
    g_mqtt_test_subscribe_count = 0U;
    g_mqtt_test_publish_count = 0U;
    g_mqtt_test_disconnect_count = 0U;
    g_mqtt_test_command_count = 0U;
}

/* 作用: 验证 TLS 连接、订阅、上传、断线和命令回调的 mock 状态机。 */
bool mqtt_service_self_test(void)
{
    static mqtt_service_platform_ops_t const ops =
    {
        .connect_tls = mqtt_test_connect_tls,
        .subscribe = mqtt_test_subscribe,
        .publish = mqtt_test_publish,
        .disconnect = mqtt_test_disconnect,
    };
    static uint8_t const command_payload[] = {1U, 2U, 3U};
    telemetry_snapshot_t snapshot = {0};
    char command_topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];

    mqtt_test_reset();
    mqtt_service_init(NULL, &ops, mqtt_test_command);
    if ((HYGRO_STATUS_NOT_READY != mqtt_service_process(true)) ||
        (MQTT_SERVICE_STATE_WAITING_FOR_WIFI != mqtt_service_get_state()))
    {
        return false;
    }

    mqtt_service_init("HYGRO-000001", &ops, mqtt_test_command);
    if ((HYGRO_STATUS_NETWORK_ERROR != mqtt_service_process(false)) ||
        (0U != g_mqtt_test_connect_count))
    {
        return false;
    }

    g_mqtt_test_connect_result = false;
    if ((HYGRO_STATUS_TLS_ERROR != mqtt_service_process(true)) ||
        (MQTT_SERVICE_STATE_BACKOFF != mqtt_service_get_state()))
    {
        return false;
    }

    g_mqtt_test_connect_result = true;
    g_mqtt_test_subscribe_result = false;
    if ((HYGRO_STATUS_NETWORK_ERROR != mqtt_service_process(true)) ||
        (MQTT_SERVICE_STATE_BACKOFF != mqtt_service_get_state()) ||
        (1U != g_mqtt_test_disconnect_count))
    {
        return false;
    }

    g_mqtt_test_subscribe_result = true;
    if ((HYGRO_STATUS_OK != mqtt_service_process(true)) ||
        (MQTT_SERVICE_STATE_CONNECTED != mqtt_service_get_state()) ||
        (2U != g_mqtt_test_subscribe_count))
    {
        return false;
    }

    snapshot.latest_measurement.temperature_centi_c = 2350;
    snapshot.latest_measurement.humidity_centi_rh = 4550U;
    snapshot.latest_measurement.battery_mv = 3850U;
    snapshot.latest_measurement.sample_sequence = 1U;
    snapshot.latest_measurement.valid = true;
    snapshot.has_valid_measurement = true;
    if ((HYGRO_STATUS_OK != mqtt_service_publish_telemetry(&snapshot)) ||
        (1U != g_mqtt_test_publish_count) ||
        !mqtt_protocol_build_topic(command_topic,
                                   sizeof(command_topic),
                                   "HYGRO-000001",
                                   MQTT_PROTOCOL_TOPIC_COMMAND) ||
        (HYGRO_STATUS_PROTOCOL_ERROR != mqtt_service_handle_incoming(
            "hygro/v1/OTHER/command", command_payload, sizeof(command_payload))) ||
        (HYGRO_STATUS_OK != mqtt_service_handle_incoming(
            command_topic, command_payload, sizeof(command_payload))) ||
        (1U != g_mqtt_test_command_count))
    {
        return false;
    }

    g_mqtt_test_publish_result = false;
    if ((HYGRO_STATUS_NETWORK_ERROR != mqtt_service_publish_telemetry(&snapshot)) ||
        (MQTT_SERVICE_STATE_BACKOFF != mqtt_service_get_state()))
    {
        return false;
    }

    mqtt_service_disconnect();
    mqtt_service_init(NULL, NULL, NULL);
    return MQTT_SERVICE_STATE_WAITING_FOR_WIFI == mqtt_service_get_state();
}
