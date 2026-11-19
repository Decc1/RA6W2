#include "wifi_service.h"

#include <string.h>

typedef struct st_wifi_service_context
{
    wifi_service_platform_ops_t platform_ops;
    wifi_service_state_t        state;
    bool                        initialized;
    bool                        platform_ready;
} wifi_service_context_t;

static wifi_service_context_t g_wifi_service;

/* 作用：初始化 Wi-Fi 服务状态机，并绑定安全存储和联网平台适配回调。 */
void wifi_service_init(wifi_service_platform_ops_t const * p_platform_ops)
{
    (void) memset(&g_wifi_service, 0, sizeof(g_wifi_service));

    if (NULL != p_platform_ops)
    {
        g_wifi_service.platform_ops = *p_platform_ops;
        g_wifi_service.platform_ready =
            (NULL != g_wifi_service.platform_ops.save_credentials_encrypted) &&
            (NULL != g_wifi_service.platform_ops.start_connect_saved_network);
    }

    g_wifi_service.initialized = true;
    g_wifi_service.state = WIFI_SERVICE_STATE_UNPROVISIONED;
}

/* 作用：安全保存 BLE 收到的 Wi-Fi 凭据，并请求连接已保存网络。 */
ble_protocol_reply_status_t wifi_service_commit_credentials(wifi_credentials_t const * p_credentials)
{
    if ((NULL == p_credentials) || !g_wifi_service.initialized)
    {
        return BLE_PROTOCOL_REPLY_INVALID_PAYLOAD;
    }

    if (!g_wifi_service.platform_ready)
    {
        return BLE_PROTOCOL_REPLY_NOT_READY;
    }

    if (!g_wifi_service.platform_ops.save_credentials_encrypted(p_credentials))
    {
        g_wifi_service.state = WIFI_SERVICE_STATE_UNPROVISIONED;
        return BLE_PROTOCOL_REPLY_FAILED;
    }

    g_wifi_service.state = WIFI_SERVICE_STATE_CREDENTIALS_SAVED;
    if (!g_wifi_service.platform_ops.start_connect_saved_network())
    {
        g_wifi_service.state = WIFI_SERVICE_STATE_CONNECT_FAILED;
        return BLE_PROTOCOL_REPLY_FAILED;
    }

    g_wifi_service.state = WIFI_SERVICE_STATE_CONNECTING;
    return BLE_PROTOCOL_REPLY_OK;
}

/* 作用：接收无线驱动的关联结果事件，并更新到等待 IP 或连接失败状态。 */
void wifi_service_notify_link_result(bool connected)
{
    if (!g_wifi_service.initialized)
    {
        return;
    }

    g_wifi_service.state = connected ? WIFI_SERVICE_STATE_WAITING_FOR_IP :
                                       WIFI_SERVICE_STATE_CONNECT_FAILED;
}

/* 作用：接收 DHCP 成功事件，将 Wi-Fi 状态切换为已联网。 */
void wifi_service_notify_ip_acquired(void)
{
    if (WIFI_SERVICE_STATE_WAITING_FOR_IP == g_wifi_service.state)
    {
        g_wifi_service.state = WIFI_SERVICE_STATE_CONNECTED;
    }
}

/* 作用：查询当前 Wi-Fi 服务状态，供上传调度和 App 状态上报使用。 */
wifi_service_state_t wifi_service_get_state(void)
{
    return g_wifi_service.state;
}

/* 作用：主动断开 Wi-Fi，降低待机功耗但保留已保存的网络凭据。 */
void wifi_service_disconnect(void)
{
    if (!g_wifi_service.initialized)
    {
        return;
    }

    if (NULL != g_wifi_service.platform_ops.disconnect)
    {
        g_wifi_service.platform_ops.disconnect();
    }

    g_wifi_service.state = WIFI_SERVICE_STATE_CREDENTIALS_SAVED;
}

/* 作用：Wi-Fi 服务自检使用的模拟加密保存回调。 */
static bool wifi_service_test_save_credentials(wifi_credentials_t const * p_credentials)
{
    return (NULL != p_credentials) && (0U != p_credentials->ssid_length) &&
           (0U != p_credentials->password_length);
}

/* 作用：Wi-Fi 服务自检使用的模拟连接启动回调。 */
static bool wifi_service_test_start_connect(void)
{
    return true;
}

/* 作用：验证凭据提交后从连接请求、链路建立到获取 IP 的状态迁移。 */
bool wifi_service_self_test(void)
{
    static wifi_service_platform_ops_t const test_ops =
    {
        .save_credentials_encrypted = wifi_service_test_save_credentials,
        .start_connect_saved_network = wifi_service_test_start_connect,
        .disconnect = NULL,
    };
    wifi_credentials_t credentials = {0};

    credentials.ssid[0] = 'T';
    credentials.password[0] = '1';
    credentials.ssid_length = 1U;
    credentials.password_length = 8U;

    wifi_service_init(&test_ops);
    if ((BLE_PROTOCOL_REPLY_OK != wifi_service_commit_credentials(&credentials)) ||
        (WIFI_SERVICE_STATE_CONNECTING != wifi_service_get_state()))
    {
        return false;
    }

    wifi_service_notify_link_result(true);
    if (WIFI_SERVICE_STATE_WAITING_FOR_IP != wifi_service_get_state())
    {
        return false;
    }

    wifi_service_notify_ip_acquired();
    if (WIFI_SERVICE_STATE_CONNECTED != wifi_service_get_state())
    {
        return false;
    }

    wifi_service_init(NULL);
    return (WIFI_SERVICE_STATE_UNPROVISIONED == wifi_service_get_state());
}
