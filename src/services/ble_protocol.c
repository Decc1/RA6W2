#include "ble_protocol.h"

#include <string.h>

/* 控制帧和回复帧中公共字段的字节偏移。 */
#define BLE_PROTOCOL_OFFSET_VERSION        (0U)
#define BLE_PROTOCOL_OFFSET_COMMAND        (1U)
#define BLE_PROTOCOL_OFFSET_COMMAND_ID     (2U)
#define BLE_PROTOCOL_OFFSET_LENGTH         (4U)
#define BLE_PROTOCOL_REPLY_OFFSET_STATUS   (4U)
#define BLE_PROTOCOL_REPLY_OFFSET_LENGTH   (5U)

static uint16_t ble_protocol_read_u16_le(uint8_t const * p_data)
{
    return (uint16_t) p_data[0] | ((uint16_t) p_data[1] << 8U);
}

static void ble_protocol_write_u16_le(uint8_t * p_data, uint16_t value)
{
    p_data[0] = (uint8_t) value;
    p_data[1] = (uint8_t) (value >> 8U);
}

ble_protocol_reply_status_t ble_protocol_parse_control_write(uint8_t const *         p_data,
                                                              size_t                  data_length,
                                                              ble_protocol_command_t * p_command)
{
    uint8_t payload_length;

    if ((NULL == p_data) || (NULL == p_command))
    {
        return BLE_PROTOCOL_REPLY_INVALID_FRAME;
    }

    if (data_length < BLE_PROTOCOL_CONTROL_HEADER_SIZE)
    {
        return BLE_PROTOCOL_REPLY_INVALID_FRAME;
    }

    if (BLE_PROTOCOL_VERSION != p_data[BLE_PROTOCOL_OFFSET_VERSION])
    {
        return BLE_PROTOCOL_REPLY_INVALID_FRAME;
    }

    payload_length = p_data[BLE_PROTOCOL_OFFSET_LENGTH];
    if ((payload_length > BLE_PROTOCOL_MAX_CONTROL_PAYLOAD_SIZE) ||
        (data_length != (BLE_PROTOCOL_CONTROL_HEADER_SIZE + (size_t) payload_length)))
    {
        return BLE_PROTOCOL_REPLY_INVALID_FRAME;
    }

    p_command->command_code   = p_data[BLE_PROTOCOL_OFFSET_COMMAND];
    p_command->command_id     = ble_protocol_read_u16_le(&p_data[BLE_PROTOCOL_OFFSET_COMMAND_ID]);
    p_command->payload_length = payload_length;
    p_command->p_payload      = &p_data[BLE_PROTOCOL_CONTROL_HEADER_SIZE];

    return BLE_PROTOCOL_REPLY_OK;
}

bool ble_protocol_encode_reply(uint8_t                    command_code,
                               uint16_t                   command_id,
                               ble_protocol_reply_status_t status,
                               uint8_t const *            p_payload,
                               uint8_t                     payload_length,
                               uint8_t *                   p_output,
                               size_t                      output_size,
                               size_t *                    p_written_length)
{
    size_t const required_size = BLE_PROTOCOL_REPLY_HEADER_SIZE + (size_t) payload_length;

    if ((NULL == p_output) || (NULL == p_written_length) ||
        ((0U != payload_length) && (NULL == p_payload)) || (output_size < required_size))
    {
        return false;
    }

    p_output[BLE_PROTOCOL_OFFSET_VERSION]      = BLE_PROTOCOL_VERSION;
    p_output[BLE_PROTOCOL_OFFSET_COMMAND]      = command_code;
    ble_protocol_write_u16_le(&p_output[BLE_PROTOCOL_OFFSET_COMMAND_ID], command_id);
    p_output[BLE_PROTOCOL_REPLY_OFFSET_STATUS] = (uint8_t) status;
    p_output[BLE_PROTOCOL_REPLY_OFFSET_LENGTH] = payload_length;

    if (0U != payload_length)
    {
        (void) memcpy(&p_output[BLE_PROTOCOL_REPLY_HEADER_SIZE], p_payload, payload_length);
    }

    *p_written_length = required_size;
    return true;
}

bool ble_protocol_self_test(void)
{
    static uint8_t const command_frame[] =
    {
        BLE_PROTOCOL_VERSION,
        BLE_PROTOCOL_COMMAND_GET_LATEST_MEASUREMENT,
        0x34U, 0x12U,
        0x00U,
    };
    static uint8_t const expected_reply[] =
    {
        BLE_PROTOCOL_VERSION,
        BLE_PROTOCOL_COMMAND_GET_LATEST_MEASUREMENT,
        0x34U, 0x12U,
        BLE_PROTOCOL_REPLY_OK,
        0x02U,
        0xAAU, 0x55U,
    };
    static uint8_t const reply_payload[] = {0xAAU, 0x55U};
    ble_protocol_command_t command;
    uint8_t                reply[BLE_PROTOCOL_REPLY_HEADER_SIZE + sizeof(reply_payload)];
    size_t                 reply_length = 0U;

    if ((BLE_PROTOCOL_REPLY_OK !=
         ble_protocol_parse_control_write(command_frame, sizeof(command_frame), &command)) ||
        (BLE_PROTOCOL_COMMAND_GET_LATEST_MEASUREMENT != command.command_code) ||
        (0x1234U != command.command_id) || (0U != command.payload_length))
    {
        return false;
    }

    if (!ble_protocol_encode_reply(command.command_code,
                                   command.command_id,
                                   BLE_PROTOCOL_REPLY_OK,
                                   reply_payload,
                                   sizeof(reply_payload),
                                   reply,
                                   sizeof(reply),
                                   &reply_length) ||
        (reply_length != sizeof(expected_reply)) ||
        (0 != memcmp(reply, expected_reply, sizeof(expected_reply))))
    {
        return false;
    }

    return (BLE_PROTOCOL_REPLY_INVALID_FRAME ==
            ble_protocol_parse_control_write(command_frame,
                                             sizeof(command_frame) - 1U,
                                             &command));
}
