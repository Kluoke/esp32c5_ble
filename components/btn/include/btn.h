#ifndef BTN_H
#define BTN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化按键模块。
 *
 * btn 模块只负责按键 GPIO 采集与防抖。
 * 检测到有效按下后，通过 APP_EVENT 事件循环抛出 APP_EVENT_BTN_PRESSED 事件，
 * 由 app_controller 决定如何响应（控制 LED、上报状态等）。
 *
 * 不再依赖 ble.h / led.h。
 */
void button_init(void);

#ifdef __cplusplus
}
#endif

#endif // BTN_H
