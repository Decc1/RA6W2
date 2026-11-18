#include "app_main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../config/app_config.h"
#include "../drivers/sht41.h"
#include "../services/telemetry.h"
#include "../services/telemetry_payload.h"

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
static uint32_t app_get_uptime_ms(void)
{
    return (uint32_t) ((uint32_t) xTaskGetTickCount() * (uint32_t) portTICK_PERIOD_MS);
}

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
        }
        else
        {
            telemetry_record_failure(status, uptime_ms);
        }

        /* BLE/Wi-Fi 任务后续从 telemetry 读取最新数据。 */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}

void app_main(void)
{
    telemetry_init();
    configASSERT(HYGRO_STATUS_OK == sht41_init());
    configASSERT(telemetry_payload_self_test());

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

void vApplicationGetIdleTaskMemory(StaticTask_t ** ppx_idle_task_tcb_buffer,
                                   StackType_t **  ppx_idle_task_stack_buffer,
                                   configSTACK_DEPTH_TYPE * p_idle_task_stack_size)
{
    *ppx_idle_task_tcb_buffer   = &g_idle_task_tcb;
    *ppx_idle_task_stack_buffer = g_idle_task_stack;
    *p_idle_task_stack_size     = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t ** ppx_timer_task_tcb_buffer,
                                    StackType_t **  ppx_timer_task_stack_buffer,
                                    configSTACK_DEPTH_TYPE * p_timer_task_stack_size)
{
    *ppx_timer_task_tcb_buffer   = &g_timer_task_tcb;
    *ppx_timer_task_stack_buffer = g_timer_task_stack;
    *p_timer_task_stack_size     = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(TaskHandle_t x_task, char * pc_task_name)
{
    (void) x_task;
    (void) pc_task_name;

    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
