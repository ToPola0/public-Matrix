#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>

void mqtt_manager_begin();
void mqtt_manager_configure(bool enabled,
                            const char* host,
                            uint16_t port,
                            const char* user,
                            const char* password,
                            const char* discoveryPrefix);
void mqtt_manager_loop();
void mqtt_manager_publish_now();
void mqtt_manager_tryDisplayHaEntity();
bool mqtt_manager_isEnabled();
bool mqtt_manager_isConnected();
String mqtt_manager_getStatus();

#endif // MQTT_MANAGER_H
