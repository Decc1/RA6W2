#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <stdbool.h>

#include "ble_protocol.h"
#include "wifi_provisioning.h"

/* Wi-Fi 服务对应用层公开的状态；不直接暴露底层无线驱动的私有状态。 */
typedef enum e_wifi_service_state
{
    WIFI_SERVICE_STATE_UNINITIALIZED = 0,
    WIFI_SERVICE_STATE_UNPROVISIONED,
    WIFI_SERVICE_STATE_CREDENTIALS_SAVED,
    WIFI_SERVICE_STATE_CONNECTING,
    WIFI_SERVICE_STATE_WAITING_FOR_IP,
    WIFI_SERVICE_STATE_CONNECTED,
    WIFI_SERVICE_STATE_CONNECT_FAILED,
} wifi_service_state_t;

/*
 * 硬件适配层接口。后续接入 RA6W2 Wi-Fi 与加密存储时，只实现此结构中的回调。
 * save_credentials_encrypted 必须在返回前复制或安全保存凭据，禁止保存指针。
 * connect_saved_network 仅启动异步连接；连接和 IP 结果通过 notify 接口回报。
 */
typedef struct st_wifi_service_platform_ops
{
    bool (* save_credentials_encrypted)(wifi_credentials_t const * p_credentials);
    bool (* start_connect_saved_network)(void);
    void (* disconnect)(void);
} wifi_service_platform_ops_t;

/* 初始化 Wi-Fi 服务。无开发板阶段传入 NULL，所有真实联网请求会返回 NOT_READY。 */
void wifi_service_init(wifi_service_platform_ops_t const * p_platform_ops);

/* 供 BLE 配网状态机在 COMMIT 时调用：安全保存凭据并开始异步连接。 */
ble_protocol_reply_status_t wifi_service_commit_credentials(wifi_credentials_t const * p_credentials);

/* 供底层 Wi-Fi 事件回调调用，分别表示链路关联结果和 DHCP/IP 获取完成。 */
void wifi_service_notify_link_result(bool connected);
void wifi_service_notify_ip_acquired(void);

/* 获取当前 Wi-Fi 业务状态，供 BLE 回复、LED 或后续 MQTT 服务使用。 */
wifi_service_state_t wifi_service_get_state(void);

/* 主动断开当前网络；无真实硬件适配层时只更新软件状态。 */
void wifi_service_disconnect(void);

/* 无硬件自检：验证保存凭据、连接和获取 IP 的状态迁移。 */
bool wifi_service_self_test(void);

#endif /* WIFI_SERVICE_H */
