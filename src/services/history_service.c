#include "history_service.h"

#include <limits.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "../config/app_config.h"

/*
 * 本模块只负责把连续采样数据聚合成固定时间窗口。
 * Flash 的页擦除、CRC、环形队列及掉电恢复应由后续 storage_flash 模块实现，
 * 并通过 history_append_callback_t 接入；这里不会把“未写入”伪装成“已保存”。
 */
typedef struct st_history_accumulator
{
    uint32_t window_start_uptime_ms;
    uint32_t first_sample_sequence;
    int64_t  temperature_sum_centi_c;
    uint32_t humidity_sum_centi_rh;
    int16_t  temperature_min_centi_c;
    int16_t  temperature_max_centi_c;
    uint16_t humidity_min_centi_rh;
    uint16_t humidity_max_centi_rh;
    uint16_t sample_count;
    bool     active;
} history_accumulator_t;

typedef struct st_history_service_context
{
    history_append_callback_t append_callback;
    history_accumulator_t     accumulator;
    history_record_t          pending_record;
    bool                      has_pending_record;
    bool                      pending_flush_in_progress;
    bool                      window_transition_in_progress;
} history_service_context_t;

static history_service_context_t g_history;

/* 将 uptime 对齐到 15 分钟窗口起点；系统 uptime 回绕时允许按 uint32_t 自然回绕。 */
/* 作用：将当前运行时间向下对齐到所属 15 分钟聚合窗口的起点。 */
static uint32_t history_window_start_ms(uint32_t uptime_ms)
{
    return (uptime_ms / APP_HISTORY_WINDOW_MS) * APP_HISTORY_WINDOW_MS;
}

/* 有符号平均值按“四舍五入到最接近整数”计算，避免负温度被错误向零偏移。 */
/* 作用：按四舍五入规则计算窗口内温度平均值，正确处理负温度。 */
static int16_t history_average_temperature(int64_t sum, uint16_t sample_count)
{
    int64_t average;

    if (sum >= 0)
    {
        average = (sum + ((int64_t) sample_count / 2)) / (int64_t) sample_count;
    }
    else
    {
        average = (sum - ((int64_t) sample_count / 2)) / (int64_t) sample_count;
    }

    return (int16_t) average;
}

/* 作用：按四舍五入规则计算窗口内湿度平均值。 */
static uint16_t history_average_humidity(uint32_t sum, uint16_t sample_count)
{
    return (uint16_t) ((sum + ((uint32_t) sample_count / 2U)) / (uint32_t) sample_count);
}

/* 作用：用当前第一笔有效采样数据创建一个新的历史聚合窗口。 */
static void history_start_window(hygro_measurement_t const * p_measurement, uint32_t window_start_ms)
{
    history_accumulator_t * p_accumulator = &g_history.accumulator;

    (void) memset(p_accumulator, 0, sizeof(*p_accumulator));
    p_accumulator->window_start_uptime_ms  = window_start_ms;
    p_accumulator->first_sample_sequence   = p_measurement->sample_sequence;
    p_accumulator->temperature_sum_centi_c = p_measurement->temperature_centi_c;
    p_accumulator->humidity_sum_centi_rh   = p_measurement->humidity_centi_rh;
    p_accumulator->temperature_min_centi_c = p_measurement->temperature_centi_c;
    p_accumulator->temperature_max_centi_c = p_measurement->temperature_centi_c;
    p_accumulator->humidity_min_centi_rh   = p_measurement->humidity_centi_rh;
    p_accumulator->humidity_max_centi_rh   = p_measurement->humidity_centi_rh;
    p_accumulator->sample_count            = 1U;
    p_accumulator->active                  = true;
}

/* 作用：将一笔有效采样累加到当前窗口，同时维护最小值和最大值。 */
static bool history_add_to_window(hygro_measurement_t const * p_measurement)
{
    history_accumulator_t * p_accumulator = &g_history.accumulator;

    /* 10 秒采样下一个窗口仅约 90 条；该保护用于防止异常高频调用导致计数回绕。 */
    if (UINT16_MAX == p_accumulator->sample_count)
    {
        return false;
    }

    p_accumulator->temperature_sum_centi_c += p_measurement->temperature_centi_c;
    p_accumulator->humidity_sum_centi_rh   += p_measurement->humidity_centi_rh;

    if (p_measurement->temperature_centi_c < p_accumulator->temperature_min_centi_c)
    {
        p_accumulator->temperature_min_centi_c = p_measurement->temperature_centi_c;
    }
    if (p_measurement->temperature_centi_c > p_accumulator->temperature_max_centi_c)
    {
        p_accumulator->temperature_max_centi_c = p_measurement->temperature_centi_c;
    }
    if (p_measurement->humidity_centi_rh < p_accumulator->humidity_min_centi_rh)
    {
        p_accumulator->humidity_min_centi_rh = p_measurement->humidity_centi_rh;
    }
    if (p_measurement->humidity_centi_rh > p_accumulator->humidity_max_centi_rh)
    {
        p_accumulator->humidity_max_centi_rh = p_measurement->humidity_centi_rh;
    }

    p_accumulator->sample_count++;
    return true;
}

/* 作用：把当前窗口的统计结果整理成一条待存储的 15 分钟历史记录。 */
static void history_make_pending_record(void)
{
    history_accumulator_t const * p_accumulator = &g_history.accumulator;
    history_record_t *            p_record      = &g_history.pending_record;

    p_record->window_start_uptime_s       = p_accumulator->window_start_uptime_ms / 1000U;
    p_record->first_sample_sequence       = p_accumulator->first_sample_sequence;
    p_record->temperature_average_centi_c = history_average_temperature(
        p_accumulator->temperature_sum_centi_c, p_accumulator->sample_count);
    p_record->temperature_min_centi_c     = p_accumulator->temperature_min_centi_c;
    p_record->temperature_max_centi_c     = p_accumulator->temperature_max_centi_c;
    p_record->humidity_average_centi_rh   = history_average_humidity(
        p_accumulator->humidity_sum_centi_rh, p_accumulator->sample_count);
    p_record->humidity_min_centi_rh       = p_accumulator->humidity_min_centi_rh;
    p_record->humidity_max_centi_rh       = p_accumulator->humidity_max_centi_rh;
    p_record->sample_count                = p_accumulator->sample_count;
    g_history.has_pending_record          = true;
}

/* 作用：清空历史聚合状态，并注册记录完成后的存储回调。 */
void history_service_init(history_append_callback_t append_callback)
{
    taskENTER_CRITICAL();

    (void) memset(&g_history, 0, sizeof(g_history));
    g_history.append_callback = append_callback;

    taskEXIT_CRITICAL();
}

/* 作用：尝试再次写入上次存储失败而暂存在 RAM 中的历史记录。 */
bool history_service_flush_pending(void)
{
    history_append_callback_t append_callback;
    history_record_t          pending_record;

    taskENTER_CRITICAL();

    if (!g_history.has_pending_record)
    {
        taskEXIT_CRITICAL();
        return true;
    }

    /* 作用: 防止多个任务重复写入同一条待持久化的历史记录。 */
    if (g_history.pending_flush_in_progress)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    /* 未接入真实 Flash 前必须明确返回失败，避免业务层误认为历史记录已落盘。 */
    if (NULL == g_history.append_callback)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    /* 作用: 在临界区复制共享数据，存储回调必须在临界区外执行。 */
    append_callback = g_history.append_callback;
    pending_record = g_history.pending_record;
    g_history.pending_flush_in_progress = true;

    taskEXIT_CRITICAL();

    if (!append_callback(&pending_record))
    {
        taskENTER_CRITICAL();
        g_history.pending_flush_in_progress = false;
        taskEXIT_CRITICAL();
        return false;
    }

    taskENTER_CRITICAL();
    g_history.has_pending_record = false;
    g_history.pending_flush_in_progress = false;
    taskEXIT_CRITICAL();

    return true;
}

/* 作用：接收一笔实时采样并完成窗口累加、跨窗口结算和待存储处理。 */
bool history_service_on_measurement(hygro_measurement_t const * p_measurement, uint32_t uptime_ms)
{
    uint32_t const window_start_ms = history_window_start_ms(uptime_ms);
    bool           added_to_window;

    if ((NULL == p_measurement) || !p_measurement->valid)
    {
        return false;
    }

    /* 每次采样都顺带重试一次此前未能写入 Flash 的记录。 */
    if (!history_service_flush_pending())
    {
        return false;
    }

    taskENTER_CRITICAL();

    /* 作用: 防止跨窗口存储完成前并发采样覆盖新窗口首条数据。 */
    if (g_history.window_transition_in_progress)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    if (!g_history.accumulator.active)
    {
        history_start_window(p_measurement, window_start_ms);
        taskEXIT_CRITICAL();
        return true;
    }

    if (window_start_ms == g_history.accumulator.window_start_uptime_ms)
    {
        added_to_window = history_add_to_window(p_measurement);
        taskEXIT_CRITICAL();
        return added_to_window;
    }

    /* 新时间窗口到来：先完成上一窗口的记录，确认成功交给存储层后再开始新窗口。 */
    history_make_pending_record();
    g_history.accumulator.active = false;
    g_history.window_transition_in_progress = true;

    taskEXIT_CRITICAL();

    if (!history_service_flush_pending())
    {
        taskENTER_CRITICAL();
        g_history.window_transition_in_progress = false;
        taskEXIT_CRITICAL();
        return false;
    }

    taskENTER_CRITICAL();
    history_start_window(p_measurement, window_start_ms);
    g_history.window_transition_in_progress = false;
    taskEXIT_CRITICAL();

    return true;
}

/* 作用：查询是否仍有因存储失败而等待重试的历史记录。 */
bool history_service_has_pending_record(void)
{
    bool has_pending_record;

    taskENTER_CRITICAL();
    has_pending_record = g_history.has_pending_record;
    taskEXIT_CRITICAL();

    return has_pending_record;
}

static history_record_t g_history_test_record;
static bool             g_history_test_store_enabled;
static uint32_t         g_history_test_write_count;

/* 作用：历史服务自检使用的模拟存储回调，可模拟写入成功或失败。 */
static bool history_test_append(history_record_t const * p_record)
{
    if (!g_history_test_store_enabled || (NULL == p_record))
    {
        return false;
    }

    g_history_test_record = *p_record;
    g_history_test_write_count++;
    return true;
}

/* 作用：验证窗口聚合、均值极值计算及存储失败后的重试逻辑。 */
bool history_service_self_test(void)
{
    hygro_measurement_t measurement =
    {
        .temperature_centi_c = 2000,
        .humidity_centi_rh   = 4000U,
        .battery_mv          = 3850U,
        .sample_sequence     = 10U,
        .valid               = true,
    };

    (void) memset(&g_history_test_record, 0, sizeof(g_history_test_record));
    g_history_test_store_enabled = true;
    g_history_test_write_count   = 0U;
    history_service_init(history_test_append);

    if (!history_service_on_measurement(&measurement, 0U))
    {
        return false;
    }

    measurement.temperature_centi_c = 2200;
    measurement.humidity_centi_rh   = 4400U;
    measurement.sample_sequence     = 11U;
    if (!history_service_on_measurement(&measurement, 300000U))
    {
        return false;
    }

    measurement.temperature_centi_c = 2600;
    measurement.humidity_centi_rh   = 4600U;
    measurement.sample_sequence     = 12U;
    if (!history_service_on_measurement(&measurement, APP_HISTORY_WINDOW_MS) ||
        (1U != g_history_test_write_count) ||
        (0U != g_history_test_record.window_start_uptime_s) ||
        (10U != g_history_test_record.first_sample_sequence) ||
        (2100 != g_history_test_record.temperature_average_centi_c) ||
        (2000 != g_history_test_record.temperature_min_centi_c) ||
        (2200 != g_history_test_record.temperature_max_centi_c) ||
        (4200U != g_history_test_record.humidity_average_centi_rh) ||
        (4000U != g_history_test_record.humidity_min_centi_rh) ||
        (4400U != g_history_test_record.humidity_max_centi_rh) ||
        (2U != g_history_test_record.sample_count))
    {
        return false;
    }

    g_history_test_store_enabled = false;
    measurement.sample_sequence  = 13U;
    if (history_service_on_measurement(&measurement, 2U * APP_HISTORY_WINDOW_MS) ||
        !history_service_has_pending_record())
    {
        return false;
    }

    g_history_test_store_enabled = true;
    if (!history_service_flush_pending() || history_service_has_pending_record())
    {
        return false;
    }

    history_service_init(NULL);
    return true;
}
