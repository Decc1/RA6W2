#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "hygro_status.h"

/* 所有温湿度数据统一使用整数，避免浮点误差和额外代码体积。 */
typedef struct st_hygro_measurement
{
    int16_t  temperature_centi_c;
    uint16_t humidity_centi_rh;
    uint16_t battery_mv;
    uint32_t sample_sequence;
    bool     valid;
} hygro_measurement_t;

#endif /* APP_TYPES_H */
