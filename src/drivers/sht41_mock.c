#include "sht41_mock.h"
#include "sht41_codec.h"

#include <stddef.h>

static uint32_t g_mock_sequence;

/* 无开发板时使用模拟数据；真实硬件到位后由 sht41_bind_i2c_bus() 接入真实总线。 */
/* 作用：初始化模拟传感器，并先验证真实传感器共用的 codec 算法。 */
hygro_status_t sht41_mock_init(void)
{
    g_mock_sequence = 0U;

    /* 无板阶段先验证 CRC 和温湿度换算算法，避免带到真实硬件后才发现计算错误。 */
    return sht41_codec_self_test();
}

/* 作用：产生稳定、可重复变化的模拟温湿度数据，供无开发板阶段联调。 */
hygro_status_t sht41_mock_read(hygro_measurement_t * p_measurement)
{
    static const int16_t  temperature_offset[] = {0, 8, 15, 20, 12, 3, -5, -10};
    static const uint16_t humidity_offset[]    = {0U, 18U, 35U, 28U, 10U, 6U, 22U, 40U};
    uint32_t const index = g_mock_sequence % (sizeof(temperature_offset) / sizeof(temperature_offset[0]));

    if (NULL == p_measurement)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    /* 生成稳定、可预测的模拟数据，便于无板阶段验证数据流。 */
    p_measurement->temperature_centi_c = (int16_t) (2350 + temperature_offset[index]);
    p_measurement->humidity_centi_rh   = (uint16_t) (4550U + humidity_offset[index]);
    p_measurement->battery_mv          = 3850U;
    p_measurement->sample_sequence     = g_mock_sequence++;
    p_measurement->valid               = true;

    return HYGRO_STATUS_OK;
}
