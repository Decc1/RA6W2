#include "ble_service.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "telemetry_payload.h"

#include "hygro_log.h"

typedef struct st_ble_service_context
{
    ble_command_callback_t     command_callback;
    ble_service_platform_ops_t platform_ops;
    bool                       initialized;
    bool                       started;
    bool                       connected;
    bool                       notify_enabled;
} ble_service_context_t;

static ble_service_context_t g_ble_service;

/* 作用: 将 BLE 服务层状态映射为统一内部错误码，供诊断日志使用。 */
static hygro_status_t ble_service_status_to_hygro_status(ble_service_status_t status)
{
    switch (status)
    {
        case BLE_SERVICE_OK:                return HYGRO_STATUS_OK;
        case BLE_SERVICE_INVALID_ARGUMENT:  return HYGRO_STATUS_INVALID_ARGUMENT;
        case BLE_SERVICE_NOT_INITIALIZED:
        case BLE_SERVICE_NOT_READY:         return HYGRO_STATUS_NOT_READY;
        case BLE_SERVICE_NOT_CONNECTED:
        case BLE_SERVICE_NOTIFY_DISABLED:   return HYGRO_STATUS_BUSY;
        default:                            return HYGRO_STATUS_IO_ERROR;
    }
}

/* 作用: 初始化 BLE 服务状态，并清除上一次运行或自检残留的回调和会话信息。 */
ble_service_status_t ble_service_init(ble_command_callback_t command_callback)
{
    taskENTER_CRITICAL();
    (void) memset(&g_ble_service, 0, sizeof(g_ble_service));
    g_ble_service.command_callback = command_callback;
    g_ble_service.initialized = true;
    taskEXIT_CRITICAL();

    return BLE_SERVICE_OK;
}

/* 作用: 绑定或清除 BLE 底层回调，供真实协议栈和 PC mock 复用服务层。 */
void ble_service_bind_platform(ble_service_platform_ops_t const * p_platform_ops)
{
    taskENTER_CRITICAL();
    (void) memset(&g_ble_service.platform_ops, 0, sizeof(g_ble_service.platform_ops));
    if (NULL != p_platform_ops)
    {
        g_ble_service.platform_ops = *p_platform_ops;
    }
    taskEXIT_CRITICAL();
}

/* 作用: 调用底层适配层启动 BLE 广播和 GATT 服务。 */
ble_service_status_t ble_service_start(void)
{
    ble_lifecycle_callback_t start_callback;
    ble_service_status_t     status;

    taskENTER_CRITICAL();
    if (!g_ble_service.initialized)
    {
        taskEXIT_CRITICAL();
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BLE", HYGRO_STATUS_NOT_READY);
        return BLE_SERVICE_NOT_INITIALIZED;
    }
    start_callback = g_ble_service.platform_ops.start;
    taskEXIT_CRITICAL();

    if (NULL == start_callback)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BLE", HYGRO_STATUS_NOT_READY);
        return BLE_SERVICE_NOT_READY;
    }

    status = start_callback();
    if (BLE_SERVICE_OK != status)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_ERROR, "BLE", ble_service_status_to_hygro_status(status));
        return status;
    }

    taskENTER_CRITICAL();
    g_ble_service.started = true;
    taskEXIT_CRITICAL();
    return BLE_SERVICE_OK;
}

/* 作用: 停止 BLE 服务并清除连接和 Notify 订阅状态。 */
ble_service_status_t ble_service_stop(void)
{
    ble_lifecycle_callback_t stop_callback;
    bool                     initialized;
    bool                     started;
    ble_service_status_t     status = BLE_SERVICE_OK;

    taskENTER_CRITICAL();
    initialized = g_ble_service.initialized;
    started = g_ble_service.started;
    stop_callback = g_ble_service.platform_ops.stop;
    taskEXIT_CRITICAL();

    if (!initialized)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }

    if (started && (NULL != stop_callback))
    {
        status = stop_callback();
    }

    taskENTER_CRITICAL();
    g_ble_service.started = false;
    g_ble_service.connected = false;
    g_ble_service.notify_enabled = false;
    taskEXIT_CRITICAL();
    return status;
}

/* 作用: 由底层连接事件更新手机连接状态，断开时同时取消 Notify 订阅。 */
void ble_service_set_connected(bool connected)
{
    taskENTER_CRITICAL();
    g_ble_service.connected = connected;
    if (!connected)
    {
        g_ble_service.notify_enabled = false;
    }
    taskEXIT_CRITICAL();
}

/* 作用: 由 CCCD 事件记录 App 是否订阅实时数据 Notify。 */
void ble_service_set_notify_enabled(bool enabled)
{
    taskENTER_CRITICAL();
    g_ble_service.notify_enabled = enabled && g_ble_service.connected;
    taskEXIT_CRITICAL();
}

/* 作用: 查询当前是否已有有效 BLE 连接。 */
bool ble_service_is_connected(void)
{
    bool connected;

    taskENTER_CRITICAL();
    connected = g_ble_service.connected;
    taskEXIT_CRITICAL();
    return connected;
}

/* 作用: 编码最新温湿度，并通过底层 GATT Notify 回调发送给已订阅的 App。 */
ble_service_status_t ble_service_notify_measurement(hygro_measurement_t const * p_measurement)
{
    ble_notify_callback_t notify_callback;
    bool                  initialized;
    bool                  started;
    bool                  connected;
    bool                  notify_enabled;
    ble_service_status_t  status;
    telemetry_snapshot_t  snapshot = {0};
    uint8_t               payload[TELEMETRY_BLE_PAYLOAD_SIZE];

    if (NULL == p_measurement)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BLE", HYGRO_STATUS_INVALID_ARGUMENT);
        return BLE_SERVICE_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    initialized = g_ble_service.initialized;
    started = g_ble_service.started;
    connected = g_ble_service.connected;
    notify_enabled = g_ble_service.notify_enabled;
    notify_callback = g_ble_service.platform_ops.notify_measurement;
    taskEXIT_CRITICAL();

    if (!initialized || !started)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }
    if (!connected)
    {
        return BLE_SERVICE_NOT_CONNECTED;
    }
    if (!notify_enabled)
    {
        return BLE_SERVICE_NOTIFY_DISABLED;
    }
    if (NULL == notify_callback)
    {
        return BLE_SERVICE_NOT_READY;
    }

    snapshot.latest_measurement = *p_measurement;
    snapshot.has_valid_measurement = p_measurement->valid;
    if (!telemetry_encode_ble_payload(&snapshot, payload, sizeof(payload)))
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BLE", HYGRO_STATUS_PROTOCOL_ERROR);
        return BLE_SERVICE_INVALID_ARGUMENT;
    }

    status = notify_callback(payload, sizeof(payload));
    if (BLE_SERVICE_OK != status)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BLE", ble_service_status_to_hygro_status(status));
    }

    return status;
}

/* 作用: 将控制 Characteristic 的 Write 数据转交给上层命令处理回调。 */
ble_service_status_t ble_service_handle_write(uint8_t const * p_command,
                                              size_t            command_length)
{
    ble_command_callback_t command_callback;
    bool                   initialized;

    if ((NULL == p_command) || (0U == command_length))
    {
        return BLE_SERVICE_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    initialized = g_ble_service.initialized;
    command_callback = g_ble_service.command_callback;
    taskEXIT_CRITICAL();

    if (!initialized)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }
    if (NULL == command_callback)
    {
        return BLE_SERVICE_NOT_READY;
    }

    return command_callback(p_command, command_length);
}

/* 作用: BLE 服务自检使用的模拟启动回调。 */
static ble_service_status_t ble_service_test_start(void)
{
    return BLE_SERVICE_OK;
}

/* 作用: BLE 服务自检使用的模拟停止回调。 */
static ble_service_status_t ble_service_test_stop(void)
{
    return BLE_SERVICE_OK;
}

/* 作用: BLE 服务自检使用的模拟 Notify 回调，只校验载荷长度。 */
static ble_service_status_t ble_service_test_notify(uint8_t const * p_payload,
                                                     size_t            payload_length)
{
    return ((NULL != p_payload) && (TELEMETRY_BLE_PAYLOAD_SIZE == payload_length)) ?
           BLE_SERVICE_OK : BLE_SERVICE_INVALID_ARGUMENT;
}

/* 作用: 验证 BLE 服务从初始化、启动、连接订阅到 Notify 发送的完整状态链路。 */
bool ble_service_self_test(void)
{
    static ble_service_platform_ops_t const test_ops =
    {
        .start = ble_service_test_start,
        .stop = ble_service_test_stop,
        .notify_measurement = ble_service_test_notify,
    };
    hygro_measurement_t measurement =
    {
        .temperature_centi_c = 2350,
        .humidity_centi_rh = 4550U,
        .battery_mv = 3850U,
        .sample_sequence = 1U,
        .valid = true,
    };

    (void) ble_service_init(NULL);
    ble_service_bind_platform(&test_ops);
    ble_service_set_connected(true);
    ble_service_set_notify_enabled(true);

    if ((BLE_SERVICE_OK != ble_service_start()) ||
        (BLE_SERVICE_OK != ble_service_notify_measurement(&measurement)) ||
        (BLE_SERVICE_OK != ble_service_stop()))
    {
        return false;
    }

    ble_service_bind_platform(NULL);
    return true;
}
