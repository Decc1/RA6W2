#ifndef SHT41_H
#define SHT41_H

#include "../app/app_types.h"

/* 初始化温湿度传感器驱动。 */
hygro_status_t sht41_init(void);

/* 读取一笔温湿度测量值。 */
hygro_status_t sht41_read(hygro_measurement_t * p_measurement);

#endif /* SHT41_H */
