#include "effects.h"
#include "display.h"
#include "quotes.h"
#include "app_logger.h"

struct Firework {
    int16_t x, y;
    CRGB color;
    uint8_t state; // 0: idle, 1: rising, 2: explode
    uint32_t startTime;
    int8_t vy;
};

#define MAX_FIREWORKS 5
static Firework fireworks[MAX_FIREWORKS];

void effects_init() {
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].state = 0;
    }
}

void launch_firework(Firework& fw) {
    fw.x = random(4, LED_WIDTH-4);
    fw.y = LED_HEIGHT-1;
    fw.color = CHSV(random8(), 255, 255);
    fw.state = 1;
    fw.startTime = millis();
    fw.vy = -1 * random(1, 3);
}

void update_firework(Firework& fw) {
    if (fw.state == 0) {
        if (random8() < 8) launch_firework(fw);
        return;
    }
    if (fw.state == 1) {
        fw.y += fw.vy;
        if (fw.y <= random(3, 7)) {
            fw.state = 2;
            fw.startTime = millis();
        }
    } else if (fw.state == 2) {
        for (int i = 0; i < 16; i++) {
            float angle = i * (PI / 8.0);
            int16_t fx = fw.x + cos(angle) * (millis() - fw.startTime) / 30.0;
            int16_t fy = fw.y + sin(angle) * (millis() - fw.startTime) / 30.0;
            if (fx >= 0 && fx < LED_WIDTH && fy >= 0 && fy < LED_HEIGHT) {
                leds[XY(fx, fy)] += fw.color;
            }
        }
        if (millis() - fw.startTime > 400) {
            fw.state = 0;
        }
    }
}

void effects_firework() {
    display_clear();
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        update_firework(fireworks[i]);
    }
    display_show();
}

// Quotes scrolling
static uint32_t lastScroll = 0;
static int16_t scrollOffset = LED_WIDTH;
static char currentQuote[128] = "";
static uint32_t quoteStartMs = 0;
static uint32_t quoteFrameCount = 0;

bool effects_quotes(const char* text) {
    if (text && text[0] != '\0' && strcmp(currentQuote, text) != 0) {
        strlcpy(currentQuote, text, sizeof(currentQuote));
        scrollOffset = LED_WIDTH;
        quoteStartMs = millis();
        quoteFrameCount = 0;
        app_logf("[QUOTE] START: %s (len=%u) animSpeed=%u", text, (unsigned)strlen(text), animation_speed);
    }
    if (currentQuote[0] == '\0') {
        return true;
    }
    uint8_t speed = animation_speed;
    if (speed < 1) speed = 1;
    uint16_t scrollStepMs = (uint16_t)(30U / speed);
    if (scrollStepMs < 6U) scrollStepMs = 6U;
    if (millis() - lastScroll > scrollStepMs) {
        scrollOffset--;
        lastScroll = millis();
        quoteFrameCount++;
    }
    display_clear();
    display_drawText(currentQuote, scrollOffset, quote_color);
    display_show();
    if (scrollOffset < -((int)strlen(currentQuote) * 6)) {
        uint32_t totalMs = millis() - quoteStartMs;
        uint32_t fps = (quoteFrameCount * 1000) / (totalMs > 0 ? totalMs : 1);
        app_logf("[QUOTE] DONE: frames=%lu ms=%lu fps=%lu", quoteFrameCount, totalMs, fps);
        scrollOffset = LED_WIDTH;
        return true;
    }
    return false;
}
