#include "ntp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>

void ntp_init(void)
{
    ESP_LOGI("NTP", "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "cn.pool.ntp.org");
    sntp_setservername(2, "ntp1.aliyun.com");
    esp_sntp_init();
    setenv("TZ", "CST-8", 1);
    tzset();
}