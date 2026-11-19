#ifndef SHT41_H
#define SHT41_H

#include "../app/app_types.h"

#include <stddef.h>
#include <stdint.h>

/* SHT41 的 7 位 I2C 地址；发送到总线时由底层外设自动处理读写位。 */
#define SHT41_I2C_ADDRESS                  (0x44U)

/* SHT41 命令与时序参数。 */
#define SHT41_COMMAND_SOFT_RESET           (0x94U)
#define SHT41_COMMAND_MEASURE_HIGH_PRECISION (0xFDU)
#define SHT41_MEASURE_DELAY_MS              (10U)
#define SHT41_MAX_READ_ATTEMPTS             (2U)

/*
 * 平台 I2C 适配接口。
 * write/read 需要由 RA6W2 FSP I2C Master 适配层实现：
 * - address 使用 7 位地址 0x44，不要在这里左移；
 * - 函数必须等待传输完成后返回；
 * - 超时、NACK、总线错误必须映射为对应 HYGRO_STATUS_*；
 * - read 失败时不得把未完成的数据交给上层解析。
 */
typedef hygro_status_t (* sht41_i2c_write_callback_t)(void *             p_context,
                                                      uint8_t             address,
                                                      uint8_t const *     p_data,
                                                      size_t              length);
typedef hygro_status_t (* sht41_i2c_read_callback_t)(void *         p_context,
                                                     uint8_t         address,
                                                     uint8_t *       p_data,
                                                     size_t          length);
typedef void (* sht41_delay_callback_t)(void * p_context, uint32_t delay_ms);

typedef struct st_sht41_i2c_bus
{
    void *                      p_context;
    sht41_i2c_write_callback_t  write;
    sht41_i2c_read_callback_t   read;
    sht41_delay_callback_t      delay_ms;
} sht41_i2c_bus_t;

/* 注册真实 I2C 总线；传入 NULL 可恢复为无板模拟后端。 */
hygro_status_t sht41_bind_i2c_bus(sht41_i2c_bus_t const * p_bus);

/* 初始化温湿度传感器驱动。 */
hygro_status_t sht41_init(void);

/* 读取一笔温湿度测量值。 */
hygro_status_t sht41_read(hygro_measurement_t * p_measurement);

#endif /* SHT41_H */
