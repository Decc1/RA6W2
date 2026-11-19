#include "sht41.h"

#include <stddef.h>
#include <string.h>

#include "sht41_codec.h"
#include "sht41_mock.h"

/* 当前驱动上下文；初始化阶段只复制回调指针，不保存临时数据指针。 */
static sht41_i2c_bus_t g_sht41_bus;
static bool             g_sht41_bus_bound;
static uint32_t         g_sht41_sample_sequence;

static bool sht41_bus_is_valid(sht41_i2c_bus_t const * p_bus)
{
    return (NULL != p_bus) && (NULL != p_bus->write) && (NULL != p_bus->read) &&
           (NULL != p_bus->delay_ms);
}

static hygro_status_t sht41_write_command(uint8_t command)
{
    return g_sht41_bus.write(g_sht41_bus.p_context,
                             SHT41_I2C_ADDRESS,
                             &command,
                             sizeof(command));
}

static hygro_status_t sht41_read_measurement_once(hygro_measurement_t * p_measurement)
{
    uint8_t             command = SHT41_COMMAND_MEASURE_HIGH_PRECISION;
    uint8_t             response[SHT41_MEASUREMENT_FRAME_SIZE];
    hygro_status_t      status;

    status = g_sht41_bus.write(g_sht41_bus.p_context,
                               SHT41_I2C_ADDRESS,
                               &command,
                               sizeof(command));
    if (HYGRO_STATUS_OK != status)
    {
        return status;
    }

    /* 高精度测量完成前不能读取；10 ms 留出数据手册最大测量时间裕量。 */
    g_sht41_bus.delay_ms(g_sht41_bus.p_context, SHT41_MEASURE_DELAY_MS);

    (void) memset(response, 0, sizeof(response));
    status = g_sht41_bus.read(g_sht41_bus.p_context,
                              SHT41_I2C_ADDRESS,
                              response,
                              sizeof(response));
    if (HYGRO_STATUS_OK != status)
    {
        return status;
    }

    /* CRC 错误或数据格式错误在 codec 层返回，不把坏数据写入 telemetry。 */
    return sht41_decode_measurement(response,
                                    0U,
                                    g_sht41_sample_sequence,
                                    p_measurement);
}

hygro_status_t sht41_bind_i2c_bus(sht41_i2c_bus_t const * p_bus)
{
    if (NULL == p_bus)
    {
        (void) memset(&g_sht41_bus, 0, sizeof(g_sht41_bus));
        g_sht41_bus_bound = false;
        return HYGRO_STATUS_OK;
    }

    if (!sht41_bus_is_valid(p_bus))
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    g_sht41_bus         = *p_bus;
    g_sht41_bus_bound   = true;
    g_sht41_sample_sequence = 0U;
    return HYGRO_STATUS_OK;
}

hygro_status_t sht41_init(void)
{
    hygro_status_t status;

    g_sht41_sample_sequence = 0U;

    if (!g_sht41_bus_bound)
    {
        return sht41_mock_init();
    }

    status = sht41_write_command(SHT41_COMMAND_SOFT_RESET);
    if (HYGRO_STATUS_OK != status)
    {
        return status;
    }

    /* 复位完成后再开始第一次测量。 */
    g_sht41_bus.delay_ms(g_sht41_bus.p_context, 1U);
    return HYGRO_STATUS_OK;
}

hygro_status_t sht41_read(hygro_measurement_t * p_measurement)
{
    hygro_status_t status = HYGRO_STATUS_NOT_READY;

    if (NULL == p_measurement)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    if (!g_sht41_bus_bound)
    {
        return sht41_mock_read(p_measurement);
    }

    /* 总线瞬态错误和 CRC 错误最多重试一次；参数错误不重试。 */
    for (uint32_t attempt = 0U; attempt < SHT41_MAX_READ_ATTEMPTS; attempt++)
    {
        status = sht41_read_measurement_once(p_measurement);
        if (HYGRO_STATUS_OK == status)
        {
            g_sht41_sample_sequence++;
            return HYGRO_STATUS_OK;
        }

        if ((HYGRO_STATUS_INVALID_ARGUMENT == status) ||
            (HYGRO_STATUS_PROTOCOL_ERROR == status))
        {
            break;
        }
    }

    p_measurement->valid = false;
    return status;
}
