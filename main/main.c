#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" // 引入队列头文件
#include "esp_log.h"

#include "nvs_flash.h"
#include "led.h"
#include "btn.h"
#include "ble.h"

static const char *TAG = "MAIN_APP";

#define LED_CMD_QUEUE_LENGTH 5
static QueueHandle_t led_cmd_queue;

/**
 * @brief LED 控制任务（消费者）
 * 
 * @param pvParameters 任务参数（未使用）
 */
static void led_control_task(void *pvParameters)
{
    uint8_t cmd;
    while (1) {
        // 阻塞等待从队列中接收命令
        if (xQueueReceive(led_cmd_queue, &cmd, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "LED task received command: 0x%02X", cmd);
            if (cmd == 0x01) {
                led_set_random_color();
            } else if (cmd == 0x00) {
                led_turn_off();
            }
        }
    }
}

void app_main(void)
{
    // 1. 初始化底层驱动和 NVS
    nvs_flash_init();
    led_init();
    button_init();

    // 2. 创建跨模块通信机制（消息队列）
    led_cmd_queue = xQueueCreate(LED_CMD_QUEUE_LENGTH, sizeof(uint8_t));
    if (led_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create led_cmd_queue");
    }

    // 3. 初始化需要通信机制的模块
    ble_init(led_cmd_queue); // 初始化 NimBLE，并传入队列句柄

    // 4. 创建并启动应用逻辑任务
    xTaskCreate(led_control_task, "led_control_task", 2048, NULL, 5, NULL);

    // 5. 设置初始状态并打印日志
    led_set_random_color(); // 启动时设置一个随机颜色
    ESP_LOGI(TAG, "All components initialized. Application running.");
    ESP_LOGI(TAG, "Press BOOT button to toggle LED");

    // 主循环现在可以用于其他任务，或者保持空闲
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}