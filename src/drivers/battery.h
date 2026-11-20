#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#include "../app/app_types.h"

/* 作用: 抽象电池电压读取接口，后续由 ADC/FSP 适配层提供真实实现。 */
typedef hygro_status_t (* battery_read_callback_t)(void *     p_context,
                                                   uint16_t * p_voltage_mv);

/* 作用: 绑定真实 ADC 读取回调；传入 NULL 可恢复无开发板 mock 电压。 */
hygro_status_t battery_bind_reader(void * p_context, battery_read_callback_t read_callback);

/* 作用: 初始化电池服务，并默认使用稳定的 mock 电压。 */
void battery_init(void);

/* 作用: 获取当前电池电压，单位 mV。 */
hygro_status_t battery_read_mv(uint16_t * p_voltage_mv);

/* 作用: 验证 mock 和真实回调的绑定、读取及异常电压拦截逻辑。 */
bool battery_self_test(void);

#endif /* BATTERY_H */
