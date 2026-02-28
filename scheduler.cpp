#include "scheduler.h"
#include "clock.h"
#include "display.h"
#include "effects.h"
#include "quotes.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// Zmienne globalne
unsigned long last_animation_change = 0;
unsigned long last_quote_check = 0;
uint8_t current_animation_index = 0;
bool scheduled_message_active = false;
char current_scheduled_message[128] = "";
unsigned long last_scheduler_minute_check = 0;
uint8_t last_hourly_quote_hour = 255;  // Tracking dla cytatów o pełnych godzinach

// Tracking dla harmonogramu z JSON
Preferences schedulePreferences;
uint8_t last_checked_hour = 255;
uint8_t last_checked_minute = 255;
char last_displayed_once_messages[10][32]; // Tracking wiadomości "once" wyświetlonych dzisiaj

// New: tracking dla zaplanowanych animacji i cytatów
uint8_t scheduled_animation_type = 0;        // Which animation type to play
bool scheduled_animation_active = false;     // Is a scheduled animation running now
bool scheduled_quote_active = false;         // Is a scheduled quote showing now
static const uint8_t MIN_EFFECTS_BETWEEN_QUOTES = 3;
static uint32_t last_quote_effect_count = 0;

extern MainConfig mainConfig;
extern NTPClient timeClient;
extern CRGB leds[];

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
    prefs.begin("wifi", true);
    String windowsJSON = prefs.getString("brightnessSchedule", "[]");
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
    
    // Reset "once" message tracking dla nowego dnia
    last_checked_hour = 255;
    memset(last_displayed_once_messages, 0, sizeof(last_displayed_once_messages));
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

bool shouldShowRandomQuote() {
    if (!mainConfig.schedule.random_quotes_enabled) {
        return false;
    }

    if (display_mode == DISPLAY_MODE_QUOTE) {
        return false;
    }

    uint32_t currentEffectCount = display_getFunClockCompletedEffectsCount();
    if ((currentEffectCount - last_quote_effect_count) < MIN_EFFECTS_BETWEEN_QUOTES) {
        return false;
    }
    
    unsigned long now = millis();
    unsigned long interval_minutes = (mainConfig.schedule.random_quotes_interval > 0)
        ? (unsigned long)mainConfig.schedule.random_quotes_interval
        : 1UL;
    if (interval_minutes < 2UL) {
        interval_minutes = 2UL;
    }
    unsigned long interval_ms = interval_minutes * 60UL * 1000UL;
    
    if (now - last_quote_check < interval_ms) {
        return false;
    }
    
    // Sprawdzaj czy następna godzina
    uint8_t current_hour = timeClient.getHours();
    uint8_t start = mainConfig.schedule.random_quotes_start_hour;
    uint8_t end = mainConfig.schedule.random_quotes_end_hour;
    
    if (start < end) {
        // np. 9-17
        if (current_hour < start || current_hour >= end) {
            return false;
        }
    } else if (start > end) {
        // np. 22-6 (przez północ)
        if (current_hour < start && current_hour >= end) {
            return false;
        }
    }
    
    return true;
}

// Sprawdź czy wyświetlić losowy cytat o pełnej godzinie
bool shouldDisplayHourlyQuote() {
    // Sprawdzenie czy cytaty są włączone
    if (!mainConfig.schedule.random_quotes_enabled) {
        return false;
    }

    if (display_mode == DISPLAY_MODE_QUOTE) {
        return false;
    }
    
    uint8_t current_hour = timeClient.getHours();
    uint8_t current_minute = timeClient.getMinutes();
    
    // Wyświetl cytat tylko gdy minute == 0 i to jest inna godzina niż poprzednio
    if (current_minute == 0 && current_hour != last_hourly_quote_hour) {
        last_hourly_quote_hour = current_hour;
        return true;
    }
    
    // Reset tracking gdy minęła godzina (minute > 0) ale wrócimy do :00
    if (current_minute > 0) {
        // Możemy wyresetować, ale nie będziemy wyświetlać jeszcze
    }
    
    return false;
}

// === MAIN SCHEDULER LOOP ===
void scheduler_loop() {
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
    
    // 5. Losowe cytaty
    if (!quoteTriggeredThisLoop && shouldShowRandomQuote()) {
        char* quote = quotes_getRandom();
        if (quote != NULL && strlen(quote) > 0) {
            last_quote_check = millis();
            last_quote_effect_count = display_getFunClockCompletedEffectsCount();
            display_mode = DISPLAY_MODE_QUOTE;
            effects_quotes(quote);  // Aktualizuj currentQuote w effects.cpp
            quoteTriggeredThisLoop = true;
            Serial.printf("[SCHEDULER] Random quote triggered: %s\n", quote);
        }
    }
    
    // 6. Trigger godzinowy cytatów wyłączony (dublował losowe cytaty)
}
