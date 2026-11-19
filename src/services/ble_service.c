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

ble_service_status_t ble_service_init(ble_command_callback_t command_callback)
{
    g_ble_service.command_callback = command_callback;
    g_ble_service.initialized      = true;
    g_ble_service.started         = false;
    g_ble_service.connected       = false;
    g_ble_service.notify_enabled  = false;

    return BLE_SERVICE_OK;
}

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

void ble_service_set_connected(bool connected)
{
    g_ble_service.connected      = connected;
    g_ble_service.notify_enabled = connected && g_ble_service.notify_enabled;
}

void ble_service_set_notify_enabled(bool enabled)
{
    g_ble_service.notify_enabled = enabled && g_ble_service.connected;
}

bool ble_service_is_connected(void)
{
    return g_ble_service.connected;
}

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
