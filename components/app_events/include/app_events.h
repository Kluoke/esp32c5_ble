#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 统一的应用事件基底。
 *
 * 所有模块通过此事件基底进行解耦通信：
 * - 各业务模块只负责“抛出(Post)”事件或“监听(Handler)”事件。
 * - 模块之间不再互相 #include，而是通过此公共契约连接。
 */
ESP_EVENT_DECLARE_BASE(APP_EVENT);

/**
 * @brief 应用统一事件 ID
 */
typedef enum {
    /* ---- BLE 入站事件（BLE 收到 GATT 数据后抛出，由 app_controller 处理）---- */
    APP_EVENT_BLE_LED_CMD,              /**< data: uint8_t cmd (0x00=off, 0x01=on) */
    APP_EVENT_BLE_WIFI_CREDENTIALS,     /**< data: app_wifi_credentials_t* */
    APP_EVENT_BLE_MQTT_BROKER,          /**< data: const char* broker_uri (字符串按值拷贝) */
    APP_EVENT_BLE_WIFI_SCAN_REQUESTED,  /**< data: NULL */
    APP_EVENT_BLE_OTA_BEGIN,            /**< data: uint32_t total_size */
    APP_EVENT_BLE_OTA_DATA,             /**< data: app_ota_data_t* (调用者负责释放内部 buffer) */
    APP_EVENT_BLE_OTA_END,              /**< data: NULL */
    APP_EVENT_BLE_OTA_ABORT,            /**< data: NULL */
    APP_EVENT_BLE_OTA_REBOOT,           /**< data: NULL */
    APP_EVENT_BLE_OTA_QUERY,            /**< data: NULL */

    /* ---- 按键事件（btn 抛出，由 app_controller 处理）---- */
    APP_EVENT_BTN_PRESSED,              /**< data: NULL */

    /* ---- 出站通知事件（业务模块抛出，由 BLE 监听并转发给 App）---- */
    APP_EVENT_LED_STATE_CHANGED,        /**< data: uint8_t state (0=off, 1=on) */
    APP_EVENT_WIFI_STATUS,              /**< data: const char* status (字符串按值拷贝) */
    APP_EVENT_WIFI_SCAN_RESULT,         /**< data: app_wifi_scan_result_t* */
    APP_EVENT_OTA_STATUS,               /**< data: app_ota_status_t* */
} app_event_id_t;

/**
 * @brief Wi-Fi 配网凭据（BLE -> app_controller）
 */
typedef struct {
    char ssid[33];
    char password[65];
} app_wifi_credentials_t;

/**
 * @brief Wi-Fi 扫描结果（wifi -> BLE 通知）
 *
 * 注意：此类型为公共契约，wifi 与 ble 模块均依赖它，避免互相 include。
 */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} app_wifi_scan_result_t;

#define APP_WIFI_SCAN_MAX_APS 20

/**
 * @brief OTA 状态枚举（公共契约，与 ota 模块内部状态值一一对应）
 */
typedef enum {
    APP_OTA_STATE_IDLE = 0,
    APP_OTA_STATE_READY,
    APP_OTA_STATE_RECEIVING,
    APP_OTA_STATE_VALIDATING,
    APP_OTA_STATE_UPDATING,
    APP_OTA_STATE_SUCCESS,
    APP_OTA_STATE_ERROR,
} app_ota_state_t;

/**
 * @brief OTA 状态上报（ota -> BLE 通知）
 */
typedef struct {
    app_ota_state_t state;
    uint8_t progress;   /**< 0-100 */
} app_ota_status_t;

/**
 * @brief OTA 数据块（BLE -> app_controller）
 *
 * @note data 指针指向由 BLE 模块 malloc 的缓冲区，app_controller 处理完后负责 free。
 */
typedef struct {
    uint8_t *data;
    size_t len;
} app_ota_data_t;

/**
 * @brief 创建应用默认事件循环。
 *
 * 必须在任何模块初始化之前调用。
 *
 * @return ESP_OK 成功
 */
esp_err_t app_events_init(void);

#ifdef __cplusplus
}
#endif

#endif // APP_EVENTS_H
