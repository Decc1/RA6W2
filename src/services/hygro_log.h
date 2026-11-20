#ifndef HYGRO_LOG_H
#define HYGRO_LOG_H

#include "../app/hygro_status.h"

/* 作用: 定义日志重要级别，数值越小表示问题越严重。 */
typedef enum e_hygro_log_level
{
    HYGRO_LOG_LEVEL_ERROR = 0,
    HYGRO_LOG_LEVEL_WARN,
    HYGRO_LOG_LEVEL_INFO,
    HYGRO_LOG_LEVEL_DEBUG,
} hygro_log_level_t;

/* 作用: 抽象日志输出后端；UART、RTT 或 PC mock 只需实现此回调。 */
typedef void (* hygro_log_sink_t)(hygro_log_level_t level,
                                  char const *      module,
                                  hygro_status_t    status);

/* 作用: 初始化日志模块，NULL 表示暂不输出日志。 */
void hygro_log_init(hygro_log_sink_t sink);

/* 作用: 绑定或替换日志输出后端。 */
void hygro_log_bind_sink(hygro_log_sink_t sink);

/* 作用: 配置允许输出的最低日志级别。 */
void hygro_log_set_level(hygro_log_level_t level);

/* 作用: 输出一条模块化错误日志，输出回调在临界区外执行。 */
void hygro_log_write(hygro_log_level_t level, char const * module, hygro_status_t status);

/* 作用: 返回日志级别名称，供底层 UART/RTT 适配层格式化输出。 */
char const * hygro_log_level_name(hygro_log_level_t level);

/* 作用: 验证日志过滤、后端切换和错误码传递。 */
bool hygro_log_self_test(void);

#endif /* HYGRO_LOG_H */
