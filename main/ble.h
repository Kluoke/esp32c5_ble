#ifndef BLE_H
#define BLE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @brief 初始化蓝牙模块。
 * 
 * @param cmd_queue 一个已经创建好的、用于传递命令的消息队列句柄。
 *                  蓝牙模块将作为此队列的生产者。
 */
void ble_init(QueueHandle_t cmd_queue);

#endif // BLE_H