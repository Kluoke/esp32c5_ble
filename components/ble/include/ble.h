#ifndef BLE_H
#define BLE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>
#include "ota.h"

/**
 * @brief 初始化蓝牙模块。
 * 
 * @param cmd_queue 一个已经创建好的、用于传递命令的消息队列句柄。
 *                  蓝牙模块将作为此队列的生产者。
 */
void ble_init(QueueHandle_t cmd_queue);

/**
 * @brief 通过蓝牙GATT通知上报LED状态。
 * 
 * @param state 要上报的状态 (0x00 for OFF, 0x01 for ON)。
 */
void ble_notify_led_state(uint8_t state);

/**
 * @brief 设置 OTA 状态回调函数（用于通知 App OTA 进度）。
 * 
 * @param callback 回调函数指针。
 */
void ble_set_ota_callback(ota_status_callback_t callback);

#endif // BLE_H