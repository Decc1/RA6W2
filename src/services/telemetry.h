#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../app/app_types.h"

/*
 * 采样状态快照。
 * BLE、Wi-Fi 和历史存储模块通过读取快照获取同一时刻的一致数据。
 * uptime 字段是系统启动后的毫秒数，不是绝对日期时间。
 */
typedef struct st_telemetry_snapshot
{
    hygro_measurement_t latest_measurement;
    uint32_t            last_attempt_uptime_ms;
    uint32_t            last_success_uptime_ms;
    uint32_t            successful_sample_count;
    uint32_t            failed_sample_count;
    uint32_t            consecutive_failure_count;
    hygro_status_t      last_error;
    bool                has_valid_measurement;
} telemetry_snapshot_t;

/* 在系统启动时清空所有测量数据和统计状态。 */
void telemetry_init(void);

/* 记录一次成功的传感器采样。 */
void telemetry_record_success(hygro_measurement_t const * p_measurement, uint32_t uptime_ms);

/* 记录一次失败的传感器采样，不覆盖上一次有效的温湿度数据。 */
void telemetry_record_failure(hygro_status_t error, uint32_t uptime_ms);

/* 获取一份完整且一致的状态快照。 */
bool telemetry_get_snapshot(telemetry_snapshot_t * p_snapshot);

/* 仅获取最近一笔有效温湿度数据，供简单调用场景使用。 */
bool telemetry_get_latest(hygro_measurement_t * p_measurement);

#endif /* TELEMETRY_H */
