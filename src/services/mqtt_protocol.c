#include "mqtt_protocol.h"

#include <stdio.h>
#include <string.h>

#include "telemetry_payload.h"
#include "history_payload.h"

static bool mqtt_protocol_is_identifier(char const * p_text)
{
    size_t length = 0U;

    if (NULL == p_text)
    {
        return false;
    }

    while ('\0' != p_text[length])
    {
        char const character = p_text[length];
        bool const valid = ((character >= 'A') && (character <= 'Z')) ||
                           ((character >= 'a') && (character <= 'z')) ||
                           ((character >= '0') && (character <= '9')) ||
                           ('_' == character) || ('-' == character);

        if (!valid || (length >= MQTT_PROTOCOL_DEVICE_ID_MAX_LENGTH))
        {
            return false;
        }

        ++length;
    }

    return (0U != length);
}

static char const * mqtt_protocol_get_topic_leaf(mqtt_protocol_topic_type_t topic_type)
{
    switch (topic_type)
    {
        case MQTT_PROTOCOL_TOPIC_TELEMETRY:
            return "telemetry";

        case MQTT_PROTOCOL_TOPIC_HISTORY:
            return "history";

        case MQTT_PROTOCOL_TOPIC_COMMAND:
            return "command";

        case MQTT_PROTOCOL_TOPIC_REPLY:
            return "reply";

        default:
            return NULL;
    }
}

bool mqtt_protocol_build_topic(char *                     p_topic,
                               size_t                     topic_size,
                               char const *               p_device_id,
                               mqtt_protocol_topic_type_t topic_type)
{
    char const * p_leaf;
    int          result;

    if ((NULL == p_topic) || (0U == topic_size) || !mqtt_protocol_is_identifier(p_device_id))
    {
        return false;
    }

    p_leaf = mqtt_protocol_get_topic_leaf(topic_type);
    if (NULL == p_leaf)
    {
        return false;
    }

    result = snprintf(p_topic, topic_size, "%s/%s/%s", MQTT_PROTOCOL_TOPIC_ROOT, p_device_id, p_leaf);
    return (result >= 0) && ((size_t) result < topic_size);
}

bool mqtt_protocol_encode_telemetry(telemetry_snapshot_t const * p_snapshot,
                                    char *                       p_json,
                                    size_t                       json_size,
                                    size_t *                     p_written_length)
{
    return telemetry_encode_mqtt_json(p_snapshot, p_json, json_size, p_written_length);
}

bool mqtt_protocol_encode_history(history_record_t const * p_record,
                                  char *                   p_json,
                                  size_t                   json_size,
                                  size_t *                 p_written_length)
{
    return history_payload_encode_mqtt_json(p_record, p_json, json_size, p_written_length);
}

bool mqtt_protocol_encode_reply(char const *                 p_command_id,
                                ble_protocol_reply_status_t status,
                                char *                       p_json,
                                size_t                       json_size,
                                size_t *                     p_written_length)
{
    int result;

    if ((NULL == p_json) || (NULL == p_written_length) || (0U == json_size) ||
        !mqtt_protocol_is_identifier(p_command_id))
    {
        return false;
    }

    result = snprintf(p_json,
                      json_size,
                      "{\"v\":1,\"id\":\"%s\",\"status\":%u}",
                      p_command_id,
                      (unsigned int) status);
    if ((result < 0) || ((size_t) result >= json_size))
    {
        return false;
    }

    *p_written_length = (size_t) result;
    return true;
}

bool mqtt_protocol_self_test(void)
{
    static char const expected_topic[] = "hygro/v1/DEV-001/telemetry";
    static char const expected_reply[] = "{\"v\":1,\"id\":\"cmd_1\",\"status\":0}";
    telemetry_snapshot_t snapshot = {0};
    uint8_t              payload[TELEMETRY_BLE_PAYLOAD_SIZE];
    char                 topic[MQTT_PROTOCOL_TOPIC_BUFFER_SIZE];
    char                 json[MQTT_PROTOCOL_JSON_BUFFER_SIZE];
    size_t               written_length = 0U;

    if (!mqtt_protocol_build_topic(topic,
                                   sizeof(topic),
                                   "DEV-001",
                                   MQTT_PROTOCOL_TOPIC_TELEMETRY) ||
        (0 != strcmp(topic, expected_topic)) ||
        mqtt_protocol_build_topic(topic,
                                  sizeof(topic),
                                  "bad/topic",
                                  MQTT_PROTOCOL_TOPIC_TELEMETRY))
    {
        return false;
    }

    if (!mqtt_protocol_encode_reply("cmd_1",
                                    BLE_PROTOCOL_REPLY_OK,
                                    json,
                                    sizeof(json),
                                    &written_length) ||
        (written_length != (sizeof(expected_reply) - 1U)) ||
        (0 != strcmp(json, expected_reply)))
    {
        return false;
    }

    snapshot.latest_measurement.temperature_centi_c = 2350;
    snapshot.latest_measurement.humidity_centi_rh = 4550U;
    snapshot.latest_measurement.battery_mv = 3850U;
    snapshot.latest_measurement.sample_sequence = 7U;
    snapshot.latest_measurement.valid = true;
    snapshot.last_success_uptime_ms = 10000U;
    snapshot.has_valid_measurement = true;

    return mqtt_protocol_encode_telemetry(&snapshot,
                                          json,
                                          sizeof(json),
                                          &written_length) &&
           telemetry_encode_ble_payload(&snapshot, payload, sizeof(payload));
}
