#include "sht41.h"

#include <stddef.h>
#include <string.h>

#include "sht41_codec.h"
#include "sht41_mock.h"

#include "../services/hygro_log.h"

/* 当前驱动上下文；初始化阶段只复制回调指针，不保存临时数据指针。 */
static sht41_i2c_bus_t g_sht41_bus;
static bool             g_sht41_bus_bound;
static uint32_t         g_sht41_sample_sequence;

/* 作用：检查真实 I2C 适配层是否提供了完整的读、写和延时回调。 */
static bool sht41_bus_is_valid(sht41_i2c_bus_t const * p_bus)
{
    return (NULL != p_bus) && (NULL != p_bus->write) && (NULL != p_bus->read) &&
           (NULL != p_bus->delay_ms);
}

/* 作用：向 SHT41 写入一个单字节命令，例如软复位或开始测量。 */
static hygro_status_t sht41_write_command(uint8_t command)
{
    return g_sht41_bus.write(g_sht41_bus.p_context,
                             SHT41_I2C_ADDRESS,
                             &command,
                             sizeof(command));
}

/* 作用：完成一次真实 I2C 测量、等待转换并交由 codec 校验和换算结果。 */
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
                                    0U,
                                    p_measurement);
}

/* 作用：绑定或解绑底层 I2C 总线；解绑后驱动自动回退到模拟传感器。 */
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
        hygro_log_write(HYGRO_LOG_LEVEL_WARN, "SHT41", HYGRO_STATUS_INVALID_ARGUMENT);
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    g_sht41_bus         = *p_bus;
    g_sht41_bus_bound   = true;
    g_sht41_sample_sequence = 0U;
    return HYGRO_STATUS_OK;
}

/* 作用：初始化 SHT41；真实硬件执行软复位，无板阶段初始化模拟数据源。 */
hygro_status_t sht41_init(void)
{
    hygro_status_t status;

    g_sht41_sample_sequence = 0U;

    if (!g_sht41_bus_bound)
    {
        status = sht41_mock_init();
        if (HYGRO_STATUS_OK != status)
        {
            hygro_log_write(HYGRO_LOG_LEVEL_ERROR, "SHT41", status);
        }
        return status;
    }

    status = sht41_write_command(SHT41_COMMAND_SOFT_RESET);
    if (HYGRO_STATUS_OK != status)
    {
        hygro_log_write(HYGRO_LOG_LEVEL_ERROR, "SHT41", status);
        return status;
    }

    /* 复位完成后再开始第一次测量。 */
    g_sht41_bus.delay_ms(g_sht41_bus.p_context, 1U);
    return HYGRO_STATUS_OK;
}

/* 作用：读取一笔温湿度数据，并对允许重试的 I2C/CRC 异常执行有限重试。 */
hygro_status_t sht41_read(hygro_measurement_t * p_measurement)
{
    hygro_status_t status = HYGRO_STATUS_NOT_READY;

    if (NULL == p_measurement)
    {
        return HYGRO_STATUS_INVALID_ARGUMENT;
    }

    if (!g_sht41_bus_bound)
    {
        status = sht41_mock_read(p_measurement);
        if (HYGRO_STATUS_OK == status)
        {
            p_measurement->sample_sequence = g_sht41_sample_sequence++;
        }
        else
        {
            hygro_log_write(HYGRO_LOG_LEVEL_WARN, "SHT41", status);
        }
        return status;
    }

    /* 总线瞬态错误和 CRC 错误最多重试一次；参数错误不重试。 */
    for (uint32_t attempt = 0U; attempt < SHT41_MAX_READ_ATTEMPTS; attempt++)
    {
        status = sht41_read_measurement_once(p_measurement);
        if (HYGRO_STATUS_OK == status)
        {
            /* 作用: 采样序号由统一入口分配，真实 I2C 和 mock 后端保持相同行为。 */
            p_measurement->sample_sequence = g_sht41_sample_sequence++;
            return HYGRO_STATUS_OK;
        }

        if ((HYGRO_STATUS_INVALID_ARGUMENT == status) ||
            (HYGRO_STATUS_PROTOCOL_ERROR == status))
        {
            break;
        }
    }

    p_measurement->valid = false;
    hygro_log_write(HYGRO_LOG_LEVEL_WARN, "SHT41", status);
    return status;
}

/* 作用: 验证无板 mock 后端能返回有效测量值，且采样序号只在统一读取入口递增。 */
bool sht41_self_test(void)
{
    hygro_measurement_t first_measurement = {0};
    hygro_measurement_t second_measurement = {0};

    if ((HYGRO_STATUS_OK != sht41_bind_i2c_bus(NULL)) ||
        (HYGRO_STATUS_OK != sht41_init()) ||
        (HYGRO_STATUS_OK != sht41_read(&first_measurement)) ||
        (HYGRO_STATUS_OK != sht41_read(&second_measurement)) ||
        !first_measurement.valid || !second_measurement.valid ||
        (0U != first_measurement.sample_sequence) ||
        (1U != second_measurement.sample_sequence))
    {
        return false;
    }

    return HYGRO_STATUS_INVALID_ARGUMENT == sht41_read(NULL);
}
