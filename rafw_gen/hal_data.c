/* generated HAL source file - do not edit */
#include "hal_data.h"
#define FSP_NOT_DEFINED (UINT32_MAX)
#if (FSP_NOT_DEFINED) != (FSP_NOT_DEFINED)
/* If the transfer module is DMAC, define a DMAC transfer callback. */
extern void i2c_master_w_tx_dmac_callback(i2c_master_w_instance_ctrl_t * const p_ctrl);

void g_i2c_sht41_tx_transfer_callback (dmac_callback_args_t * p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    i2c_master_w_tx_dmac_callback(&g_i2c_sht41_ctrl);
}
#endif

#if (FSP_NOT_DEFINED) != (FSP_NOT_DEFINED)

/* If the transfer module is DMAC, define a DMAC transfer callback. */
extern void i2c_master_w_rx_dmac_callback(i2c_master_w_instance_ctrl_t * const p_ctrl);

void g_i2c_sht41_rx_transfer_callback (dmac_callback_args_t * p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    i2c_master_w_rx_dmac_callback(&g_i2c_sht41_ctrl);
}
#endif
#undef FSP_NOT_DEFINED

i2c_master_w_instance_ctrl_t g_i2c_sht41_ctrl;
const i2c_master_w_extended_cfg_t g_i2c_sht41_extend =
        {
#if (I2C_MASTER_W_CFG_DMA_ENABLE)
    .enable_dma_bursts_tx    = 0,
    .enable_dma_bursts_rx    = 0,
#endif
          /* Actual calculated bitrate: 396040. Actual calculated duty cycle: 50%. Frequency of the selected clock source: 40000000. */.clock_settings.scl_lcnt =
                  50,
          .clock_settings.scl_hcnt = 47,
          .bus_clear = I2C_MASTER_W_BUS_CLEAR_DISABLED,
          .scl_stuck_timeout = 0xFFFFFFFF,
          .sda_stuck_timeout = 0xFFFFFFFF,
          .select_divn = BSP_CFG_I2C1CLK_SOURCE,
#if defined(VECTOR_NUMBER_I2CW1_IRQ)
    .gen_irq             = VECTOR_NUMBER_I2CW1_IRQ,
#else
          .gen_irq = FSP_INVALID_VECTOR,
#endif
          .gen_ipl = (12), };
const i2c_master_cfg_t g_i2c_sht41_cfg =
{ .channel = 1 - I2C_MASTER_W_CHANNEL_OFFSET, .rate = I2C_MASTER_RATE_FAST, .slave = 0x44, .addr_mode =
          I2C_MASTER_ADDR_MODE_7BIT,
#define FSP_NOT_DEFINED (1)
#if (FSP_NOT_DEFINED == FSP_NOT_DEFINED)
  .p_transfer_tx = NULL,
#else
                .p_transfer_tx       = &FSP_NOT_DEFINED,
#endif
#if (FSP_NOT_DEFINED == FSP_NOT_DEFINED)
  .p_transfer_rx = NULL,
#else
                .p_transfer_rx       = &FSP_NOT_DEFINED,
#endif
#undef FSP_NOT_DEFINED
  .p_callback = NULL,
  .p_context = NULL,
#if defined(VECTOR_NUMBER_I2CW1_RX_IRQ)
    .rxi_irq             = VECTOR_NUMBER_I2CW1_RX_IRQ,
#else
  .rxi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_I2CW1_TXE_IRQ)
    .txi_irq             = VECTOR_NUMBER_I2CW1_TXE_IRQ,
#else
  .txi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_I2CW1_TXR_IRQ)
    .tei_irq             = VECTOR_NUMBER_I2CW1_TXR_IRQ,
#else
  .tei_irq = FSP_INVALID_VECTOR,
#endif
  .ipl = (BSP_IRQ_DISABLED),
  .p_extend = &g_i2c_sht41_extend, };
/* Instance structure to use this module. */
const i2c_master_instance_t g_i2c_sht41 =
{ .p_ctrl = &g_i2c_sht41_ctrl, .p_cfg = &g_i2c_sht41_cfg, .p_api = &g_i2c_master_on_i2c_w };
void g_hal_init(void)
{
    g_common_init ();
}
