#ifndef DEVICE_COMMAND_H
#define DEVICE_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 命令结果帧最大长度：6 字节头 + 12 字节实时测量值或设备信息。 */
#define DEVICE_COMMAND_MAX_REPLY_SIZE     (64U)

/* 初始化命令分发器；当前 Wi-Fi 提交回调尚未接入真实无线服务。 */
void device_command_init(void);

/*
 * 处理一帧来自 BLE 控制 Characteristic 的原始 Write 数据，并编码回复帧。
 * encrypted_and_bonded 由未来 BLE 协议栈适配层传入，不能由 App 自行声明。
 */
bool device_command_handle_ble_write(uint8_t const * p_request,
                                     size_t          request_length,
                                     bool            encrypted_and_bonded,
                                     uint8_t *       p_reply,
                                     size_t          reply_size,
                                     size_t *        p_reply_length);

/* 无硬件自检：验证最新测量值查询和无效帧回复。 */
bool device_command_self_test(void);

#endif /* DEVICE_COMMAND_H */
