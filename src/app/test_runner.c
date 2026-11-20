#include "test_runner.h"
#include "app_schedule.h"

#include <stddef.h>

#include <FreeRTOS.h>
#include <task.h>

#include "hygro_status.h"
#include "../drivers/battery.h"
#include "../drivers/sht41.h"
#include "../services/ble_protocol.h"
#include "../services/ble_service.h"
#include "../services/device_command.h"
#include "../services/device_identity.h"
#include "../services/history_payload.h"
#include "../services/history_service.h"
#include "../services/history_storage.h"
#include "../services/hygro_log.h"
#include "../services/mqtt_protocol.h"
#include "../services/mqtt_service.h"
#include "../services/network_sync_service.h"
#include "../services/ota_service.h"
#include "../services/telemetry.h"
#include "../services/telemetry_payload.h"
#include "../services/wifi_provisioning.h"
#include "../services/wifi_service.h"

typedef bool (* test_runner_case_callback_t)(void);

typedef struct st_test_runner_case
{
    char const *                module_name;
    test_runner_case_callback_t callback;
} test_runner_case_t;

static test_runner_report_t g_test_runner_report;

/* 作用: 执行一组自检用例，汇总通过和失败数量，并在需要时输出模块级日志。 */
static bool test_runner_execute_cases(test_runner_case_t const * p_cases,
                                      size_t                     case_count,
                                      test_runner_report_t *     p_report,
                                      bool                       write_log)
{
    test_runner_report_t report = {0};

    if ((NULL == p_cases) || (NULL == p_report))
    {
        return false;
    }

    for (size_t index = 0U; index < case_count; index++)
    {
        bool const passed = (NULL != p_cases[index].callback) && p_cases[index].callback();

        report.total_count++;
        if (passed)
        {
            report.passed_count++;
        }
        else
        {
            report.failed_count++;
        }

        if (write_log)
        {
            hygro_log_write(passed ? HYGRO_LOG_LEVEL_INFO : HYGRO_LOG_LEVEL_ERROR,
                            p_cases[index].module_name,
                            passed ? HYGRO_STATUS_OK : HYGRO_STATUS_PROTOCOL_ERROR);
        }
    }

    *p_report = report;
    return 0U == report.failed_count;
}

/* 作用: 提供统一执行顺序，避免 app_main 随着模块增加而堆积大量自检调用。 */
bool test_runner_run_all(void)
{
    static test_runner_case_t const g_test_cases[] =
    {
        {"STATUS", hygro_status_self_test},
        {"LOG", hygro_log_self_test},
        {"SCHEDULE", app_schedule_self_test},
        {"SHT41", sht41_self_test},
        {"BATTERY", battery_self_test},
        {"TELEMETRY", telemetry_self_test},
        {"TELEMETRY_PAYLOAD", telemetry_payload_self_test},
        {"BLE_PROTOCOL", ble_protocol_self_test},
        {"BLE_SERVICE", ble_service_self_test},
        {"DEVICE_ID", device_identity_self_test},
        {"WIFI_PROVISION", wifi_provisioning_self_test},
        {"WIFI_SERVICE", wifi_service_self_test},
        {"MQTT_PROTOCOL", mqtt_protocol_self_test},
        {"MQTT_SERVICE", mqtt_service_self_test},
        {"NET_SYNC", network_sync_service_self_test},
        {"OTA", ota_service_self_test},
        {"DEVICE_COMMAND", device_command_self_test},
        {"HISTORY_SERVICE", history_service_self_test},
        {"HISTORY_STORAGE", history_storage_self_test},
        {"HISTORY_PAYLOAD", history_payload_self_test},
    };
    test_runner_report_t report = {0};
    bool const passed = test_runner_execute_cases(g_test_cases,
                                                  sizeof(g_test_cases) / sizeof(g_test_cases[0]),
                                                  &report,
                                                  true);

    taskENTER_CRITICAL();
    g_test_runner_report = report;
    taskEXIT_CRITICAL();

    return passed;
}

/* 作用: 在临界区内复制最近一次自检统计结果，避免与启动自检任务并发读写不一致。 */
void test_runner_get_report(test_runner_report_t * p_report)
{
    if (NULL == p_report)
    {
        return;
    }

    taskENTER_CRITICAL();
    *p_report = g_test_runner_report;
    taskEXIT_CRITICAL();
}

/* 作用: 为统一自检执行器提供固定的成功用例。 */
static bool test_runner_test_pass(void)
{
    return true;
}

/* 作用: 为统一自检执行器提供固定的失败用例。 */
static bool test_runner_test_fail(void)
{
    return false;
}

/* 作用: 验证执行器能够准确统计成功和失败用例，且失败结果不会误判为成功。 */
bool test_runner_self_test(void)
{
    static test_runner_case_t const g_self_test_cases[] =
    {
        {"PASS", test_runner_test_pass},
        {"FAIL", test_runner_test_fail},
    };
    test_runner_report_t report = {0};

    if (test_runner_execute_cases(g_self_test_cases,
                                  sizeof(g_self_test_cases) / sizeof(g_self_test_cases[0]),
                                  &report,
                                  false))
    {
        return false;
    }

    return (2U == report.total_count) && (1U == report.passed_count) &&
           (1U == report.failed_count);
}
