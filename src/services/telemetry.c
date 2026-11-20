#include "telemetry.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

static telemetry_snapshot_t g_telemetry_snapshot;

/* 作用：重置最新测量值、采样统计计数和最近一次错误状态。 */
void telemetry_init(void)
{
    taskENTER_CRITICAL();

    /* 支持重新初始化：清除旧测量值、计数器和错误状态。 */
    g_telemetry_snapshot = (telemetry_snapshot_t) {0};
    g_telemetry_snapshot.last_error = HYGRO_STATUS_NOT_READY;

    taskEXIT_CRITICAL();
}

/* 作用：保存一次有效传感器数据，并更新成功计数、时间戳和错误状态。 */
void telemetry_record_success(hygro_measurement_t const * p_measurement, uint32_t uptime_ms)
{
    if ((NULL == p_measurement) || !p_measurement->valid)
    {
        telemetry_record_failure(HYGRO_STATUS_INVALID_ARGUMENT, uptime_ms);
        return;
    }

    taskENTER_CRITICAL();

    g_telemetry_snapshot.latest_measurement         = *p_measurement;
    g_telemetry_snapshot.last_attempt_uptime_ms     = uptime_ms;
    g_telemetry_snapshot.last_success_uptime_ms     = uptime_ms;
    g_telemetry_snapshot.successful_sample_count++;
    g_telemetry_snapshot.consecutive_failure_count  = 0U;
    g_telemetry_snapshot.last_error                 = HYGRO_STATUS_OK;
    g_telemetry_snapshot.has_valid_measurement      = true;

    taskEXIT_CRITICAL();
}

/* 作用：记录一次采样失败，保留上一次有效数据并累加失败统计。 */
void telemetry_record_failure(hygro_status_t error, uint32_t uptime_ms)
{
    taskENTER_CRITICAL();

    g_telemetry_snapshot.last_attempt_uptime_ms = uptime_ms;
    g_telemetry_snapshot.failed_sample_count++;
    g_telemetry_snapshot.consecutive_failure_count++;
    g_telemetry_snapshot.last_error = (HYGRO_STATUS_OK == error) ? HYGRO_STATUS_IO_ERROR : error;

    taskEXIT_CRITICAL();
}

/* 作用：以临界区保护方式复制完整遥测快照，避免任务并发读写不一致。 */
bool telemetry_get_snapshot(telemetry_snapshot_t * p_snapshot)
{
    if (NULL == p_snapshot)
    {
        return false;
    }

    taskENTER_CRITICAL();

    *p_snapshot = g_telemetry_snapshot;

    taskEXIT_CRITICAL();

    return p_snapshot->has_valid_measurement;
}

/* 作用：仅获取最近一笔有效温湿度测量数据。 */
bool telemetry_get_latest(hygro_measurement_t * p_measurement)
{
    telemetry_snapshot_t snapshot;

    if ((NULL == p_measurement) || !telemetry_get_snapshot(&snapshot))
    {
        return false;
    }

    *p_measurement = snapshot.latest_measurement;
    return true;
}

/* 作用: 验证遥测快照在成功和失败采样后的统计值及历史有效数据保留规则。 */
bool telemetry_self_test(void)
{
    hygro_measurement_t const measurement =
    {
        .temperature_centi_c = 2350,
        .humidity_centi_rh = 4560U,
        .battery_mv = 3850U,
        .sample_sequence = 7U,
        .valid = true,
    };
    telemetry_snapshot_t snapshot = {0};
    hygro_measurement_t latest_measurement = {0};

    telemetry_init();
    if (telemetry_get_snapshot(&snapshot) ||
        (HYGRO_STATUS_NOT_READY != snapshot.last_error))
    {
        return false;
    }

    telemetry_record_success(&measurement, 100U);
    if (!telemetry_get_snapshot(&snapshot) ||
        (1U != snapshot.successful_sample_count) ||
        (0U != snapshot.failed_sample_count) ||
        (100U != snapshot.last_success_uptime_ms) ||
        (HYGRO_STATUS_OK != snapshot.last_error) ||
        !telemetry_get_latest(&latest_measurement) ||
        (measurement.sample_sequence != latest_measurement.sample_sequence))
    {
        return false;
    }

    telemetry_record_failure(HYGRO_STATUS_BUS_TIMEOUT, 200U);
    if (!telemetry_get_snapshot(&snapshot) ||
        (1U != snapshot.failed_sample_count) ||
        (1U != snapshot.consecutive_failure_count) ||
        (200U != snapshot.last_attempt_uptime_ms) ||
        (HYGRO_STATUS_BUS_TIMEOUT != snapshot.last_error) ||
        (measurement.sample_sequence != snapshot.latest_measurement.sample_sequence))
    {
        return false;
    }

    telemetry_init();
    return true;
}
