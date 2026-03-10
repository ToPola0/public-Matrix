#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "config.h"
#include "display.h"
#include "effects.h"
#include "clock.h"
#include "wifi_manager.h"
#include "web_panel.h"
#include "quotes.h"
#include "scheduler.h"
#include "mqtt_manager.h"
#include "app_logger.h"
#include "ota_manager.h"
#include "github_ota_manager.h"

// === FACTORY RESET FLAG ===
// Set to true to perform factory reset on next boot (clears all Preferences and LittleFS)
// IMPORTANT: Flash this code with flag=true, wait for boot complete, then change to false and re-flash
#define FACTORY_RESET_ON_BOOT false

unsigned long lastStatus = 0;
unsigned long lastBuzzer = 0;
bool buzzerState = false;
unsigned long bootMillis = 0;

// === Buzzer & Quote safety ===
uint8_t last_buzzer_second = 255;
bool buzzer_active = false;
uint32_t buzzer_start_time = 0;
uint32_t buzzer_duration = 0;

MainConfig mainConfig;
WebServer webServer(80);

WifiManager wifiManager;

static void drawOtaProgress(uint32_t progress, uint32_t total, bool errorState = false, bool forceRedraw = false) {
    static int16_t lastPercentShown = -1;
    static bool lastErrorState = false;

    int16_t percent = -1;
    if (!errorState) {
        if (total > 0U) {
            uint32_t p = ((uint32_t)progress * 100UL) / (uint32_t)total;
            if (p > 100UL) p = 100UL;
            percent = (int16_t)p;
        } else {
            percent = 0;
        }
    }

    if (!forceRedraw && errorState == lastErrorState && percent == lastPercentShown) {
        return;
    }
    lastErrorState = errorState;
    lastPercentShown = percent;

    display_clear();

    CRGB textColor = errorState ? CRGB::Red : CRGB::Green;
    String text;
    if (errorState) {
        text = "ERR";
    } else {
        text = String(percent) + "%";
    }

    int16_t textWidth = (int16_t)text.length() * 6 - 1;
    if (textWidth < 0) textWidth = 0;
    int16_t textX = (LED_WIDTH > textWidth) ? ((LED_WIDTH - textWidth) / 2) : 0;
    display_drawText(text.c_str(), textX, textColor);
    updateLEDs();
}

// OTA Progress callback for manager
static void otaProgressCallback(uint32_t progress, uint32_t total) {
    drawOtaProgress(progress, total, false, false);
}

// OTA Status callback for manager
static void otaStatusCallback(OTA_Status status) {
    switch(status) {
        case OTA_STATUS_DOWNLOADING:
            app_log("[OTA] Starting download...");
            wifiManager.setExternalOtaActive(true);
            message_active = false;
            display_suppressFunClockEffects(300000);
            display_setNegative(false);
            WiFi.setSleep(false);
            FastLED.setDither(0);
            drawOtaProgress(0, 100, false, true);
            break;
        case OTA_STATUS_WRITING:
            drawOtaProgress(0, 100, false, false);
            break;
        case OTA_STATUS_SUCCESS:
            drawOtaProgress(100, 100, false, true);
            app_log("[OTA] Update successful, device restarting");
            break;
        case OTA_STATUS_ERROR_WRITE:
        case OTA_STATUS_ERROR_VALIDATION:
        case OTA_STATUS_ERROR_BOOT:
            drawOtaProgress(0, 1, true, true);
            wifiManager.setExternalOtaActive(false);
            FastLED.setDither(1);
            app_logf("[OTA] Update failed with error code: %u", otaManager.getLastError());
            break;
        case OTA_STATUS_ROLLED_BACK:
            app_log("[OTA] Automatic rollback to previous firmware");
            break;
        default:
            break;
    }
}

#if LED_TESTER_MODE
static void run_led_tester() {
    static uint8_t stage = 0;
    static uint16_t pixelIndex = 0;
    static uint32_t lastStageMs = 0;
    static uint32_t lastPixelMs = 0;
    uint32_t now = millis();

    if (stage < 5 && (now - lastStageMs) >= 1200) {
        stage++;
        if (stage > 5) stage = 0;
        lastStageMs = now;
    }

    switch (stage) {
        case 0:
            fill_solid(leds, NUM_LEDS, CRGB::Red);
            display_show();
            break;
        case 1:
            fill_solid(leds, NUM_LEDS, CRGB::Green);
            display_show();
            break;
        case 2:
            fill_solid(leds, NUM_LEDS, CRGB::Blue);
            display_show();
            break;
        case 3:
            fill_solid(leds, NUM_LEDS, CRGB::White);
            display_show();
            break;
        case 4:
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            display_show();
            break;
        default:
            if ((now - lastPixelMs) >= 40) {
                fill_solid(leds, NUM_LEDS, CRGB::Black);
                leds[pixelIndex] = CRGB::Yellow;
                display_show();
                pixelIndex = (pixelIndex + 1) % NUM_LEDS;
                lastPixelMs = now;
            }
            break;
    }

    if (stage == 5 && (now - lastStageMs) >= 4000) {
        stage = 0;
        lastStageMs = now;
    }
}
#endif

void setup() {
    Serial.begin(115200);
    otaManager.handleBootGuardOnStartup();
    
    // === FACTORY RESET PROCEDURE ===
    #if FACTORY_RESET_ON_BOOT
    {
        delay(2000);  // Give Serial time to initialize
        Serial.println("\n\n=== FACTORY RESET IN PROGRESS ===");
        Serial.println("[FACTORY] Clearing all Preferences namespaces...");
        
        // Clear all Preferences namespaces
        Preferences prefs;
        const char* namespaces[] = {"wifi", "schedule", "mqtt", "ha-entities"};
        for (size_t i = 0; i < sizeof(namespaces)/sizeof(namespaces[0]); i++) {
            prefs.begin(namespaces[i], false);
            Serial.printf("[FACTORY] Clearing namespace: %s\n", namespaces[i]);
            prefs.clear();
            prefs.end();
        }
        
        // Format LittleFS
        Serial.println("[FACTORY] Formatting LittleFS...");
        LittleFS.format();
        
        Serial.println("=== FACTORY RESET COMPLETE ===");
        Serial.println("[FACTORY] ALL data erased. Please change FACTORY_RESET_ON_BOOT to false");
        Serial.println("[FACTORY] and re-compile/re-flash.\n\n");
        
        // Endless loop - user must change flag and re-flash
        while (true) {
            digitalWrite(BUILTIN_LED, HIGH);
            delay(500);
            digitalWrite(BUILTIN_LED, LOW);
            delay(500);
        }
    }
    #endif
    
    display_init();
    display_enabled = true;
    display_mode = DISPLAY_MODE_CLOCK;

#if LED_TESTER_MODE
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("[TESTER] LED_TESTER_MODE active");
    Serial.printf("[TESTER] LED_PIN=%d, NUM_LEDS=%d\n", LED_PIN, NUM_LEDS);
    return;
#endif

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    wifiManager.begin(&webServer);
    
    // Initialize A/B OTA System
    otaManager.setProgressCallback(otaProgressCallback);
    otaManager.setStatusCallback(otaStatusCallback);
    githubOtaManager.setProgressCallback(otaProgressCallback);
    githubOtaManager.setStatusCallback(otaStatusCallback);
    githubOtaManager.begin(
        true,
        FIRMWARE_VERSION,
        GITHUB_OTA_VERSION_URL,
        GITHUB_OTA_FIRMWARE_URL,
        GITHUB_OTA_CHECK_INTERVAL_MS,
        GITHUB_OTA_BOOT_DELAY_MS);
    app_log("[SETUP] OTA System initialized - A/B partitions configured");
    
    effects_init();
    clock_init();
    quotes_init();
    scheduler_init();
    webPanel_setup();
    mqtt_manager_begin();
}

static uint32_t loopStartMs = 0;
static uint32_t lastDiagnosticMs = 0;
static uint32_t loopCount = 0;

void loop() {
    loopStartMs = millis();
    loopCount++;

    otaManager.processBootGuard();
    
#if LED_TESTER_MODE
    run_led_tester();
    delay(1);
    return;
#endif

#if LED_CORNER_CALIBRATION_MODE
    display_drawCornerCalibration();
    delay(50);
    return;
#endif

    // Handle WiFi-based OTA (A/B partitions, 100% safe)
    if (wifiManager.isConnected()) {
        otaManager.begin(OTA_HOSTNAME, OTA_PORT, OTA_PASSWORD);
        otaManager.handle();
        githubOtaManager.loop(
            true,
            otaManager.isInProgress() || wifiManager.isOtaUpdating());
    }
    
    // If OTA is in progress, block everything else
    if (otaManager.isInProgress() || githubOtaManager.isInProgress()) {
        delay(1);
        return;
    }

    wifiManager.loop();
    mqtt_manager_loop();
    if (wifiManager.isOtaUpdating()) {
        delay(1);
        return;
    }
    if (display_mode != DISPLAY_MODE_LAMP) {
        clock_update();
    }
    if (!wifiManager.isAPMode()) {
        webPanel_loop();
    }
    
    if (wifiManager.isConnected()) {
        if (display_mode != DISPLAY_MODE_LAMP) {
            scheduler_loop();
        }
        mqtt_manager_tryDisplayHaEntity();
        
        // === BUZZER LOGIC (non-blocking) ===
        // Handle buzzer off
        if (buzzer_active && (millis() - buzzer_start_time) > buzzer_duration) {
            digitalWrite(BUZZER_PIN, LOW);
            buzzer_active = false;
        }
        
        // Start buzzer if needed
        if (currentSecond != last_buzzer_second) {
            last_buzzer_second = currentSecond;
            
            if (display_enabled && !buzzer_active) {
                bool should_buzz = false;
                uint32_t buzz_duration = 100;
                
                if (currentHour >= 7 && currentHour < 21 && currentSecond == 0) {
                    // Hour signal: LONG at :00:00
                    should_buzz = true;
                    buzz_duration = 200;
                } else if (currentMinute == 6 && currentSecond >= 56 && currentSecond <= 58) {
                    // Pre-hour signal: SHORT at :06:56, 57, 58
                    should_buzz = true;
                    buzz_duration = 100;
                }
                
                if (should_buzz) {
                    digitalWrite(BUZZER_PIN, HIGH);
                    buzzer_active = true;
                    buzzer_start_time = millis();
                    buzzer_duration = buzz_duration;
                }
            }
        }
        
        // === MESSAGE TIMEOUT ===
        if (message_active && message_time_left > 0) {
            if ((millis() - message_start_time) > message_time_left) {
                message_active = false;
            }
        }
        
        // === DISPLAY LOGIC - PRIORITY ===
        if (!display_enabled) {
            display_clear();
            display_show();
        } else if (message_active && message_time_left > 0) {
            // Priority 2: Manual message (highest after OFF)
            uint32_t now = millis();
            uint16_t messageIntervalMs = (uint16_t)(30.0 / message_speed);
            if (messageIntervalMs < 1) messageIntervalMs = 1;
            if (now - last_message_update > messageIntervalMs) {
                message_offset--;
                if (message_offset < -((int)strlen(message_text) * 6)) {
                    message_active = false;
                }
                last_message_update = now;
            }
            if (message_active) {
                display_clear();
                display_drawMessage(message_text, message_offset, message_color);
                display_show();
            }
        } else if (display_mode == DISPLAY_MODE_ANIMATION) {
            // Priority 3: Animation
            switch(animation_mode) {
                case ANIM_RAINBOW: anim_rainbow(); break;
                case ANIM_FADE: anim_fade(); break;
                case ANIM_WAVE: anim_wave(); break;
                case ANIM_PULSE: anim_pulse(); break;
                case ANIM_NIGHT: anim_night(); break;
                case ANIM_RAINBOW_BACKGROUND: anim_rainbow_background(); break;
                default: anim_rainbow();
            }
            display_show();
        } else if (display_mode == DISPLAY_MODE_QUOTE) {
            // Priority 4: Quote - ale zawsze update animacje zegara w tle
            display_drawClock(currentHour, currentMinute, currentSecond, colonState, false);
            if (effects_quotes(nullptr)) {
                display_mode = DISPLAY_MODE_CLOCK;
                display_resetFunClockNextEffectTimer();
            }
        } else if (display_mode == DISPLAY_MODE_LAMP) {
            // Priority 5: Lamp
            display_clear();
            display_drawLamp();
            display_show();
        } else {
            // Priority 6: Clock (default)
            display_clear();
            display_drawClock(currentHour, currentMinute, currentSecond, colonState, true);
            display_show();
        }
    } else {
        static int16_t apTextOffset = LED_WIDTH;
        static uint32_t apTextLastUpdate = 0;
        const char* apText = "TRYB AP";
        uint32_t now = millis();
        if (now - apTextLastUpdate > 30) {
            apTextOffset--;
            if (apTextOffset < -((int)strlen(apText) * 6)) {
                apTextOffset = LED_WIDTH;
            }
            apTextLastUpdate = now;
        }
        display_clear();
        display_drawMessage(apText, apTextOffset, CRGB::Cyan);
        display_show();
    }
    
    // === DIAGNOSTICS (every 5s) ===
    uint32_t nowMs = millis();
    if (nowMs - lastDiagnosticMs > 5000) {
        lastDiagnosticMs = nowMs;
        uint32_t loopDtime = nowMs - loopStartMs;
        uint32_t fps = (loopCount * 1000) / ((nowMs > loopStartMs) ? (nowMs - loopStartMs + 1) : 1);
        app_logf("[DIAG] FPS=%lu animSpeed=%u msgSpeed=%u loopMs=%lu mode=%u msg=%u",
            fps, animation_speed, message_speed, loopDtime, display_mode, message_active);
        loopCount = 0;
    }
}
