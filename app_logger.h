#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <Arduino.h>

void app_logger_set_enabled(bool enabled);
bool app_logger_is_enabled();
void app_logger_clear();
uint32_t app_logger_latest_seq();

void app_log(const String& message);
void app_logf(const char* fmt, ...);

void app_logger_build_json(uint32_t sinceSeq, uint16_t limit, String& outJson);

#endif // APP_LOGGER_H
