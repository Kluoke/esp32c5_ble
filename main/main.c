#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "led.h"
#include "btn.h"
#include "ble.h"

static const char *TAG = "MAIN_APP";

void app_main(void)
{
    nvs_flash_init(); // 初始化 NVS，用于存储配置数据
    ble_init(); // 初始化 NimBLE 广播
    // 初始化 LED
    led_init();

    // 初始化按键（包含防抖逻辑）
    button_init();
    led_set_random_color(); // 启动时设置一个随机颜色
    ESP_LOGI(TAG, "All components initialized. Application running.");
    ESP_LOGI(TAG, "Press BOOT button to toggle LED");

    // 主循环现在可以用于其他任务，或者保持空闲
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}