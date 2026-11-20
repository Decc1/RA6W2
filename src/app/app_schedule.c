#include "app_schedule.h"

#include <stddef.h>
#include <stdint.h>

#include <FreeRTOS.h>
#include <task.h>

#include "../config/app_config.h"

typedef struct st_app_schedule_context
{
    uint32_t next_sample_ms;
    uint32_t next_ble_notify_ms;
    uint32_t next_wifi_sync_ms;
    bool     initialized;
} app_schedule_context_t;

static app_schedule_context_t g_app_schedule;

/* 作用: 使用有符号差值判断时间是否已到达，支持 uint32_t 毫秒计数自然回绕。 */
static bool app_schedule_time_reached(uint32_t now_ms, uint32_t due_ms)
{
    return (int32_t) (now_ms - due_ms) >= 0;
}

/* 作用: 判断一个独立周期是否到期，并跳过已经错过的周期以避免恢复后连续补跑。 */
static bool app_schedule_event_due(uint32_t now_ms, uint32_t * p_next_due_ms, uint32_t period_ms)
{
    bool due = false;

    if ((NULL == p_next_due_ms) || (0U == period_ms))
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (g_app_schedule.initialized && app_schedule_time_reached(now_ms, *p_next_due_ms))
    {
        do
        {
            *p_next_due_ms += period_ms;
        } while (app_schedule_time_reached(now_ms, *p_next_due_ms));
        due = true;
    }
    taskEXIT_CRITICAL();

    return due;
}

/* 作用: 使用当前毫秒时间初始化采样、BLE Notify 和 Wi-Fi 同步的周期调度状态。 */
void app_schedule_init(uint32_t now_ms)
{
    taskENTER_CRITICAL();
    g_app_schedule.next_sample_ms = now_ms;
    g_app_schedule.next_ble_notify_ms = now_ms;
    g_app_schedule.next_wifi_sync_ms = now_ms;
    g_app_schedule.initialized = true;
    taskEXIT_CRITICAL();
}

/* 作用: 判断当前是否到达温湿度采样周期，到期时仅返回一次 true。 */
bool app_schedule_sample_due(uint32_t now_ms)
{
    return app_schedule_event_due(now_ms,
                                  &g_app_schedule.next_sample_ms,
                                  APP_SAMPLE_PERIOD_MS);
}

/* 作用: 判断当前是否到达 BLE Notify 周期，到期时仅返回一次 true。 */
bool app_schedule_ble_notify_due(uint32_t now_ms)
{
    return app_schedule_event_due(now_ms,
                                  &g_app_schedule.next_ble_notify_ms,
                                  APP_BLE_NOTIFY_PERIOD_MS);
}

/* 作用: 判断当前是否到达 Wi-Fi/MQTT 同步周期，到期时仅返回一次 true。 */
bool app_schedule_wifi_sync_due(uint32_t now_ms)
{
    return app_schedule_event_due(now_ms,
                                  &g_app_schedule.next_wifi_sync_ms,
                                  APP_WIFI_SYNC_PERIOD_MS);
}

/* 作用: 使用模拟毫秒时间验证三个周期、跨周期跳过和时间戳回绕处理。 */
bool app_schedule_self_test(void)
{
    app_schedule_init(1000U);
    if (!app_schedule_sample_due(1000U) ||
        app_schedule_sample_due(1001U) ||
        app_schedule_sample_due(10999U) ||
        !app_schedule_sample_due(11000U) ||
        !app_schedule_sample_due(35000U) ||
        app_schedule_sample_due(40999U) ||
        !app_schedule_sample_due(41000U))
    {
        return false;
    }

    if (!app_schedule_ble_notify_due(1000U) ||
        app_schedule_ble_notify_due(10999U) ||
        !app_schedule_ble_notify_due(11000U) ||
        !app_schedule_wifi_sync_due(1000U) ||
        app_schedule_wifi_sync_due(900999U) ||
        !app_schedule_wifi_sync_due(901000U))
    {
        return false;
    }

    app_schedule_init(UINT32_MAX - 5000U);
    if (!app_schedule_sample_due(UINT32_MAX - 5000U) ||
        app_schedule_sample_due(4998U) ||
        !app_schedule_sample_due(4999U))
    {
        return false;
    }

    app_schedule_init(0U);
    return true;
}
