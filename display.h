#ifndef DISPLAY_H
#define DISPLAY_H

#include <FastLED.h>
#include "config.h"

// === Display modes ===
#define DISPLAY_MODE_CLOCK      0
#define DISPLAY_MODE_ANIMATION  1
#define DISPLAY_MODE_MESSAGE    2
#define DISPLAY_MODE_QUOTE      3
#define DISPLAY_MODE_LAMP       4

// === Animation types ===
#define ANIM_RAINBOW   0
#define ANIM_FADE      1
#define ANIM_WAVE      2
#define ANIM_PULSE     3
#define ANIM_NIGHT     4
#define ANIM_RAINBOW_BACKGROUND 5

extern CRGB leds[NUM_LEDS];
extern uint8_t globalBrightness;
extern CRGB globalColor;

// === Global display state ===
extern uint8_t display_mode;
extern bool display_enabled;

// === Animation state ===
extern uint8_t animation_mode;
extern uint8_t animation_speed;
extern uint32_t last_animation_update;
extern float animation_hue;

// === Message state ===
extern bool message_active;
extern char message_text[128];
extern CRGB message_color;
extern uint8_t message_speed;
extern int16_t message_offset;
extern uint32_t message_time_left;
extern uint32_t message_start_time;
extern uint32_t last_message_update;

// === Colors ===
extern CRGB clock_color;
extern CRGB quote_color;
extern CRGB animation_color;

void display_init();
void display_bootTest();
void display_clear();
void display_show();
void updateLEDs();
void display_setBrightness(uint8_t brightness);
void display_setColor(CRGB color);
void display_setNegative(bool enabled);
void display_setMatrixRotate180(bool enabled);
bool display_getMatrixRotate180();
uint16_t XY(uint8_t x, uint8_t y);
void display_drawClock(uint8_t hour, uint8_t minute, uint8_t second, bool colon, bool showSeconds);
void display_drawText(const char* text, int16_t offset, CRGB color);
void display_drawLamp();
void display_drawMessage(const char* text, int16_t offset, CRGB color);
void display_drawCornerCalibration();
bool display_triggerFunClockAnyEnabled();
void display_triggerFunClockMirror();
void display_triggerFunClockRainbow();
void display_triggerFunClockHoursSlide();
void display_triggerFunClockMatrixFont();
void display_triggerFunClockMatrixSideways();
void display_triggerFunClockUpsideDown();
void display_triggerFunClockRotate180();
void display_triggerFunClockFullRotate();
void display_triggerFunClockMiddleSwap();
void display_triggerFunClockTetris();
void display_triggerFunClockPileup();
void display_triggerFunClockNegative();
void display_triggerFunClockRainbowBackground();
void display_setFunClockIntervalSeconds(uint16_t seconds);
void display_resetFunClockNextEffectTimer();
uint32_t display_getFunClockCompletedEffectsCount();
void display_setFunClockEffectsEnabled(bool mirrorEnabled, bool rainbowEnabled, bool hoursSlideEnabled, bool matrixFontEnabled, bool matrixSidewaysEnabled, bool upsideDownEnabled, bool rotate180Enabled, bool fullRotateEnabled, bool middleSwapEnabled, bool tetrisEnabled, bool pileupEnabled, bool rainbowBackgroundEnabled, bool negativeEnabled);
void display_suppressFunClockEffects(uint16_t durationMs);

// === Animation functions ===
void anim_rainbow();
void anim_fade();
void anim_wave();
void anim_pulse();
void anim_night();
void anim_rainbow_background();

#endif // DISPLAY_H
