#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <stdbool.h>
#include <stdint.h>

#include "ble_protocol.h"

/* 第一版仅支持 2.4 GHz WPA2/WPA3-Personal，SSID 和密码按 UTF-8 字节序列传输。 */
#define WIFI_PROVISIONING_MAX_SSID_LENGTH       (32U)
#define WIFI_PROVISIONING_MIN_PASSWORD_LENGTH   (8U)
#define WIFI_PROVISIONING_MAX_PASSWORD_LENGTH   (63U)

/* 配网过程中密码只暂存于 RAM，提交时由 Wi-Fi 服务负责加密持久化和联网。 */
typedef struct st_wifi_credentials
{
    uint8_t ssid[WIFI_PROVISIONING_MAX_SSID_LENGTH];
    uint8_t password[WIFI_PROVISIONING_MAX_PASSWORD_LENGTH];
    uint8_t ssid_length;
    uint8_t password_length;
} wifi_credentials_t;

/*
 * 由未来 Wi-Fi 服务实现：保存凭据并发起连接，返回 BLE 协议统一状态码。
 * 回调必须在函数返回前复制或安全保存凭据，不得保留 p_credentials 指针。
 */
typedef ble_protocol_reply_status_t (* wifi_provisioning_commit_callback_t)(wifi_credentials_t const * p_credentials);

/* 初始化配网状态机。传入 NULL 仅允许验证帧，提交配网会返回 NOT_READY。 */
void wifi_provisioning_init(wifi_provisioning_commit_callback_t commit_callback);

/*
 * 执行一条配网命令。
 * encrypted_and_bonded 必须由真实 BLE 协议栈根据连接安全状态填写；为 false 时拒绝所有配网操作。
 */
ble_protocol_reply_status_t wifi_provisioning_handle_command(ble_protocol_command_t const * p_command,
                                                              bool                         encrypted_and_bonded);

/* 丢弃 RAM 中尚未提交的凭据；断连、超时、恢复出厂时均应调用。 */
void wifi_provisioning_abort(void);

/* 无硬件自检：验证帧顺序、长度检查和安全状态检查。 */
bool wifi_provisioning_self_test(void);

#endif /* WIFI_PROVISIONING_H */
