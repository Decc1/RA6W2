#include "sht41_codec.h"

#include <stddef.h>

/* SHT4x CRC-8 参数：多项式0x31、初始值0xFF、无最终异或。 */
#define SHT41_CRC8_POLYNOMIAL          (0x31U)
#define SHT41_CRC8_INITIAL_VALUE       (0xFFU)

/* 原始16位测量值换算使用的分母。 */
#define SHT41_RAW_VALUE_MAX            (65535U)

/* 摄氏度和相对湿度均使用 0.01 为单位，避免应用层浮点运算。 */
#define SHT41_TEMPERATURE_OFFSET_CC    (-4500L)
#define SHT41_TEMPERATURE_SCALE_CC     (17500L)
#define SHT41_HUMIDITY_OFFSET_CRH      (-600L)
#define SHT41_HUMIDITY_SCALE_CRH       (12500L)
#define SHT41_HUMIDITY_MIN_CRH         (0L)
#define SHT41_HUMIDITY_MAX_CRH         (10000L)

#define SHT41_TEMPERATURE_MSB_INDEX    (0U)
#define SHT41_TEMPERATURE_LSB_INDEX    (1U)
#define SHT41_TEMPERATURE_CRC_INDEX    (2U)
#define SHT41_HUMIDITY_MSB_INDEX       (3U)
#define SHT41_HUMIDITY_LSB_INDEX       (4U)
#define SHT41_HUMIDITY_CRC_INDEX       (5U)
#define SHT41_SIGNAL_LENGTH            (2U)

static uint16_t sht41_read_u16_be(uint8_t const * p_data)
{
    return (uint16_t) (((uint16_t) p_data[0] << 8U) | p_data[1]);
}

static int32_t sht41_round_divide(int32_t numerator, int32_t denominator)
{
    return (numerator + (denominator / 2L)) / denominator;
}

uint8_t sht41_crc8_calculate(uint8_t const * p_data, size_t length)
{
    uint8_t crc = SHT41_CRC8_INITIAL_VALUE;

    if (NULL == p_data)
    {
        return 0U;
    }

    for (size_t byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= p_data[byte_index];

        for (uint8_t bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if (0U != (crc & 0x80U))
            {
                crc = (uint8_t) ((crc << 1U) ^ SHT41_CRC8_POLYNOMIAL);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

bool sht41_crc8_is_valid(uint8_t const * p_data, size_t length, uint8_t expected_crc)
{
    return (NULL != p_data) && (sht41_crc8_calculate(p_data, length) == expected_crc);
}

hygro_status_t sht41_decode_measurement(uint8_t const *       p_frame,
                                        uint16_t               battery_mv,
                                        uint32_t               sample_sequence,
                                        hygro_measurement_t * p_measurement)
{
    uint16_t temperature_raw;
    uint16_t humidity_raw;
    int32_t  temperature_centi_c;
    int32_t  humidity_centi_rh;

    if ((NULL == p_frame) || (NULL == p_measurement))
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    if (!sht41_crc8_is_valid(&p_frame[SHT41_TEMPERATURE_MSB_INDEX],
                             SHT41_SIGNAL_LENGTH,
                             p_frame[SHT41_TEMPERATURE_CRC_INDEX]) ||
        !sht41_crc8_is_valid(&p_frame[SHT41_HUMIDITY_MSB_INDEX],
                             SHT41_SIGNAL_LENGTH,
                             p_frame[SHT41_HUMIDITY_CRC_INDEX]))
    {
        return HYGRO_STATUS_CRC_ERROR;
    }

    temperature_raw = sht41_read_u16_be(&p_frame[SHT41_TEMPERATURE_MSB_INDEX]);
    humidity_raw    = sht41_read_u16_be(&p_frame[SHT41_HUMIDITY_MSB_INDEX]);

    temperature_centi_c = SHT41_TEMPERATURE_OFFSET_CC +
                          sht41_round_divide(SHT41_TEMPERATURE_SCALE_CC * (int32_t) temperature_raw,
                                             SHT41_RAW_VALUE_MAX);
    humidity_centi_rh = SHT41_HUMIDITY_OFFSET_CRH +
                        sht41_round_divide(SHT41_HUMIDITY_SCALE_CRH * (int32_t) humidity_raw,
                                           SHT41_RAW_VALUE_MAX);

    /* 数据手册公式在原始值边界可得到小于0或大于100的湿度，应用层统一钳位。 */
    if (humidity_centi_rh < SHT41_HUMIDITY_MIN_CRH)
    {
        humidity_centi_rh = SHT41_HUMIDITY_MIN_CRH;
    }
    else if (humidity_centi_rh > SHT41_HUMIDITY_MAX_CRH)
    {
        humidity_centi_rh = SHT41_HUMIDITY_MAX_CRH;
    }

    p_measurement->temperature_centi_c = (int16_t) temperature_centi_c;
    p_measurement->humidity_centi_rh   = (uint16_t) humidity_centi_rh;
    p_measurement->battery_mv          = battery_mv;
    p_measurement->sample_sequence     = sample_sequence;
    p_measurement->valid               = true;

    return HYGRO_STATUS_OK;
}

hygro_status_t sht41_codec_self_test(void)
{
    static uint8_t const valid_frame[SHT41_MEASUREMENT_FRAME_SIZE] =
    {
        0x66U, 0x66U, 0x93U, /* 25.00 摄氏度 */
        0x72U, 0xB0U, 0xDCU, /* 50.00 %RH */
    };
    static uint8_t const crc_reference_data[] = {0xBEU, 0xEFU};
    hygro_measurement_t measurement;
    uint8_t             corrupt_frame[SHT41_MEASUREMENT_FRAME_SIZE];

    if (0x92U != sht41_crc8_calculate(crc_reference_data, sizeof(crc_reference_data)))
    {
        return HYGRO_STATUS_CRC_ERROR;
    }

    if (HYGRO_STATUS_OK != sht41_decode_measurement(valid_frame, 3850U, 7U, &measurement))
    {
        return HYGRO_STATUS_IO_ERROR;
    }

    if ((2500 != measurement.temperature_centi_c) ||
        (5000U != measurement.humidity_centi_rh) ||
        (3850U != measurement.battery_mv) ||
        (7U != measurement.sample_sequence) ||
        !measurement.valid)
    {
        return HYGRO_STATUS_IO_ERROR;
    }

    for (uint32_t index = 0U; index < SHT41_MEASUREMENT_FRAME_SIZE; index++)
    {
        corrupt_frame[index] = valid_frame[index];
    }

    corrupt_frame[SHT41_HUMIDITY_LSB_INDEX] ^= 0x01U;
    return (HYGRO_STATUS_CRC_ERROR == sht41_decode_measurement(corrupt_frame, 0U, 0U, &measurement)) ?
           HYGRO_STATUS_OK : HYGRO_STATUS_IO_ERROR;
}
