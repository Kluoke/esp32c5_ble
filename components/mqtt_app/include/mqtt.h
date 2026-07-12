#ifndef __MQTT_H__
#define __MQTT_H__

void mqtt_app_init(void);
void send_message(void);
void mqtt_app_save_broker_and_connect(const char *broker_uri);

#endif