#include "ble_service.h"

#include <stddef.h>

/*
 * 当前无开发板、也未接入 RA6W2 无线协议栈，因此这里只实现服务层状态管理。
 * 后续接入真实 BLE 栈时，底层事件仍通过本文件的接口进入应用层。
 */
typedef struct st_ble_service_context
{
    ble_command_callback_t command_callback;
    bool                   initialized;
    bool                   started;
    bool                   connected;
    bool                   notify_enabled;
} ble_service_context_t;

static ble_service_context_t g_ble_service;

/* 作用：初始化 BLE 服务层状态，并保存上层命令处理回调。 */
ble_service_status_t ble_service_init(ble_command_callback_t command_callback)
{
    g_ble_service.command_callback = command_callback;
    g_ble_service.initialized      = true;
    g_ble_service.started         = false;
    g_ble_service.connected       = false;
    g_ble_service.notify_enabled  = false;

    return BLE_SERVICE_OK;
}

/* 作用：请求启动 BLE 广播和 GATT 服务；无线协议栈未接入时明确返回未就绪。 */
ble_service_status_t ble_service_start(void)
{
    if (!g_ble_service.initialized)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }

    /*
     * TODO: 接入 RA6W2 BLE 广播和 GATT 服务启动函数。
     * 协议栈未接入前不能修改 started，避免上层误以为已经开始广播。
     */
    return BLE_SERVICE_NOT_READY;
}

/* 作用：停止 BLE 服务并清除连接、订阅通知等会话状态。 */
ble_service_status_t ble_service_stop(void)
{
    if (!g_ble_service.initialized)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }

    /* TODO: 在这里接入 RA6W2 BLE 广播停止和连接释放函数。 */
    g_ble_service.started        = false;
    g_ble_service.connected      = false;
    g_ble_service.notify_enabled = false;
    return BLE_SERVICE_OK;
}

/* 作用：由真实 BLE 连接事件调用，更新手机是否已连接。 */
void ble_service_set_connected(bool connected)
{
    g_ble_service.connected      = connected;
    g_ble_service.notify_enabled = connected && g_ble_service.notify_enabled;
}

/* 作用：由 CCCD 配置事件调用，记录 App 是否订阅实时数据 Notify。 */
void ble_service_set_notify_enabled(bool enabled)
{
    g_ble_service.notify_enabled = enabled && g_ble_service.connected;
}

/* 作用：供应用层查询当前是否存在有效 BLE 连接。 */
bool ble_service_is_connected(void)
{
    return g_ble_service.connected;
}

/* 作用：检查 BLE 会话状态后发送实时测量 Notify；当前仅保留接口。 */
ble_service_status_t ble_service_notify_measurement(hygro_measurement_t const * p_measurement)
{
    if (NULL == p_measurement)
    {
        return BLE_SERVICE_INVALID_ARGUMENT;
    }

    if (!g_ble_service.initialized || !g_ble_service.started)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }

    if (!g_ble_service.connected)
    {
        return BLE_SERVICE_NOT_CONNECTED;
    }

    if (!g_ble_service.notify_enabled)
    {
        return BLE_SERVICE_NOTIFY_DISABLED;
    }

    /* TODO: 编码后调用真实 GATT Notify；当前无板阶段不伪造发送成功。 */
    (void) p_measurement;
    return BLE_SERVICE_NOT_READY;
}

/* 作用：接收控制 Characteristic 的 Write 数据，并转交设备命令处理层。 */
ble_service_status_t ble_service_handle_write(uint8_t const * p_command,
                                              size_t            command_length)
{
    if ((NULL == p_command) || (0U == command_length))
    {
        return BLE_SERVICE_INVALID_ARGUMENT;
    }

    if (!g_ble_service.initialized)
    {
        return BLE_SERVICE_NOT_INITIALIZED;
    }

    if (NULL == g_ble_service.command_callback)
    {
        return BLE_SERVICE_NOT_READY;
    }

    return g_ble_service.command_callback(p_command, command_length);
}
