#ifndef SHT41_MOCK_H
#define SHT41_MOCK_H

#include "sht41.h"

/* 兼容头文件：新代码应包含 sht41.h。 */

/* 无开发板阶段使用的模拟后端，仅供 sht41.c 调用。 */
hygro_status_t sht41_mock_init(void);
hygro_status_t sht41_mock_read(hygro_measurement_t * p_measurement);

#endif /* SHT41_MOCK_H */
