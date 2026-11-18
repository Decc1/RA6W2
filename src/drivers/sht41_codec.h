#ifndef SHT41_CODEC_H
#define SHT41_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../app/app_types.h"

/* SHT41 每次测量固定返回：温度2字节+CRC、湿度2字节+CRC。 */
#define SHT41_MEASUREMENT_FRAME_SIZE    (6U)

/* 根据 SHT4x 数据手册计算 CRC-8。 */
uint8_t sht41_crc8_calculate(uint8_t const * p_data, size_t length);

/* 校验一段数据及其随后携带的 CRC 字节。 */
bool sht41_crc8_is_valid(uint8_t const * p_data, size_t length, uint8_t expected_crc);

/*
 * 解析 SHT41 返回的 6 字节测量帧，并换算为软件内部使用的整数单位。
 * 温度单位为 0.01 摄氏度，湿度单位为 0.01 %RH。
 */
hygro_status_t sht41_decode_measurement(uint8_t const *       p_frame,
                                        uint16_t               battery_mv,
                                        uint32_t               sample_sequence,
                                        hygro_measurement_t * p_measurement);

/* 无板阶段可执行的基础算法自检。 */
hygro_status_t sht41_codec_self_test(void);

#endif /* SHT41_CODEC_H */
