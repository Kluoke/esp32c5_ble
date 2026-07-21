Exit code: 0
Wall time: 1.1 seconds
Output:
#include "ble.h"
#include "ble_protocol.h"
#include "app_events.h"
#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdlib.h>
#include <string.h>

#define TAG "BLE_MODULE"
#define DEVICE_NAME "ESP32_BLE_DEVICE"
#define BLE_PREFERRED_MTU 128
#define RX_BUFFER_SIZE 512

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t command_value_handle;
static uint16_t event_value_handle;
static uint16_t ota_data_value_handle;
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static size_t rx_buffer_len;

static void start_advertising(void);

/* All UUIDs are vendor UUIDs.  Do not use the Bluetooth SIG 16-bit UUID space
 * for product-specific protocol characteristics. */
#define UUID_SERVICE 0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, \
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
#define UUID_COMMAND 0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, \
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
#define UUID_EVENT   0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, \
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
#define UUID_OTA_DATA 0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, \
                      0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12

static void notify_packet(uint8_t command, uint16_t sequence,
                          const uint8_t *payload, uint16_t payload_len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    uint8_t frame[BLE_PROTOCOL_HEADER_SIZE + BLE_PROTOCOL_MAX_PAYLOAD + BLE_PROTOCOL_CRC_SIZE];
    const size_t frame_len = ble_protocol_encode(frame, sizeof(frame), 0, command,
                                                 sequence, payload, payload_len);
    if (frame_len == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, frame_len);
    if (om == NULL) return;
    int rc = ble_gatts_notify_custom(g_conn_handle, event_value_handle, om);
    if (rc != 0) ESP_LOGW(TAG, "notify failed: %d", rc);
}

static void notify_ack(uint8_t request_command, uint16_t sequence)
{
    notify_packet(BLE_CMD_ACK, sequence, &request_command, 1);
}

static void notify_error(uint8_t request_command, uint16_t sequence, uint8_t error)
{
    const uint8_t payload[] = { request_command, error };
    notify_packet(BLE_CMD_ERROR, sequence, payload, sizeof(payload));
}

static void notify_wifi_status(const char *status)
{
    notify_packet(BLE_EVENT_WIFI_STATUS, 0, (const uint8_t *)status, strlen(status));
}

static void notify_ota_status(app_ota_state_t state, uint8_t progress)
{
    const uint8_t payload[] = { (uint8_t)state, progress };
    notify_packet(BLE_EVENT_OTA_STATUS, 0, payload, sizeof(payload));
}

static void notify_scan_result(const app_wifi_scan_result_t *result)
{
    const size_t ssid_len = strnlen(result->ssid, sizeof(result->ssid));
    uint8_t payload[1 + sizeof(result->ssid) + 2];
    payload[0] = (uint8_t)ssid_len;
    memcpy(payload + 1, result->ssid, ssid_len);
    payload[1 + ssid_len] = (uint8_t)result->rssi;
    payload[2 + ssid_len] = result->authmode;
    notify_packet(BLE_EVENT_WIFI_SCAN_RESULT, 0, payload, (uint16_t)(ssid_len + 3));
}

static void notify_led_state(uint8_t state)
{
    notify_packet(BLE_EVENT_LED_STATE, 0, &state, 1);
}

static void ble_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != APP_EVENT) return;
    switch ((app_event_id_t)id) {
        case APP_EVENT_LED_STATE_CHANGED: notify_led_state(*(uint8_t *)data); break;
        case APP_EVENT_WIFI_STATUS: notify_wifi_status((const char *)data); break;
        case APP_EVENT_WIFI_SCAN_RESULT: notify_scan_result((const app_wifi_scan_result_t *)data); break;
        case APP_EVENT_OTA_STATUS: {
            const app_ota_status_t *status = data;
            notify_ota_status(status->state, status->progress);
            break;
        }
        default: break;
    }
}

static void dispatch_command(const ble_protocol_packet_t *packet)
{
    esp_err_t err = ESP_OK;
    switch (packet->command) {
        case BLE_CMD_SET_WIFI: {
            if (packet->payload_len < 2) { notify_error(packet->command, packet->sequence, BLE_PROTOCOL_ERR_INVALID_ARGUMENT); return; }
            uint8_t ssid_len = packet->payload[0];
            uint8_t password_len = packet->payload[1];
            if (ssid_len == 0 || ssid_len > 32 || password_len > 64 ||
                packet->payload_len != (uint16_t)(2 + ssid_len + password_len)) {
                notify_error(packet->command, packet->sequence, BLE_PROTOCOL_ERR_INVALID_ARGUMENT); return;
            }
            app_wifi_credentials_t credentials = {0};
            memcpy(credentials.ssid, packet->payload + 2, ssid_len);
            memcpy(credentials.password, packet->payload + 2 + ssid_len, password_len);
            err = esp_event_post(APP_EVENT, APP_EVENT_BLE_WIFI_CREDENTIALS,
                                 &credentials, sizeof(credentials), 0);
            break;
        }
        case BLE_CMD_SET_MQTT: {
            if (packet->payload_len == 0 || packet->payload_len > 128) { notify_error(packet->command, packet->sequence, BLE_PROTOCOL_ERR_INVALID_ARGUMENT); return; }
            char broker[129] = {0};
            memcpy(broker, packet->payload, packet->payload_len);
            err = esp_event_post(APP_EVENT, APP_EVENT_BLE_MQTT_BROKER, broker,
                                 packet->payload_len + 1, 0);
            break;
        }
        case BLE_CMD_WIFI_SCAN:
            err = esp_event_post(APP_EVENT, APP_EVENT_BLE_WIFI_SCAN_REQUESTED, NULL, 0, 0);
            break;
        case BLE_CMD_OTA_BEGIN: {
            if (packet->payload_len != 4) { notify_error(packet->command, packet->sequence, BLE_PROTOCOL_ERR_INVALID_ARGUMENT); return; }
            uint32_t size = (uint32_t)packet->payload[0] | ((uint32_t)packet->payload[1] << 8) |
                            ((uint32_t)packet->payload[2] << 16) | ((uint32_t)packet->payload[3] << 24);
            err = esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_BEGIN, &size, sizeof(size), 0);
            break;
        }
        case BLE_CMD_OTA_END: err = esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_END, NULL, 0, 0); break;
        case BLE_CMD_OTA_ABORT: err = esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_ABORT, NULL, 0, 0); break;
        case BLE_CMD_REBOOT: err = esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_REBOOT, NULL, 0, 0); break;
        case BLE_CMD_OTA_QUERY:
        case BLE_CMD_GET_STATUS: err = esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_QUERY, NULL, 0, 0); break;
        default: notify_error(packet->command, packet->sequence, BLE_PROTOCOL_ERR_UNSUPPORTED); return;
    }
    if (err == ESP_OK) notify_ack(packet->command, packet->sequence);
    else notify_error(packet->command, packet->sequence, BLE_PROTOCOL_ERR_BUSY);
}

static void consume_rx_buffer(void)
{
    while (rx_buffer_len >= BLE_PROTOCOL_HEADER_SIZE + BLE_PROTOCOL_CRC_SIZE) {
        if (rx_buffer[0] != BLE_PROTOCOL_MAGIC_0 || rx_buffer[1] != BLE_PROTOCOL_MAGIC_1) {
            memmove(rx_buffer, rx_buffer + 1, --rx_buffer_len);
            continue;
        }
        const uint16_t payload_len = (uint16_t)rx_buffer[7] | ((uint16_t)rx_buffer[8] << 8);
        const size_t frame_len = BLE_PROTOCOL_HEADER_SIZE + payload_len + BLE_PROTOCOL_CRC_SIZE;
        if (rx_buffer[2] != BLE_PROTOCOL_VERSION || payload_len > BLE_PROTOCOL_MAX_PAYLOAD) {
            memmove(rx_buffer, rx_buffer + 1, --rx_buffer_len);
            continue;
        }
        if (rx_buffer_len < frame_len) return;
        const uint16_t expected_crc = (uint16_t)rx_buffer[frame_len - 2] | ((uint16_t)rx_buffer[frame_len - 1] << 8);
        if (ble_protocol_crc16(rx_buffer, frame_len - BLE_PROTOCOL_CRC_SIZE) == expected_crc) {
            ble_protocol_packet_t packet = {
                .flags = rx_buffer[3], .command = rx_buffer[4],
                .sequence = (uint16_t)rx_buffer[5] | ((uint16_t)rx_buffer[6] << 8),
                .payload_len = payload_len, .payload = rx_buffer + BLE_PROTOCOL_HEADER_SIZE,
            };
            dispatch_command(&packet);
        }
        const size_t remaining = rx_buffer_len - frame_len;
        if (remaining) memmove(rx_buffer, rx_buffer + frame_len, remaining);
        rx_buffer_len = remaining;
    }
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (attr_handle == command_value_handle) {
        if (len > RX_BUFFER_SIZE - rx_buffer_len) { rx_buffer_len = 0; return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; }
        os_mbuf_copydata(ctxt->om, 0, len, rx_buffer + rx_buffer_len);
        rx_buffer_len += len;
        consume_rx_buffer();
        return 0;
    }
    if (attr_handle == ota_data_value_handle) {
        uint8_t *data = malloc(len);
        if (data == NULL) return BLE_ATT_ERR_INSUFFICIENT_RES;
        os_mbuf_copydata(ctxt->om, 0, len, data);
        app_ota_data_t ota_data = { .data = data, .len = len };
        if (esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_DATA, &ota_data, sizeof(ota_data), 0) != ESP_OK) {
            free(data);
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) g_conn_handle = event->connect.conn_handle;
            else start_advertising();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            rx_buffer_len = 0;
            start_advertising();
            break;
        default: break;
    }
    return 0;
}

struct ble_gatt_svc_def gatt_svcs[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID128_DECLARE(UUID_SERVICE),
      .characteristics = (struct ble_gatt_chr_def[]) {
          { .uuid = BLE_UUID128_DECLARE(UUID_COMMAND), .access_cb = gatt_access,
            .flags = BLE_GATT_CHR_F_WRITE, .val_handle = &command_value_handle },
          { .uuid = BLE_UUID128_DECLARE(UUID_EVENT), .flags = BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &event_value_handle },
          { .uuid = BLE_UUID128_DECLARE(UUID_OTA_DATA), .access_cb = gatt_access,
            .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE, .val_handle = &ota_data_value_handle },
          { 0 }
      } },
    { 0 }
};

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(200);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(500);
    if (ble_gap_adv_set_fields(&fields) == 0)
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event_handler, NULL);
}

static void ble_on_sync(void) { start_advertising(); }
static void host_task(void *arg) { nimble_port_run(); }

void ble_init(void)
{
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    esp_event_handler_register(APP_EVENT, APP_EVENT_LED_STATE_CHANGED, ble_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_STATUS, ble_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_SCAN_RESULT, ble_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_OTA_STATUS, ble_event_handler, NULL);
    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(host_task);
}

