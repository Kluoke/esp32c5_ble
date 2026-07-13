#include "ota.h"
#include "app_events.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "OTA_MODULE";

// OTA 上下文结构
typedef struct {
    esp_ota_handle_t ota_handle;         // OTA 操作句柄
    const esp_partition_t *update_partition; // 更新分区
    uint32_t total_size;                 // 固件总大小
    uint32_t received_size;              // 已接收大小
    ota_state_t state;                   // 当前状态
    ota_error_t error;                   // 错误码
    bool is_aborted;                     // 是否被中止
} ota_context_t;

static ota_context_t ota_ctx = {
    .state = OTA_STATE_IDLE,
    .error = OTA_ERR_NONE,
    .is_aborted = false,
};

/**
 * @brief 通过事件循环上报 OTA 状态。
 *
 * 将 ota_state_t 转换为公共的 app_ota_state_t（值一一对应）后抛出，
 * 监听者（如 BLE）据此发送 GATT Notify。
 *
 * @note OTA 接口会被 app_controller 在事件循环任务中调用，因此使用非阻塞
 *       方式投递，避免队列满时死锁。
 */
static void ota_report_status(ota_state_t state, uint8_t progress)
{
    app_ota_status_t status = {
        .state = (app_ota_state_t)state,
        .progress = progress,
    };
    esp_event_post(APP_EVENT, APP_EVENT_OTA_STATUS, &status, sizeof(status), 0);
}

void ota_init(void)
{
    // 初始化 OTA 上下文
    ota_ctx.state = OTA_STATE_IDLE;
    ota_ctx.error = OTA_ERR_NONE;
    ota_ctx.is_aborted = false;
    ESP_LOGI(TAG, "OTA 模块已初始化");
}

esp_err_t ota_begin(uint32_t total_size)
{
    if (ota_ctx.state != OTA_STATE_IDLE) {
        ESP_LOGE(TAG, "OTA 状态错误，当前状态: %d", ota_ctx.state);
        ota_ctx.error = OTA_ERR_INVALID_STATE;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_ERR_INVALID_STATE;
    }

    if (total_size == 0) {
        ESP_LOGE(TAG, "固件大小无效: %u", total_size);
        ota_ctx.error = OTA_ERR_INVALID_SIZE;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_ERR_INVALID_ARG;
    }

    // 获取下一个可用的 OTA 分区
    ota_ctx.update_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_ctx.update_partition == NULL) {
        ESP_LOGE(TAG, "未找到可用的 OTA 分区");
        ota_ctx.error = OTA_ERR_WRITE_FAILED;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "开始 OTA 升级，目标分区: %s，固件大小: %u 字节",
             ota_ctx.update_partition->label, total_size);

    // 开始 OTA 操作
    esp_err_t err = esp_ota_begin(ota_ctx.update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_ctx.ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin 失败: %s", esp_err_to_name(err));
        ota_ctx.error = OTA_ERR_WRITE_FAILED;
        ota_report_status(OTA_STATE_ERROR, 0);
        return err;
    }

    // 初始化 OTA 上下文
    ota_ctx.total_size = total_size;
    ota_ctx.received_size = 0;
    ota_ctx.state = OTA_STATE_RECEIVING;
    ota_ctx.is_aborted = false;

    ota_report_status(OTA_STATE_RECEIVING, 0);

    ESP_LOGI(TAG, "OTA 会话已开始");
    return ESP_OK;
}

esp_err_t ota_write(const uint8_t *data, size_t len)
{
    if (ota_ctx.state != OTA_STATE_RECEIVING) {
        ESP_LOGE(TAG, "OTA 状态错误，当前状态: %d", ota_ctx.state);
        ota_ctx.error = OTA_ERR_INVALID_STATE;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_ERR_INVALID_STATE;
    }

    if (ota_ctx.is_aborted) {
        ESP_LOGW(TAG, "OTA 已被中止");
        ota_ctx.error = OTA_ERR_ABORTED;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_FAIL;
    }

    // 写入数据到 OTA 分区
    esp_err_t err = esp_ota_write(ota_ctx.ota_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA write 失败: %s", esp_err_to_name(err));
        ota_ctx.error = OTA_ERR_WRITE_FAILED;
        ota_report_status(OTA_STATE_ERROR, 0);
        ota_abort();
        return err;
    }

    // 更新已接收大小
    ota_ctx.received_size += len;

    // 计算进度
    uint8_t progress = 0;
    if (ota_ctx.total_size > 0) {
        progress = (uint8_t)((ota_ctx.received_size * 100) / ota_ctx.total_size);
        if (progress > 100) progress = 100;
    }

    ESP_LOGD(TAG, "已写入 %u/%u 字节 (%d%%)",
             ota_ctx.received_size, ota_ctx.total_size, progress);

    // 上报进度更新
    ota_report_status(OTA_STATE_RECEIVING, progress);

    // 检查是否接收完成
    if (ota_ctx.received_size >= ota_ctx.total_size) {
        ESP_LOGI(TAG, "固件数据接收完成，准备验证");
        ota_ctx.state = OTA_STATE_VALIDATING;
        ota_report_status(OTA_STATE_VALIDATING, 100);
    }

    return ESP_OK;
}

esp_err_t ota_end(void)
{
    if (ota_ctx.state != OTA_STATE_RECEIVING && ota_ctx.state != OTA_STATE_VALIDATING) {
        ESP_LOGE(TAG, "OTA 状态错误，当前状态: %d", ota_ctx.state);
        ota_ctx.error = OTA_ERR_INVALID_STATE;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_ERR_INVALID_STATE;
    }

    if (ota_ctx.is_aborted) {
        ESP_LOGW(TAG, "OTA 已被中止");
        ota_ctx.error = OTA_ERR_ABORTED;
        ota_report_status(OTA_STATE_ERROR, 0);
        return ESP_FAIL;
    }

    // 结束 OTA 操作
    esp_err_t err = esp_ota_end(ota_ctx.ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end 失败: %s", esp_err_to_name(err));
        ota_ctx.error = OTA_ERR_VERIFY_FAILED;
        ota_report_status(OTA_STATE_ERROR, 0);
        ota_abort();
        return err;
    }

    // 验证固件完整性
    ota_ctx.state = OTA_STATE_UPDATING;
    ota_report_status(OTA_STATE_UPDATING, 100);

    // 设置 OTA 启动分区
    err = esp_ota_set_boot_partition(ota_ctx.update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置启动分区失败: %s", esp_err_to_name(err));
        ota_ctx.error = OTA_ERR_VERIFY_FAILED;
        ota_report_status(OTA_STATE_ERROR, 0);
        ota_abort();
        return err;
    }

    // OTA 成功
    ota_ctx.state = OTA_STATE_SUCCESS;
    ota_ctx.error = OTA_ERR_NONE;

    ESP_LOGI(TAG, "OTA 升级成功，新固件位于分区: %s", ota_ctx.update_partition->label);

    ota_report_status(OTA_STATE_SUCCESS, 100);

    return ESP_OK;
}

void ota_abort(void)
{
    if (ota_ctx.state == OTA_STATE_IDLE) {
        return;
    }

    ESP_LOGI(TAG, "中止 OTA 升级");

    // 中止 OTA 操作
    if (ota_ctx.state == OTA_STATE_RECEIVING) {
        esp_ota_abort(ota_ctx.ota_handle);
    }

    // 重置上下文
    ota_ctx.state = OTA_STATE_ERROR;
    ota_ctx.error = OTA_ERR_ABORTED;
    ota_ctx.is_aborted = true;

    ota_report_status(OTA_STATE_ERROR, 0);
}

ota_state_t ota_get_state(void)
{
    return ota_ctx.state;
}

uint32_t ota_get_received_size(void)
{
    return ota_ctx.received_size;
}

uint8_t ota_get_progress(void)
{
    if (ota_ctx.total_size == 0) {
        return 0;
    }

    uint8_t progress = (uint8_t)((ota_ctx.received_size * 100) / ota_ctx.total_size);
    return (progress > 100) ? 100 : progress;
}

void ota_restart(void)
{
    if (ota_ctx.state == OTA_STATE_SUCCESS) {
        ESP_LOGI(TAG, "重启系统以运行新固件");
        esp_restart();
    } else {
        ESP_LOGW(TAG, "OTA 升级未成功，无法重启到新固件");
    }
}

const char* ota_get_current_partition(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        return running->label;
    }
    return "Unknown";
}
