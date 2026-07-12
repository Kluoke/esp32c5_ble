#include "ble.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "led.h"
#include "wifi.h"
#include "mqtt.h"
#include <string.h>

#define TAG "BLE_MODULE"
#define DEVICE_NAME "ESP32_BLE_DEVICE"

static QueueHandle_t led_cmd_queue;

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t rx_value_handle;
static uint16_t tx_value_handle;
static uint16_t wifi_ssid_value_handle;
static uint16_t wifi_pwd_value_handle;
static uint16_t mqtt_broker_value_handle;

static char wifi_ssid_buf[33] = {0};
static char wifi_pwd_buf[65] = {0};
static char mqtt_broker_buf[129] = {0};

void start_advertising(void);

static int gatt_event_handeler(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if(attr_handle == rx_value_handle){
            struct os_mbuf *om = ctxt->om;
            if (OS_MBUF_PKTLEN(om) > 0) {
                uint8_t cmd = om->om_data[0];
                if (xQueueSend(led_cmd_queue, &cmd, 0) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to send to led_cmd_queue");
                }
            }
        }
        else if(attr_handle == wifi_ssid_value_handle){
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < sizeof(wifi_ssid_buf)) {
                os_mbuf_copydata(om, 0, len, wifi_ssid_buf);
                wifi_ssid_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 WiFi SSID: %s", wifi_ssid_buf);
            }
        }
        else if(attr_handle == wifi_pwd_value_handle){
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < sizeof(wifi_pwd_buf)) {
                os_mbuf_copydata(om, 0, len, wifi_pwd_buf);
                wifi_pwd_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 WiFi Password，开始配网");
                wifi_save_credentials_and_connect(wifi_ssid_buf, wifi_pwd_buf);
            }
        }
        else if(attr_handle == mqtt_broker_value_handle){
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < sizeof(mqtt_broker_buf)) {
                os_mbuf_copydata(om, 0, len, mqtt_broker_buf);
                mqtt_broker_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 MQTT 服务器地址: %s，开始连接", mqtt_broker_buf);
                mqtt_app_save_broker_and_connect(mqtt_broker_buf);
            }
        }
        ESP_LOGI(TAG, "Received write to handle %d", attr_handle);
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if(attr_handle == tx_value_handle){
            const char *response = "Hello from ESP32!";
            os_mbuf_append(ctxt->om, response, strlen(response));
        }
        ESP_LOGI(TAG, "Received read from handle %d", attr_handle);
    }
    return 0;
}
int gap_event_handler(struct ble_gap_event *event, void *arg){
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "连接成功，连接句柄: %d", event->connect.conn_handle);
        } else {
            ESP_LOGE(TAG, "连接失败，重新开始广播");
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "连接断开，原因: %d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertising();
        break;
    default:
        break;
    }
    return 0;
}
struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16),
        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                .uuid = BLE_UUID16_DECLARE(0xFF01),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &rx_value_handle,
                .arg = NULL,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF02),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &tx_value_handle,
                .arg = NULL,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF03),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &wifi_ssid_value_handle,
                .arg = NULL,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF04),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &wifi_pwd_value_handle,
                .arg = NULL,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF05),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &mqtt_broker_value_handle,
                .arg = NULL,
            },
            {
                0,
            }
        },
    },
    {
        0,
    },
};

void start_advertising(void){
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(200);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(500);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL,BLE_HS_FOREVER,&adv_params,gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertisement; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "广播已启动等待连接");
}
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "设备初始化完毕");
    start_advertising();
}
void host_task( void * arg){
    nimble_port_run();
}
void ble_init(QueueHandle_t cmd_queue)
{
    led_cmd_queue = cmd_queue;

    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(host_task);
}

void ble_notify_led_state(uint8_t state)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "无法发送通知：蓝牙未连接");
        return;
    }

    char *response = NULL;

    if(state == 0){
        response = "LED is OFF";
    }else if (state == 1){
        response = "LED is ON";
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(response, strlen(response));
    if (!om) {
        ESP_LOGE(TAG, "无法为通知分配 mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(g_conn_handle, tx_value_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "发送通知失败; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "成功发送LED状态通知: 0x%02X", state);
    }
}