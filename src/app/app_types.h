#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* 所有温湿度数据统一使用整数，避免浮点误差和额外代码体积。 */
typedef struct st_hygro_measurement
{
    int16_t  temperature_centi_c;
    uint16_t humidity_centi_rh;
    uint16_t battery_mv;
    uint32_t sample_sequence;
    bool     valid;
} hygro_measurement_t;

/* 驱动和服务模块使用的统一返回值。 */
typedef enum e_hygro_status
{
    HYGRO_STATUS_OK = 0,
    HYGRO_STATUS_INVALID_ARGUMENT,
    HYGRO_STATUS_NOT_READY,
    HYGRO_STATUS_IO_ERROR,
    HYGRO_STATUS_CRC_ERROR,
    HYGRO_STATUS_BUS_NACK,
    HYGRO_STATUS_BUS_TIMEOUT,
    HYGRO_STATUS_BUS_ERROR,
    HYGRO_STATUS_PROTOCOL_ERROR,
} hygro_status_t;

#endif /* APP_TYPES_H */
