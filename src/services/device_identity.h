#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>

/* 设备 ID 只用于产品身份、MQTT Topic 和二维码匹配，不得承载密钥或用户信息。 */
#define DEVICE_IDENTITY_MAX_LENGTH      (32U)

/*
 * 平台适配层从工厂预置的受保护 NVM/安全区读取唯一设备 ID。
 * 严禁在每次启动时随机生成 ID，否则云端历史数据和绑定关系会失效。
 */
typedef bool (* device_identity_read_callback_t)(char * p_device_id, size_t device_id_size);

/* 初始化身份模块；无真实存储时传入 NULL，设备 ID 状态为未就绪。 */
void device_identity_init(device_identity_read_callback_t read_callback);

/* 将工厂预置 ID 拷贝到调用者缓冲区；返回 false 表示未读取到合法 ID。 */
bool device_identity_get(char * p_device_id, size_t device_id_size);

/* 无硬件自检：验证 ID 字符和长度限制。 */
bool device_identity_self_test(void);

#endif /* DEVICE_IDENTITY_H */
