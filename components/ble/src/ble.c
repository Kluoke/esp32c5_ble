#include "ble.h"
#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led.h"
#include "wifi.h"
#include "mqtt.h"
#include "ota.h"
#include <stdio.h>
#include <string.h>

#define TAG "BLE_MODULE"
#define DEVICE_NAME "ESP32_BLE_DEVICE"
#define BLE_PREFERRED_MTU 128
#define WIFI_SCAN_TASK_STACK_SIZE 8192

static QueueHandle_t led_cmd_queue;

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
static bool wifi_scan_in_progress;

static char wifi_ssid_buf[33] = {0};
static char wifi_pwd_buf[65] = {0};
static char mqtt_broker_buf[129] = {0};

void start_advertising(void);

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

static void ble_ota_status_callback(ota_state_t state, uint8_t progress)
{
    char status_msg[32];
    switch (state) {
        case OTA_STATE_IDLE:
            snprintf(status_msg, sizeof(status_msg), "idle");
            break;
        case OTA_STATE_RECEIVING:
            snprintf(status_msg, sizeof(status_msg), "receiving,%u", progress);
            break;
        case OTA_STATE_VALIDATING:
            snprintf(status_msg, sizeof(status_msg), "validating");
            break;
        case OTA_STATE_UPDATING:
            snprintf(status_msg, sizeof(status_msg), "updating");
            break;
        case OTA_STATE_SUCCESS:
            snprintf(status_msg, sizeof(status_msg), "success");
            break;
        case OTA_STATE_ERROR:
            snprintf(status_msg, sizeof(status_msg), "error");
            break;
        default:
            snprintf(status_msg, sizeof(status_msg), "unknown");
            break;
    }
    ble_notify_ota_status(status_msg);
}

static void ble_notify_wifi_scan_result(const wifi_scan_result_t *result)
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

static void wifi_scan_task(void *arg)
{
    wifi_scan_result_t results[WIFI_SCAN_MAX_APS] = {0};
    uint16_t count = WIFI_SCAN_MAX_APS;

    ble_notify_wifi_status("scanning");
    esp_err_t err = wifi_scan_networks(results, &count);
    if (err != ESP_OK) {
        ble_notify_wifi_status("scan_failed");
    } else {
        for (uint16_t i = 0; i < count; ++i) {
            ble_notify_wifi_scan_result(&results[i]);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        ble_notify_wifi_status("scan_done");
    }

    wifi_scan_in_progress = false;
    vTaskDelete(NULL);
}

static int gatt_event_handeler(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attr_handle == rx_value_handle) {
            struct os_mbuf *om = ctxt->om;
            if (OS_MBUF_PKTLEN(om) > 0) {
                uint8_t cmd = om->om_data[0];
                if (xQueueSend(led_cmd_queue, &cmd, 0) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to send to led_cmd_queue");
                }
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
                wifi_save_credentials_and_connect(wifi_ssid_buf, wifi_pwd_buf);
            }
        } else if (attr_handle == mqtt_broker_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len > 0 && len < sizeof(mqtt_broker_buf)) {
                os_mbuf_copydata(om, 0, len, mqtt_broker_buf);
                mqtt_broker_buf[len] = '\0';
                ESP_LOGI(TAG, "收到 MQTT 服务器地址: %s，开始连接", mqtt_broker_buf);
                mqtt_app_save_broker_and_connect(mqtt_broker_buf);
            }
        } else if (attr_handle == wifi_scan_cmd_value_handle) {
            char command[5] = {0};
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 4) {
                os_mbuf_copydata(ctxt->om, 0, len, command);
                if (memcmp(command, "SCAN", 4) == 0) {
                    if (wifi_scan_in_progress) {
                        ble_notify_wifi_status("scan_busy");
                    } else {
                        wifi_scan_in_progress = true;
                        BaseType_t task_created = xTaskCreate(wifi_scan_task, "wifi_scan",
                                                               WIFI_SCAN_TASK_STACK_SIZE, NULL, 5, NULL);
                        if (task_created != pdPASS) {
                            wifi_scan_in_progress = false;
                            ble_notify_wifi_status("scan_failed");
                            ESP_LOGE(TAG, "创建 Wi-Fi 扫描任务失败");
                        }
                    }
                }
            }
        } else if (attr_handle == ota_cmd_value_handle) {
            struct os_mbuf *om = ctxt->om;
            uint16_t len = OS_MBUF_PKTLEN(om);
            if (len >= 4) {
                char command[16] = {0};
                os_mbuf_copydata(om, 0, len, command);

                // BEGIN 命令：开始 OTA 升级
                if (strncmp(command, "BEGIN", 5) == 0) {
                    // 提取固件大小参数：BEGIN,size_in_bytes
                    char *size_str = strstr(command, ",");
                    if (size_str) {
                        uint32_t firmware_size = atol(size_str + 1);
                        ESP_LOGI(TAG, "OTA BEGIN: 固件大小 %u 字节", firmware_size);
                        esp_err_t err = ota_begin(firmware_size);
                        if (err == ESP_OK) {
                            ble_notify_ota_status("begin_ok");
                        } else {
                            ble_notify_ota_status("begin_fail");
                        }
                    }
                }
                // END 命令：结束 OTA 升级
                else if (strncmp(command, "END", 3) == 0) {
                    ESP_LOGI(TAG, "OTA END: 结束升级");
                    esp_err_t err = ota_end();
                    if (err == ESP_OK) {
                        ble_notify_ota_status("end_ok");
                    } else {
                        ble_notify_ota_status("end_fail");
                    }
                }
                // ABORT 命令：中止 OTA 升级
                else if (strncmp(command, "ABORT", 5) == 0) {
                    ESP_LOGI(TAG, "OTA ABORT: 中止升级");
                    ota_abort();
                    ble_notify_ota_status("aborted");
                }
                // REBOOT 命令：重启设备
                else if (strncmp(command, "REBOOT", 6) == 0) {
                    ESP_LOGI(TAG, "OTA REBOOT: 重启设备");
                    ble_notify_ota_status("rebooting");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    ota_restart();
                }
                // QUERY 命令：查询 OTA 状态
                else if (strncmp(command, "QUERY", 5) == 0) {
                    ota_state_t state = ota_get_state();
                    uint8_t progress = ota_get_progress();
                    ble_ota_status_callback(state, progress);
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
                    esp_err_t err = ota_write(data, len);
                    free(data);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "OTA 写入失败: %s", esp_err_to_name(err));
                        ble_notify_ota_status("write_fail");
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

void ble_init(QueueHandle_t cmd_queue)
{
    led_cmd_queue = cmd_queue;

    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_att_set_preferred_mtu(BLE_PREFERRED_MTU);

    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    wifi_set_status_callback(ble_notify_wifi_status);

    // 初始化 OTA 模块并设置回调
    ota_init();
    ota_set_status_callback(ble_ota_status_callback);

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(host_task);
}

void ble_notify_led_state(uint8_t state)
{
    const char *response = state == 0 ? "LED is OFF" : "LED is ON";
    ble_notify_string(tx_value_handle, response);
}

void ble_set_ota_callback(ota_status_callback_t callback)
{
    ota_set_status_callback(callback);
}
