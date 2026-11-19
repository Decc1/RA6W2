# SHT41 真实 I2C 驱动接入说明 V0.1

## 1. 当前完成内容

`src/drivers/sht41.c` 已完成 SHT41 协议层：

1. 使用 7 位地址 `0x44`。
2. 上电初始化发送软复位命令 `0x94`。
3. 读取时发送高精度测量命令 `0xFD`。
4. 等待 10 ms 后读取 6 字节：温度 2 字节、温度 CRC、湿度 2 字节、湿度 CRC。
5. 由 `sht41_codec.c` 校验 CRC 并转换为 0.01 °C、0.01 %RH。
6. 总线失败和 CRC 失败最多重试 1 次。
7. 失败时将 `measurement.valid` 置为 `false`，不会覆盖实时有效数据。

## 2. RA6W2/FSP 适配层需要完成的事情

当前工程还没有生成 I2C Master 实例，因此暂时不能写死实例名。完成 FSP 配置后，增加一个平台适配文件，例如：

```text
src/ports/sht41_i2c_fsp.c
```

适配层只负责把 FSP I2C API 转成以下三个回调：

```c
static hygro_status_t sht41_fsp_write(void *             p_context,
                                      uint8_t             address,
                                      uint8_t const *     p_data,
                                      size_t              length);

static hygro_status_t sht41_fsp_read(void *       p_context,
                                     uint8_t       address,
                                     uint8_t *     p_data,
                                     size_t        length);

static void sht41_fsp_delay(void * p_context, uint32_t delay_ms);
```

回调完成后注册：

```c
sht41_i2c_bus_t bus =
{
    .p_context = &g_i2c_master_ctrl,
    .write     = sht41_fsp_write,
    .read      = sht41_fsp_read,
    .delay_ms  = sht41_fsp_delay,
};

configASSERT(HYGRO_STATUS_OK == sht41_bind_i2c_bus(&bus));
configASSERT(HYGRO_STATUS_OK == sht41_init());
```

实际 FSP 实例的控制块、配置对象和实例句柄必须以 e² studio 生成的 `hal_data.h` 为准，不能手写猜测。

## 3. 错误映射要求

| FSP/I2C 现场 | 驱动返回值 |
| --- | --- |
| 地址无应答、设备未上电 | `HYGRO_STATUS_BUS_NACK` |
| 等待传输完成超时 | `HYGRO_STATUS_BUS_TIMEOUT` |
| 仲裁丢失、总线错误、硬件错误 | `HYGRO_STATUS_BUS_ERROR` |
| 传入空指针或长度非法 | `HYGRO_STATUS_INVALID_ARGUMENT` |
| 6 字节 CRC 错误 | `HYGRO_STATUS_CRC_ERROR` |
| 设备返回长度/帧格式不符合协议 | `HYGRO_STATUS_PROTOCOL_ERROR` |

不要把所有 FSP 错误都转换成 `HYGRO_STATUS_OK`，也不要在底层无限等待。FSP 回调必须使用明确的超时，并在失败后执行 I2C close/reopen 或总线恢复策略（具体以 FSP I2C API 支持情况为准）。

## 4. 设备端硬件检查

真实测试前需要确认：

- SHT41 供电在允许范围内。
- SDA、SCL 都有上拉电阻，且电压与 RA6W2 I/O 电压一致。
- SDA/SCL 没有被其他外设复用占用。
- 传感器地址为 `0x44`；SHT41 不应按 `0x88` 作为 7 位地址传入驱动。
- 示波器或逻辑分析仪能看到 START、`0x44`、ACK、命令 `0xFD`、等待、读取 6 字节和 STOP。

## 5. 无开发板阶段

没有板子时，`sht41_bind_i2c_bus(NULL)` 或不绑定总线会继续使用模拟后端，保证工程可以编译和运行自检。此时只能验证 CRC、温湿度换算、错误状态机和接口调用，不能证明真实 I2C 波形、SHT41 ACK 或传感器数据正确。
