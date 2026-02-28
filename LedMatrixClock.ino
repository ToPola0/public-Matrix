#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "display.h"
#include "effects.h"
#include "clock.h"
#include "wifi_manager.h"
#include "web_panel.h"
#include "quotes.h"
#include "scheduler.h"
#include "mqtt_manager.h"

unsigned long lastStatus = 0;
unsigned long lastBuzzer = 0;
bool buzzerState = false;
unsigned long bootMillis = 0;

// === Buzzer & Quote safety ===
uint8_t last_buzzer_second = 255;
uint8_t last_quote_hour = 255;
bool buzzer_active = false;
uint32_t buzzer_start_time = 0;
uint32_t buzzer_duration = 0;

MainConfig mainConfig;
WebServer webServer(80);

WifiManager wifiManager;

static bool otaInitialized = false;
static bool otaUploadActive = false;

static void drawArduinoOtaProgress(unsigned int progress, unsigned int total) {
    display_clear();

    const int16_t barHeight = (LED_HEIGHT >= 8) ? 3 : 2;
    const int16_t barY = (LED_HEIGHT - barHeight) / 2;

    for (int16_t x = 0; x < LED_WIDTH; x++) {
        for (int16_t y = 0; y < barHeight; y++) {
            int16_t py = barY + y;
            if (py >= 0 && py < LED_HEIGHT) {
                leds[XY(x, py)] = CRGB::Black;
            }
        }
    }

    int16_t fill = 0;
    if (total > 0) {
        fill = (int16_t)(((uint32_t)progress * LED_WIDTH) / total);
        if (fill > LED_WIDTH) fill = LED_WIDTH;
    }

    for (int16_t x = 0; x < fill; x++) {
        for (int16_t y = 0; y < barHeight; y++) {
            int16_t py = barY + y;
            if (py >= 0 && py < LED_HEIGHT) {
                leds[XY(x, py)] = CRGB::Green;
            }
        }
    }

    display_show();
}

static void otaBeginIfNeeded() {
    if (otaInitialized) return;
    if (!wifiManager.isConnected()) return;

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setTimeout(120000);
    if (strlen(OTA_PASSWORD) > 0) {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    }

    ArduinoOTA.onStart([]() {
        otaUploadActive = true;
        wifiManager.setExternalOtaActive(true);
        WiFi.setSleep(false);
        FastLED.setDither(0);
        drawArduinoOtaProgress(0, 100);
        Serial.println("[OTA] Start");
    });

    ArduinoOTA.onEnd([]() {
        drawArduinoOtaProgress(100, 100);
        otaUploadActive = false;
        wifiManager.setExternalOtaActive(false);
        FastLED.setDither(1);
        Serial.println("\n[OTA] End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPercent = 255;
        uint8_t percent = (uint8_t)((progress * 100U) / total);
        drawArduinoOtaProgress(progress, total);
        if (percent != lastPercent && (percent % 10 == 0 || percent == 99)) {
            Serial.printf("[OTA] Progress: %u%%\n", percent);
            lastPercent = percent;
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaUploadActive = false;
        wifiManager.setExternalOtaActive(false);
        FastLED.setDither(1);
        Serial.printf("[OTA] Error[%u]\n", (unsigned)error);
    });

    ArduinoOTA.begin();
    otaInitialized = true;
    Serial.print("[OTA] Ready: ");
    Serial.print(OTA_HOSTNAME);
    Serial.print(" @ ");
    Serial.println(WiFi.localIP());
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
    display_init();
    display_bootTest();
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
    otaBeginIfNeeded();
    effects_init();
    clock_init();
    quotes_init();
    scheduler_init();
    webPanel_setup();
    mqtt_manager_begin();
}

void loop() {
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

    otaBeginIfNeeded();
    if (otaInitialized) {
        ArduinoOTA.handle();
    }
    if (otaUploadActive) {
        delay(1);
        return;
    }

    wifiManager.loop();
    mqtt_manager_loop();
    if (wifiManager.isOtaUpdating()) {
        delay(1);
        return;
    }
    clock_update();
    if (!wifiManager.isAPMode()) {
        webPanel_loop();
    }
    
    if (wifiManager.isConnected()) {
        scheduler_loop();
        
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
        
        // === QUOTE LOGIC (wyłączone jeśli scheduler ma losowe cytaty) ===
        if (!mainConfig.schedule.random_quotes_enabled && currentSecond == 2 && currentHour != last_quote_hour) {
            last_quote_hour = currentHour;
            display_mode = DISPLAY_MODE_QUOTE;
            effects_quotes(quotes_getRandom());
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
            if (now - last_message_update > 30 / message_speed) {
                message_offset--;
                if (message_offset < -((int)strlen(message_text) * 6)) {
                    message_offset = LED_WIDTH;
                }
                last_message_update = now;
            }
            display_clear();
            display_drawMessage(message_text, message_offset, message_color);
            display_show();
        } else if (display_mode == DISPLAY_MODE_ANIMATION) {
            // Priority 3: Animation
            switch(animation_mode) {
                case ANIM_RAINBOW: anim_rainbow(); break;
                case ANIM_FADE: anim_fade(); break;
                case ANIM_WAVE: anim_wave(); break;
                case ANIM_PULSE: anim_pulse(); break;
                case ANIM_NIGHT: anim_night(); break;
                default: anim_rainbow();
            }
            display_show();
        } else if (display_mode == DISPLAY_MODE_QUOTE) {
            // Priority 4: Quote
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
}
