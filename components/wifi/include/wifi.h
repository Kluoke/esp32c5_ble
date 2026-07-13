#ifndef __WIFI_H__
#define __WIFI_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_events.h"

/* 复用 app_events 中的公共扫描结果类型，避免 ble 模块依赖本头文件 */
#define WIFI_SCAN_MAX_APS APP_WIFI_SCAN_MAX_APS

/**
 * @brief 初始化 Wi-Fi 模块（STA 模式）。
 *
 * 状态变化（连接中/已连接/重连/失败等）通过 APP_EVENT_WIFI_STATUS 事件抛出，
 * 不再使用回调函数。
 */
void wifi_init(void);

/**
 * @brief 是否已保存过 Wi-Fi 凭据。
 */
bool wifi_is_provisioned(void);

/**
 * @brief 保存 Wi-Fi 凭据并启动连接。
 *
 * @param ssid     Wi-Fi SSID
 * @param password Wi-Fi 密码
 */
void wifi_save_credentials_and_connect(const char *ssid, const char *password);

/**
 * @brief Scan nearby access points synchronously.
 *
 * @param results Caller-provided array containing at least WIFI_SCAN_MAX_APS entries.
 * @param count In: capacity of results. Out: number of records returned.
 */
esp_err_t wifi_scan_networks(app_wifi_scan_result_t *results, uint16_t *count);

#endif
