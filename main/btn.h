#ifndef BTN_H
#define BTN_H

#include <stdint.h>

/**
 * @brief LED 状态变化的回调函数类型。
 * 
 * @param state 新的 LED 状态 (0x00 for OFF, 0x01 for ON)。
 */
typedef void (*led_state_change_callback_t)(uint8_t state);

/**
 * @brief 初始化按键，并注册一个状态变化回调函数。
 * 
 * @param cb 当 LED 状态因按键发生变化时要调用的回调函数。
 */
void button_init(led_state_change_callback_t cb);

#endif // BTN_H