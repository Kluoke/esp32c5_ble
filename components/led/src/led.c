#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h> // 用于 rand() 和 srand()
#include <time.h>   // 用于 time()

static const char *TAG = "LED_MODULE";

static led_strip_handle_t led_strip_handle; // 声明为静态全局变量，只在本文件可见

void led_init(void)
{
    /* Seed the random number generator */
    srand((unsigned int)time(NULL));

    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
        .led_model = LED_MODEL_WS2812, // 指定型号
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_handle));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip_handle);
    ESP_ERROR_CHECK(led_strip_refresh(led_strip_handle));
    ESP_LOGI(TAG, "LED initialized on GPIO %d", BLINK_GPIO);
}

void led_set_random_color(void)
{
    uint8_t red = rand() % 256;
    uint8_t green = rand() % 256;
    uint8_t blue = rand() % 256;

    led_strip_set_pixel(led_strip_handle, 0, red, green, blue);
    ESP_ERROR_CHECK(led_strip_refresh(led_strip_handle));
    // ESP_LOGI(TAG, "Set random color to R:%d, G:%d, B:%d", red, green, blue);
}

void led_turn_off(void)
{
    led_strip_clear(led_strip_handle);
    ESP_ERROR_CHECK(led_strip_refresh(led_strip_handle));
    ESP_LOGI(TAG, "LED turned off");
}

void led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    led_strip_set_pixel(led_strip_handle, 0, red, green, blue);
    ESP_ERROR_CHECK(led_strip_refresh(led_strip_handle));
    ESP_LOGI(TAG, "Set color to R:%d, G:%d, B:%d", red, green, blue);
}