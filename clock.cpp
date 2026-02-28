#include "clock.h"
#include "display.h"
#include "web_panel.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);
uint8_t currentHour = 0, currentMinute = 0, currentSecond = 0;
bool colonState = true;
static uint32_t lastColonToggle = 0;
static uint32_t lastNtpRetry = 0;
static uint8_t ntpFailCount = 0;
static uint32_t lastClockRefresh = 0;

int32_t clock_normalizeTimezoneOffset(int32_t rawOffset) {
    if (rawOffset >= -24 && rawOffset <= 24) {
        return rawOffset * 3600;
    }
    return rawOffset;
}

void clock_init() {
    timeClient.begin();
    mainConfig.display.timezone = (int16_t)clock_normalizeTimezoneOffset(mainConfig.display.timezone);
    timeClient.setTimeOffset(mainConfig.display.timezone);
    Serial.println("[Clock] NTP Init started");
}

void clock_update() {
    uint32_t nowMs = millis();
    if (nowMs - lastClockRefresh < 1000) {
        return;
    }
    lastClockRefresh = nowMs;

    // Use configured NTP server or fallback to default
    const char* ntpServer = (mainConfig.display.ntpServer[0] != '\0') 
        ? mainConfig.display.ntpServer 
        : "pool.ntp.org";
    
    mainConfig.display.timezone = (int16_t)clock_normalizeTimezoneOffset(mainConfig.display.timezone);
    timeClient.setPoolServerName(ntpServer);
    timeClient.setTimeOffset(mainConfig.display.timezone);
    timeClient.update();
    
    // Check if synchronized (epoch > Jan 1, 2020)
    time_t now = timeClient.getEpochTime();
    if (now < 1577836800) {  // Before 2020-01-01
        ntpFailCount++;
        if (nowMs - lastNtpRetry > 5000) {
            Serial.printf("[Clock] NTP sync failed (attempt %d), server: %s\n", ntpFailCount, mainConfig.display.ntpServer);
            lastNtpRetry = nowMs;
        }
    } else {
        if (ntpFailCount > 0) {
            Serial.printf("[Clock] NTP synchronized after %d attempts\n", ntpFailCount);
            ntpFailCount = 0;
        }
    }
    
    currentHour = timeClient.getHours();
    currentMinute = timeClient.getMinutes();
    currentSecond = timeClient.getSeconds();
    
    if (nowMs - lastColonToggle > 1000) {
        colonState = !colonState;
        lastColonToggle = nowMs;
    }
}
