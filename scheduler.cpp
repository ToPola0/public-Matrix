#include "scheduler.h"
#include "clock.h"
#include "display.h"
#include "effects.h"
#include "quotes.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

// Zmienne globalne
unsigned long last_animation_change = 0;
unsigned long last_quote_check = 0;
uint8_t current_animation_index = 0;
bool scheduled_message_active = false;
char current_scheduled_message[128] = "";
unsigned long last_scheduler_minute_check = 0;
uint8_t last_hourly_quote_hour = 255;  // Tracking dla cytatów o pełnych godzinach
bool ntp_sync_complete = false;  // NTP synchronization complete, safe to use time

// Tracking dla harmonogramu z JSON
Preferences schedulePreferences;
uint8_t last_checked_hour = 255;
uint8_t last_checked_minute = 255;
char last_displayed_once_messages[10][32]; // Tracking wiadomości "once" wyświetlonych dzisiaj

// New: tracking dla zaplanowanych animacji i cytatów
uint8_t scheduled_animation_type = 0;        // Which animation type to play
bool scheduled_animation_active = false;     // Is a scheduled animation running now
bool scheduled_quote_active = false;         // Is a scheduled quote showing now
static uint32_t quote_suppressed_until_ms = 0;
static uint32_t last_quote_effect_count = 0;
static uint32_t next_birthday_greeting_ms = 0;

extern MainConfig mainConfig;
extern NTPClient timeClient;
extern CRGB leds[];

static uint32_t randomBirthdayGreetingIntervalMs(bool hasBirthdayToday) {
    if (hasBirthdayToday) {
        return (uint32_t)random(240000UL, 840001UL); // 4-14 min
    }
    return (uint32_t)random(1800000UL, 5400001UL);   // 30-90 min
}

static bool parseIsoDateMonthDay(const String& isoDate, uint8_t& outMonth, uint8_t& outDay) {
    if (isoDate.length() != 10) return false;
    if (isoDate.charAt(4) != '-' || isoDate.charAt(7) != '-') return false;

    int month = isoDate.substring(5, 7).toInt();
    int day = isoDate.substring(8, 10).toInt();
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;

    outMonth = (uint8_t)month;
    outDay = (uint8_t)day;
    return true;
}

static bool getCurrentMonthDay(uint8_t& outMonth, uint8_t& outDay) {
    if (!ntp_sync_complete) return false;

    time_t localEpoch = (time_t)timeClient.getEpochTime();
    struct tm timeInfo;
    if (!gmtime_r(&localEpoch, &timeInfo)) return false;

    outMonth = (uint8_t)(timeInfo.tm_mon + 1);
    outDay = (uint8_t)timeInfo.tm_mday;
    return true;
}

static uint8_t collectTodayBirthdayNames(char outNames[][41], uint8_t maxCount) {
    if (maxCount == 0) return 0;

    uint8_t month = 0;
    uint8_t day = 0;
    if (!getCurrentMonthDay(month, day)) return 0;

    Preferences prefs;
    prefs.begin("wifi", true);
    String birthdaysJson = prefs.getString("birthdays", "[]");
    prefs.end();

    if (birthdaysJson.length() < 2) return 0;

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, birthdaysJson);
    if (err || !doc.is<JsonArray>()) return 0;

    JsonArray arr = doc.as<JsonArray>();
    uint8_t count = 0;
    for (JsonVariant v : arr) {
        if (!v.is<JsonObject>()) continue;
        JsonObject obj = v.as<JsonObject>();
        const char* rawName = obj["name"] | "";
        const char* rawDate = obj["date"] | "";
        if (!rawName || !rawDate || rawName[0] == '\0') continue;

        String dateValue = String(rawDate);
        uint8_t birthdayMonth = 0;
        uint8_t birthdayDay = 0;
        if (!parseIsoDateMonthDay(dateValue, birthdayMonth, birthdayDay)) continue;
        if (birthdayMonth != month || birthdayDay != day) continue;

        strlcpy(outNames[count], rawName, 41);
        count++;
        if (count >= maxCount) break;
    }

    return count;
}

static void buildBirthdayGreeting(const char* name, char* outText, size_t outSize) {
    static const char* templates[] = {
        "Sto Lat %s!",
        "Wszystkiego Najlepszego %s!",
        "Szczęścia i Zdrowia %s!",
        "Dużo Radości %s!",
        "Spełnienia Marzeń %s!",
        "Najlepsze Życzenia %s!"
    };

    if (!outText || outSize == 0) return;
    const size_t templateCount = sizeof(templates) / sizeof(templates[0]);
    uint8_t idx = (uint8_t)random(0, (long)templateCount);
    snprintf(outText, outSize, templates[idx], (name && name[0] != '\0') ? name : "Solenizant");
    outText[outSize - 1] = '\0';
}

static bool tryTriggerRandomBirthdayGreeting() {
    char names[24][41];
    uint8_t count = collectTodayBirthdayNames(names, 24);
    if (count == 0) return false;

    if (message_active) return false;
    if (display_mode == DISPLAY_MODE_QUOTE || display_mode == DISPLAY_MODE_LAMP) return false;

    uint8_t selectedIndex = (uint8_t)random(0, (long)count);
    char greeting[128];
    buildBirthdayGreeting(names[selectedIndex], greeting, sizeof(greeting));

    strncpy(message_text, greeting, sizeof(message_text) - 1);
    message_text[sizeof(message_text) - 1] = '\0';

    message_active = true;
    message_offset = LED_WIDTH;
    message_time_left = (uint32_t)random(9000UL, 15001UL);
    message_start_time = millis();

    uint8_t hue = (uint8_t)random(0, 256);
    message_color = CHSV(hue, 230, 255);

    display_mode = DISPLAY_MODE_MESSAGE;
    scheduler_snoozeQuotes(45000U);

    Serial.printf("[SCHEDULER] Birthday greeting: %s\n", message_text);
    return true;
}

static bool parseTimeHHMM(const char* timeStr, uint16_t& outMinutes) {
    if (!timeStr) return false;
    if (strlen(timeStr) < 5) return false;
    if (timeStr[2] != ':') return false;
    int h = (timeStr[0] - '0') * 10 + (timeStr[1] - '0');
    int m = (timeStr[3] - '0') * 10 + (timeStr[4] - '0');
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    outMinutes = (uint16_t)(h * 60 + m);
    return true;
}

static bool isMinuteInWindow(uint16_t nowMinute, uint16_t startMinute, uint16_t endMinute) {
    if (startMinute == endMinute) return true;
    if (startMinute < endMinute) {
        return nowMinute >= startMinute && nowMinute < endMinute;
    }
    return nowMinute >= startMinute || nowMinute < endMinute;
}

static bool parseHexColorForSchedule(const char* colorText, CRGB& outColor) {
    if (!colorText) return false;
    const char* hex = colorText;
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return false;

    char* endPtr = nullptr;
    unsigned long rgb = strtoul(hex, &endPtr, 16);
    if (!endPtr || *endPtr != '\0') return false;

    outColor = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

static void applyBrightnessWindowsIfNeeded() {
    static int16_t lastAppliedMinuteOfDay = -1;

    uint16_t nowMinuteOfDay = (uint16_t)(timeClient.getHours() * 60 + timeClient.getMinutes());
    if ((int16_t)nowMinuteOfDay == lastAppliedMinuteOfDay) {
        return;
    }
    lastAppliedMinuteOfDay = (int16_t)nowMinuteOfDay;

    Preferences prefs;
    prefs.begin("schedule", true);  // Read from new "schedule" namespace
    String windowsJSON = prefs.getString("windows", "[]");
    prefs.end();
    
    // Get default brightness/color from wifi settings
    prefs.begin("wifi", true);
    int defaultBrightness = constrain(prefs.getString("animBrightness", "200").toInt(), 1, 255);
    String defaultColorStr = prefs.getString("animColor", "#00ff00");
    prefs.end();
    
    int targetBrightness = defaultBrightness;
    CRGB defaultColor = globalColor;
    if (!parseHexColorForSchedule(defaultColorStr.c_str(), defaultColor)) {
        defaultColor = globalColor;
    }
    CRGB targetColor = defaultColor;

    if (windowsJSON.length() > 2) {
        StaticJsonDocument<4096> doc;
        if (deserializeJson(doc, windowsJSON) == DeserializationError::Ok && doc.is<JsonArray>()) {
            JsonArray windows = doc.as<JsonArray>();
            for (JsonObject w : windows) {
                if ((w["enabled"].as<int>() == 0)) continue;

                const char* start = w["start"] | "00:00";
                const char* end = w["end"] | "00:00";
                int b = constrain((int)(w["brightness"] | defaultBrightness), 1, 255);
                const char* color = w["color"] | defaultColorStr.c_str();

                uint16_t startMinute = 0;
                uint16_t endMinute = 0;
                if (!parseTimeHHMM(start, startMinute) || !parseTimeHHMM(end, endMinute)) {
                    continue;
                }

                if (isMinuteInWindow(nowMinuteOfDay, startMinute, endMinute)) {
                    targetBrightness = b;
                    CRGB parsedColor;
                    if (parseHexColorForSchedule(color, parsedColor)) {
                        targetColor = parsedColor;
                    }
                    break;
                }
            }
        }
    }

    if (globalBrightness != (uint8_t)targetBrightness) {
        display_setBrightness((uint8_t)targetBrightness);
        Serial.printf("[SCHEDULER] Brightness window apply: %d at %02u:%02u\n",
                      targetBrightness,
                      timeClient.getHours(),
                      timeClient.getMinutes());
    }

    if (globalColor.r != targetColor.r || globalColor.g != targetColor.g || globalColor.b != targetColor.b) {
        animation_color = targetColor;
        clock_color = targetColor;
        quote_color = targetColor;
        message_color = targetColor;
        globalColor = targetColor;
        display_setColor(targetColor);
        Serial.printf("[SCHEDULER] Color window apply: #%02X%02X%02X at %02u:%02u\n",
                      targetColor.r, targetColor.g, targetColor.b,
                      timeClient.getHours(),
                      timeClient.getMinutes());
    }
}

// === INIT ===
void scheduler_init() {
    last_animation_change = millis();
    last_quote_check = millis();
    last_scheduler_minute_check = millis();
    current_animation_index = 0;
    scheduled_message_active = false;
    
    // Initialize Preferences for schedule tracking
    schedulePreferences.begin("scheduleTracker", false);
    last_quote_effect_count = display_getFunClockCompletedEffectsCount();
    next_birthday_greeting_ms = millis() + randomBirthdayGreetingIntervalMs(true);
    
    // Reset "once" message tracking dla nowego dnia
    last_checked_hour = 255;
    memset(last_displayed_once_messages, 0, sizeof(last_displayed_once_messages));
}

void scheduler_snoozeQuotes(uint32_t durationMs) {
    uint32_t now = millis();
    uint32_t safeDuration = durationMs;
    if (safeDuration < 2000U) safeDuration = 2000U;

    quote_suppressed_until_ms = now + safeDuration;
    last_quote_check = now;
    last_quote_effect_count = display_getFunClockCompletedEffectsCount();
    Serial.printf("[SCHEDULER] Quotes snoozed for %lu ms\n", (unsigned long)safeDuration);
}

// === HELPERS ===

// Sprawdź czy wiadomość powinna być wyświetlona dzisiaj (based on repeat type)
bool shouldShowMessageToday(const char* repeatType, const char* daysStr, uint8_t hour, uint8_t minute) {
    if (!repeatType) return true;
    
    if (strcmp(repeatType, "once") == 0) {
        // "once" - sprawdź czy już wyświetlona dzisiaj
        char msgKey[64];
        snprintf(msgKey, sizeof(msgKey), "once_%02d%02d", hour, minute);
        return !schedulePreferences.getBool(msgKey, false);
    } 
    else if (strcmp(repeatType, "daily") == 0) {
        return true;
    }
    else if (strcmp(repeatType, "weekly") == 0) {
        // Sprawdź czy dzisiaj jest w daysStr (format: "0,1,2,3,4,5,6" gdzie 0=Sunday, 1=Monday, etc.)
        if (!daysStr || strlen(daysStr) == 0) {
            return false;  // Jeśli nie ma dni - nie wyświetlaj
        }
        
        // Calculate weekday from epoch time (0=Sunday)
        // Jan 1, 1970 was a Thursday (day 4), so add 4 and mod 7
        uint32_t days_since_epoch = timeClient.getEpochTime() / 86400;
        uint8_t weekday = (days_since_epoch + 4) % 7;
        
        // Konwertuj bieżący dzień na string
        char weekdayStr[4];
        snprintf(weekdayStr, sizeof(weekdayStr), "%d", weekday);
        
        // Szukaj w daysStr
        return strstr(daysStr, weekdayStr) != nullptr;
    }
    
    return false;
}

// Mark "once" message as displayed today
void markOnceMessageDisplayed(uint8_t hour, uint8_t minute) {
    char msgKey[64];
    snprintf(msgKey, sizeof(msgKey), "once_%02d%02d", hour, minute);
    schedulePreferences.putBool(msgKey, true);
}

// Reset daily tracking at midnight
void resetDailyTracking() {
    // Clear all "once_*" entries at midnight
    schedulePreferences.clear();
    Serial.println("[SCHEDULER] Daily tracking reset");
}

bool shouldRotateAnimation() {
    if (!mainConfig.schedule.animation.enabled) {
        return false;
    }
    
    unsigned long now = millis();
    unsigned long interval_ms = (unsigned long)mainConfig.schedule.animation.rotation_interval * 60 * 1000;
    
    if (now - last_animation_change >= interval_ms) {
        last_animation_change = now;
        return true;
    }
    return false;
}

bool shouldShowScheduledMessage(uint8_t& messageIndex) {
    unsigned long now = millis();
    
    // Sprawdzaj co minutę aby nie obciążać pętli
    if (now - last_scheduler_minute_check < 60000) {
        return false;
    }
    last_scheduler_minute_check = now;
    
    uint8_t current_hour = timeClient.getHours();
    uint8_t current_minute = timeClient.getMinutes();
    
    // Reset tracking messages at midnight (when hour changes from 23 to 0)
    if (last_checked_hour == 23 && current_hour == 0) {
        resetDailyTracking();
    }
    last_checked_hour = current_hour;
    
    // Try to load schedule from Preferences
    Preferences prefs;
    prefs.begin("wifi", true);  // Read-only mode
    String scheduleJSON = prefs.getString("schedule", "[]");
    prefs.end();
    
    if (scheduleJSON.length() == 0 || scheduleJSON == "[]") {
        return false;  // No schedule stored
    }
    
    // Parse JSON schedule
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, scheduleJSON);
    
    if (error) {
        Serial.print("[SCHEDULER] JSON parse error: ");
        Serial.println(error.c_str());
        return false;
    }
    
    if (!doc.is<JsonArray>()) {
        Serial.println("[SCHEDULER] Schedule is not an array");
        return false;
    }
    
    JsonArray scheduleArray = doc.as<JsonArray>();
    uint8_t itemCount = 0;
    
    // Iterate through schedule items
    for (JsonObject item : scheduleArray) {
        if (itemCount >= 10) break;  // Max 10 messages
        
        // Check if enabled
        if (!item["enabled"] || item["enabled"].as<int>() == 0) {
            continue;
        }
        
        uint8_t msgHour = item["hour"].as<uint8_t>();
        uint8_t msgMinute = item["minute"].as<uint8_t>();
        
        // Check if this message should trigger now
        if (msgHour == current_hour && msgMinute == current_minute) {
            // Check repeat type and days
            const char* repeatType = item["repeat"] | "once";
            const char* daysStr = item["days"] | "";
            
            if (shouldShowMessageToday(repeatType, daysStr, msgHour, msgMinute)) {
                // Found matching event!
                messageIndex = itemCount;
                
                // Mark as displayed if "once"
                if (strcmp(repeatType, "once") == 0) {
                    markOnceMessageDisplayed(msgHour, msgMinute);
                }
                
                // Get event type (text, anim, or quote)
                const char* eventType = item["type"] | "text";
                uint32_t duration = item["duration"].as<uint32_t>();
                
                // Handle different event types
                if (strcmp(eventType, "anim") == 0) {
                    // Animation event
                    uint8_t animType = item["anim"].as<uint8_t>();
                    scheduled_animation_type = animType;
                    scheduled_animation_active = true;
                    Serial.printf("[SCHEDULER] Animation scheduled at %02d:%02d: type=%d, duration=%us\n", 
                                  msgHour, msgMinute, animType, duration);
                } 
                else if (strcmp(eventType, "quote") == 0) {
                    // Quote event
                    scheduled_quote_active = true;
                    Serial.printf("[SCHEDULER] Quote scheduled at %02d:%02d, duration=%us\n", 
                                  msgHour, msgMinute, duration);
                } 
                else {
                    // Text event (default)
                    const char* msgText = item["text"] | "";
                    strncpy(current_scheduled_message, msgText, sizeof(current_scheduled_message) - 1);
                    current_scheduled_message[sizeof(current_scheduled_message) - 1] = '\0';
                    Serial.printf("[SCHEDULER] Text message at %02d:%02d: %s, duration=%us\n", 
                                  msgHour, msgMinute, msgText, duration);
                }
                
                return true;
            }
        }
        
        itemCount++;
    }
    
    return false;
}

// Sprawdź czy wyświetlić losowy cytat o pełnej godzinie
bool shouldDisplayHourlyQuote() {
    // Sprawdzenie czy cytaty są włączone
    if (!mainConfig.schedule.random_quotes_enabled) {
        return false;
    }

    // Nie wyświetlaj cytatów dopóki NTP się nie zsynchronizuje
    if (!ntp_sync_complete) {
        return false;
    }

    if (quote_suppressed_until_ms != 0U && millis() < quote_suppressed_until_ms) {
        return false;
    }

    if (display_mode == DISPLAY_MODE_QUOTE) {
        return false;
    }
    
    uint8_t current_hour = timeClient.getHours();
    uint8_t current_minute = timeClient.getMinutes();
    uint8_t second = currentSecond;
    
    // Wyświetl cytat tylko o HH:00:03 i tylko raz na godzinę
    if (current_minute == 0 && second == 3 && current_hour != last_hourly_quote_hour) {
        last_hourly_quote_hour = current_hour;
        return true;
    }
    
    // Reset tracking gdy minęła godzina (minute > 0) ale wrócimy do :00
    if (current_minute > 0) {
        // Możemy wyresetować, ale nie będziemy wyświetlać jeszcze
    }
    
    return false;
}

// === HELPER: Initialize NTP sync tracking ===
void scheduler_initNtpTracking() {
    static bool initialized = false;
    if (!initialized && timeClient.getEpochTime() > 1577836800) {  // After 2020-01-01
        ntp_sync_complete = true;
        last_hourly_quote_hour = timeClient.getHours();  // Prevent immediate quote
        initialized = true;
    }
}

// === MAIN SCHEDULER LOOP ===
void scheduler_loop() {
    if (display_mode == DISPLAY_MODE_LAMP) {
        return;
    }

    scheduler_initNtpTracking();
    applyBrightnessWindowsIfNeeded();

    static bool quoteWasActive = false;
    bool quoteIsActive = (display_mode == DISPLAY_MODE_QUOTE);
    if (quoteWasActive && !quoteIsActive) {
        last_quote_check = millis();
    }
    quoteWasActive = quoteIsActive;

    bool quoteTriggeredThisLoop = false;

    // 1. Rotacja animacji
    if (display_mode == DISPLAY_MODE_ANIMATION && shouldRotateAnimation()) {
        // Szukamy następnej aktywnej animacji w liście
        uint8_t next_index = (current_animation_index + 1) % mainConfig.schedule.animation.num_animations;
        
        // Upewnij się że ta animacja jest aktywna
        for (uint8_t i = 0; i < mainConfig.schedule.animation.num_animations; i++) {
            if (mainConfig.schedule.animation.animations[next_index].enabled) {
                current_animation_index = next_index;
                animation_mode = mainConfig.schedule.animation.animations[next_index].type;
                Serial.printf("[SCHEDULER] Animation rotated to type %d\n", animation_mode);
                break;
            }
            next_index = (next_index + 1) % mainConfig.schedule.animation.num_animations;
        }
    }
    
    // 2. Zaplanowane animacje
    if (scheduled_animation_active) {
        scheduled_animation_active = false;  // Reset flag after processing
        animation_mode = scheduled_animation_type;
        display_mode = DISPLAY_MODE_ANIMATION;
        Serial.printf("[SCHEDULER] Scheduled animation triggered: type=%d\n", scheduled_animation_type);
    }
    
    // 3. Zaplanowane cytaty
    if (scheduled_quote_active) {
        if (quote_suppressed_until_ms != 0U && millis() < quote_suppressed_until_ms) {
            scheduled_quote_active = false;
        }
    }

    if (scheduled_quote_active) {
        scheduled_quote_active = false;  // Reset flag after processing
        char* quote = quotes_getRandom();
        if (quote != NULL && strlen(quote) > 0) {
            last_quote_check = millis();
            last_quote_effect_count = display_getFunClockCompletedEffectsCount();
            display_mode = DISPLAY_MODE_QUOTE;
            effects_quotes(quote);
            quoteTriggeredThisLoop = true;
            Serial.printf("[SCHEDULER] Scheduled quote shown: %s\n", quote);
        }
    }
    
    // 4. Zaplanowane wiadomości z JSON (tekst)
    uint8_t msg_idx = 0;
    if (shouldShowScheduledMessage(msg_idx)) {
        // Only process text messages here - animations and quotes are handled above
        // Check this specific message type
        Preferences prefs;
        prefs.begin("wifi", true);
        String scheduleJSON = prefs.getString("schedule", "[]");
        prefs.end();
        
        uint32_t duration_ms = 10000;  // Default 10 seconds
        bool isTextMessage = false;
        
        if (scheduleJSON.length() > 0 && scheduleJSON != "[]") {
            StaticJsonDocument<4096> doc;
            if (deserializeJson(doc, scheduleJSON) == DeserializationError::Ok) {
                JsonArray scheduleArray = doc.as<JsonArray>();
                uint8_t idx = 0;
                for (JsonObject item : scheduleArray) {
                    if (idx == msg_idx && item["enabled"] && item["enabled"].as<int>() != 0) {
                        const char* eventType = item["type"] | "text";
                        if (strcmp(eventType, "text") == 0) {
                            isTextMessage = true;
                        }
                        duration_ms = item["duration"].as<uint32_t>() * 1000;  // Convert to milliseconds
                        break;
                    }
                    idx++;
                }
            }
        }
        
        // Only display if it's a text message
        if (isTextMessage && strlen(current_scheduled_message) > 0) {
            // Ustaw wiadomość do wyświetlenia
            strncpy(message_text, current_scheduled_message, sizeof(message_text)-1);
            message_active = true;
            message_offset = 64;
            message_time_left = duration_ms;
            message_start_time = millis();
            
            // Priorytet - zmień na tryb wiadomości
            display_mode = DISPLAY_MODE_MESSAGE;
            
            Serial.printf("[SCHEDULER] Text message displayed: %s (duration: %lums)\n", 
                          current_scheduled_message, duration_ms);
        }
    }
    
    // 5. Cytat godzinowy o HH:00:03
    unsigned long now = millis();
    if (now >= next_birthday_greeting_ms) {
        bool birthdayShown = tryTriggerRandomBirthdayGreeting();
        char tmpNames[1][41];
        bool hasBirthdayToday = (collectTodayBirthdayNames(tmpNames, 1) > 0);
        next_birthday_greeting_ms = now + randomBirthdayGreetingIntervalMs(hasBirthdayToday);
        if (birthdayShown) {
            quoteTriggeredThisLoop = true;
        }
    }

    // 6. Cytat godzinowy o HH:00:03
    if (!quoteTriggeredThisLoop && shouldDisplayHourlyQuote()) {
        const char* quote = quotes_getRandom();
        if (quote != NULL && strlen(quote) > 0) {
            last_quote_check = millis();
            last_quote_effect_count = display_getFunClockCompletedEffectsCount();
            display_mode = DISPLAY_MODE_QUOTE;
            effects_quotes(quote);
            quoteTriggeredThisLoop = true;
            Serial.printf("[SCHEDULER] Hourly quote triggered at %02u:00:03: %s\n", currentHour, quote);
        }
    }
    
    // 7. Koniec pętli scheduler
}
