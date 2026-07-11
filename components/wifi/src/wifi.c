#include "wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"

static const char *TAG = "WIFI";

void wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        switch(event_id){
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI(TAG, "开始连接 \n");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "连接成功 \n");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "断开连接 \n");
                esp_wifi_stop();
                break;
            default:
                break;
        }
    }
    else if(event_base == IP_EVENT){
        switch(event_id){
            case IP_EVENT_STA_GOT_IP:
                esp_netif_ip_info_t* ip_info = (esp_netif_ip_info_t*) event_data;
                printf("Got IP: " IPSTR "\n", IP2STR(&ip_info->ip));
                ESP_LOGI(TAG, "Got IP: " IPSTR "\n", IP2STR(&ip_info->ip));
                break;
            default:
                break;
        }           
    }
}

void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&config);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config);
    esp_wifi_start();
}