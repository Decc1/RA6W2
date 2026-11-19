/* generated configuration header file - do not edit */
#ifndef R_BLE_CFG_H_
#define R_BLE_CFG_H_
#ifdef __cplusplus
            extern "C" {
            #endif

#define DA14531_DEVICE      (1)
#define DA14535_DEVICE      (2)

#define BLE_CFG_DA1453x_DEVICE                   (DA14531_DEVICE)
#define BLE_CFG_DA14xxx_DEVICE                   (BLE_CFG_DA1453x_DEVICE)

#define BLE_CFG_BOOTLOADER_RESET            (1)
#define BLE_CFG_RESET_PIN                   (BSP_IO_PORT_01_PIN_14)
#if BLE_CFG_BOOTLOADER_RESET
#define BLE_CFG_BOOTLOADER_RESET_PIN        (BSP_IO_PORT_01_PIN_10)
#endif
#define BLE_CFG_RESET_POLARITY              (00)
#define BLE_CFG_RTS_PIN                     (BSP_IO_PORT_01_PIN_13)
#define BLE_CFG_CTS_PIN                     (BSP_IO_PORT_01_PIN_12)
#define BLE_CFG_HOST_BOOT_MODE              (2)

#define DA1453x_GTL_UART_TX_PIN             (0x00)
#define DA1453x_GTL_UART_RX_PIN             (0x01)
#define DA1453x_GTL_POR_PIN                 (0x02)
#define DA1453x_GTL_RTS_PIN                 (0x03)
#define DA1453x_GTL_CTS_PIN                 (0x04)

#if (CFG_PMGR == 1)
            #define BLE_ABS_CFG_SLEEP_SUPPORT           (1)
            #endif

#ifdef __cplusplus
            }
            #endif
#endif /* R_BLE_CFG_H_ */
