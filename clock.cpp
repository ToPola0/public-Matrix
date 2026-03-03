#include "clock.h"
#include "display.h"
#include "web_panel.h"
#include "scheduler.h"
#include "app_logger.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);
uint8_t currentHour = 0, currentMinute = 0, currentSecond = 0;
bool colonState = true;
static uint32_t lastColonToggle = 0;
static uint32_t lastNtpRetry = 0;
static uint8_t ntpFailCount = 0;
static uint32_t lastClockRefresh = 0;
static bool ntp_init_scheduler = false;  // Track if scheduler NTP init has been called
static bool ntpFirstAttemptDone = false;  // Track if first NTP attempt made

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

    // Always read current time from cache (non-blocking)
    currentHour = timeClient.getHours();
    currentMinute = timeClient.getMinutes();
    currentSecond = timeClient.getSeconds();

    // Try NTP update: immediately on first attempt, then every 30 seconds
    bool shouldUpdateNtp = !ntpFirstAttemptDone || (nowMs - lastNtpRetry > 30000);
    
    if (shouldUpdateNtp) {
        // Always attempt NTP update (collision is minor performance impact, not critical)
        // NTP timeout is short (few ms if cached), animation can handle brief stalls
        
        lastNtpRetry = nowMs;
        ntpFirstAttemptDone = true;
        app_logf("[Clock] NTP attempting update...");
        
        // Use configured NTP server or fallback to default
        const char* ntpServer = (mainConfig.display.ntpServer[0] != '\0') 
            ? mainConfig.display.ntpServer 
            : "pool.ntp.org";
        
        mainConfig.display.timezone = (int16_t)clock_normalizeTimezoneOffset(mainConfig.display.timezone);
        timeClient.setPoolServerName(ntpServer);
        timeClient.setTimeOffset(mainConfig.display.timezone);
        
        app_logf("[Clock] NTP trying %s (TZ offset: %d)", ntpServer, mainConfig.display.timezone);
        
        timeClient.update();
        
        // Check if synchronized (epoch > Jan 1, 2020)
        time_t now = timeClient.getEpochTime();
        if (now < 1577836800) {  // Before 2020-01-01
            ntpFailCount++;
            app_logf("[Clock] NTP sync failed (attempt %d), server: %s", ntpFailCount, mainConfig.display.ntpServer);
            
            // Fallback on consecutive failures
            if (ntpFailCount >= 3 && strcmp(ntpServer, "pool.ntp.org") != 0) {
                app_logf("[Clock] NTP fallback to pool.ntp.org");
                timeClient.setPoolServerName("pool.ntp.org");
                timeClient.update();
                now = timeClient.getEpochTime();
            }
        } else {
            if (ntpFailCount > 0) {
                app_logf("[Clock] NTP synchronized after %d attempts", ntpFailCount);
                ntpFailCount = 0;
            }
            // Initialize scheduler NTP tracking on first successful sync
            if (!ntp_init_scheduler) {
                ntp_init_scheduler = true;
                scheduler_initNtpTracking();
            }
        }
    }
    
    if (nowMs - lastColonToggle > 1000) {
        colonState = !colonState;
        lastColonToggle = nowMs;
    }
}
