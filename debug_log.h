#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>
#include "config.h"

#ifndef LOG_DEBUG_ENABLED
#define LOG_DEBUG_ENABLED 0
#endif

#define LOGI(...) do { Serial.printf(__VA_ARGS__); } while (0)

#if LOG_DEBUG_ENABLED
#define LOGD(...) do { Serial.printf(__VA_ARGS__); } while (0)
#else
#define LOGD(...) do { } while (0)
#endif

#endif // DEBUG_LOG_H
