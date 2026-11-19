#include "history_payload.h"

#include <stdio.h>
#include <string.h>

#include "history_storage.h"

#define HISTORY_PAYLOAD_VERSION            (1U)

#define HISTORY_OFFSET_WINDOW_START         (0U)
#define HISTORY_OFFSET_TEMPERATURE_AVERAGE  (4U)
#define HISTORY_OFFSET_HUMIDITY_AVERAGE     (6U)
#define HISTORY_OFFSET_TEMPERATURE_MINIMUM  (8U)
#define HISTORY_OFFSET_TEMPERATURE_MAXIMUM  (10U)
#define HISTORY_OFFSET_HUMIDITY_MINIMUM     (12U)
#define HISTORY_OFFSET_HUMIDITY_MAXIMUM     (14U)
#define HISTORY_OFFSET_SAMPLE_COUNT         (16U)

/* 作用：将 16 位字段以小端格式写入历史 BLE 载荷。 */
static void history_payload_write_u16_le(uint8_t * p_destination, uint16_t value)
{
    p_destination[0] = (uint8_t) value;
    p_destination[1] = (uint8_t) (value >> 8U);
}

/* 作用：将 32 位字段以小端格式写入历史 BLE 载荷。 */
static void history_payload_write_u32_le(uint8_t * p_destination, uint32_t value)
{
    p_destination[0] = (uint8_t) value;
    p_destination[1] = (uint8_t) (value >> 8U);
    p_destination[2] = (uint8_t) (value >> 16U);
    p_destination[3] = (uint8_t) (value >> 24U);
}

/* 作用：把一条 15 分钟历史聚合记录编码为固定 18 字节 BLE 二进制数据。 */
bool history_payload_encode_ble_record(history_record_t const * p_record,
                                       uint8_t *                p_payload,
                                       size_t                   payload_size)
{
    if ((NULL == p_record) || (NULL == p_payload) || (payload_size < HISTORY_BLE_RECORD_SIZE))
    {
        return false;
    }

    history_payload_write_u32_le(&p_payload[HISTORY_OFFSET_WINDOW_START],
                                 p_record->window_start_uptime_s);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_TEMPERATURE_AVERAGE],
                                 (uint16_t) p_record->temperature_average_centi_c);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_HUMIDITY_AVERAGE],
                                 p_record->humidity_average_centi_rh);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_TEMPERATURE_MINIMUM],
                                 (uint16_t) p_record->temperature_min_centi_c);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_TEMPERATURE_MAXIMUM],
                                 (uint16_t) p_record->temperature_max_centi_c);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_HUMIDITY_MINIMUM],
                                 p_record->humidity_min_centi_rh);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_HUMIDITY_MAXIMUM],
                                 p_record->humidity_max_centi_rh);
    history_payload_write_u16_le(&p_payload[HISTORY_OFFSET_SAMPLE_COUNT],
                                 p_record->sample_count);
    return true;
}

/* 作用：按索引分页读取历史记录，并封装为一帧 BLE 回复载荷。 */
bool history_payload_encode_ble_page(uint16_t first_index,
                                     uint8_t  requested_count,
                                     uint8_t * p_payload,
                                     size_t    payload_size,
                                     size_t *  p_written_length)
{
    uint16_t total_count;
    uint8_t  returned_count = 0U;
    uint8_t  requested_limit;
    size_t   written_length = HISTORY_BLE_PAGE_HEADER_SIZE;

    if ((NULL == p_payload) || (NULL == p_written_length) ||
        (payload_size < HISTORY_BLE_PAGE_HEADER_SIZE) ||
        !history_storage_get_count(&total_count))
    {
        return false;
    }

    requested_limit = (requested_count > HISTORY_BLE_MAX_RECORDS_PER_PAGE) ?
                      HISTORY_BLE_MAX_RECORDS_PER_PAGE : requested_count;
    p_payload[0] = HISTORY_PAYLOAD_VERSION;
    history_payload_write_u16_le(&p_payload[1], total_count);
    p_payload[3] = 0U;

    while ((returned_count < requested_limit) &&
           ((uint32_t) first_index + (uint32_t) returned_count < (uint32_t) total_count))
    {
        history_record_t record;

        if ((payload_size - written_length < HISTORY_BLE_RECORD_SIZE) ||
            !history_storage_read((uint16_t) (first_index + returned_count), &record) ||
            !history_payload_encode_ble_record(&record, &p_payload[written_length],
                                               payload_size - written_length))
        {
            return false;
        }

        written_length += HISTORY_BLE_RECORD_SIZE;
        returned_count++;
    }

    p_payload[3] = returned_count;
    *p_written_length = written_length;
    return true;
}

/* 作用：把一条历史聚合记录编码为云端 MQTT 上报使用的 JSON。 */
bool history_payload_encode_mqtt_json(history_record_t const * p_record,
                                      char *                   p_json,
                                      size_t                   json_size,
                                      size_t *                 p_written_length)
{
    int result;

    if ((NULL == p_record) || (NULL == p_json) || (NULL == p_written_length) || (0U == json_size))
    {
        return false;
    }

    result = snprintf(p_json,
                      json_size,
                      "{\"v\":1,\"window_s\":%lu,\"temp_avg_cc\":%d,\"hum_avg_crh\":%u,"
                      "\"temp_min_cc\":%d,\"temp_max_cc\":%d,\"hum_min_crh\":%u,"
                      "\"hum_max_crh\":%u,\"samples\":%u}",
                      (unsigned long) p_record->window_start_uptime_s,
                      (int) p_record->temperature_average_centi_c,
                      (unsigned int) p_record->humidity_average_centi_rh,
                      (int) p_record->temperature_min_centi_c,
                      (int) p_record->temperature_max_centi_c,
                      (unsigned int) p_record->humidity_min_centi_rh,
                      (unsigned int) p_record->humidity_max_centi_rh,
                      (unsigned int) p_record->sample_count);

    if ((result < 0) || ((size_t) result >= json_size))
    {
        return false;
    }

    *p_written_length = (size_t) result;
    return true;
}

/* 作用：验证历史 BLE 二进制格式、分页边界和 MQTT JSON 格式。 */
bool history_payload_self_test(void)
{
    static uint8_t const expected_record[HISTORY_BLE_RECORD_SIZE] =
    {
        0x84U, 0x03U, 0x00U, 0x00U, /* window = 900 s */
        0x2EU, 0x09U,               /* average temperature = 23.50 C */
        0xC6U, 0x11U,               /* average humidity = 45.50 %RH */
        0xF4U, 0x08U,               /* minimum temperature = 22.92 C */
        0x68U, 0x09U,               /* maximum temperature = 24.08 C */
        0x80U, 0x10U,               /* minimum humidity = 42.24 %RH */
        0x0AU, 0x12U,               /* maximum humidity = 46.18 %RH */
        0x5AU, 0x00U,               /* 90 samples */
    };
    static char const expected_json[] =
        "{\"v\":1,\"window_s\":900,\"temp_avg_cc\":2350,\"hum_avg_crh\":4550,"
        "\"temp_min_cc\":2292,\"temp_max_cc\":2408,\"hum_min_crh\":4224,"
        "\"hum_max_crh\":4618,\"samples\":90}";
    history_record_t record =
    {
        .window_start_uptime_s       = 900U,
        .first_sample_sequence       = 10U,
        .temperature_average_centi_c = 2350,
        .temperature_min_centi_c     = 2292,
        .temperature_max_centi_c     = 2408,
        .humidity_average_centi_rh   = 4550U,
        .humidity_min_centi_rh       = 4224U,
        .humidity_max_centi_rh       = 4618U,
        .sample_count                = 90U,
    };
    uint8_t payload[HISTORY_BLE_PAGE_HEADER_SIZE + HISTORY_BLE_MAX_RECORDS_PER_PAGE * HISTORY_BLE_RECORD_SIZE];
    char    json[HISTORY_MQTT_JSON_BUFFER_SIZE];
    size_t  written_length = 0U;

    if (!history_payload_encode_ble_record(&record, payload, sizeof(payload)) ||
        (0 != memcmp(payload, expected_record, sizeof(expected_record))))
    {
        return false;
    }

    history_storage_init();
    if (!history_storage_append(&record) ||
        !history_payload_encode_ble_page(0U, HISTORY_BLE_MAX_RECORDS_PER_PAGE,
                                         payload, sizeof(payload), &written_length) ||
        (HISTORY_BLE_PAGE_HEADER_SIZE + HISTORY_BLE_RECORD_SIZE != written_length) ||
        (HISTORY_PAYLOAD_VERSION != payload[0]) || (1U != payload[1]) ||
        (0U != payload[2]) || (1U != payload[3]) ||
        (0 != memcmp(&payload[HISTORY_BLE_PAGE_HEADER_SIZE], expected_record, sizeof(expected_record))))
    {
        return false;
    }

    if (!history_payload_encode_ble_page(1U, 1U, payload, sizeof(payload), &written_length) ||
        (HISTORY_BLE_PAGE_HEADER_SIZE != written_length) || (0U != payload[3]) ||
        !history_payload_encode_mqtt_json(&record, json, sizeof(json), &written_length) ||
        (written_length != (sizeof(expected_json) - 1U)) || (0 != strcmp(json, expected_json)))
    {
        return false;
    }

    history_storage_clear();
    return true;
}
