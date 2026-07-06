#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_intr_alloc.h"

#include "ble.h"
#include "btn.h"
#include "led.h" // 需要控制 LED

static const char *TAG = "BUTTON";

#define BOOT_BUTTON_GPIO GPIO_NUM_28   // Boot 按键

// -- 私有全局变量 --
static bool led_state = true;
static TimerHandle_t debounce_timer; // 用于防抖的软件定时器
static led_state_change_callback_t on_state_change_cb = NULL; // 状态变化回调函数指针

// -- 私有函数 --

/**
 * @brief 定时器回调函数，在按键稳定后执行。
 * 
 * @param xTimer 定时器的句柄。
 */
static void debounce_timer_callback(TimerHandle_t xTimer)
{
    // 定时器成功触发，说明按键已经稳定
    led_state = !led_state;
    ESP_LOGI(TAG, "Button press stable! New LED state: %s", led_state ? "ON" : "OFF");

    if (led_state) {
        led_set_random_color();
    } else {
        led_turn_off();
    }

    // 如果注册了回调函数，则调用它来上报新状态
    if (on_state_change_cb) {
        on_state_change_cb(led_state);
    }
}

/**
 * @brief 按键 GPIO 的中断服务函数。
 * 
 * @param arg 传递给 ISR 的参数（未使用）。
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    // 在中断中，只重置（或启动）防抖定时器
    xTimerResetFromISR(debounce_timer, NULL);
}

// -- 公开函数 --

void button_init(led_state_change_callback_t cb)
{
    on_state_change_cb = cb; // 保存回调函数指针

    // 创建一个 50ms 的单次软件定时器用于防抖
    debounce_timer = xTimerCreate(
        "DebounceTimer",
        pdMS_TO_TICKS(50),
        pdFALSE, // 单次定时器
        (void *)0,
        debounce_timer_callback
    );

    // 配置按键 GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    // 安装 GPIO 中断服务
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    
    // 为按键引脚添加中断处理函数
    gpio_isr_handler_add(BOOT_BUTTON_GPIO, button_isr_handler, NULL);

    ESP_LOGI(TAG, "Button initialized on GPIO %d", BOOT_BUTTON_GPIO);
}