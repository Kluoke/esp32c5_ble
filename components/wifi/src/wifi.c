#include "wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "WIFI";

static int s_retry_num = 0;
#define MAX_RETRY 5

#define NVS_NAMESPACE "wifi_creds"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "password"

static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;

void wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        switch(event_id){
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI(TAG, "开始连接");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                s_retry_num = 0;
                ESP_LOGI(TAG, "连接成功");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_retry_num < MAX_RETRY) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGW(TAG, "断开连接，重连中... 第 %d 次", s_retry_num);
                } else {
                    ESP_LOGE(TAG, "达到最大重连次数，停止重连");
                }
                break;
            default:
                break;
        }
    }
    else if(event_base == IP_EVENT){
        switch(event_id){
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                break;
            default:
                break;
        }
    }
}

bool wifi_is_provisioned(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_len = 0;
    err = nvs_get_str(handle, NVS_KEY_SSID, NULL, &ssid_len);
    nvs_close(handle);

    return (err == ESP_OK && ssid_len > 0);
}

void wifi_save_credentials_and_connect(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 失败: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, NVS_KEY_SSID, ssid);
    nvs_set_str(handle, NVS_KEY_PASS, password);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi 凭据已保存到 NVS，准备连接");

    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    s_retry_num = 0;
    esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config);
    esp_wifi_stop();
    esp_wifi_start();
}

void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&config);
    esp_wifi_set_mode(WIFI_MODE_STA);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        char ssid[33] = {0};
        char password[65] = {0};
        size_t ssid_len = sizeof(ssid);
        size_t password_len = sizeof(password);

        esp_err_t ssid_err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &ssid_len);
        esp_err_t pwd_err = nvs_get_str(handle, NVS_KEY_PASS, password, &password_len);
        nvs_close(handle);

        if (ssid_err == ESP_OK && pwd_err == ESP_OK) {
            wifi_config_t sta_config = {
                .sta = {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                },
            };
            strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
            strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

            esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config);
            ESP_LOGI(TAG, "从 NVS 读取到 WiFi 凭据，准备连接");
        } else {
            ESP_LOGW(TAG, "NVS 中无 WiFi 凭据，等待 BLE 配网");
        }
    } else {
        ESP_LOGW(TAG, "NVS 中无 WiFi 凭据，等待 BLE 配网");
    }

    esp_wifi_start();
}