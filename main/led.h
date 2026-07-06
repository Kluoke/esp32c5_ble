#ifndef _LED_H_
#define _LED_H_

#include "driver/gpio.h"
#include "led_strip.h"

// 板载 RGB LED 的 GPIO 引脚
#define BLINK_GPIO  GPIO_NUM_27 // 保持与您当前代码一致

/**
 * @brief 初始化板载 RGB LED
 * 
 * 配置 LED strip 驱动，并清除 LED 状态。
 */
void led_init(void);

/**
 * @brief 设置板载 RGB LED 为随机颜色
 * 
 * 生成随机的 RGB 值并设置给 LED。
 */
void led_set_random_color(void);

/**
 * @brief 关闭板载 RGB LED
 * 
 * 清除 LED 状态，使其熄灭。
 */
void led_turn_off(void);

#endif // _LED_H_