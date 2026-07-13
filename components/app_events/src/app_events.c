#include "app_events.h"
#include "esp_log.h"

static const char *TAG = "APP_EVENTS";

ESP_EVENT_DEFINE_BASE(APP_EVENT);

esp_err_t app_events_init(void)
{
    /* 创建默认事件循环，供所有模块注册 handler 与 post 事件 */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "创建默认事件循环失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "应用事件循环已就绪");
    return ESP_OK;
}
