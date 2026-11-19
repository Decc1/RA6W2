#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* SHT41 温湿度采样周期：第一版固定为 10 秒。 */
#define APP_SAMPLE_PERIOD_MS             (10000U)

/* BLE 已连接时，实时数据的 Notify 周期。 */
#define APP_BLE_NOTIFY_PERIOD_MS         (10000U)

/* Wi-Fi 短连接同步周期：15 分钟。 */
#define APP_WIFI_SYNC_PERIOD_MS          (15U * 60U * 1000U)

/* 历史曲线聚合周期：每 15 分钟生成一笔温湿度统计记录。 */
#define APP_HISTORY_WINDOW_MS             (15U * 60U * 1000U)

/*
 * 无板阶段的历史记录 RAM 缓冲区容量：96 条 × 15 分钟 = 24 小时。
 * 此值仅用于 mock 存储；真实产品将根据最终 Flash 容量和历史保留策略确定。
 */
#define APP_HISTORY_RAM_CAPACITY          (96U)

/* 采样任务使用静态内存，避免运行时动态申请内存。 */
#define APP_SAMPLE_TASK_STACK_WORDS      (512U)
#define APP_SAMPLE_TASK_PRIORITY         (2U)

#endif /* APP_CONFIG_H */
