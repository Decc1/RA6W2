#include "telemetry.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

static telemetry_snapshot_t g_telemetry_snapshot;

void telemetry_init(void)
{
    taskENTER_CRITICAL();

    /* 支持重新初始化：清除旧测量值、计数器和错误状态。 */
    g_telemetry_snapshot = (telemetry_snapshot_t) {0};
    g_telemetry_snapshot.last_error = HYGRO_STATUS_NOT_READY;

    taskEXIT_CRITICAL();
}

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

void telemetry_record_failure(hygro_status_t error, uint32_t uptime_ms)
{
    taskENTER_CRITICAL();

    g_telemetry_snapshot.last_attempt_uptime_ms = uptime_ms;
    g_telemetry_snapshot.failed_sample_count++;
    g_telemetry_snapshot.consecutive_failure_count++;
    g_telemetry_snapshot.last_error = (HYGRO_STATUS_OK == error) ? HYGRO_STATUS_IO_ERROR : error;

    taskEXIT_CRITICAL();
}

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
