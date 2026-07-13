#include "ble.h"
#include "app_events.h"
#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TAG "BLE_MODULE"
#define DEVICE_NAME "ESP32_BLE_DEVICE"
#define BLE_PREFERRED_MTU 128

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t rx_value_handle;
static uint16_t tx_value_handle;
static uint16_t wifi_ssid_value_handle;
static uint16_t wifi_pwd_value_handle;
static uint16_t mqtt_broker_value_handle;
static uint16_t wifi_scan_cmd_value_handle;
static uint16_t wifi_scan_result_value_handle;
static uint16_t wifi_status_value_handle;
static uint16_t ota_cmd_value_handle;
static uint16_t ota_data_value_handle;
static uint16_t ota_status_value_handle;

/* BLE 内部缓存：用于组装多段写入的配网信息（属于 BLE 自身状态） */
static char wifi_ssid_buf[33] = {0};
static char wifi_pwd_buf[65] = {0};

void start_advertising(void);

/* ---------- GATT Notify 辅助函数 ---------- */

static void ble_notify_string(uint16_t value_handle, const char *value)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "无法发送通知：蓝牙未连接");
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(value, strlen(value));
    if (om == NULL) {
        ESP_LOGE(TAG, "无法为通知分配 mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(g_conn_handle, value_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "发送通知失败; rc=%d", rc);
    }
}

static void ble_notify_wifi_status(const char *status)
{
    ble_notify_string(wifi_status_value_handle, status);
}

static void ble_notify_ota_status(const char *status)
{
    ble_notify_string(ota_status_value_handle, status);
}

static void ble_notify_wifi_scan_result(const app_wifi_scan_result_t *result)
{
    char payload[64];
    int len = snprintf(payload, sizeof(payload), "%s,%d,%u",
                       result->ssid, result->rssi, result->authmode);
    if (len < 0 || len >= sizeof(payload)) {
        ESP_LOGW(TAG, "Wi-Fi 扫描结果过长，已忽略");
        return;
    }

    ble_notify_string(wifi_scan_result_value_handle, payload);
}

static void ble_notify_led_state(uint8_t state)
{
    const char *response = state == 0 ? "LED is OFF" : "LED is ON";
    ble_notify_string(tx_value_handle, response);
}

/* ---------- OTA 状态 -> 字符串（监听 APP_EVENT_OTA_STATUS 后调用）---------- */

static void ble_report_ota_status(app_ota_state_t state, uint8_t progress)
{
    char status_msg[32];
    switch (state) {
        case APP_OTA_STATE_IDLE:
            snprintf(status_msg, sizeof(status_msg), "idle");
            break;
        case APP_OTA_STATE_RECEIVING:
            snprintf(status_msg, sizeof(status_msg), "receiving,%u", progress);
            break;
        case APP_OTA_STATE_VALIDATING:
            snprintf(status_msg, sizeof(status_msg), "validating");
            break;
        case APP_OTA_STATE_UPDATING:
            snprintf(status_msg, sizeof(status_msg), "updating");
            break;
        case APP_OTA_STATE_SUCCESS:
            snprintf(status_msg, sizeof(status_msg), "success");
            break;
        case APP_OTA_STATE_ERROR:
            snprintf(status_msg, sizeof(status_msg), "error");
            break;
        default:
            snprintf(status_msg, sizeof(status_msg), "unknown");
            break;
    }
    ble_notify_ota_status(status_msg);
}

/* ---------- 出站事件监听（业务模块 -> BLE Notify）---------- */

static void ble_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base != APP_EVENT) {
        return;
    }

    switch ((app_event_id_t)event_id) {
        case APP_EVENT_LED_STATE_CHANGED: {
            uint8_t state = *(uint8_t *)event_data;
            ble_notify_led_state(state);
            break;
        }
        case APP_EVENT_WIFI_STATUS: {
            /* event_data 为按值拷贝的状态字符串 */
            const char *status = (const char *)event_data;
            ble_notify_wifi_status(status);
            break;
        }
        case APP_EVENT_WIFI_SCAN_RESULT: {
            app_wifi_scan_result_t *result = (app_wifi_scan_result_t *)event_data;
            ble_notify_wifi_scan_result(result);
            break;
        }
        case APP_EVENT_OTA_STATUS: {
            app_ota_status_t *st = (app_ota_status_t *)event_data;
            ble_report_ota_status(st->state, st->progress);
            break;
        }
        default:
            /* BLE 入站事件由 app_controller 处理，此处忽略 */
            break;
    }
}

/* ---------- GATT 访问回调（入站数据 -> 抛出事件）---------- */

static int gatt_event_handeler(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attr_handle == rx_value_handle) {
            struct os_mbuf *om = ctxt->om;
            if (OS_MBUF_PKTLEN(om) > 0) {
                uint8_t cmd = om->om_data[0];
                esp_event_post(APP_EVENT, APP_EVENT_BLE_LED_CMD, &cmd, sizeof(cmd), 0);
            }
        } else if (attr_handle == wifi_ssid_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < sizeof(wifi_ssid_buf)) {
                os_mbuf_copydata(om, 0, len, wifi_ssid_buf);
                wifi_ssid_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 WiFi SSID: %s", wifi_ssid_buf);
            }
        } else if (attr_handle == wifi_pwd_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < sizeof(wifi_pwd_buf)) {
                os_mbuf_copydata(om, 0, len, wifi_pwd_buf);
                wifi_pwd_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 WiFi Password，开始配网");
                app_wifi_credentials_t creds;
                memset(&creds, 0, sizeof(creds));
                strlcpy(creds.ssid, wifi_ssid_buf, sizeof(creds.ssid));
                strlcpy(creds.password, wifi_pwd_buf, sizeof(creds.password));
                esp_event_post(APP_EVENT, APP_EVENT_BLE_WIFI_CREDENTIALS,
                               &creds, sizeof(creds), 0);
            }
        } else if (attr_handle == mqtt_broker_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < 129) {
                char broker_buf[129] = {0};
                os_mbuf_copydata(om, 0, len, broker_buf);
                broker_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 MQTT 服务器地址: %s，开始连接", broker_buf);
                esp_event_post(APP_EVENT, APP_EVENT_BLE_MQTT_BROKER,
                               broker_buf, strlen(broker_buf) + 1, 0);
            }
        } else if (attr_handle == wifi_scan_cmd_value_handle) {
            char command[5] = {0};
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 4) {
                os_mbuf_copydata(ctxt->om, 0, len, command);
                if (memcmp(command, "SCAN", 4) == 0) {
                    esp_event_post(APP_EVENT, APP_EVENT_BLE_WIFI_SCAN_REQUESTED,
                                   NULL, 0, 0);
                }
            }
        } else if (attr_handle == ota_cmd_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len >= 4) {
                char command[16] = {0};
                os_mbuf_copydata(om, 0, len, command);

                if (strncmp(command, "BEGIN", 5) == 0) {
                    char *size_str = strstr(command, ",");
                    if (size_str) {
                        uint32_t firmware_size = (uint32_t)atol(size_str + 1);
                        ESP_LOGI(TAG, "OTA BEGIN: 固件大小 %u 字节", firmware_size);
                        esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_BEGIN,
                                       &firmware_size, sizeof(firmware_size), 0);
                    }
                } else if (strncmp(command, "END", 3) == 0) {
                    ESP_LOGI(TAG, "OTA END: 结束升级");
                    esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_END, NULL, 0, 0);
                } else if (strncmp(command, "ABORT", 5) == 0) {
                    ESP_LOGI(TAG, "OTA ABORT: 中止升级");
                    esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_ABORT, NULL, 0, 0);
                } else if (strncmp(command, "REBOOT", 6) == 0) {
                    ESP_LOGI(TAG, "OTA REBOOT: 重启设备");
                    ble_notify_ota_status("rebooting");
                    esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_REBOOT, NULL, 0, 0);
                } else if (strncmp(command, "QUERY", 5) == 0) {
                    esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_QUERY, NULL, 0, 0);
                }
            }
        } else if (attr_handle == ota_data_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0) {
                uint8_t *data = malloc(len);
                if (data) {
                    os_mbuf_copydata(om, 0, len, data);
                    ESP_LOGD(TAG, "OTA DATA: 接收到 %u 字节", len);
                    app_ota_data_t ota_data = { .data = data, .len = len };
                    esp_err_t post_err = esp_event_post(APP_EVENT, APP_EVENT_BLE_OTA_DATA,
                                                        &ota_data, sizeof(ota_data), 0);
                    if (post_err != ESP_OK) {
                        ESP_LOGE(TAG, "OTA 数据事件投递失败: %s", esp_err_to_name(post_err));
                        free(data);
                    }
                } else {
                    ESP_LOGE(TAG, "无法分配内存接收 OTA 数据");
                    ble_notify_ota_status("mem_fail");
                }
            }
        }
        ESP_LOGI(TAG, "Received write to handle %d", attr_handle);
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr_handle == tx_value_handle) {
            const char *response = "Hello from ESP32!";
            os_mbuf_append(ctxt->om, response, strlen(response));
        }
        ESP_LOGI(TAG, "Received read from handle %d", attr_handle);
    }
    return 0;
}

int gap_event_handler(struct ble_gap_event *event, void *arg)
{
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
        .uuid = BLE_UUID128_DECLARE(0xf0, 0xde, 0xbc, 0x9a,
                                    0x78, 0x56, 0x34, 0x12,
                                    0x78, 0x56, 0x34, 0x12,
                                    0x78, 0x56, 0x34, 0x12),
        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                .uuid = BLE_UUID16_DECLARE(0xFF01),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &rx_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF02),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &tx_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF03),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &wifi_ssid_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF04),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &wifi_pwd_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF05),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &mqtt_broker_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF06),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &wifi_scan_cmd_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF07),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &wifi_scan_result_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF08),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &wifi_status_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF09),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &ota_cmd_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF0A),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &ota_data_value_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF0B),
                .access_cb = gatt_event_handeler,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &ota_status_value_handle,
            },
            {0}
        },
    },
    {0}
};

void start_advertising(void)
{
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

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
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

void host_task(void *arg)
{
    nimble_port_run();
}

void ble_init(void)
{
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);

    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    /* 监听业务模块抛出的出站通知事件，转换为 GATT Notify 上报给 App */
    esp_event_handler_register(APP_EVENT, APP_EVENT_LED_STATE_CHANGED, ble_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_STATUS, ble_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_SCAN_RESULT, ble_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_OTA_STATUS, ble_event_handler, NULL);

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(host_task);
}
