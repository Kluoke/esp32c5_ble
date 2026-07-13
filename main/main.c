/**
 * @file main.c
 * @brief 应用入口 + app_controller（胶水层）。
 *
 * 本文件是整个系统的“装配层”：
 * - 负责 NVS、事件循环以及各组件（led/btn/wifi/ble/ntp/mqtt/ota）的初始化；
 * - 通过注册 APP_EVENT 事件处理器，把各“黑盒”模块用事件循环连接起来。
 *
 * 各 components/ 下的模块之间不再互相 #include，跨模块业务流统一在此处编排：
 *   BLE 入站事件  ->  app_controller  ->  wifi / ota / mqtt / led
 *   btn 按键事件  ->  app_controller  ->  led + LED 状态上报
 *   wifi/ota 状态 -> (事件循环)       ->  BLE GATT Notify
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"

#include "nvs_flash.h"
#include "app_events.h"
#include "led.h"
#include "btn.h"
#include "ble.h"
#include "wifi.h"
#include "ntp.h"
#include "mqtt.h"
#include "ota.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MAIN_APP";

#define WIFI_SCAN_TASK_STACK_SIZE 8192

/* app_controller 维护的业务状态 */
static bool s_led_state = true;            /* 当前 LED 开关状态（按键切换用） */
static bool s_wifi_scan_in_progress = false; /* Wi-Fi 扫描忙标志 */

/* ---------- Wi-Fi 扫描任务（业务编排，原 ble.c 中的逻辑移至此处）---------- */

static void wifi_scan_task(void *arg)
{
    app_wifi_scan_result_t results[APP_WIFI_SCAN_MAX_APS] = {0};
    uint16_t count = APP_WIFI_SCAN_MAX_APS;

    /* “scanning” 状态由 wifi 模块/本任务抛出，BLE 监听后 Notify 给 App */
    esp_event_post(APP_EVENT, APP_EVENT_WIFI_STATUS, "scanning",
                   strlen("scanning") + 1, portMAX_DELAY);

    esp_err_t err = wifi_scan_networks(results, &count);
    if (err != ESP_OK) {
        esp_event_post(APP_EVENT, APP_EVENT_WIFI_STATUS, "scan_failed",
                       strlen("scan_failed") + 1, portMAX_DELAY);
    } else {
        for (uint16_t i = 0; i < count; ++i) {
            esp_event_post(APP_EVENT, APP_EVENT_WIFI_SCAN_RESULT,
                           &results[i], sizeof(results[i]), portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        esp_event_post(APP_EVENT, APP_EVENT_WIFI_STATUS, "scan_done",
                       strlen("scan_done") + 1, portMAX_DELAY);
    }

    s_wifi_scan_in_progress = false;
    vTaskDelete(NULL);
}

/* ---------- app_controller 事件处理器（跨模块业务编排）---------- */

static void app_controller_event_handler(void *arg, esp_event_base_t event_base,
                                         int32_t event_id, void *event_data)
{
    if (event_base != APP_EVENT) {
        return;
    }

    switch ((app_event_id_t)event_id) {
        /* ---- BLE 下发的 LED 命令 ---- */
        case APP_EVENT_BLE_LED_CMD: {
            uint8_t cmd = *(uint8_t *)event_data;
            ESP_LOGI(TAG, "LED cmd received: 0x%02X", cmd);
            if (cmd == 0x01) {
                led_set_random_color();
                s_led_state = true;
            } else if (cmd == 0x00) {
                led_turn_off();
                s_led_state = false;
                /* 关灯时发布一条 MQTT 消息（保持原业务行为） */
                send_message();
            }
            break;
        }

        /* ---- 按键按下：切换 LED 并上报新状态 ---- */
        case APP_EVENT_BTN_PRESSED: {
            s_led_state = !s_led_state;
            ESP_LOGI(TAG, "Button pressed, new LED state: %s",
                     s_led_state ? "ON" : "OFF");
            if (s_led_state) {
                led_set_random_color();
            } else {
                led_turn_off();
            }
            /* 抛出 LED 状态变化事件，BLE 监听后通过 GATT Notify 上报给 App。
             * 非阻塞投递：本处理器运行在事件循环任务中，避免队列满时死锁。 */
            uint8_t state = s_led_state ? 1 : 0;
            esp_event_post(APP_EVENT, APP_EVENT_LED_STATE_CHANGED,
                           &state, sizeof(state), 0);
            break;
        }

        /* ---- BLE 配网：Wi-Fi 凭据 ---- */
        case APP_EVENT_BLE_WIFI_CREDENTIALS: {
            app_wifi_credentials_t *creds = (app_wifi_credentials_t *)event_data;
            wifi_save_credentials_and_connect(creds->ssid, creds->password);
            break;
        }

        /* ---- BLE 配网：MQTT 服务器地址 ---- */
        case APP_EVENT_BLE_MQTT_BROKER: {
            const char *broker_uri = (const char *)event_data;
            mqtt_app_save_broker_and_connect(broker_uri);
            break;
        }

        /* ---- BLE 触发的 Wi-Fi 扫描 ---- */
        case APP_EVENT_BLE_WIFI_SCAN_REQUESTED: {
            if (s_wifi_scan_in_progress) {
                esp_event_post(APP_EVENT, APP_EVENT_WIFI_STATUS, "scan_busy",
                               strlen("scan_busy") + 1, 0);
            } else {
                s_wifi_scan_in_progress = true;
                BaseType_t task_created = xTaskCreate(wifi_scan_task, "wifi_scan",
                                                      WIFI_SCAN_TASK_STACK_SIZE,
                                                      NULL, 5, NULL);
                if (task_created != pdPASS) {
                    s_wifi_scan_in_progress = false;
                    esp_event_post(APP_EVENT, APP_EVENT_WIFI_STATUS, "scan_failed",
                                   strlen("scan_failed") + 1, 0);
                    ESP_LOGE(TAG, "创建 Wi-Fi 扫描任务失败");
                }
            }
            break;
        }

        /* ---- OTA 控制命令 ---- */
        case APP_EVENT_BLE_OTA_BEGIN: {
            uint32_t total_size = *(uint32_t *)event_data;
            ota_begin(total_size);
            break;
        }
        case APP_EVENT_BLE_OTA_DATA: {
            app_ota_data_t *od = (app_ota_data_t *)event_data;
            esp_err_t err = ota_write(od->data, od->len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA 写入失败: %s", esp_err_to_name(err));
            }
            free(od->data); /* 释放 BLE 模块 malloc 的缓冲区 */
            break;
        }
        case APP_EVENT_BLE_OTA_END:
            ota_end();
            break;
        case APP_EVENT_BLE_OTA_ABORT:
            ota_abort();
            break;
        case APP_EVENT_BLE_OTA_REBOOT:
            /* 留出时间让 BLE 的 "rebooting" 通知发出 */
            vTaskDelay(pdMS_TO_TICKS(100));
            ota_restart();
            break;
        case APP_EVENT_BLE_OTA_QUERY: {
            /* 读取 OTA 当前状态并抛出，BLE 监听后 Notify 给 App */
            app_ota_status_t status = {
                .state = (app_ota_state_t)ota_get_state(),
                .progress = ota_get_progress(),
            };
            esp_event_post(APP_EVENT, APP_EVENT_OTA_STATUS,
                           &status, sizeof(status), 0);
            break;
        }

        /* 出站通知事件（WIFI_STATUS / OTA_STATUS / SCAN_RESULT / LED_STATE_CHANGED）
         * 由 BLE 模块监听处理，app_controller 不需要响应 */
        default:
            break;
    }
}

/**
 * @brief 注册 app_controller 关心的事件处理器。
 *
 * 显式注册各个入站事件 ID，避免捕获出站通知事件。
 */
static void app_controller_register_handlers(void)
{
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_LED_CMD, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BTN_PRESSED, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_WIFI_CREDENTIALS, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_MQTT_BROKER, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_WIFI_SCAN_REQUESTED, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_OTA_BEGIN, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_OTA_DATA, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_OTA_END, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_OTA_ABORT, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_OTA_REBOOT, app_controller_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVENT_BLE_OTA_QUERY, app_controller_event_handler, NULL);
}

void app_main(void)
{
    time_t now;
    struct tm timeinfo;

    /* 1. 底层驱动与 NVS */
    nvs_flash_init();

    /* 2. 创建统一事件循环（必须在任何模块初始化之前） */
    app_events_init();

    /* 3. 注册 app_controller 事件处理器（胶水层就位，准备接收各模块事件） */
    app_controller_register_handlers();

    /* 4. 初始化各功能模块（模块间互不依赖，仅依赖 app_events 公共契约） */
    led_init();
    ota_init();
    ble_init();      /* 内部注册 BLE 出站通知监听 */
    button_init();   /* 不再传入回调，按键事件走事件循环 */
    wifi_init();
    ntp_init();
    mqtt_app_init();

    /* 5. 初始状态与日志 */
    led_set_random_color(); /* 启动时设置一个随机颜色 */
    s_led_state = true;
    ESP_LOGI(TAG, "All components initialized. Application running.");
    ESP_LOGI(TAG, "Press BOOT button to toggle LED");

    ESP_LOGI(TAG, "Running from partition: %s", ota_get_current_partition());

    now = time(NULL);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", asctime(&timeinfo));

    /* 主循环保持空闲，所有业务由事件循环驱动 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
