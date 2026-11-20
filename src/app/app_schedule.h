#ifndef APP_SCHEDULE_H
#define APP_SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

/* 作用: 使用当前毫秒时间初始化采样、BLE Notify 和 Wi-Fi 同步的周期调度状态。 */
void app_schedule_init(uint32_t now_ms);

/* 作用: 判断当前是否到达温湿度采样周期，到期时仅返回一次 true。 */
bool app_schedule_sample_due(uint32_t now_ms);

/* 作用: 判断当前是否到达 BLE Notify 周期，到期时仅返回一次 true。 */
bool app_schedule_ble_notify_due(uint32_t now_ms);

/* 作用: 判断当前是否到达 Wi-Fi/MQTT 同步周期，到期时仅返回一次 true。 */
bool app_schedule_wifi_sync_due(uint32_t now_ms);

/* 作用: 使用模拟毫秒时间验证三个周期、跨周期跳过和时间戳回绕处理。 */
bool app_schedule_self_test(void);

#endif /* APP_SCHEDULE_H */
