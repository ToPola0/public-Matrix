#ifndef CONFIG_H
#define CONFIG_H

// WiFi
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// MQTT
#define MQTT_BROKER   "YOUR_MQTT_BROKER"
#define MQTT_PORT     1883
#define MQTT_USER     "YOUR_MQTT_USER"
#define MQTT_PASSWORD "YOUR_MQTT_PASSWORD"

// LED Matrix / board defaults
#define LED_PIN       2
#define LED_WIDTH     32
#define LED_HEIGHT    8
#define NUM_LEDS      (LED_WIDTH * LED_HEIGHT)
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB

// Matrix mapping (adjust if text is mirrored/garbled)
#define MATRIX_SERPENTINE 1
#define MATRIX_COLUMN_MAJOR 1
#define MATRIX_FLIP_X     0
#define MATRIX_FLIP_Y     0

// Clock font style: 1 = classic readable, 2 = blocky
#define CLOCK_FONT_STYLE 1

// Diagnostic mode: 1 = run standalone LED tester, 0 = normal clock app
#define LED_TESTER_MODE 0

// Corner calibration mode: 1 = show 4 corners (top red, bottom green)
#define LED_CORNER_CALIBRATION_MODE 0

// Arduino IDE OTA (Network Port)
#define OTA_HOSTNAME "LedMatrixClock"
#define OTA_PORT 3232
#define OTA_PASSWORD "12345678"

// GitHub OTA auto-update (A/B, no USB required)
// Set ENABLE to 1 and provide raw GitHub URLs for version.txt and firmware.bin.
#define GITHUB_OTA_ENABLED 1
#define GITHUB_OTA_VERSION_URL "https://raw.githubusercontent.com/ToPola0/public-Matrix/main/Firmware/version.txt"
#define GITHUB_OTA_FIRMWARE_URL "https://raw.githubusercontent.com/ToPola0/public-Matrix/main/Firmware/firmware.bin"
#define GITHUB_OTA_CHECK_INTERVAL_MS 3600000UL
#define GITHUB_OTA_BOOT_DELAY_MS 30000UL
#define GITHUB_OTA_TEST_BUTTON 0  // zmień na 0 aby ukryć guzik testowy w menu OTA
#define GITHUB_OTA_RETRY_INTERVAL_MS 1800000UL  // 30 minut między próbami
#define GITHUB_OTA_TOTAL_TIMEOUT_MS 600000UL    // 10 minut całkowity timeout
#define GITHUB_OTA_MAX_RETRIES 3                // max 3 próby aktualizacji

// Build-time logs: 1 = verbose debug logs, 0 = only essential logs
#define LOG_DEBUG_ENABLED 0

// Buzzer
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define BUZZER_PIN    18
#else
#define BUZZER_PIN    18
#endif

// MQTT Topics
#define MQTT_TOPIC_MODE        "home/led/mode"
#define MQTT_TOPIC_COLOR       "home/led/color"
#define MQTT_TOPIC_BRIGHTNESS  "home/led/brightness"
#define MQTT_TOPIC_MUTE        "home/led/mute"
#define MQTT_TOPIC_STATUS      "home/led/status"
#define MQTT_TOPIC_IP          "home/led/ip"
// === NEW TOPICS ===
#define MQTT_TOPIC_DISPLAY_MODE    "home/led/display_mode"
#define MQTT_TOPIC_DISPLAY_ENABLED  "home/led/display_enabled"
#define MQTT_TOPIC_ANIMATION_MODE   "home/led/animation_mode"
#define MQTT_TOPIC_ANIMATION_SPEED  "home/led/animation_speed"
#define MQTT_TOPIC_CLOCK_COLOR      "home/led/clock_color"
#define MQTT_TOPIC_QUOTE_COLOR      "home/led/quote_color"
#define MQTT_TOPIC_ANIM_COLOR       "home/led/animation_color"
#define MQTT_TOPIC_MESSAGE          "home/led/message"
#define MQTT_TOPIC_MESSAGE_COLOR    "home/led/message_color"
#define MQTT_TOPIC_MESSAGE_SPEED    "home/led/message_speed"
#define MQTT_TOPIC_MESSAGE_TIME     "home/led/message_time"

#endif // CONFIG_H
