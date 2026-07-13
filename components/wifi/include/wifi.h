#ifndef __WIFI_H__
#define __WIFI_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WIFI_SCAN_MAX_APS 20

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_scan_result_t;

typedef void (*wifi_status_callback_t)(const char *status);

void wifi_init(void);
bool wifi_is_provisioned(void);
void wifi_save_credentials_and_connect(const char *ssid, const char *password);

/**
 * @brief Scan nearby access points synchronously.
 *
 * @param results Caller-provided array containing at least WIFI_SCAN_MAX_APS entries.
 * @param count In: capacity of results. Out: number of records returned.
 */
esp_err_t wifi_scan_networks(wifi_scan_result_t *results, uint16_t *count);

/**
 * @brief Register a callback for Wi-Fi provisioning status changes.
 */
void wifi_set_status_callback(wifi_status_callback_t callback);

#endif