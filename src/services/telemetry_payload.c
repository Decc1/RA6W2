#include "telemetry_payload.h"

#include <stdio.h>
#include <string.h>

/* BLE 数据包中的字段偏移，集中定义可避免后续修改协议时遗漏。 */
#define BLE_OFFSET_VERSION                 (0U)
#define BLE_OFFSET_FLAGS                   (1U)
#define BLE_OFFSET_TEMPERATURE             (2U)
#define BLE_OFFSET_HUMIDITY                (4U)
#define BLE_OFFSET_BATTERY                 (6U)
#define BLE_OFFSET_SEQUENCE                (8U)

/* 当前仅表示“本包包含有效测量数据”；后续新增标志位时不改变已有字段。 */
#define BLE_FLAG_MEASUREMENT_VALID         (0x01U)

/*
 * 将 16 位和 32 位数据显式写成小端序。
 * 不依赖 MCU 的字节序，也避免直接强制转换指针带来的对齐问题。
 */
/* 作用：将 16 位遥测字段按 BLE 规定的小端序写入输出缓冲区。 */
static void telemetry_write_u16_le(uint8_t * p_destination, uint16_t value)
{
    p_destination[0] = (uint8_t) value;
    p_destination[1] = (uint8_t) (value >> 8U);
}

/* 作用：将 32 位遥测字段按 BLE 规定的小端序写入输出缓冲区。 */
static void telemetry_write_u32_le(uint8_t * p_destination, uint32_t value)
{
    p_destination[0] = (uint8_t) value;
    p_destination[1] = (uint8_t) (value >> 8U);
    p_destination[2] = (uint8_t) (value >> 16U);
    p_destination[3] = (uint8_t) (value >> 24U);
}

/* 作用：把最新遥测快照编码为实时数据 Characteristic 的 12 字节 BLE 载荷。 */
bool telemetry_encode_ble_payload(telemetry_snapshot_t const * p_snapshot,
                                  uint8_t *                    p_payload,
                                  size_t                       payload_size)
{
    hygro_measurement_t const * p_measurement;

    if ((NULL == p_snapshot) || (NULL == p_payload) ||
        (payload_size < TELEMETRY_BLE_PAYLOAD_SIZE) ||
        !p_snapshot->has_valid_measurement || !p_snapshot->latest_measurement.valid)
    {
        return false;
    }

    p_measurement = &p_snapshot->latest_measurement;

    p_payload[BLE_OFFSET_VERSION] = TELEMETRY_BLE_PAYLOAD_VERSION;
    p_payload[BLE_OFFSET_FLAGS]   = BLE_FLAG_MEASUREMENT_VALID;
    telemetry_write_u16_le(&p_payload[BLE_OFFSET_TEMPERATURE],
                           (uint16_t) p_measurement->temperature_centi_c);
    telemetry_write_u16_le(&p_payload[BLE_OFFSET_HUMIDITY], p_measurement->humidity_centi_rh);
    telemetry_write_u16_le(&p_payload[BLE_OFFSET_BATTERY], p_measurement->battery_mv);
    telemetry_write_u32_le(&p_payload[BLE_OFFSET_SEQUENCE], p_measurement->sample_sequence);

    return true;
}

/* 作用：把最新遥测快照编码为上报云端使用的 MQTT JSON。 */
bool telemetry_encode_mqtt_json(telemetry_snapshot_t const * p_snapshot,
                                 char *                       p_json,
                                 size_t                       json_size,
                                 size_t *                     p_written_length)
{
    hygro_measurement_t const * p_measurement;
    int                         result;

    if ((NULL == p_snapshot) || (NULL == p_json) || (NULL == p_written_length) ||
        (0U == json_size) || !p_snapshot->has_valid_measurement ||
        !p_snapshot->latest_measurement.valid)
    {
        return false;
    }

    p_measurement = &p_snapshot->latest_measurement;

    /*
     * 使用整数单位传输：例如 temp_cc=2350 表示 23.50 摄氏度。
     * MQTT Topic 后续承担设备身份，不把尚未确定的设备 ID 固化进消息格式。
     */
    result = snprintf(p_json,
                      json_size,
                      "{\"v\":1,\"seq\":%lu,\"temp_cc\":%d,\"hum_crh\":%u,"
                      "\"bat_mv\":%u,\"uptime_ms\":%lu}",
                      (unsigned long) p_measurement->sample_sequence,
                      (int) p_measurement->temperature_centi_c,
                      (unsigned int) p_measurement->humidity_centi_rh,
                      (unsigned int) p_measurement->battery_mv,
                      (unsigned long) p_snapshot->last_success_uptime_ms);

    if ((result < 0) || ((size_t) result >= json_size))
    {
        return false;
    }

    *p_written_length = (size_t) result;
    return true;
}

/* 作用：验证实时 BLE 二进制数据和 MQTT JSON 的字段顺序及编码结果。 */
bool telemetry_payload_self_test(void)
{
    static uint8_t const expected_ble_payload[TELEMETRY_BLE_PAYLOAD_SIZE] =
    {
        0x01U, 0x01U,                 /* 协议版本、有效数据标志 */
        0x2EU, 0x09U,                 /* 2350 = 23.50 摄氏度 */
        0xC6U, 0x11U,                 /* 4550 = 45.50 %RH */
        0x0AU, 0x0FU,                 /* 3850 mV */
        0x07U, 0x00U, 0x00U, 0x00U,  /* 采样序号 7 */
    };
    static char const expected_json[] =
        "{\"v\":1,\"seq\":7,\"temp_cc\":2350,\"hum_crh\":4550,"
        "\"bat_mv\":3850,\"uptime_ms\":10000}";
    telemetry_snapshot_t snapshot = {0};
    uint8_t              ble_payload[TELEMETRY_BLE_PAYLOAD_SIZE];
    char                 json[TELEMETRY_MQTT_JSON_BUFFER_SIZE];
    size_t               json_length = 0U;

    snapshot.latest_measurement.temperature_centi_c = 2350;
    snapshot.latest_measurement.humidity_centi_rh   = 4550U;
    snapshot.latest_measurement.battery_mv          = 3850U;
    snapshot.latest_measurement.sample_sequence     = 7U;
    snapshot.latest_measurement.valid               = true;
    snapshot.last_success_uptime_ms                  = 10000U;
    snapshot.has_valid_measurement                   = true;

    if (!telemetry_encode_ble_payload(&snapshot, ble_payload, sizeof(ble_payload)) ||
        (0 != memcmp(ble_payload, expected_ble_payload, sizeof(expected_ble_payload))))
    {
        return false;
    }

    if (!telemetry_encode_mqtt_json(&snapshot, json, sizeof(json), &json_length) ||
        (json_length != (sizeof(expected_json) - 1U)) ||
        (0 != strcmp(json, expected_json)))
    {
        return false;
    }

    return true;
}
