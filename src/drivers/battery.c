#include "battery.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../services/hygro_log.h"

#define BATTERY_MOCK_VOLTAGE_MV       (3850U)
#define BATTERY_VALID_MIN_VOLTAGE_MV  (2500U)
#define BATTERY_VALID_MAX_VOLTAGE_MV  (4500U)

typedef struct st_battery_context
{
    void *                  p_context;
    battery_read_callback_t read_callback;
} battery_context_t;

static battery_context_t g_battery;

/* 作用: 无开发板阶段返回固定电压，让遥测、BLE 和 MQTT 数据链路可完整验证。 */
static hygro_status_t battery_mock_read(void * p_context, uint16_t * p_voltage_mv)
{
    (void) p_context;

    if (NULL == p_voltage_mv)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    *p_voltage_mv = BATTERY_MOCK_VOLTAGE_MV;
    return HYGRO_STATUS_OK;
}

/* 作用: 判断 ADC 适配层读取的电压是否落在单节锂电池允许范围。 */
static bool battery_voltage_is_valid(uint16_t voltage_mv)
{
    return (voltage_mv >= BATTERY_VALID_MIN_VOLTAGE_MV) &&
           (voltage_mv <= BATTERY_VALID_MAX_VOLTAGE_MV);
}

/* 作用: 初始化电池读取上下文，默认绑定 mock 回调。 */
void battery_init(void)
{
    taskENTER_CRITICAL();
    (void) memset(&g_battery, 0, sizeof(g_battery));
    g_battery.read_callback = battery_mock_read;
    taskEXIT_CRITICAL();
}

/* 作用: 切换真实 ADC 或 mock 读取后端，避免上层依赖具体硬件。 */
hygro_status_t battery_bind_reader(void * p_context, battery_read_callback_t read_callback)
{
    taskENTER_CRITICAL();
    g_battery.p_context = p_context;
    g_battery.read_callback = (NULL != read_callback) ? read_callback : battery_mock_read;
    taskEXIT_CRITICAL();

    return HYGRO_STATUS_OK;
}

/* 作用: 读取并校验电池电压；真实回调错误或异常电压不会伪装成有效数据。 */
hygro_status_t battery_read_mv(uint16_t * p_voltage_mv)
{
    battery_read_callback_t read_callback;
    void *                  p_context;
    hygro_status_t          status;
    uint16_t                voltage_mv = 0U;

    if (NULL == p_voltage_mv)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    read_callback = g_battery.read_callback;
    p_context = g_battery.p_context;
    taskEXIT_CRITICAL();

    if (NULL == read_callback)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BATTERY", HYGRO_STATUS_NOT_READY);
        return HYGRO_STATUS_NOT_READY;
    }

    status = read_callback(p_context, &voltage_mv);
    if (HYGRO_STATUS_OK != status)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BATTERY", status);
        return status;
    }
    if (!battery_voltage_is_valid(voltage_mv))
    {
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "BATTERY", HYGRO_STATUS_PROTOCOL_ERROR);
        return HYGRO_STATUS_PROTOCOL_ERROR;
    }

    *p_voltage_mv = voltage_mv;
    return HYGRO_STATUS_OK;
}

/* 作用: 电池自检使用的正常真实读取回调。 */
static hygro_status_t battery_test_read_ok(void * p_context, uint16_t * p_voltage_mv)
{
    (void) p_context;

    if (NULL == p_voltage_mv)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    *p_voltage_mv = 3700U;
    return HYGRO_STATUS_OK;
}

/* 作用: 电池自检使用的异常电压回调。 */
static hygro_status_t battery_test_read_invalid(void * p_context, uint16_t * p_voltage_mv)
{
    (void) p_context;

    if (NULL == p_voltage_mv)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    *p_voltage_mv = 0U;
    return HYGRO_STATUS_OK;
}

/* 作用: 验证 mock 默认值、真实回调切换和零电压异常拦截。 */
bool battery_self_test(void)
{
    uint16_t voltage_mv = 0U;

    battery_init();
    if ((HYGRO_STATUS_OK != battery_read_mv(&voltage_mv)) ||
        (BATTERY_MOCK_VOLTAGE_MV != voltage_mv))
    {
        return false;
    }

    (void) battery_bind_reader(NULL, battery_test_read_ok);
    if ((HYGRO_STATUS_OK != battery_read_mv(&voltage_mv)) || (3700U != voltage_mv))
    {
        return false;
    }

    (void) battery_bind_reader(NULL, battery_test_read_invalid);
    if (HYGRO_STATUS_PROTOCOL_ERROR != battery_read_mv(&voltage_mv))
    {
        return false;
    }

    battery_init();
    return true;
}
