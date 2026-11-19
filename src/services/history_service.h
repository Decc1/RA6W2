#ifndef HISTORY_SERVICE_H
#define HISTORY_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../app/app_types.h"

/* 15 分钟聚合记录：供 App 历史曲线、云端补传和本地诊断使用。 */
typedef struct st_history_record
{
    uint32_t window_start_uptime_s;
    uint32_t first_sample_sequence;
    int16_t  temperature_average_centi_c;
    int16_t  temperature_min_centi_c;
    int16_t  temperature_max_centi_c;
    uint16_t humidity_average_centi_rh;
    uint16_t humidity_min_centi_rh;
    uint16_t humidity_max_centi_rh;
    uint16_t sample_count;
} history_record_t;

/*
 * 后续外部 Flash 存储层实现的同步追加接口。
 * 回调必须在返回前写入或复制 record，不能保留 p_record 指针。
 */
typedef bool (* history_append_callback_t)(history_record_t const * p_record);

void history_service_init(history_append_callback_t append_callback);

/* 写入一笔有效传感器数据；返回 false 表示上一个待存记录尚未成功落盘。 */
bool history_service_on_measurement(hygro_measurement_t const * p_measurement, uint32_t uptime_ms);

/* 主循环或存储任务可定期调用，尝试重新写入待存记录。 */
bool history_service_flush_pending(void);

bool history_service_has_pending_record(void);
bool history_service_self_test(void);

#endif /* HISTORY_SERVICE_H */
