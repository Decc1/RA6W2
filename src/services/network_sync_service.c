#include "network_sync_service.h"

#include <stddef.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "hygro_log.h"

typedef struct st_network_sync_context
{
    network_sync_platform_ops_t platform_ops;
    network_sync_state_t        state;
    hygro_status_t              last_result;
    uint32_t                    wifi_deadline_ms;
    bool                        ready;
} network_sync_context_t;

static network_sync_context_t g_network_sync;

/* 作用: 判断平台适配层是否提供一轮网络同步所需的完整非阻塞回调。 */
static bool network_sync_service_platform_is_ready(network_sync_platform_ops_t const * p_platform_ops)
{
    return (NULL != p_platform_ops) && (NULL != p_platform_ops->start_wifi) &&
           (NULL != p_platform_ops->is_wifi_connected) &&
           (NULL != p_platform_ops->process_mqtt) &&
           (NULL != p_platform_ops->publish_telemetry) &&
           (NULL != p_platform_ops->get_telemetry_snapshot);
}

/* 作用: 用有符号差值判断时间是否到期，支持毫秒计数自然回绕。 */
static bool network_sync_service_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t) (now_ms - deadline_ms) >= 0;
}

/* 作用: 在临界区内复制共享状态，外部无线回调始终在临界区外执行。 */
static void network_sync_service_copy_context(network_sync_platform_ops_t * p_platform_ops,
                                              network_sync_state_t *        p_state,
                                              hygro_status_t *              p_last_result,
                                              uint32_t *                    p_wifi_deadline_ms,
                                              bool *                        p_ready)
{
    taskENTER_CRITICAL();
    *p_platform_ops = g_network_sync.platform_ops;
    *p_state = g_network_sync.state;
    *p_last_result = g_network_sync.last_result;
    *p_wifi_deadline_ms = g_network_sync.wifi_deadline_ms;
    *p_ready = g_network_sync.ready;
    taskEXIT_CRITICAL();
}

/* 作用: 完成一次同步会话，先断开无线资源，再保存结果并回到空闲状态。 */
static hygro_status_t network_sync_service_finish(hygro_status_t result)
{
    network_sync_platform_ops_t platform_ops;
    network_sync_state_t        state;
    hygro_status_t              last_result;
    uint32_t                    wifi_deadline_ms;
    bool                        ready;

    network_sync_service_copy_context(&platform_ops,
                                      &state,
                                      &last_result,
                                      &wifi_deadline_ms,
                                      &ready);
    (void) state;
    (void) last_result;
    (void) wifi_deadline_ms;

    if (ready && (NULL != platform_ops.disconnect_mqtt))
    {
        platform_ops.disconnect_mqtt();
    }
    if (ready && (NULL != platform_ops.disconnect_wifi))
    {
        platform_ops.disconnect_wifi();
    }

    taskENTER_CRITICAL();
    g_network_sync.state = NETWORK_SYNC_STATE_IDLE;
    g_network_sync.last_result = result;
    taskEXIT_CRITICAL();

    if (HYGRO_STATUS_OK != result)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "NET_SYNC", result);
    }

    return result;
}

/* 作用: 初始化网络同步服务，NULL 表示尚未绑定真实 Wi-Fi/MQTT 平台适配层。 */
void network_sync_service_init(network_sync_platform_ops_t const * p_platform_ops)
{
    taskENTER_CRITICAL();
    (void) memset(&g_network_sync, 0, sizeof(g_network_sync));
    if (network_sync_service_platform_is_ready(p_platform_ops))
    {
        g_network_sync.platform_ops = *p_platform_ops;
        g_network_sync.ready = true;
        g_network_sync.state = NETWORK_SYNC_STATE_IDLE;
        g_network_sync.last_result = HYGRO_STATUS_OK;
    }
    else
    {
        g_network_sync.state = NETWORK_SYNC_STATE_UNINITIALIZED;
        g_network_sync.last_result = HYGRO_STATUS_NOT_READY;
    }
    taskEXIT_CRITICAL();
}

/* 作用: 请求开始一次同步会话，异步启动 Wi-Fi 并设置连接超时。 */
hygro_status_t network_sync_service_request(uint32_t now_ms, uint32_t wifi_timeout_ms)
{
    network_sync_platform_ops_t platform_ops;
    network_sync_state_t        state;
    hygro_status_t              last_result;
    uint32_t                    wifi_deadline_ms;
    bool                        ready;
    hygro_status_t              status;

    if (0U == wifi_timeout_ms)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    network_sync_service_copy_context(&platform_ops,
                                      &state,
                                      &last_result,
                                      &wifi_deadline_ms,
                                      &ready);
    (void) last_result;
    (void) wifi_deadline_ms;

    if (!ready)
    {
        return HYGRO_STATUS_NOT_READY;
    }
    if (NETWORK_SYNC_STATE_IDLE != state)
    {
        return HYGRO_STATUS_BUSY;
    }

    status = platform_ops.start_wifi();
    if (HYGRO_STATUS_OK != status)
    {
        taskENTER_CRITICAL();
        g_network_sync.last_result = status;
        taskEXIT_CRITICAL();
        return status;
    }

    taskENTER_CRITICAL();
    g_network_sync.state = NETWORK_SYNC_STATE_WAITING_WIFI;
    g_network_sync.wifi_deadline_ms = now_ms + wifi_timeout_ms;
    taskEXIT_CRITICAL();
    return HYGRO_STATUS_OK;
}

/* 作用: 推进一次同步状态机，调用频率由应用网络任务决定。 */
hygro_status_t network_sync_service_process(uint32_t now_ms)
{
    network_sync_platform_ops_t platform_ops;
    network_sync_state_t        state;
    hygro_status_t              last_result;
    uint32_t                    wifi_deadline_ms;
    bool                        ready;
    telemetry_snapshot_t        snapshot;
    hygro_status_t              status;

    network_sync_service_copy_context(&platform_ops,
                                      &state,
                                      &last_result,
                                      &wifi_deadline_ms,
                                      &ready);
    (void) last_result;

    if (!ready)
    {
        return HYGRO_STATUS_NOT_READY;
    }

    switch (state)
    {
        case NETWORK_SYNC_STATE_IDLE:
            return HYGRO_STATUS_OK;

        case NETWORK_SYNC_STATE_WAITING_WIFI:
            if (platform_ops.is_wifi_connected())
            {
                taskENTER_CRITICAL();
                g_network_sync.state = NETWORK_SYNC_STATE_WAITING_MQTT;
                taskEXIT_CRITICAL();
                return HYGRO_STATUS_OK;
            }
            return network_sync_service_time_reached(now_ms, wifi_deadline_ms) ?
                   network_sync_service_finish(HYGRO_STATUS_TIMEOUT) : HYGRO_STATUS_BUSY;

        case NETWORK_SYNC_STATE_WAITING_MQTT:
            status = platform_ops.process_mqtt(true);
            if (HYGRO_STATUS_OK != status)
            {
                return network_sync_service_finish(status);
            }
            taskENTER_CRITICAL();
            g_network_sync.state = NETWORK_SYNC_STATE_PUBLISHING;
            taskEXIT_CRITICAL();
            return HYGRO_STATUS_OK;

        case NETWORK_SYNC_STATE_PUBLISHING:
            if (!platform_ops.get_telemetry_snapshot(&snapshot))
            {
                return network_sync_service_finish(HYGRO_STATUS_NOT_READY);
            }
            status = platform_ops.publish_telemetry(&snapshot);
            return network_sync_service_finish(status);

        case NETWORK_SYNC_STATE_UNINITIALIZED:
        default:
            return HYGRO_STATUS_NOT_READY;
    }
}

/* 作用: 获取当前网络同步会话状态。 */
network_sync_state_t network_sync_service_get_state(void)
{
    network_sync_state_t state;

    taskENTER_CRITICAL();
    state = g_network_sync.state;
    taskEXIT_CRITICAL();
    return state;
}

/* 作用: 获取最近一次同步会话的最终结果。 */
hygro_status_t network_sync_service_get_last_result(void)
{
    hygro_status_t result;

    taskENTER_CRITICAL();
    result = g_network_sync.last_result;
    taskEXIT_CRITICAL();
    return result;
}

static bool            g_network_sync_test_wifi_connected;
static hygro_status_t  g_network_sync_test_start_result;
static hygro_status_t  g_network_sync_test_mqtt_result;
static hygro_status_t  g_network_sync_test_publish_result;
static bool            g_network_sync_test_snapshot_available;
static uint32_t        g_network_sync_test_start_count;
static uint32_t        g_network_sync_test_mqtt_count;
static uint32_t        g_network_sync_test_publish_count;
static uint32_t        g_network_sync_test_disconnect_mqtt_count;
static uint32_t        g_network_sync_test_disconnect_wifi_count;

/* 作用: 模拟异步 Wi-Fi 连接启动回调。 */
static hygro_status_t network_sync_test_start_wifi(void)
{
    g_network_sync_test_start_count++;
    return g_network_sync_test_start_result;
}

/* 作用: 模拟 Wi-Fi 是否已经获得 IP 的状态查询。 */
static bool network_sync_test_is_wifi_connected(void)
{
    return g_network_sync_test_wifi_connected;
}

/* 作用: 模拟 MQTT/TLS 连接和订阅处理回调。 */
static hygro_status_t network_sync_test_process_mqtt(bool wifi_connected)
{
    g_network_sync_test_mqtt_count++;
    return wifi_connected ? g_network_sync_test_mqtt_result : HYGRO_STATUS_NETWORK_ERROR;
}

/* 作用: 模拟 MQTT 遥测发布回调。 */
static hygro_status_t network_sync_test_publish_telemetry(telemetry_snapshot_t const * p_snapshot)
{
    g_network_sync_test_publish_count++;
    return ((NULL != p_snapshot) && p_snapshot->has_valid_measurement) ?
           g_network_sync_test_publish_result : HYGRO_STATUS_NOT_READY;
}

/* 作用: 提供固定有效遥测快照，供网络同步自检验证发布路径。 */
static bool network_sync_test_get_snapshot(telemetry_snapshot_t * p_snapshot)
{
    if (!g_network_sync_test_snapshot_available || (NULL == p_snapshot))
    {
        return false;
    }

    (void) memset(p_snapshot, 0, sizeof(*p_snapshot));
    p_snapshot->latest_measurement.temperature_centi_c = 2350;
    p_snapshot->latest_measurement.humidity_centi_rh = 4550U;
    p_snapshot->latest_measurement.battery_mv = 3850U;
    p_snapshot->latest_measurement.valid = true;
    p_snapshot->has_valid_measurement = true;
    return true;
}

/* 作用: 统计 MQTT 断开请求，用于验证低功耗同步结束后释放无线资源。 */
static void network_sync_test_disconnect_mqtt(void)
{
    g_network_sync_test_disconnect_mqtt_count++;
}

/* 作用: 统计 Wi-Fi 断开请求，用于验证低功耗同步结束后释放无线资源。 */
static void network_sync_test_disconnect_wifi(void)
{
    g_network_sync_test_disconnect_wifi_count++;
}

/* 作用: 重置网络同步 mock 回调结果及调用次数。 */
static void network_sync_test_reset(void)
{
    g_network_sync_test_wifi_connected = false;
    g_network_sync_test_start_result = HYGRO_STATUS_OK;
    g_network_sync_test_mqtt_result = HYGRO_STATUS_OK;
    g_network_sync_test_publish_result = HYGRO_STATUS_OK;
    g_network_sync_test_snapshot_available = true;
    g_network_sync_test_start_count = 0U;
    g_network_sync_test_mqtt_count = 0U;
    g_network_sync_test_publish_count = 0U;
    g_network_sync_test_disconnect_mqtt_count = 0U;
    g_network_sync_test_disconnect_wifi_count = 0U;
}

/* 作用: 验证连接、发布、超时、断开及 mock 回调调用顺序。 */
bool network_sync_service_self_test(void)
{
    static network_sync_platform_ops_t const test_ops =
    {
        .start_wifi = network_sync_test_start_wifi,
        .is_wifi_connected = network_sync_test_is_wifi_connected,
        .process_mqtt = network_sync_test_process_mqtt,
        .publish_telemetry = network_sync_test_publish_telemetry,
        .get_telemetry_snapshot = network_sync_test_get_snapshot,
        .disconnect_mqtt = network_sync_test_disconnect_mqtt,
        .disconnect_wifi = network_sync_test_disconnect_wifi,
    };

    network_sync_service_init(NULL);
    if (HYGRO_STATUS_NOT_READY != network_sync_service_request(0U, 1U))
    {
        return false;
    }

    network_sync_test_reset();
    network_sync_service_init(&test_ops);
    if ((HYGRO_STATUS_OK != network_sync_service_request(100U, 50U)) ||
        (1U != g_network_sync_test_start_count) ||
        (NETWORK_SYNC_STATE_WAITING_WIFI != network_sync_service_get_state()) ||
        (HYGRO_STATUS_BUSY != network_sync_service_process(120U)))
    {
        return false;
    }

    g_network_sync_test_wifi_connected = true;
    if ((HYGRO_STATUS_OK != network_sync_service_process(121U)) ||
        (NETWORK_SYNC_STATE_WAITING_MQTT != network_sync_service_get_state()) ||
        (HYGRO_STATUS_OK != network_sync_service_process(122U)) ||
        (NETWORK_SYNC_STATE_PUBLISHING != network_sync_service_get_state()) ||
        (HYGRO_STATUS_OK != network_sync_service_process(123U)) ||
        (NETWORK_SYNC_STATE_IDLE != network_sync_service_get_state()) ||
        (HYGRO_STATUS_OK != network_sync_service_get_last_result()) ||
        (1U != g_network_sync_test_mqtt_count) ||
        (1U != g_network_sync_test_publish_count) ||
        (1U != g_network_sync_test_disconnect_mqtt_count) ||
        (1U != g_network_sync_test_disconnect_wifi_count))
    {
        return false;
    }

    network_sync_test_reset();
    network_sync_service_init(&test_ops);
    if ((HYGRO_STATUS_OK != network_sync_service_request(1000U, 50U)) ||
        (HYGRO_STATUS_TIMEOUT != network_sync_service_process(1050U)) ||
        (HYGRO_STATUS_TIMEOUT != network_sync_service_get_last_result()) ||
        (1U != g_network_sync_test_disconnect_mqtt_count) ||
        (1U != g_network_sync_test_disconnect_wifi_count))
    {
        return false;
    }

    network_sync_service_init(NULL);
    return NETWORK_SYNC_STATE_UNINITIALIZED == network_sync_service_get_state();
}
