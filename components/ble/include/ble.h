#ifndef BLE_H
#define BLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化蓝牙模块。
 *
 * BLE 模块作为“黑盒”：
 * - 仅负责蓝牙协议栈、GATT 收发与广播。
 * - 收到 App 下发的数据时，通过 APP_EVENT 事件循环抛出
 *   APP_EVENT_BLE_* 事件，由 app_controller 处理。
 * - 监听 APP_EVENT_*(_STATUS / _SCAN_RESULT / LED_STATE_CHANGED) 事件，
 *   将业务模块的状态通过 GATT Notify 上报给 App。
 *
 * 不再依赖 led/wifi/mqtt/ota 等业务模块的头文件。
 */
void ble_init(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_H
