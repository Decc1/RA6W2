#ifndef HISTORY_PAYLOAD_H
#define HISTORY_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "history_service.h"

/*
 * 历史曲线记录的传输格式。
 *
 * BLE 使用 18 字节紧凑记录；一个 BLE 命令回复最多返回 3 条记录。
 * MQTT 使用一条 JSON 对应一条 15 分钟聚合记录，供云端保存和 App 曲线查询。
 */
#define HISTORY_BLE_RECORD_SIZE            (18U)
#define HISTORY_BLE_PAGE_HEADER_SIZE       (4U)
#define HISTORY_BLE_MAX_RECORDS_PER_PAGE   (3U)
#define HISTORY_MQTT_JSON_BUFFER_SIZE      (224U)

/*
 * 编码单条 BLE 历史记录，所有多字节字段为小端序：
 * window_start_s(4) + temp_avg(2) + hum_avg(2) + temp_min(2) + temp_max(2)
 * + hum_min(2) + hum_max(2) + sample_count(2)。
 */
bool history_payload_encode_ble_record(history_record_t const * p_record,
                                       uint8_t *                p_payload,
                                       size_t                   payload_size);

/*
 * 从 storage 的 oldest_index 起编码历史分页回复：
 * version(1) + total_count(2) + returned_count(1) + N * 18-byte records。
 * 若请求位置越界则返回成功空页（returned_count = 0），便于 App 判断没有更多数据。
 */
bool history_payload_encode_ble_page(uint16_t first_index,
                                     uint8_t  requested_count,
                                     uint8_t * p_payload,
                                     size_t    payload_size,
                                     size_t *  p_written_length);

/* 将单条历史记录编码为 MQTT JSON；设备 ID 由 MQTT Topic 表示，因此不重复写入 JSON。 */
bool history_payload_encode_mqtt_json(history_record_t const * p_record,
                                      char *                   p_json,
                                      size_t                   json_size,
                                      size_t *                 p_written_length);

/* 无硬件阶段验证 BLE 二进制格式、分页边界及 MQTT JSON 格式。 */
bool history_payload_self_test(void);

#endif /* HISTORY_PAYLOAD_H */
