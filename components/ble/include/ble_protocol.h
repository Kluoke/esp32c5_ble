Exit code: 0
Wall time: 1 seconds
Output:
#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BLE_PROTOCOL_MAGIC_0 0xAA
#define BLE_PROTOCOL_MAGIC_1 0x55
#define BLE_PROTOCOL_VERSION 1
#define BLE_PROTOCOL_HEADER_SIZE 9
#define BLE_PROTOCOL_CRC_SIZE 2
#define BLE_PROTOCOL_MAX_PAYLOAD 240

typedef enum {
    BLE_CMD_SET_WIFI = 0x01,
    BLE_CMD_SET_MQTT = 0x03,
    BLE_CMD_REBOOT = 0x06,
    BLE_CMD_GET_STATUS = 0x08,
    BLE_CMD_WIFI_SCAN = 0x09,
    BLE_CMD_OTA_BEGIN = 0x60,
    BLE_CMD_OTA_END = 0x61,
    BLE_CMD_OTA_ABORT = 0x62,
    BLE_CMD_OTA_QUERY = 0x63,
    BLE_CMD_ACK = 0x80,
    BLE_CMD_ERROR = 0xC0,
    BLE_EVENT_WIFI_STATUS = 0xE0,
    BLE_EVENT_WIFI_SCAN_RESULT = 0xE1,
    BLE_EVENT_OTA_STATUS = 0xE2,
    BLE_EVENT_LED_STATE = 0xE3,
} ble_protocol_command_t;

typedef enum {
    BLE_PROTOCOL_OK = 0,
    BLE_PROTOCOL_ERR_MALFORMED = 1,
    BLE_PROTOCOL_ERR_UNSUPPORTED = 2,
    BLE_PROTOCOL_ERR_INVALID_ARGUMENT = 3,
    BLE_PROTOCOL_ERR_BUSY = 4,
    BLE_PROTOCOL_ERR_INTERNAL = 5,
} ble_protocol_error_t;

typedef struct {
    uint8_t flags;
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_len;
    const uint8_t *payload;
} ble_protocol_packet_t;

uint16_t ble_protocol_crc16(const uint8_t *data, size_t len);
size_t ble_protocol_encode(uint8_t *out, size_t out_size, uint8_t flags,
                           uint8_t command, uint16_t sequence,
                           const uint8_t *payload, uint16_t payload_len);

#endif

