#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * OTA 状态定义
 */
typedef enum {
    OTA_STATE_IDLE = 0,           // 空闲状态
    OTA_STATE_READY,              // 准备接收固件
    OTA_STATE_RECEIVING,          // 正在接收固件数据
    OTA_STATE_VALIDATING,         // 正在验证固件
    OTA_STATE_UPDATING,           // 正在更新固件
    OTA_STATE_SUCCESS,            // 升级成功
    OTA_STATE_ERROR,              // 升级失败
} ota_state_t;

/**
 * OTA 错误码定义
 */
typedef enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_INVALID_STATE,
    OTA_ERR_INVALID_SIZE,
    OTA_ERR_INVALID_CRC,
    OTA_ERR_WRITE_FAILED,
    OTA_ERR_VERIFY_FAILED,
    OTA_ERR_ABORTED,
} ota_error_t;

/**
 * OTA 回调函数类型
 */
typedef void (*ota_status_callback_t)(ota_state_t state, uint8_t progress);

/**
 * 初始化 OTA 模块
 */
void ota_init(void);

/**
 * 开始 OTA 升级会话
 *
 * @param total_size 预期的固件总大小（字节）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ota_begin(uint32_t total_size);

/**
 * 写入固件数据块
 *
 * @param data 数据指针
 * @param len 数据长度
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ota_write(const uint8_t *data, size_t len);

/**
 * 结束 OTA 升级会话并验证固件
 *
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t ota_end(void);

/**
 * 取消 OTA 升级
 */
void ota_abort(void);

/**
 * 获取当前 OTA 状态
 *
 * @return OTA 状态
 */
ota_state_t ota_get_state(void);

/**
 * 获取已接收的固件大小
 *
 * @return 已接收字节数
 */
uint32_t ota_get_received_size(void);

/**
 * 获取升级进度百分比
 *
 * @return 进度百分比 (0-100)
 */
uint8_t ota_get_progress(void);

/**
 * 设置 OTA 状态回调函数
 *
 * @param callback 回调函数指针
 */
void ota_set_status_callback(ota_status_callback_t callback);

/**
 * 重启到新固件
 */
void ota_restart(void);

/**
 * 查询当前运行的分区信息
 *
 * @return 当前分区名称字符串
 */
const char* ota_get_current_partition(void);

#endif // OTA_H