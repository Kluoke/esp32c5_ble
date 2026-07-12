#ifndef __WIFI_H__
#define __WIFI_H__

#include <stdbool.h>

void wifi_init(void);
bool wifi_is_provisioned(void);
void wifi_save_credentials_and_connect(const char *ssid, const char *password);

#endif