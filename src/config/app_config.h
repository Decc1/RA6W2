#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* SHT41 温湿度采样周期：第一版固定为 10 秒。 */
#define APP_SAMPLE_PERIOD_MS             (10000U)

/* BLE 已连接时，实时数据的 Notify 周期。 */
#define APP_BLE_NOTIFY_PERIOD_MS         (10000U)

/* Wi-Fi 短连接同步周期：15 分钟。 */
#define APP_WIFI_SYNC_PERIOD_MS          (15U * 60U * 1000U)

/* 采样任务使用静态内存，避免运行时动态申请内存。 */
#define APP_SAMPLE_TASK_STACK_WORDS      (512U)
#define APP_SAMPLE_TASK_PRIORITY         (2U)

#endif /* APP_CONFIG_H */
