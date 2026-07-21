Exit code: 0
Wall time: 1.1 seconds
Output:
#include "ble_protocol.h"

uint16_t ble_protocol_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

size_t ble_protocol_encode(uint8_t *out, size_t out_size, uint8_t flags,
                           uint8_t command, uint16_t sequence,
                           const uint8_t *payload, uint16_t payload_len)
{
    const size_t frame_len = BLE_PROTOCOL_HEADER_SIZE + payload_len + BLE_PROTOCOL_CRC_SIZE;
    if (out == NULL || payload_len > BLE_PROTOCOL_MAX_PAYLOAD || out_size < frame_len) return 0;
    out[0] = BLE_PROTOCOL_MAGIC_0;
    out[1] = BLE_PROTOCOL_MAGIC_1;
    out[2] = BLE_PROTOCOL_VERSION;
    out[3] = flags;
    out[4] = command;
    out[5] = (uint8_t)sequence;
    out[6] = (uint8_t)(sequence >> 8);
    out[7] = (uint8_t)payload_len;
    out[8] = (uint8_t)(payload_len >> 8);
    if (payload_len > 0 && payload != NULL) {
        for (uint16_t i = 0; i < payload_len; ++i) out[BLE_PROTOCOL_HEADER_SIZE + i] = payload[i];
    }
    uint16_t crc = ble_protocol_crc16(out, BLE_PROTOCOL_HEADER_SIZE + payload_len);
    out[BLE_PROTOCOL_HEADER_SIZE + payload_len] = (uint8_t)crc;
    out[BLE_PROTOCOL_HEADER_SIZE + payload_len + 1] = (uint8_t)(crc >> 8);
    return frame_len;
}

