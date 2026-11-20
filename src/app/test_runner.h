#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdbool.h>
#include <stdint.h>

/* 作用: 保存最近一次统一自检的统计结果，供串口日志或后续诊断接口读取。 */
typedef struct st_test_runner_report
{
    uint32_t total_count;
    uint32_t passed_count;
    uint32_t failed_count;
} test_runner_report_t;

/* 作用: 依次执行所有模块自检，并以日志形式报告每个模块 PASS 或 FAIL。 */
bool test_runner_run_all(void);

/* 作用: 读取最近一次统一自检统计结果。 */
void test_runner_get_report(test_runner_report_t * p_report);

/* 作用: 验证统一自检执行器对成功和失败用例的统计逻辑。 */
bool test_runner_self_test(void);

#endif /* TEST_RUNNER_H */
