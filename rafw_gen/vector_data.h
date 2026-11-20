/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
        extern "C" {
        #endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (1)
#endif
/* ISR prototypes */
void i2c_master_w_gen_isr(void);
#if defined(BSP_MCU_GROUP_RA6B1) || defined(BSP_MCU_GROUP_RA6U1) //BROMINE-TODO
        /* Vector table allocations */
        #define VECTOR_NUMBER_I2CW1_IRQ ((IRQn_Type) 0) /* I2CW1 IRQ (Generic interrupt) */
        #define I2CW1_IRQ_IRQn          ((IRQn_Type) 0) /* I2CW1 IRQ (Generic interrupt) */
#else
/* Vector table allocations */
#define VECTOR_NUMBER_I2CW1_IRQ ((IRQn_Type) 5) /* I2CW1 IRQ (Generic interrupt) */
#define I2CW1_IRQ_IRQn          ((IRQn_Type) 5) /* I2CW1 IRQ (Generic interrupt) */
#endif
#ifdef __cplusplus
        }
        #endif
#endif /* VECTOR_DATA_H */
