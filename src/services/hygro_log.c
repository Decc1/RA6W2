#include "hygro_log.h"

#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

typedef struct st_hygro_log_context
{
    hygro_log_sink_t  sink;
    hygro_log_level_t minimum_level;
    bool              initialized;
} hygro_log_context_t;

static hygro_log_context_t g_hygro_log;

/* 作用: 判断日志级别是否处于支持范围。 */
static bool hygro_log_level_is_valid(hygro_log_level_t level)
{
    return HYGRO_LOG_LEVEL_DEBUG >= level;
}

/* 作用: 初始化日志模块，NULL 表示暂不输出日志。 */
void hygro_log_init(hygro_log_sink_t sink)
{
    taskENTER_CRITICAL();
    (void) memset(&g_hygro_log, 0, sizeof(g_hygro_log));
    g_hygro_log.sink = sink;
    g_hygro_log.minimum_level = HYGRO_LOG_LEVEL_WARN;
    g_hygro_log.initialized = true;
    taskEXIT_CRITICAL();
}

/* 作用: 绑定或替换日志输出后端。 */
void hygro_log_bind_sink(hygro_log_sink_t sink)
{
    taskENTER_CRITICAL();
    g_hygro_log.sink = sink;
    taskEXIT_CRITICAL();
}

/* 作用: 配置允许输出的最低日志级别。 */
void hygro_log_set_level(hygro_log_level_t level)
{
    if (!hygro_log_level_is_valid(level))
    {
        return;
    }

    taskENTER_CRITICAL();
    g_hygro_log.minimum_level = level;
    taskEXIT_CRITICAL();
}

/* 作用: 输出一条模块化错误日志，输出回调在临界区外执行。 */
void hygro_log_write(hygro_log_level_t level, char const * module, hygro_status_t status)
{
    hygro_log_sink_t  sink;
    hygro_log_level_t minimum_level;
    bool              initialized;

    if (!hygro_log_level_is_valid(level))
    {
        return;
    }

    taskENTER_CRITICAL();
    initialized = g_hygro_log.initialized;
    sink = g_hygro_log.sink;
    minimum_level = g_hygro_log.minimum_level;
    taskEXIT_CRITICAL();

    if (!initialized || (NULL == sink) || (level > minimum_level))
    {
        return;
    }

    sink(level, (NULL != module) ? module : "UNKNOWN", status);
}

/* 作用: 返回日志级别名称，供底层 UART/RTT 适配层格式化输出。 */
char const * hygro_log_level_name(hygro_log_level_t level)
{
    switch (level)
    {
        case HYGRO_LOG_LEVEL_ERROR: return "ERROR";
        case HYGRO_LOG_LEVEL_WARN:  return "WARN";
        case HYGRO_LOG_LEVEL_INFO:  return "INFO";
        case HYGRO_LOG_LEVEL_DEBUG: return "DEBUG";
        default:                    return "UNKNOWN";
    }
}

static hygro_log_level_t g_hygro_log_test_level;
static hygro_status_t    g_hygro_log_test_status;
static char const *      g_hygro_log_test_module;
static uint32_t          g_hygro_log_test_count;

/* 作用: 接收日志自检产生的记录，用于验证过滤和参数传递。 */
static void hygro_log_test_sink(hygro_log_level_t level,
                                char const *      module,
                                hygro_status_t    status)
{
    g_hygro_log_test_level = level;
    g_hygro_log_test_module = module;
    g_hygro_log_test_status = status;
    g_hygro_log_test_count++;
}

/* 作用: 验证日志过滤、后端切换和错误码传递。 */
bool hygro_log_self_test(void)
{
    g_hygro_log_test_level = HYGRO_LOG_LEVEL_DEBUG;
    g_hygro_log_test_module = NULL;
    g_hygro_log_test_status = HYGRO_STATUS_OK;
    g_hygro_log_test_count = 0U;

    hygro_log_init(hygro_log_test_sink);
    hygro_log_write(HYGRO_LOG_LEVEL_INFO, "TEST", HYGRO_STATUS_OK);
    if (0U != g_hygro_log_test_count)
    {
        return false;
    }

    hygro_log_set_level(HYGRO_LOG_LEVEL_INFO);
    hygro_log_write(HYGRO_LOG_LEVEL_INFO, "TEST", HYGRO_STATUS_NETWORK_ERROR);
    if ((1U != g_hygro_log_test_count) ||
        (HYGRO_LOG_LEVEL_INFO != g_hygro_log_test_level) ||
        (HYGRO_STATUS_NETWORK_ERROR != g_hygro_log_test_status) ||
        (0 != strcmp("TEST", g_hygro_log_test_module)) ||
        (0 != strcmp("INFO", hygro_log_level_name(HYGRO_LOG_LEVEL_INFO))) ||
        (0 != strcmp("UNKNOWN", hygro_log_level_name((hygro_log_level_t) 99))))
    {
        return false;
    }

    hygro_log_init(NULL);
    return true;
}
