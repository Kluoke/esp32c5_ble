#include "mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "MQTT";

#define NVS_NAMESPACE "mqtt_conf"
#define NVS_KEY_BROKER "broker_uri"

#define MAX_BROKER_URI_LEN 128

static const char test_data[] = "{"
    "\"id\": \"123\","
    "\"version\": \"1.0\","
    "\"params\": {"
        "\"BatteryPercentage\": {"
            "\"value\": %d"
        "}"
    "}"
"}";

char payload[256];

static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT 连接成功");
        esp_mqtt_client_subscribe(event->client, "esp32/cmd", 1);
        ESP_LOGI(TAG, "已订阅主题: esp32/cmd");
        snprintf(payload, sizeof(payload), test_data, 50);
        esp_mqtt_client_publish(event->client, "$sys/e0M6u06LFp/aowu/thing/property/post", payload, 0, 1, 0);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 断开连接");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "收到消息 - 主题: %.*s, 数据: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        break;
    default:
        break;
    }
}

void mqtt_app_save_broker_and_connect(const char *broker_uri)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 失败: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, NVS_KEY_BROKER, broker_uri);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "MQTT 服务器地址已保存: %s", broker_uri);

    if (s_mqtt_client != NULL) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

void send_message(void){
    esp_mqtt_client_publish(s_mqtt_client, "/test/esp32", "Hello from ESP32!", 0, 1, 0);
}

void mqtt_app_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 中无 MQTT 服务器地址，等待 BLE 配置");
        return;
    }

    char broker_uri[MAX_BROKER_URI_LEN] = {0};
    size_t uri_len = sizeof(broker_uri);
    err = nvs_get_str(handle, NVS_KEY_BROKER, broker_uri, &uri_len);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 中无 MQTT 服务器地址，等待 BLE 配置");
        return;
    }

    ESP_LOGI(TAG, "从 NVS 读取到 MQTT 服务器地址: %s", broker_uri);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        // .broker.address.uri = "mqtt://www.onenet.hk.chinamobile.com:1883",
        // .credentials.username = "e0M6u06LFp", //OneNet product ID
        // .credentials.client_id = "aowu",  //OneNet device name
        // .credentials.authentication.password = "version=2018-10-31&res=products%2Fe0M6u06LFp%2Fdevices%2Faowu&et=1799598843&method=md5&sign=XdO%2FMgjxwfTj7ggtMtHItg%3D%3D", // token
        // .session.keepalive = 60
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}