#ifndef NETWORK_SYNC_SERVICE_H
#define NETWORK_SYNC_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../app/hygro_status.h"
#include "telemetry.h"

/* 作用: 定义一次低功耗 Wi-Fi/MQTT 同步会话的内部状态。 */
typedef enum e_network_sync_state
{
    NETWORK_SYNC_STATE_UNINITIALIZED = 0,
    NETWORK_SYNC_STATE_IDLE,
    NETWORK_SYNC_STATE_WAITING_WIFI,
    NETWORK_SYNC_STATE_WAITING_MQTT,
    NETWORK_SYNC_STATE_PUBLISHING,
} network_sync_state_t;

/* 作用: 抽象 Wi-Fi、MQTT、遥测快照和断开操作，使服务层不依赖具体无线 SDK。 */
typedef struct st_network_sync_platform_ops
{
    hygro_status_t (* start_wifi)(void);
    bool (* is_wifi_connected)(void);
    hygro_status_t (* process_mqtt)(bool wifi_connected);
    hygro_status_t (* publish_telemetry)(telemetry_snapshot_t const * p_snapshot);
    bool (* get_telemetry_snapshot)(telemetry_snapshot_t * p_snapshot);
    void (* disconnect_mqtt)(void);
    void (* disconnect_wifi)(void);
} network_sync_platform_ops_t;

/* 作用: 初始化网络同步服务，NULL 表示尚未绑定真实 Wi-Fi/MQTT 平台适配层。 */
void network_sync_service_init(network_sync_platform_ops_t const * p_platform_ops);

/* 作用: 请求开始一次同步会话，异步启动 Wi-Fi 并设置连接超时。 */
hygro_status_t network_sync_service_request(uint32_t now_ms, uint32_t wifi_timeout_ms);

/* 作用: 推进一次同步状态机，调用频率由应用网络任务决定。 */
hygro_status_t network_sync_service_process(uint32_t now_ms);

/* 作用: 获取当前网络同步会话状态。 */
network_sync_state_t network_sync_service_get_state(void);

/* 作用: 获取最近一次同步会话的最终结果。 */
hygro_status_t network_sync_service_get_last_result(void);

/* 作用: 验证连接、发布、超时、断开及 mock 回调调用顺序。 */
bool network_sync_service_self_test(void);

#endif /* NETWORK_SYNC_SERVICE_H */
