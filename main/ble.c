#include "ble.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "BLE_MODULE"
#define DEVICE_NAME "ESP32_BLE_DEVICE"
static bool ble_adv_active = false;
static uint16_t rx_value_handle;
static uint16_t tx_value_handle;
static int gatt_event_handeler(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // 处理写入操作
        if(attr_handle == rx_value_handle){
            struct os_mbuf *om = ctxt->om;
            if(om->om_data[0] == 0x01){
                led_set_random_color();
            }
            else if(om->om_data[0] == 0x00){
                led_turn_off();
            }
            // 这里可以处理接收到的数据，例如存储或触发其他操作
            // ESP_LOGI(TAG, "Received data: %.*s", OS_MBUF_PKTLEN(om), (char *)OS_MBUF_DATA(om));
        }
        ESP_LOGI(TAG, "Received write to handle %d", attr_handle);
        // 这里可以处理接收到的数据，例如存储或触发其他操作
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // 处理读取操作
        if(attr_handle == tx_value_handle){
            // 这里可以设置要返回的数据
            const char *response = "Hello from ESP32!";
            os_mbuf_append(ctxt->om, response, strlen(response));
        }
        ESP_LOGI(TAG, "Received read from handle %d", attr_handle);
        // 这里可以返回数据给客户端
    }
    return 0; // 返回成功
}
int gap_event_handler(struct ble_gap_event *event, void *arg){
    if(event->type == BLE_GAP_EVENT_CONNECT){
        if(event->connect.status == 0){
            ble_adv_active = false; // 连接成功，停止广播
            ESP_LOGI(TAG, "连接成功，连接句柄: %d", event->connect.conn_handle);
        }
        else{
            if(!ble_adv_active){
                ESP_LOGE(TAG, "重新开始广播");
                start_advertising(); // 重新开始广播
            }
        }
    }else if(event->type == BLE_GAP_EVENT_DISCONNECT){
            ESP_LOGE(TAG, "连接失败，错误码: %d", event->connect.status);
            if(!ble_adv_active){
                ESP_LOGE(TAG, "重新开始广播");
                start_advertising(); // 重新开始广播
            }
    }
    return 0;
}
struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16),
        .characteristics = (struct ble_gatt_chr_def[])
        {
            //接收函数
            {
                .uuid = BLE_UUID16_DECLARE(0xFF01),
                .access_cb = gatt_event_handeler, // 这里可以设置访问回调函数
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &rx_value_handle, // 这里可以设置接收值的句柄
                .arg = NULL, // 可选参数
            }, 
            //发送函数
            {
                .uuid = BLE_UUID16_DECLARE(0xFF02),
                .access_cb = gatt_event_handeler, // 这里可以设置访问回调函数
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &tx_value_handle, // 这里可以设置发送值的句柄
                .arg = NULL, // 可选参数
            }, 
            {
                0, // 终止符
            } 
        },
    },
    {
        0, // 终止符
    },
};

void start_advertising(void){
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;  //是否是全称
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;  //是否设置发射功率
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;  //自动设置发射功率

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // 可连接
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // 可发现
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(200); // 最小间隔
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(500); // 最大间隔

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL,BLE_HS_FOREVER,&adv_params,gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertisement; rc=%d", rc);
        return;
    }else{
        ESP_LOGE(TAG, "广播已启动等待连接");
        ble_adv_active = true;
    }
}
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "设备初始化完毕");
    start_advertising();
}
void host_task( void * arg){
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
}
void ble_init(void)
{
    // Initialize the NimBLE host stack
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Set the device name
    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(host_task);
}