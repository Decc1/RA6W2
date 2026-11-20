#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../app/app_types.h"

/* BLE 服务层返回值；无线协议栈接入后仍保持这组业务接口不变。 */
typedef enum e_ble_service_status
{
    BLE_SERVICE_OK = 0,
    BLE_SERVICE_INVALID_ARGUMENT,
    BLE_SERVICE_NOT_INITIALIZED,
    BLE_SERVICE_NOT_CONNECTED,
    BLE_SERVICE_NOTIFY_DISABLED,
    BLE_SERVICE_NOT_READY,
} ble_service_status_t;

/* 手机通过 GATT Write 写入设备命令时，由应用层提供处理回调。 */
typedef ble_service_status_t (* ble_command_callback_t)(uint8_t const * p_command,
                                                        size_t            command_length);

/* 作用: 抽象底层 GATT Notify 发送接口，真实协议栈和 PC mock 通过绑定切换。 */
typedef ble_service_status_t (* ble_notify_callback_t)(uint8_t const * p_payload,
                                                       size_t            payload_length);

/* 作用: 抽象 BLE 广播和 GATT 服务的启停接口。 */
typedef ble_service_status_t (* ble_lifecycle_callback_t)(void);

typedef struct st_ble_service_platform_ops
{
    ble_lifecycle_callback_t start;
    ble_lifecycle_callback_t stop;
    ble_notify_callback_t    notify_measurement;
} ble_service_platform_ops_t;

/* 初始化 BLE 服务状态；不启动具体无线协议栈。 */
ble_service_status_t ble_service_init(ble_command_callback_t command_callback);

/* 作用: 绑定或解绑 BLE 底层适配回调，NULL 表示当前没有真实无线实现。 */
void ble_service_bind_platform(ble_service_platform_ops_t const * p_platform_ops);

/* 启动/停止 BLE 广播；当前实现只保留协议栈适配入口。 */
ble_service_status_t ble_service_start(void);
ble_service_status_t ble_service_stop(void);

/* 由底层 BLE 连接事件更新连接状态。 */
void ble_service_set_connected(bool connected);
void ble_service_set_notify_enabled(bool enabled);
bool ble_service_is_connected(void);

/*
 * 将最新测量值交给 BLE 层发送 Notify。
 * 真正的 GATT Notify 由无线协议栈适配文件完成，应用层不直接调用协议栈 API。
 */
ble_service_status_t ble_service_notify_measurement(hygro_measurement_t const * p_measurement);

/* 底层收到 GATT Write 后调用，统一转交到应用层命令回调。 */
ble_service_status_t ble_service_handle_write(uint8_t const * p_command,
                                              size_t            command_length);

/* 作用: 验证 BLE 服务状态、Notify 回调和连接订阅条件。 */
bool ble_service_self_test(void);

#endif /* BLE_SERVICE_H */
