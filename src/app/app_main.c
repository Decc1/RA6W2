#include "app_main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../config/app_config.h"
#include "../drivers/sht41.h"
#include "../services/ble_protocol.h"
#include "../services/device_command.h"
#include "../services/device_identity.h"
#include "../services/history_service.h"
#include "../services/history_payload.h"
#include "../services/history_storage.h"
#include "../services/mqtt_protocol.h"
#include "../services/ota_service.h"
#include "../services/telemetry.h"
#include "../services/telemetry_payload.h"
#include "../services/wifi_provisioning.h"
#include "../services/wifi_service.h"

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

/* 作用：按固定周期读取 SHT41，并把成功或失败结果写入遥测与历史服务。 */
static void app_sample_task(void * p_argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void) p_argument;

    for (;;)
    {
        hygro_measurement_t measurement;
        hygro_status_t      status;
        uint32_t const      uptime_ms = app_get_uptime_ms();

        status = sht41_read(&measurement);

        if (HYGRO_STATUS_OK == status)
        {
            telemetry_record_success(&measurement, uptime_ms);
            (void) history_service_on_measurement(&measurement, uptime_ms);
        }
        else
        {
            telemetry_record_failure(status, uptime_ms);
        }

        /* BLE/Wi-Fi 任务后续从 telemetry 读取最新数据。 */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}

/* 作用：初始化全部应用服务、执行无硬件自检、创建采样任务并启动调度器。 */
void app_main(void)
{
    telemetry_init();
    configASSERT(HYGRO_STATUS_OK == sht41_init());
    configASSERT(telemetry_payload_self_test());
    configASSERT(ble_protocol_self_test());
    configASSERT(device_identity_self_test());
    configASSERT(wifi_provisioning_self_test());
    configASSERT(wifi_service_self_test());
    configASSERT(mqtt_protocol_self_test());
    configASSERT(ota_service_self_test());
    configASSERT(device_command_self_test());
    configASSERT(history_service_self_test());
    configASSERT(history_storage_self_test());
    configASSERT(history_payload_self_test());

    /* 命令自检会写入一笔模拟遥测数据；进入正常业务前必须清除。 */
    telemetry_init();
    device_identity_init(NULL);
    device_command_init();
    ota_service_init(NULL);
    /* 当前使用 RAM 模拟后端；真实 Flash 到位后只替换 history_storage 的内部实现。 */
    history_storage_init();
    history_service_init(history_storage_append);

    g_sample_task_handle = xTaskCreateStatic(app_sample_task,
                                              "sample",
                                              APP_SAMPLE_TASK_STACK_WORDS,
                                              NULL,
                                              APP_SAMPLE_TASK_PRIORITY,
                                              g_sample_task_stack,
                                              &g_sample_task_tcb);
    configASSERT(NULL != g_sample_task_handle);

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
