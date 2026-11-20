#include "app_main.h"
#include "app_schedule.h"
#include "test_runner.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../config/app_config.h"
#include "../drivers/battery.h"
#include "../drivers/sht41.h"
#include "../services/ble_protocol.h"
#include "../services/ble_service.h"
#include "../services/device_command.h"
#include "../services/device_identity.h"
#include "../services/history_service.h"
#include "../services/history_payload.h"
#include "../services/history_storage.h"
#include "../services/mqtt_protocol.h"
#include "../services/mqtt_service.h"
#include "../services/network_sync_service.h"
#include "../services/ota_service.h"
#include "../services/telemetry.h"
#include "../services/telemetry_payload.h"
#include "../services/wifi_provisioning.h"
#include "../services/wifi_service.h"

#include "../services/hygro_log.h"

/* FreeRTOS 静态任务内存回调。 */
void vApplicationGetIdleTaskMemory(StaticTask_t ** ppx_idle_task_tcb_buffer,
                                   StackType_t **  ppx_idle_task_stack_buffer,
                                   configSTACK_DEPTH_TYPE * p_idle_task_stack_size);
void vApplicationGetTimerTaskMemory(StaticTask_t ** ppx_timer_task_tcb_buffer,
                                    StackType_t **  ppx_timer_task_stack_buffer,
                                    configSTACK_DEPTH_TYPE * p_timer_task_stack_size);

/* 采样任务及 FreeRTOS 内部任务都采用静态内存，适合低功耗产品。 */
static StaticTask_t g_sample_task_tcb;
static StackType_t  g_sample_task_stack[APP_SAMPLE_TASK_STACK_WORDS];
static TaskHandle_t g_sample_task_handle;
static StaticTask_t g_ble_notify_task_tcb;
static StackType_t  g_ble_notify_task_stack[APP_BLE_NOTIFY_TASK_STACK_WORDS];
static TaskHandle_t g_ble_notify_task_handle;
static StaticTask_t g_network_task_tcb;
static StackType_t  g_network_task_stack[APP_NETWORK_TASK_STACK_WORDS];
static TaskHandle_t g_network_task_handle;

/* RAFW 配置要求应用提供 Idle 和 Timer 服务任务的静态内存。 */
static StaticTask_t g_idle_task_tcb;
static StackType_t  g_idle_task_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t g_timer_task_tcb;
static StackType_t  g_timer_task_stack[configTIMER_TASK_STACK_DEPTH];

/* 将 FreeRTOS 时钟节拍转换为系统启动后的毫秒数；溢出后允许自然回绕。 */
/* 作用：把 FreeRTOS 的系统节拍换算为毫秒运行时间，供采样和历史记录统一使用。 */
static uint32_t app_get_uptime_ms(void)
{
    return (uint32_t) ((uint32_t) xTaskGetTickCount() * (uint32_t) portTICK_PERIOD_MS);
}

/* 作用: 根据 Wi-Fi 服务状态发起一次已保存凭据的连接，已连接时不重复关联。 */
static hygro_status_t app_network_start_wifi(void)
{
    return (WIFI_SERVICE_STATE_CONNECTED == wifi_service_get_state()) ?
           HYGRO_STATUS_OK : wifi_service_start_saved_network();
}

/* 作用: 向网络同步服务提供当前是否已完成 Wi-Fi 关联和 DHCP 的查询结果。 */
static bool app_network_is_wifi_connected(void)
{
    return WIFI_SERVICE_STATE_CONNECTED == wifi_service_get_state();
}

/* 作用: 向网络同步服务转交 MQTT/TLS 连接及订阅状态机推进请求。 */
static hygro_status_t app_network_process_mqtt(bool wifi_connected)
{
    return mqtt_service_process(wifi_connected);
}

/* 作用: 向网络同步服务转交最新遥测数据的 MQTT QoS 1 上报请求。 */
static hygro_status_t app_network_publish_telemetry(telemetry_snapshot_t const * p_snapshot)
{
    return mqtt_service_publish_telemetry(p_snapshot);
}

/* 作用: 向网络同步服务提供线程安全的最新遥测快照读取接口。 */
static bool app_network_get_telemetry_snapshot(telemetry_snapshot_t * p_snapshot)
{
    return telemetry_get_snapshot(p_snapshot);
}

/* 作用: 在一次同步会话结束时断开 MQTT/TLS，以降低待机功耗。 */
static void app_network_disconnect_mqtt(void)
{
    mqtt_service_disconnect();
}

/* 作用: 在一次同步会话结束时断开 Wi-Fi，以降低待机功耗。 */
static void app_network_disconnect_wifi(void)
{
    wifi_service_disconnect();
}

static network_sync_platform_ops_t const g_network_sync_ops =
{
    .start_wifi = app_network_start_wifi,
    .is_wifi_connected = app_network_is_wifi_connected,
    .process_mqtt = app_network_process_mqtt,
    .publish_telemetry = app_network_publish_telemetry,
    .get_telemetry_snapshot = app_network_get_telemetry_snapshot,
    .disconnect_mqtt = app_network_disconnect_mqtt,
    .disconnect_wifi = app_network_disconnect_wifi,
};

/* 作用：按固定周期读取 SHT41，并把成功或失败结果写入遥测与历史服务。 */
static void app_sample_task(void * p_argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void) p_argument;

    for (;;)
    {
        if (app_schedule_sample_due(app_get_uptime_ms()))
        {
            hygro_measurement_t measurement;
            hygro_status_t      status;
            uint32_t const      uptime_ms = app_get_uptime_ms();

            status = sht41_read(&measurement);

            if (HYGRO_STATUS_OK == status)
            {
                if (HYGRO_STATUS_OK != battery_read_mv(&measurement.battery_mv))
                {
                    /* 作用: 温湿度有效时保留采样结果，电池读取失败以 0 mV 标记未知电量。 */
                    measurement.battery_mv = 0U;
                }
                telemetry_record_success(&measurement, uptime_ms);
                if (!history_service_on_measurement(&measurement, uptime_ms))
                {
                    /* 作用: 历史记录落盘失败时立即重试一次，后续采样周期还会继续重试。 */
                    (void) history_service_flush_pending();
                }
            }
            else
            {
                telemetry_record_failure(status, uptime_ms);
            }
        }

        /* BLE/Wi-Fi 任务后续从 telemetry 读取最新数据。 */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}

/* 作用: 周期读取最新遥测快照，并在 BLE 已连接且已订阅时发送 Notify。 */
static void app_ble_notify_task(void * p_argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void) p_argument;

    for (;;)
    {
        if (app_schedule_ble_notify_due(app_get_uptime_ms()))
        {
            telemetry_snapshot_t snapshot;

            if (ble_service_is_connected() && telemetry_get_snapshot(&snapshot))
            {
                (void) ble_service_notify_measurement(&snapshot.latest_measurement);
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_BLE_NOTIFY_PERIOD_MS));
    }
}

/* 作用: 在 15 分钟周期或 BLE 配网后 Wi-Fi 已连接时，推进一次短连接 MQTT 同步会话。 */
static void app_network_sync_task(void * p_argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void) p_argument;

    for (;;)
    {
        uint32_t const now_ms = app_get_uptime_ms();
        bool const wifi_connected = app_network_is_wifi_connected();
        network_sync_state_t const sync_state = network_sync_service_get_state();

        if (app_schedule_wifi_sync_due(now_ms) ||
            (wifi_connected && (NETWORK_SYNC_STATE_IDLE == sync_state)))
        {
            (void) network_sync_service_request(now_ms, APP_WIFI_CONNECT_TIMEOUT_MS);
        }

        (void) network_sync_service_process(now_ms);
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_NETWORK_TASK_PERIOD_MS));
    }
}

/* 作用：初始化全部应用服务、执行无硬件自检、创建采样任务并启动调度器。 */
void app_main(void)
{
    hygro_log_init(NULL);
    telemetry_init();
    battery_init();
    configASSERT(HYGRO_STATUS_OK == sht41_init());
    configASSERT(test_runner_self_test());
    configASSERT(test_runner_run_all());

    /* 命令自检会写入一笔模拟遥测数据；进入正常业务前必须清除。 */
    telemetry_init();
    battery_init();
    hygro_log_init(NULL);
    configASSERT(HYGRO_STATUS_OK == sht41_init());
    (void) ble_service_init(NULL);
    ble_service_bind_platform(NULL);
    device_identity_init(NULL);
    device_command_init();
    ota_service_init(NULL);
    /* 当前使用 RAM 模拟后端；真实 Flash 到位后只替换 history_storage 的内部实现。 */
    history_storage_init();
    history_service_init(history_storage_append);
    network_sync_service_init(&g_network_sync_ops);
    app_schedule_init(app_get_uptime_ms());

    g_sample_task_handle = xTaskCreateStatic(app_sample_task,
                                              "sample",
                                              APP_SAMPLE_TASK_STACK_WORDS,
                                              NULL,
                                              APP_SAMPLE_TASK_PRIORITY,
                                              g_sample_task_stack,
                                              &g_sample_task_tcb);
    configASSERT(NULL != g_sample_task_handle);

    g_ble_notify_task_handle = xTaskCreateStatic(app_ble_notify_task,
                                                  "ble_notify",
                                                  APP_BLE_NOTIFY_TASK_STACK_WORDS,
                                                  NULL,
                                                  APP_BLE_NOTIFY_TASK_PRIORITY,
                                                  g_ble_notify_task_stack,
                                                  &g_ble_notify_task_tcb);
    configASSERT(NULL != g_ble_notify_task_handle);

    g_network_task_handle = xTaskCreateStatic(app_network_sync_task,
                                               "net_sync",
                                               APP_NETWORK_TASK_STACK_WORDS,
                                               NULL,
                                               APP_NETWORK_TASK_PRIORITY,
                                               g_network_task_stack,
                                               &g_network_task_tcb);
    configASSERT(NULL != g_network_task_handle);

    vTaskStartScheduler();

    /* 正常情况下调度器不会返回；若返回，说明系统初始化失败。 */
    for (;;)
    {
        taskDISABLE_INTERRUPTS();
    }
}

/* 作用：为 FreeRTOS Idle 任务提供静态任务控制块和栈，避免动态分配内存。 */
void vApplicationGetIdleTaskMemory(StaticTask_t ** ppx_idle_task_tcb_buffer,
                                   StackType_t **  ppx_idle_task_stack_buffer,
                                   configSTACK_DEPTH_TYPE * p_idle_task_stack_size)
{
    *ppx_idle_task_tcb_buffer   = &g_idle_task_tcb;
    *ppx_idle_task_stack_buffer = g_idle_task_stack;
    *p_idle_task_stack_size     = configMINIMAL_STACK_SIZE;
}

/* 作用：为 FreeRTOS 软件定时器服务任务提供静态任务控制块和栈。 */
void vApplicationGetTimerTaskMemory(StaticTask_t ** ppx_timer_task_tcb_buffer,
                                    StackType_t **  ppx_timer_task_stack_buffer,
                                    configSTACK_DEPTH_TYPE * p_timer_task_stack_size)
{
    *ppx_timer_task_tcb_buffer   = &g_timer_task_tcb;
    *ppx_timer_task_stack_buffer = g_timer_task_stack;
    *p_timer_task_stack_size     = configTIMER_TASK_STACK_DEPTH;
}

/* 作用：捕获任务栈溢出；当前停止调度以便调试器定位问题。 */
void vApplicationStackOverflowHook(TaskHandle_t x_task, char * pc_task_name)
{
    (void) x_task;
    (void) pc_task_name;

    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
