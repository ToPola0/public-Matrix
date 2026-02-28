#include "display.h"

#include <Arduino.h>

#include "font5x7.h"

// === Global display state ===
CRGB leds[NUM_LEDS];
uint8_t globalBrightness = 50;
CRGB globalColor = CRGB::White;

uint8_t display_mode = DISPLAY_MODE_CLOCK;
bool display_enabled = true;

// === Animation state ===
uint8_t animation_mode = ANIM_RAINBOW;
uint8_t animation_speed = 1;
uint32_t last_animation_update = 0;
float animation_hue = 0;

// === Message state ===
bool message_active = false;
char message_text[128] = "";
CRGB message_color = CRGB::Yellow;
uint8_t message_speed = 1;
int16_t message_offset = 64;
uint32_t message_time_left = 0;
uint32_t message_start_time = 0;
uint32_t last_message_update = 0;

// === Colors ===
CRGB clock_color = CRGB::Green;
CRGB quote_color = CRGB::Cyan;
CRGB animation_color = CRGB::Magenta;
static bool displayNegativeEnabled = false;

static int8_t digitOffset[8] = {0};
static uint16_t funClockIntervalSeconds = 10;
static uint32_t funClockRainbowHuePhaseQ16 = 0;
static uint32_t funClockRainbowLastUs = 0;
static CRGB funClockRainbowSmoothed[8] = {
    CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black,
    CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black
};
static bool funClockRainbowSmoothedInitialized = false;
static bool funClockMoveEnabled = true;
static bool funClockMirrorEnabled = true;
static bool funClockRainbowEnabled = true;
static bool funClockHoursSlideEnabled = true;
static bool funClockMatrixFontEnabled = true;
static bool funClockMatrixSidewaysEnabled = true;
static bool funClockUpsideDownEnabled = true;
static bool funClockRotate180Enabled = true;
static bool funClockFullRotateEnabled = true;
static bool funClockMiddleSwapEnabled = true;
static bool funClockPileupEnabled = true;
static bool funClockNegativeEnabled = false;
static CRGB displayFrameBackup[NUM_LEDS];
static CRGB displayRenderFrame[NUM_LEDS];
static CRGB displayPrevShownFrame[NUM_LEDS];
static CRGB displayLastSentFrame[NUM_LEDS];
static bool displayPrevShownFrameValid = false;
static bool displayLastSentFrameValid = false;
static bool displayForceRefresh = true;
static uint32_t funClockSuppressUntilMs = 0;
static uint32_t funClockCompletedEffectsCount = 0;

enum FunClockEffect : uint8_t {
    FUN_CLOCK_EFFECT_NONE = 0,
    FUN_CLOCK_EFFECT_MOVE = 1,
    FUN_CLOCK_EFFECT_MIRROR = 2,
    FUN_CLOCK_EFFECT_RAINBOW = 3,
    FUN_CLOCK_EFFECT_HOURS_SLIDE = 4,
    FUN_CLOCK_EFFECT_MATRIX_FONT = 5,
    FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS = 6,
    FUN_CLOCK_EFFECT_UPSIDE_DOWN = 7,
    FUN_CLOCK_EFFECT_ROTATE_180 = 8,
    FUN_CLOCK_EFFECT_FULL_ROTATE = 9,
    FUN_CLOCK_EFFECT_MIDDLE_SWAP = 10,
    FUN_CLOCK_EFFECT_PILEUP = 11,
    FUN_CLOCK_EFFECT_NEGATIVE = 12
};

static FunClockEffect funClockLastEffect = FUN_CLOCK_EFFECT_NONE;

struct FunClockState {
    bool initialized;
    FunClockEffect activeEffect;
    uint32_t effectStartMs;
    uint32_t effectDurationMs;
    uint32_t nextEffectMs;
    int8_t direction[8];
    uint8_t amplitude[8];
};

static FunClockState funClockState = {
    false,
    FUN_CLOCK_EFFECT_NONE,
    0,
    0,
    0,
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0}
};

static void funClockStartDigitAnimation(uint32_t now);
static void funClockStartMirror(uint32_t now);
static void funClockStartRainbow(uint32_t now);
static void funClockStartHoursSlide(uint32_t now);
static void funClockStartMatrixFont(uint32_t now);
static void funClockStartMatrixSideways(uint32_t now);
static void funClockStartUpsideDown(uint32_t now);
static void funClockStartRotate180(uint32_t now);
static void funClockStartFullRotate(uint32_t now);
static void funClockStartMiddleSwap(uint32_t now);
static void funClockStartPileup(uint32_t now);
static void funClockStartNegative(uint32_t now);

static bool displayFrameEquals(const CRGB* a, const CRGB* b) {
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b) {
            return false;
        }
    }
    return true;
}

static uint8_t displayAdaptiveBlendWeight() {
    if (display_mode == DISPLAY_MODE_ANIMATION) {
        uint8_t speed = animation_speed;
        if (speed < 1) speed = 1;
        if (speed > 10) speed = 10;

        uint8_t baseWeight = 72;
        switch (animation_mode) {
            case ANIM_RAINBOW: baseWeight = 84; break;
            case ANIM_FADE: baseWeight = 112; break;
            case ANIM_WAVE: baseWeight = 72; break;
            case ANIM_PULSE: baseWeight = 64; break;
            case ANIM_NIGHT: baseWeight = 120; break;
            default: break;
        }

        uint8_t penalty = (uint8_t)((speed - 1) * 5);
        return (baseWeight > penalty + 24) ? (uint8_t)(baseWeight - penalty) : 24;
    }

    if (message_active) {
        uint8_t speed = message_speed;
        if (speed < 1) speed = 1;
        if (speed > 10) speed = 10;
        uint8_t penalty = (uint8_t)((speed - 1) * 4);
        return (70 > penalty + 24) ? (uint8_t)(70 - penalty) : 24;
    }

    switch (funClockState.activeEffect) {
        case FUN_CLOCK_EFFECT_MOVE: return 56;
        case FUN_CLOCK_EFFECT_MIRROR: return 40;
        case FUN_CLOCK_EFFECT_RAINBOW: return 96;
        case FUN_CLOCK_EFFECT_HOURS_SLIDE: return 64;
        case FUN_CLOCK_EFFECT_MATRIX_FONT: return 52;
        case FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS: return 46;
        case FUN_CLOCK_EFFECT_UPSIDE_DOWN: return 36;
        case FUN_CLOCK_EFFECT_ROTATE_180: return 36;
        case FUN_CLOCK_EFFECT_FULL_ROTATE: return 34;
        case FUN_CLOCK_EFFECT_MIDDLE_SWAP: return 40;
        case FUN_CLOCK_EFFECT_PILEUP: return 32;
        case FUN_CLOCK_EFFECT_NEGATIVE: return 0;
        case FUN_CLOCK_EFFECT_NONE:
        default:
            return 72;
    }
}

static CRGB funClockRainbowColorFromHueQ8(uint16_t hueQ8, uint8_t sat, uint8_t val) {
    uint8_t hue0 = (uint8_t)(hueQ8 >> 8);
    uint8_t frac = (uint8_t)(hueQ8 & 0xFF);
    CRGB c0 = CHSV(hue0, sat, val);
    CRGB c1 = CHSV((uint8_t)(hue0 + 1U), sat, val);
    return blend(c0, c1, frac);
}

static void funClockInitIfNeeded() {
    if (funClockState.initialized) return;

    uint32_t now = millis();
    funClockState.initialized = true;
    funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
}

static bool funClockStartAnyEnabledEffect(uint32_t now) {
    FunClockEffect enabled[12];
    uint8_t count = 0;

    if (funClockMoveEnabled) enabled[count++] = FUN_CLOCK_EFFECT_MOVE;
    if (funClockMirrorEnabled) enabled[count++] = FUN_CLOCK_EFFECT_MIRROR;
    if (funClockRainbowEnabled) enabled[count++] = FUN_CLOCK_EFFECT_RAINBOW;
    if (funClockHoursSlideEnabled) enabled[count++] = FUN_CLOCK_EFFECT_HOURS_SLIDE;
    if (funClockMatrixFontEnabled) enabled[count++] = FUN_CLOCK_EFFECT_MATRIX_FONT;
    if (funClockMatrixSidewaysEnabled) enabled[count++] = FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS;
    if (funClockUpsideDownEnabled) enabled[count++] = FUN_CLOCK_EFFECT_UPSIDE_DOWN;
    if (funClockRotate180Enabled) enabled[count++] = FUN_CLOCK_EFFECT_ROTATE_180;
    if (funClockFullRotateEnabled) enabled[count++] = FUN_CLOCK_EFFECT_FULL_ROTATE;
    if (funClockMiddleSwapEnabled) enabled[count++] = FUN_CLOCK_EFFECT_MIDDLE_SWAP;
    if (funClockPileupEnabled) enabled[count++] = FUN_CLOCK_EFFECT_PILEUP;
    if (funClockNegativeEnabled) enabled[count++] = FUN_CLOCK_EFFECT_NEGATIVE;

    if (count == 0) return false;

    FunClockEffect blocked = FUN_CLOCK_EFFECT_NONE;
    if (count > 1 && funClockLastEffect != FUN_CLOCK_EFFECT_NONE) {
        blocked = funClockLastEffect;
    }

    FunClockEffect selectable[12];
    uint8_t selectableCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (enabled[i] != blocked) {
            selectable[selectableCount++] = enabled[i];
        }
    }

    if (selectableCount == 0) {
        for (uint8_t i = 0; i < count; i++) {
            selectable[selectableCount++] = enabled[i];
        }
    }

    uint8_t selectedIndex = (uint8_t)random(0, selectableCount);
    FunClockEffect selected = selectable[selectedIndex];

    funClockLastEffect = selected;

    if (selected == FUN_CLOCK_EFFECT_MOVE) {
        funClockStartDigitAnimation(now);
    } else if (selected == FUN_CLOCK_EFFECT_MIRROR) {
        funClockStartMirror(now);
    } else if (selected == FUN_CLOCK_EFFECT_RAINBOW) {
        funClockStartRainbow(now);
    } else if (selected == FUN_CLOCK_EFFECT_HOURS_SLIDE) {
        funClockStartHoursSlide(now);
    } else if (selected == FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS) {
        funClockStartMatrixSideways(now);
    } else if (selected == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
        funClockStartUpsideDown(now);
    } else if (selected == FUN_CLOCK_EFFECT_ROTATE_180) {
        funClockStartRotate180(now);
    } else if (selected == FUN_CLOCK_EFFECT_FULL_ROTATE) {
        funClockStartFullRotate(now);
    } else if (selected == FUN_CLOCK_EFFECT_MIDDLE_SWAP) {
        funClockStartMiddleSwap(now);
    } else if (selected == FUN_CLOCK_EFFECT_PILEUP) {
        funClockStartPileup(now);
    } else if (selected == FUN_CLOCK_EFFECT_NEGATIVE) {
        funClockStartNegative(now);
    } else {
        funClockStartMatrixFont(now);
    }
    return true;
}

static void funClockStartDigitAnimation(uint32_t now) {
    funClockState.activeEffect = FUN_CLOCK_EFFECT_MOVE;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(3000, 4001);

    for (uint8_t i = 0; i < 8; i++) {
        if (i == 2 || i == 5) {
            funClockState.direction[i] = 0;
            funClockState.amplitude[i] = 0;
            digitOffset[i] = 0;
        } else {
            funClockState.direction[i] = (random(0, 2) == 0) ? -1 : 1;
            funClockState.amplitude[i] = (uint8_t)random(2, 5);
        }
    }
}

static void funClockStartMirror(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_MIRROR;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(2000, 3001);
}

static void funClockStartRainbow(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_RAINBOW;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(6000, 9001);
    funClockRainbowHuePhaseQ16 = (uint32_t)((uint64_t)now * 1536ULL);
    funClockRainbowLastUs = micros();
    funClockRainbowSmoothedInitialized = false;
}

static void funClockStartHoursSlide(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_HOURS_SLIDE;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = 4400U; // out(1200) + gap(2000) + in(1200)
}

static void funClockStartMatrixFont(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_MATRIX_FONT;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(5000, 8001);
}

static void funClockStartMatrixSideways(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(5200, 7601);
}

static void funClockStartUpsideDown(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_UPSIDE_DOWN;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(3000, 5001);
}

static void funClockStartRotate180(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_ROTATE_180;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(3000, 5001);
}

static void funClockStartFullRotate(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_FULL_ROTATE;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(4200, 5601);
}

static void funClockStartMiddleSwap(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_MIDDLE_SWAP;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = 2650U;
}

static void funClockStartPileup(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_PILEUP;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = (uint32_t)random(3400, 4401);
}

static void funClockStartNegative(uint32_t now) {
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_NEGATIVE;
    funClockState.effectStartMs = now;
    funClockState.effectDurationMs = 3000U;
}

static void funClockUpdateState() {
    funClockInitIfNeeded();
    uint32_t now = millis();

    if (funClockSuppressUntilMs != 0 && now < funClockSuppressUntilMs) {
        if (funClockState.activeEffect != FUN_CLOCK_EFFECT_NONE) {
            for (uint8_t i = 0; i < 8; i++) {
                digitOffset[i] = 0;
            }
            funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
        }
        return;
    }

    if (funClockState.activeEffect == FUN_CLOCK_EFFECT_NONE) {
        if (now >= funClockState.nextEffectMs) {
            if (!funClockStartAnyEnabledEffect(now)) {
                funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
            }
        }
    }

    if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MOVE) {
        uint32_t elapsed = now - funClockState.effectStartMs;
        if (elapsed >= funClockState.effectDurationMs) {
            for (uint8_t i = 0; i < 8; i++) {
                digitOffset[i] = 0;
            }
            funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
            funClockCompletedEffectsCount++;
            funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
        } else {
            float progress = (float)elapsed / (float)funClockState.effectDurationMs;
            float envelope = sinf(progress * PI);
            float wobble = sinf(progress * PI * 4.0f) * (1.0f - progress) * 0.20f;

            for (uint8_t i = 0; i < 8; i++) {
                if (funClockState.amplitude[i] == 0 || funClockState.direction[i] == 0) {
                    digitOffset[i] = 0;
                    continue;
                }

                float animated = (float)funClockState.direction[i] *
                                 (float)funClockState.amplitude[i] *
                                 (envelope + wobble);
                digitOffset[i] = (int8_t)roundf(animated);
            }
        }
    }

    if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MIRROR ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_RAINBOW ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_HOURS_SLIDE ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_MATRIX_FONT ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_ROTATE_180 ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_FULL_ROTATE ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_MIDDLE_SWAP ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_PILEUP ||
        funClockState.activeEffect == FUN_CLOCK_EFFECT_NEGATIVE) {
        uint32_t elapsed = now - funClockState.effectStartMs;
        if (elapsed >= funClockState.effectDurationMs) {
            funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
            funClockCompletedEffectsCount++;
            funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
        }
    }
}

static int16_t funClockApplyMirrorX(int16_t x, int16_t symbolWidth) {
    if (funClockState.activeEffect != FUN_CLOCK_EFFECT_MIRROR) {
        return x;
    }
    return (LED_WIDTH - symbolWidth - x);
}

void display_triggerFunClockAnimation() {
    funClockInitIfNeeded();
    funClockStartDigitAnimation(millis());
}

void display_triggerFunClockMirror() {
    funClockInitIfNeeded();
    funClockStartMirror(millis());
}

void display_triggerFunClockRainbow() {
    funClockInitIfNeeded();
    funClockStartRainbow(millis());
}

void display_triggerFunClockHoursSlide() {
    funClockInitIfNeeded();
    funClockStartHoursSlide(millis());
}

void display_triggerFunClockMatrixFont() {
    funClockInitIfNeeded();
    funClockStartMatrixFont(millis());
}

void display_triggerFunClockMatrixSideways() {
    funClockInitIfNeeded();
    funClockStartMatrixSideways(millis());
}

void display_triggerFunClockUpsideDown() {
    funClockInitIfNeeded();
    funClockStartUpsideDown(millis());
}

void display_triggerFunClockRotate180() {
    funClockInitIfNeeded();
    funClockStartRotate180(millis());
}

void display_triggerFunClockFullRotate() {
    funClockInitIfNeeded();
    funClockStartFullRotate(millis());
}

void display_triggerFunClockMiddleSwap() {
    funClockInitIfNeeded();
    funClockStartMiddleSwap(millis());
}

void display_triggerFunClockPileup() {
    funClockInitIfNeeded();
    funClockStartPileup(millis());
}

void display_setFunClockIntervalSeconds(uint16_t seconds) {
    funClockIntervalSeconds = constrain(seconds, 10, 3600);
    uint32_t now = millis();
    if (funClockState.activeEffect == FUN_CLOCK_EFFECT_NONE) {
        funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
    }
}

void display_resetFunClockNextEffectTimer() {
    funClockInitIfNeeded();
    uint32_t now = millis();
    funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
}

uint32_t display_getFunClockCompletedEffectsCount() {
    return funClockCompletedEffectsCount;
}

void display_setFunClockEffectsEnabled(bool moveEnabled, bool mirrorEnabled, bool rainbowEnabled, bool hoursSlideEnabled, bool matrixFontEnabled, bool matrixSidewaysEnabled, bool upsideDownEnabled, bool rotate180Enabled, bool fullRotateEnabled, bool middleSwapEnabled, bool pileupEnabled, bool negativeEnabled) {
    funClockMoveEnabled = moveEnabled;
    funClockMirrorEnabled = mirrorEnabled;
    funClockRainbowEnabled = rainbowEnabled;
    funClockHoursSlideEnabled = hoursSlideEnabled;
    funClockMatrixFontEnabled = matrixFontEnabled;
    funClockMatrixSidewaysEnabled = matrixSidewaysEnabled;
    funClockUpsideDownEnabled = upsideDownEnabled;
    funClockRotate180Enabled = rotate180Enabled;
    funClockFullRotateEnabled = fullRotateEnabled;
    funClockMiddleSwapEnabled = middleSwapEnabled;
    funClockPileupEnabled = pileupEnabled;
    funClockNegativeEnabled = negativeEnabled;
    funClockLastEffect = FUN_CLOCK_EFFECT_NONE;

    uint32_t now = millis();
    if (!funClockMoveEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_MOVE) {
        for (uint8_t i = 0; i < 8; i++) digitOffset[i] = 0;
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockMirrorEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_MIRROR) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockRainbowEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_RAINBOW) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockHoursSlideEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_HOURS_SLIDE) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockMatrixFontEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_MATRIX_FONT) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockMatrixSidewaysEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockUpsideDownEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockRotate180Enabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_ROTATE_180) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockFullRotateEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_FULL_ROTATE) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockMiddleSwapEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_MIDDLE_SWAP) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockPileupEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_PILEUP) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }
    if (!funClockNegativeEnabled && funClockState.activeEffect == FUN_CLOCK_EFFECT_NEGATIVE) {
        funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
    }

    if (funClockState.activeEffect == FUN_CLOCK_EFFECT_NONE) {
        funClockState.nextEffectMs = now + (uint32_t)funClockIntervalSeconds * 1000UL;
    }
}

void display_suppressFunClockEffects(uint16_t durationMs) {
    uint32_t now = millis();
    funClockSuppressUntilMs = now + (uint32_t)durationMs;
    for (uint8_t i = 0; i < 8; i++) {
        digitOffset[i] = 0;
    }
    funClockState.activeEffect = FUN_CLOCK_EFFECT_NONE;
}

static const uint8_t clockDigits3x4[10][4] = {
    {0b111, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b111, 0b100}, // 2
    {0b111, 0b001, 0b111, 0b001}, // 3
    {0b101, 0b101, 0b111, 0b001}, // 4
    {0b111, 0b100, 0b111, 0b001}, // 5
    {0b111, 0b100, 0b111, 0b101}, // 6
    {0b111, 0b001, 0b001, 0b001}, // 7
    {0b111, 0b101, 0b111, 0b101}, // 8
    {0b111, 0b101, 0b111, 0b001}  // 9
};

static const uint8_t clockDigits4x7[10][4] = {
    {0x3E, 0x41, 0x41, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40}, // 1
    {0x72, 0x49, 0x49, 0x46}, // 2
    {0x22, 0x49, 0x49, 0x36}, // 3
    {0x0F, 0x08, 0x08, 0x7F}, // 4
    {0x2F, 0x49, 0x49, 0x31}, // 5
    {0x3E, 0x49, 0x49, 0x32}, // 6
    {0x01, 0x71, 0x09, 0x07}, // 7
    {0x36, 0x49, 0x49, 0x36}, // 8
    {0x26, 0x49, 0x49, 0x3E}  // 9
};

static const uint8_t clockDigits3x5[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b010, 0b100, 0b100}, // 7
    {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b111, 0b001, 0b111}  // 9
};

static const uint8_t clockDigits4x6[10][6] = {
    {0b1111, 0b1001, 0b1001, 0b1001, 0b1001, 0b1111}, // 0
    {0b0010, 0b0110, 0b0010, 0b0010, 0b0010, 0b0111}, // 1
    {0b1111, 0b0001, 0b0001, 0b1111, 0b1000, 0b1111}, // 2
    {0b1111, 0b0001, 0b0001, 0b0111, 0b0001, 0b1111}, // 3
    {0b1001, 0b1001, 0b1001, 0b1111, 0b0001, 0b0001}, // 4
    {0b1111, 0b1000, 0b1000, 0b1111, 0b0001, 0b1111}, // 5
    {0b1111, 0b1000, 0b1000, 0b1111, 0b1001, 0b1111}, // 6
    {0b1111, 0b0001, 0b0010, 0b0100, 0b0100, 0b0100}, // 7
    {0b1111, 0b1001, 0b1001, 0b1111, 0b1001, 0b1111}, // 8
    {0b1111, 0b1001, 0b1001, 0b1111, 0b0001, 0b1111}  // 9
};

// No frameBuffer - operate directly on `leds[]` and FastLED

static void drawClockDot2x1(int16_t x, int16_t y, CRGB color) {
    for (int8_t dx = 0; dx < 2; dx++) {
        int16_t px = x + dx;
        int16_t py = y;
        if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
            leds[XY(px, py)] = color;
        }
    }
}

static void drawClockDigit3x4_2x2(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t row = 0; row < 4; row++) {
        uint8_t bits = clockDigits3x4[digit][row];
        for (uint8_t col = 0; col < 3; col++) {
            if (bits & (1 << (2 - col))) {
                drawClockDot2x1(x + col * 2, y + row * 2, color);
            }
        }
    }
}

static void drawClockColon(int16_t x, int16_t y, bool visible, CRGB color) {
    if (!visible) return;
    drawClockDot2x1(x, y + 2, color);
    drawClockDot2x1(x, y + 5, color);
}

static void drawClockDigit4x7(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t bits = clockDigits4x7[digit][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int16_t px = x + col;
                int16_t py = y + row;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static const uint8_t predatorGlyphs4x7[10][4] = {
    {0b0111111, 0b1000001, 0b1000001, 0b0111111},
    {0b0000000, 0b1111111, 0b0000000, 0b0000000},
    {0b1110011, 0b1001001, 0b1001001, 0b1001111},
    {0b1100011, 0b1001001, 0b1001001, 0b0110110},
    {0b0001110, 0b0001000, 0b0001000, 0b1111111},
    {0b1001111, 0b1001001, 0b1001001, 0b1110001},
    {0b0111110, 0b1001001, 0b1001001, 0b0110000},
    {0b1110000, 0b1000111, 0b1001000, 0b1110000},
    {0b0110110, 0b1001001, 0b1001001, 0b0110110},
    {0b0000110, 0b1001001, 0b1001001, 0b0111110}
};

static void drawPredatorGlyph4x7(uint8_t glyphIndex, int16_t x, int16_t y, CRGB color) {
    glyphIndex %= 10;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t bits = predatorGlyphs4x7[glyphIndex][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int16_t px = x + col;
                int16_t py = y + row;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawPredatorGlyph7x4Sideways(uint8_t glyphIndex, int16_t x, int16_t y, CRGB color) {
    glyphIndex %= 10;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t bits = predatorGlyphs4x7[glyphIndex][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int16_t px = x + (6 - row);
                int16_t py = y + col;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawClockDigit4x7Mirrored(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t bits = clockDigits4x7[digit][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int16_t px = x + (3 - col);
                int16_t py = y + row;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawClockDigit4x7UpsideDown(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t bits = clockDigits4x7[digit][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int16_t px = x + col;
                int16_t py = y + (6 - row);
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawClockDigit4x7Rotate180(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t bits = clockDigits4x7[digit][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int16_t px = x + (3 - col);
                int16_t py = y + (6 - row);
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawClockColonClassicUpsideDown(int16_t x, int16_t y, bool visible, CRGB color) {
    if (!visible) return;
    if (x >= 0 && x < LED_WIDTH) {
        if (y + 2 >= 0 && y + 2 < LED_HEIGHT) leds[XY(x, y + 4)] = color;
        if (y + 4 >= 0 && y + 4 < LED_HEIGHT) leds[XY(x, y + 2)] = color;
    }
}

static void drawClockColonClassic(int16_t x, int16_t y, bool visible, CRGB color) {
    if (!visible) return;
    if (x >= 0 && x < LED_WIDTH) {
        if (y + 2 >= 0 && y + 2 < LED_HEIGHT) leds[XY(x, y + 2)] = color;
        if (y + 4 >= 0 && y + 4 < LED_HEIGHT) leds[XY(x, y + 4)] = color;
    }
}

static void drawDigit3x5(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t row = 0; row < 5; row++) {
        uint8_t bits = clockDigits3x5[digit][row];
        for (uint8_t col = 0; col < 3; col++) {
            if (bits & (1 << (2 - col))) {
                int16_t px = x + col;
                int16_t py = y + row;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawDigit4x6(uint8_t digit, int16_t x, int16_t y, CRGB color) {
    if (digit > 9) return;
    for (uint8_t row = 0; row < 6; row++) {
        uint8_t bits = clockDigits4x6[digit][row];
        for (uint8_t col = 0; col < 4; col++) {
            if (bits & (1 << (3 - col))) {
                int16_t px = x + col;
                int16_t py = y + row;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void drawColon3x5(int16_t x, int16_t y, bool visible, CRGB color) {
    if (!visible) return;
    if (x >= 0 && x < LED_WIDTH) {
        if (y + 1 >= 0 && y + 1 < LED_HEIGHT) leds[XY(x, y + 1)] = color;
        if (y + 3 >= 0 && y + 3 < LED_HEIGHT) leds[XY(x, y + 3)] = color;
    }
}

static void drawColon4x6(int16_t x, int16_t y, bool visible, CRGB color) {
    if (!visible) return;
    if (x >= 0 && x < LED_WIDTH) {
        if (y + 1 >= 0 && y + 1 < LED_HEIGHT) leds[XY(x, y + 1)] = color;
        if (y + 4 >= 0 && y + 4 < LED_HEIGHT) leds[XY(x, y + 4)] = color;
    }
}

void updateLEDs() {
    FastLED.show();
}

void display_init() {
    // Initialize FastLED output
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(globalBrightness);
    FastLED.setCorrection(TypicalSMD5050);
    FastLED.setDither(1);
    display_clear();
    display_show();
}

void display_bootTest() {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    display_show();
    delay(350);

    fill_solid(leds, NUM_LEDS, CRGB::Green);
    display_show();
    delay(350);

    fill_solid(leds, NUM_LEDS, CRGB::Blue);
    display_show();
    delay(350);

    fill_solid(leds, NUM_LEDS, CRGB::Black);
    display_show();
}

void display_clear() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
}

void display_show() {
    bool dynamicScene = (display_mode == DISPLAY_MODE_ANIMATION) || message_active || (funClockState.activeEffect != FUN_CLOCK_EFFECT_NONE);
    bool negativeFrame = displayNegativeEnabled || (funClockState.activeEffect == FUN_CLOCK_EFFECT_NEGATIVE);
    uint8_t blendWeight = dynamicScene ? displayAdaptiveBlendWeight() : 0;

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        CRGB src = leds[i];
        displayFrameBackup[i] = src;

        if (negativeFrame) {
            bool wasOff = (src.r == 0 && src.g == 0 && src.b == 0);
            src = wasOff ? CRGB::White : CRGB::Black;
        }

        if (dynamicScene && displayPrevShownFrameValid && blendWeight > 0) {
            src = blend(src, displayPrevShownFrame[i], blendWeight);
        }

        displayRenderFrame[i] = src;

        if (dynamicScene) {
            displayPrevShownFrame[i] = src;
        }
    }

    if (!dynamicScene) {
        displayPrevShownFrameValid = false;
    } else {
        displayPrevShownFrameValid = true;
    }

    bool frameChanged = !displayLastSentFrameValid || !displayFrameEquals(displayRenderFrame, displayLastSentFrame);
    if (displayForceRefresh || frameChanged) {
        for (uint16_t i = 0; i < NUM_LEDS; i++) {
            leds[i] = displayRenderFrame[i];
            displayLastSentFrame[i] = displayRenderFrame[i];
        }
        updateLEDs();
        displayLastSentFrameValid = true;
        displayForceRefresh = false;
    }

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = displayFrameBackup[i];
    }
}

void display_setBrightness(uint8_t brightness) {
    globalBrightness = brightness;
    FastLED.setBrightness(globalBrightness);
    displayForceRefresh = true;
}

void display_setColor(CRGB color) {
    globalColor = color;
}

void display_setNegative(bool enabled) {
    displayNegativeEnabled = enabled;
}

uint16_t XY(uint8_t x, uint8_t y) {
    if (x >= LED_WIDTH || y >= LED_HEIGHT) return NUM_LEDS;

    uint8_t mappedX = MATRIX_FLIP_X ? (LED_WIDTH - 1 - x) : x;
    uint8_t mappedY = MATRIX_FLIP_Y ? (LED_HEIGHT - 1 - y) : y;

#if MATRIX_COLUMN_MAJOR
#if MATRIX_SERPENTINE
    if (mappedX % 2 == 0) {
        return mappedX * LED_HEIGHT + mappedY;
    }
    return mappedX * LED_HEIGHT + (LED_HEIGHT - 1 - mappedY);
#else
    return mappedX * LED_HEIGHT + mappedY;
#endif
#else
#if MATRIX_SERPENTINE
    if (mappedY % 2 == 0) {
        return mappedY * LED_WIDTH + mappedX;
    }
    return mappedY * LED_WIDTH + (LED_WIDTH - 1 - mappedX);
#else
    return mappedY * LED_WIDTH + mappedX;
#endif
#endif
}

void display_drawChar(char c, int16_t x, int16_t y, CRGB color) {
    if (c < 32 || c > 126) return;
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t col = pgm_read_byte(&font5x7[c - 32][i]);
        for (uint8_t j = 0; j < 7; j++) {
            if (col & (1 << j)) {
                int16_t px = x + i;
                int16_t py = y + j;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void display_drawGlyph5x7(const uint8_t glyph[5], int16_t x, int16_t y, CRGB color) {
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t col = glyph[i];
        for (uint8_t j = 0; j < 7; j++) {
            if (col & (1 << j)) {
                int16_t px = x + i;
                int16_t py = y + j;
                if (px >= 0 && px < LED_WIDTH && py >= 0 && py < LED_HEIGHT) {
                    leds[XY(px, py)] = color;
                }
            }
        }
    }
}

static void display_setPixelSafe(int16_t x, int16_t y, CRGB color) {
    if (x >= 0 && x < LED_WIDTH && y >= 0 && y < LED_HEIGHT) {
        leds[XY(x, y)] = color;
    }
}

enum Diacritic : uint8_t { DIA_NONE, DIA_ACUTE, DIA_DOT, DIA_OGONEK, DIA_STROKE };

static void display_drawBaseWithDiacritic(char base, Diacritic mark, int16_t x, int16_t y, CRGB color) {
    if (!base) return;

    display_drawChar(base, x, y, color);

    if (mark == DIA_ACUTE) {
        display_setPixelSafe(x + 3, y + 0, color);
        display_setPixelSafe(x + 4, y + 1, color);
    } else if (mark == DIA_DOT) {
        display_setPixelSafe(x + 2, y + 0, color);
    } else if (mark == DIA_OGONEK) {
        display_setPixelSafe(x + 4, y + 6, color);
        display_setPixelSafe(x + 3, y + 6, color);
    } else if (mark == DIA_STROKE) {
        display_setPixelSafe(x + 1, y + 4, color);
        display_setPixelSafe(x + 2, y + 3, color);
        display_setPixelSafe(x + 3, y + 2, color);
    }
}

static bool display_drawPolishSingleByteChar(unsigned char c, int16_t x, int16_t y, CRGB color) {
    char base = 0;
    Diacritic mark = DIA_NONE;

    switch (c) {
        case 0xA5: case 0xA1: base = 'A'; mark = DIA_OGONEK; break; // Ą (cp1250/iso-8859-2)
        case 0xB9: case 0xB1: base = 'a'; mark = DIA_OGONEK; break; // ą
        case 0xC6: base = 'C'; mark = DIA_ACUTE; break;              // Ć
        case 0xE6: base = 'c'; mark = DIA_ACUTE; break;              // ć
        case 0xCA: base = 'E'; mark = DIA_OGONEK; break;             // Ę
        case 0xEA: base = 'e'; mark = DIA_OGONEK; break;             // ę
        case 0xA3: base = 'L'; mark = DIA_STROKE; break;             // Ł
        case 0xB3: base = 'l'; mark = DIA_STROKE; break;             // ł
        case 0xD1: base = 'N'; mark = DIA_ACUTE; break;              // Ń
        case 0xF1: base = 'n'; mark = DIA_ACUTE; break;              // ń
        case 0xD3: base = 'O'; mark = DIA_ACUTE; break;              // Ó
        case 0xF3: base = 'o'; mark = DIA_ACUTE; break;              // ó
        case 0x8C: case 0xA6: base = 'S'; mark = DIA_ACUTE; break;   // Ś (cp1250/iso-8859-2)
        case 0x9C: case 0xB6: base = 's'; mark = DIA_ACUTE; break;   // ś
        case 0x8F: case 0xAC: base = 'Z'; mark = DIA_ACUTE; break;   // Ź
        case 0x9F: case 0xBC: base = 'z'; mark = DIA_ACUTE; break;   // ź
        case 0xAF: base = 'Z'; mark = DIA_DOT; break;                // Ż
        case 0xBF: base = 'z'; mark = DIA_DOT; break;                // ż
        default: break;
    }

    if (!base) return false;
    display_drawBaseWithDiacritic(base, mark, x, y, color);
    return true;
}

static bool display_drawPolishUtf8Char(const char* text, size_t& index, int16_t x, int16_t y, CRGB color) {
    char base = 0;
    Diacritic mark = DIA_NONE;

    unsigned char b1 = static_cast<unsigned char>(text[index]);
    unsigned char b2 = static_cast<unsigned char>(text[index + 1]);

    if (b1 == 0xC4) {
        if (b2 == 0x84) { base = 'A'; mark = DIA_OGONEK; }     // Ą
        else if (b2 == 0x85) { base = 'a'; mark = DIA_OGONEK; } // ą
        else if (b2 == 0x86) { base = 'C'; mark = DIA_ACUTE; }  // Ć
        else if (b2 == 0x87) { base = 'c'; mark = DIA_ACUTE; }  // ć
        else if (b2 == 0x98) { base = 'E'; mark = DIA_OGONEK; } // Ę
        else if (b2 == 0x99) { base = 'e'; mark = DIA_OGONEK; } // ę
    } else if (b1 == 0xC5) {
        if (b2 == 0x81) { base = 'L'; mark = DIA_STROKE; }      // Ł
        else if (b2 == 0x82) { base = 'l'; mark = DIA_STROKE; }  // ł
        else if (b2 == 0x83) { base = 'N'; mark = DIA_ACUTE; }   // Ń
        else if (b2 == 0x84) { base = 'n'; mark = DIA_ACUTE; }   // ń
        else if (b2 == 0x9A) { base = 'S'; mark = DIA_ACUTE; }   // Ś
        else if (b2 == 0x9B) { base = 's'; mark = DIA_ACUTE; }   // ś
        else if (b2 == 0xB9) { base = 'Z'; mark = DIA_ACUTE; }   // Ź
        else if (b2 == 0xBA) { base = 'z'; mark = DIA_ACUTE; }   // ź
        else if (b2 == 0xBB) { base = 'Z'; mark = DIA_DOT; }     // Ż
        else if (b2 == 0xBC) { base = 'z'; mark = DIA_DOT; }     // ż
    } else if (b1 == 0xC3) {
        if (b2 == 0x93) { base = 'O'; mark = DIA_ACUTE; }        // Ó
        else if (b2 == 0xB3) { base = 'o'; mark = DIA_ACUTE; }    // ó
    }

    if (!base) {
        return false;
    }

    display_drawBaseWithDiacritic(base, mark, x, y, color);

    index += 2;
    return true;
}

void display_drawText(const char* text, int16_t offset, CRGB color) {
    int16_t y = (LED_HEIGHT > 7) ? ((LED_HEIGHT - 7) / 2) : 0;
    int16_t x = offset;
    for (size_t i = 0; text && text[i] != '\0'; ) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if (c >= 32 && c <= 126) {
            display_drawChar((char)c, x, y, color);
            i++;
            x += 6;
            continue;
        }

        if (display_drawPolishUtf8Char(text, i, x, y, color)) {
            x += 6;
            continue;
        }

        if (display_drawPolishSingleByteChar(c, x, y, color)) {
            i++;
            x += 6;
            continue;
        }

        i++;
        x += 6;
    }
}

void display_drawMessage(const char* text, int16_t offset, CRGB color) {
    display_drawText(text, offset, color);
}

void display_drawClock(uint8_t hour, uint8_t minute, uint8_t second, bool colon, bool showSeconds) {
    display_clear();

    uint8_t h1 = hour / 10;
    uint8_t h2 = hour % 10;
    uint8_t m1 = minute / 10;
    uint8_t m2 = minute % 10;

    if (showSeconds) {
        funClockUpdateState();

        uint8_t s1 = second / 10;
        uint8_t s2 = second % 10;

        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MATRIX_FONT) {
            const int16_t digitW = 4;
            const int16_t colonW = 1;
            const int16_t gap = 0;
            const int16_t totalW =
                digitW + gap +
                digitW + gap +
                colonW + gap +
                digitW + gap +
                digitW + gap +
                colonW + gap +
                digitW + gap +
                digitW;
            int16_t x = (LED_WIDTH > totalW) ? ((LED_WIDTH - totalW) / 2) : 0;
            int16_t y = (LED_HEIGHT > 7) ? ((LED_HEIGHT - 7) / 2) : 0;

            const int16_t symbolX[8] = {
                x,
                (int16_t)(x + digitW + gap),
                (int16_t)(x + 2 * (digitW + gap)),
                (int16_t)(x + 2 * (digitW + gap) + colonW + gap),
                (int16_t)(x + 3 * (digitW + gap) + colonW + gap),
                (int16_t)(x + 4 * (digitW + gap) + colonW + gap),
                (int16_t)(x + 4 * (digitW + gap) + 2 * (colonW + gap)),
                (int16_t)(x + 5 * (digitW + gap) + 2 * (colonW + gap))
            };

            CRGB matrixColor[8];
            uint32_t nowMs = millis();
            for (uint8_t i = 0; i < 8; i++) {
                uint8_t pulse = (uint8_t)(170 + (sin8((uint8_t)((nowMs / 8U) + i * 31U)) >> 1));
                matrixColor[i] = CRGB(pulse, (uint8_t)(pulse / 7), 0);
            }

            uint8_t digits[6] = {h1, h2, m1, m2, s1, s2};
            const uint8_t symbolToDigitIdx[8] = {0, 1, 0xFF, 2, 3, 0xFF, 4, 5};

            for (uint8_t i = 0; i < 8; i++) {
                if (i == 2 || i == 5) {
                    bool colonVisible = colon && ((((nowMs / 110U) + i) % 3U) != 0U);
                    drawClockColonClassic(symbolX[i], y, colonVisible, matrixColor[i]);
                    continue;
                }

                uint8_t dIdx = symbolToDigitIdx[i];
                uint8_t glyph = digits[dIdx];

                uint8_t glitchSeed = (uint8_t)((nowMs / 95U) + i * 19U);
                bool glitchNow = (glitchSeed % 9U) == 0U;
                if (glitchNow) {
                    glyph = (uint8_t)((glyph + 3U + (glitchSeed % 7U)) % 10U);
                }

                int8_t jitterY = glitchNow ? ((glitchSeed & 0x01U) ? 1 : -1) : 0;
                drawPredatorGlyph4x7(glyph, symbolX[i], y + jitterY, matrixColor[i]);
            }
            return;
        }

        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MATRIX_SIDEWAYS) {
            const int16_t glyphW = 7;
            const int16_t glyphH = 4;
            const int16_t gap = 1;
            const int16_t digitsPerRow = 4;
            const int16_t rowW = glyphW * digitsPerRow + gap * (digitsPerRow - 1);
            const int16_t totalH = glyphH * 2;
            int16_t yTop = (LED_HEIGHT > totalH) ? ((LED_HEIGHT - totalH) / 2) : 0;

            uint32_t elapsed = millis() - funClockState.effectStartMs;
            uint32_t duration = funClockState.effectDurationMs;
            if (duration == 0) duration = 1;
            if (elapsed > duration) elapsed = duration;

            int16_t scrollRange = rowW + LED_WIDTH;
            int16_t scroll = (int16_t)(((uint32_t)scrollRange * elapsed) / duration);
            int16_t xBase = LED_WIDTH - scroll;

            uint8_t pulseTop = (uint8_t)(160 + (sin8((uint8_t)(millis() / 8U)) >> 1));
            uint8_t pulseBottom = (uint8_t)(160 + (sin8((uint8_t)(millis() / 8U + 77U)) >> 1));
            CRGB topColor = CRGB(0, pulseTop, 0);
            CRGB bottomColor = CRGB(0, pulseBottom, 0);

            uint8_t rowTop[4] = {h1, h2, m1, m2};
            uint8_t rowBottom[4] = {m1, m2, s1, s2};

            for (uint8_t i = 0; i < 4; i++) {
                int16_t px = xBase + (int16_t)i * (glyphW + gap);
                drawPredatorGlyph7x4Sideways(rowTop[i], px, yTop, topColor);
                drawPredatorGlyph7x4Sideways(rowBottom[i], px, yTop + glyphH, bottomColor);
            }
            return;
        }

        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_FULL_ROTATE) {
            const int16_t digitW = 4;
            const int16_t colonW = 1;
            const int16_t gap_h1_h2 = 0;
            const int16_t gap_h2_colon = 1;
            const int16_t gap_colon_m1 = 1;
            const int16_t gap_m1_m2 = 1;
            const int16_t gap_m2_colon = 1;
            const int16_t gap_colon_s1 = 1;
            const int16_t gap_s1_s2 = 1;
            int16_t x = 0;
            int16_t y = (LED_HEIGHT > 7) ? ((LED_HEIGHT - 7) / 2) : 0;

            const int16_t tweakX[8] = {0, 1, 1, 1, 1, 1, 1, 0};
            int16_t symbolX[8];
            symbolX[0] = x;
            x += digitW + gap_h1_h2;
            symbolX[1] = x;
            x += digitW + gap_h2_colon;
            symbolX[2] = x;
            x += colonW + gap_colon_m1;
            symbolX[3] = x;
            x += digitW + gap_m1_m2;
            symbolX[4] = x;
            x += digitW + gap_m2_colon;
            symbolX[5] = x;
            x += colonW + gap_colon_s1;
            symbolX[6] = x;
            x += digitW + gap_s1_s2;
            symbolX[7] = x;

            drawClockDigit4x7(h1, symbolX[0] + tweakX[0], y + 1, clock_color);
            drawClockDigit4x7(h2, symbolX[1] + tweakX[1], y, clock_color);
            drawClockColonClassic(symbolX[2] + tweakX[2], y, colon, clock_color);
            drawClockDigit4x7(m1, symbolX[3] + tweakX[3], y + 1, clock_color);
            drawClockDigit4x7(m2, symbolX[4] + tweakX[4], y, clock_color);
            drawClockColonClassic(symbolX[5] + tweakX[5], y, colon, clock_color);
            drawClockDigit4x7(s1, symbolX[6] + tweakX[6], y + 1, clock_color);
            drawClockDigit4x7(s2, symbolX[7] + tweakX[7], y, clock_color);

            CRGB src[NUM_LEDS];
            for (uint16_t i = 0; i < NUM_LEDS; i++) {
                src[i] = leds[i];
                leds[i] = CRGB::Black;
            }

            uint32_t elapsed = millis() - funClockState.effectStartMs;
            uint32_t duration = funClockState.effectDurationMs;
            if (duration == 0) duration = 1;
            float progress = (float)elapsed / (float)duration;
            if (progress > 1.0f) progress = 1.0f;
            float angle = -progress * 2.0f * PI;

            float cx = ((float)LED_WIDTH - 1.0f) * 0.5f;
            float cy = ((float)LED_HEIGHT - 1.0f) * 0.5f;
            float ca = cosf(angle);
            float sa = sinf(angle);

            for (int16_t sy = 0; sy < LED_HEIGHT; sy++) {
                for (int16_t sx = 0; sx < LED_WIDTH; sx++) {
                    uint16_t srcIdx = XY((uint8_t)sx, (uint8_t)sy);
                    if (srcIdx >= NUM_LEDS) continue;
                    CRGB p = src[srcIdx];
                    if (p.r == 0 && p.g == 0 && p.b == 0) continue;

                    float dx = (float)sx - cx;
                    float dy = (float)sy - cy;
                    float rx = cx + (dx * ca) + (dy * sa);
                    float ry = cy - (dx * sa) + (dy * ca);

                    int16_t tx = (int16_t)roundf(rx);
                    int16_t ty = (int16_t)roundf(ry);
                    if (tx >= 0 && tx < LED_WIDTH && ty >= 0 && ty < LED_HEIGHT) {
                        leds[XY((uint8_t)tx, (uint8_t)ty)] = p;
                    }
                }
            }
            return;
        }

        const int16_t digitW = 4;
        const int16_t colonW = 1;
        const int16_t gap_h1_h2 = 0;
        const int16_t gap_h2_colon = 1;
        const int16_t gap_colon_m1 = 1;
        const int16_t gap_m1_m2 = 1;
        const int16_t gap_m2_colon = 1;
        const int16_t gap_colon_s1 = 1;
        const int16_t gap_s1_s2 = 1;
        const int16_t totalW =
            digitW + gap_h1_h2 +
            digitW + gap_h2_colon +
            colonW + gap_colon_m1 +
            digitW + gap_m1_m2 +
            digitW + gap_m2_colon +
            colonW + gap_colon_s1 +
            digitW + gap_s1_s2 +
            digitW;
        int16_t x = 0;
        int16_t y = (LED_HEIGHT > 7) ? ((LED_HEIGHT - 7) / 2) : 0;

        const int16_t tweakX[8] = {0, 1, 1, 1, 1, 1, 1, 0};
        const int16_t symbolW[8] = {digitW, digitW, colonW, digitW, digitW, colonW, digitW, digitW};
        int16_t symbolX[8];

        symbolX[0] = x;
        x += digitW + gap_h1_h2;
        symbolX[1] = x;
        x += digitW + gap_h2_colon;
        symbolX[2] = x;
        x += colonW + gap_colon_m1;
        symbolX[3] = x;
        x += digitW + gap_m1_m2;
        symbolX[4] = x;
        x += digitW + gap_m2_colon;
        symbolX[5] = x;
        x += colonW + gap_colon_s1;
        symbolX[6] = x;
        x += digitW + gap_s1_s2;
        symbolX[7] = x;

        int16_t x0 = funClockApplyMirrorX(symbolX[0] + tweakX[0], symbolW[0]);
        int16_t x1 = funClockApplyMirrorX(symbolX[1] + tweakX[1], symbolW[1]);
        int16_t x2 = funClockApplyMirrorX(symbolX[2] + tweakX[2], symbolW[2]);
        int16_t x3 = funClockApplyMirrorX(symbolX[3] + tweakX[3], symbolW[3]);
        int16_t x4 = funClockApplyMirrorX(symbolX[4] + tweakX[4], symbolW[4]);
        int16_t x5 = funClockApplyMirrorX(symbolX[5] + tweakX[5], symbolW[5]);
        int16_t x6 = funClockApplyMirrorX(symbolX[6] + tweakX[6], symbolW[6]);
        int16_t x7 = funClockApplyMirrorX(symbolX[7] + tweakX[7], symbolW[7]);

        bool drawHours = true;
        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_HOURS_SLIDE) {
            const uint32_t phaseOutMs = 1200U;
            const uint32_t phaseGapMs = 2000U;
            const uint32_t phaseInMs = 1200U;
            uint32_t elapsed = millis() - funClockState.effectStartMs;
            const int16_t offLeftX = -digitW;
            const int16_t inStartX0 = LED_WIDTH;
            const int16_t inStartX1 = LED_WIDTH + digitW;

            if (elapsed < phaseOutMs) {
                float p = (float)elapsed / (float)phaseOutMs;
                x0 = (int16_t)roundf((float)x0 + (offLeftX - x0) * p);
                x1 = (int16_t)roundf((float)x1 + (offLeftX - x1) * p);
            } else if (elapsed < (phaseOutMs + phaseGapMs)) {
                drawHours = false;
            } else {
                uint32_t inElapsed = elapsed - (phaseOutMs + phaseGapMs);
                if (inElapsed > phaseInMs) inElapsed = phaseInMs;
                float p = (float)inElapsed / (float)phaseInMs;
                x0 = (int16_t)roundf((float)inStartX0 + (symbolX[0] + tweakX[0] - inStartX0) * p);
                x1 = (int16_t)roundf((float)inStartX1 + (symbolX[1] + tweakX[1] - inStartX1) * p);
            }
        }

        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MIDDLE_SWAP) {
            const uint32_t perDigitDelayMs = 220U;
            const uint32_t phaseMs = 520U;

            auto applyOutInSlide = [phaseMs](int16_t baseX, int16_t outX, int16_t inStartX, uint32_t t) -> int16_t {
                if (t < phaseMs) {
                    float p = (float)t / (float)phaseMs;
                    return (int16_t)roundf((float)baseX + ((float)outX - (float)baseX) * p);
                }
                if (t < (phaseMs * 2U)) {
                    float p = (float)(t - phaseMs) / (float)phaseMs;
                    return (int16_t)roundf((float)inStartX + ((float)baseX - (float)inStartX) * p);
                }
                return baseX;
            };

            uint32_t elapsed = millis() - funClockState.effectStartMs;
            int16_t* digitSlots[6] = {&x0, &x1, &x3, &x4, &x6, &x7};
            for (uint8_t i = 0; i < 6; i++) {
                uint32_t startDelay = (uint32_t)i * perDigitDelayMs;
                if (elapsed < startDelay) {
                    continue;
                }
                uint32_t t = elapsed - startDelay;
                bool slideRight = (i % 2U) == 0U;
                int16_t outX = slideRight ? (LED_WIDTH + digitW) : -digitW;
                int16_t inStartX = slideRight ? -digitW : (LED_WIDTH + digitW);
                *digitSlots[i] = applyOutInSlide(*digitSlots[i], outX, inStartX, t);
            }
        }

        bool pileupActive = (funClockState.activeEffect == FUN_CLOCK_EFFECT_PILEUP);
        int8_t pileupYOffset[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        uint8_t pileupMode[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // 0=normal,1=mirror,2=upside,3=rot180

        if (pileupActive) {
            uint32_t elapsed = millis() - funClockState.effectStartMs;
            uint32_t duration = funClockState.effectDurationMs;
            if (duration == 0U) duration = 1U;

            int16_t* slots[8] = {&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7};
            int16_t baseX[8] = {x0, x1, x2, x3, x4, x5, x6, x7};
            const int16_t pileCenterX = x0;

            for (uint8_t i = 0; i < 8; i++) {
                uint32_t startDelay = (uint32_t)i * 90U;
                if (elapsed < startDelay) continue;

                uint32_t localElapsed = elapsed - startDelay;
                uint32_t localDuration = (duration > startDelay) ? (duration - startDelay) : 1U;
                float p = (float)localElapsed / (float)localDuration;
                if (p > 1.0f) p = 1.0f;
                float ease = 1.0f - (1.0f - p) * (1.0f - p);

                int16_t crashTargetX = (int16_t)(pileCenterX + ((int16_t)(i % 4) - 1));
                *slots[i] = (int16_t)roundf((float)baseX[i] + ((float)crashTargetX - (float)baseX[i]) * ease);

                int8_t down = (int8_t)min(3, (int)(i / 2));
                pileupYOffset[i] = (int8_t)roundf((float)down * ease);
                if (p > 0.30f) {
                    pileupMode[i] = (uint8_t)((i + (p > 0.72f ? 1 : 0)) % 4);
                }
                if (p > 0.78f && ((i + (uint8_t)(elapsed / 90U)) % 2U) == 0U) {
                    pileupYOffset[i] = (int8_t)(pileupYOffset[i] + 1);
                }
            }
        }

        CRGB symbolColor[8] = {
            clock_color, clock_color, clock_color, clock_color,
            clock_color, clock_color, clock_color, clock_color
        };
        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_RAINBOW) {
            uint32_t nowUs = micros();
            uint32_t dtUs = nowUs - funClockRainbowLastUs;
            if (dtUs > 100000U) dtUs = 100000U;
            funClockRainbowLastUs = nowUs;
            funClockRainbowHuePhaseQ16 += (uint32_t)(((uint64_t)dtUs * 1536ULL) / 1000ULL);

            uint16_t baseHueQ8 = (uint16_t)((funClockRainbowHuePhaseQ16 >> 8) & 0xFFFFU);
            const uint16_t symbolHueStepQ8 = 14U << 8;
            uint32_t dtMs = (dtUs + 500U) / 1000U;
            uint8_t blendAmount = (uint8_t)constrain((int)(dtMs * 16U), 8, 72);

            for (uint8_t i = 0; i < 8; i++) {
                uint16_t symbolHueQ8 = (uint16_t)(baseHueQ8 + (uint16_t)i * symbolHueStepQ8);
                CRGB target = funClockRainbowColorFromHueQ8(symbolHueQ8, 190, 220);
                if (!funClockRainbowSmoothedInitialized) {
                    funClockRainbowSmoothed[i] = target;
                } else {
                    nblend(funClockRainbowSmoothed[i], target, blendAmount);
                }
                symbolColor[i] = funClockRainbowSmoothed[i];
            }
            if (!funClockRainbowSmoothedInitialized) {
                funClockRainbowSmoothedInitialized = true;
            }
        }

        if (pileupActive) {
            auto drawDigitWithMode = [&](uint8_t digit, int16_t dx, int16_t dy, CRGB c, uint8_t mode) {
                if (mode == 1) {
                    drawClockDigit4x7Mirrored(digit, dx, dy, c);
                } else if (mode == 2) {
                    drawClockDigit4x7UpsideDown(digit, dx, dy, c);
                } else if (mode == 3) {
                    drawClockDigit4x7Rotate180(digit, dx, dy, c);
                } else {
                    drawClockDigit4x7(digit, dx, dy, c);
                }
            };

            drawDigitWithMode(h1, x0, y + 1 + digitOffset[0] + pileupYOffset[0], symbolColor[0], pileupMode[0]);
            drawDigitWithMode(h2, x1, y + digitOffset[1] + pileupYOffset[1], symbolColor[1], pileupMode[1]);
            if (pileupMode[2] == 2 || pileupMode[2] == 3) {
                drawClockColonClassicUpsideDown(x2, y + digitOffset[2] + pileupYOffset[2], colon, symbolColor[2]);
            } else {
                drawClockColonClassic(x2, y + digitOffset[2] + pileupYOffset[2], colon, symbolColor[2]);
            }
            drawDigitWithMode(m1, x3, y + 1 + digitOffset[3] + pileupYOffset[3], symbolColor[3], pileupMode[3]);
            drawDigitWithMode(m2, x4, y + digitOffset[4] + pileupYOffset[4], symbolColor[4], pileupMode[4]);
            if (pileupMode[5] == 2 || pileupMode[5] == 3) {
                drawClockColonClassicUpsideDown(x5, y + digitOffset[5] + pileupYOffset[5], colon, symbolColor[5]);
            } else {
                drawClockColonClassic(x5, y + digitOffset[5] + pileupYOffset[5], colon, symbolColor[5]);
            }
            drawDigitWithMode(s1, x6, y + 1 + digitOffset[6] + pileupYOffset[6], symbolColor[6], pileupMode[6]);
            drawDigitWithMode(s2, x7, y + digitOffset[7] + pileupYOffset[7], symbolColor[7], pileupMode[7]);
            return;
        }

        if (drawHours) {
            if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MIRROR) {
                drawClockDigit4x7Mirrored(h1, x0, y + 1 + digitOffset[0], symbolColor[0]);
                drawClockDigit4x7Mirrored(h2, x1, y + digitOffset[1], symbolColor[1]);
            } else if (funClockState.activeEffect == FUN_CLOCK_EFFECT_ROTATE_180) {
                drawClockDigit4x7Rotate180(h1, x0, y + 1 + digitOffset[0], symbolColor[0]);
                drawClockDigit4x7Rotate180(h2, x1, y + digitOffset[1], symbolColor[1]);
            } else if (funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
                drawClockDigit4x7UpsideDown(h1, x0, y + 1 + digitOffset[0], symbolColor[0]);
                drawClockDigit4x7UpsideDown(h2, x1, y + digitOffset[1], symbolColor[1]);
            } else {
                drawClockDigit4x7(h1, x0, y + 1 + digitOffset[0], symbolColor[0]);
                drawClockDigit4x7(h2, x1, y + digitOffset[1], symbolColor[1]);
            }
        }
        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
            drawClockColonClassicUpsideDown(x2, y + digitOffset[2], colon, symbolColor[2]);
        } else {
            drawClockColonClassic(x2, y + digitOffset[2], colon, symbolColor[2]);
        }
        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MIRROR) {
            drawClockDigit4x7Mirrored(m1, x3, y + 1 + digitOffset[3], symbolColor[3]);
            drawClockDigit4x7Mirrored(m2, x4, y + digitOffset[4], symbolColor[4]);
        } else if (funClockState.activeEffect == FUN_CLOCK_EFFECT_ROTATE_180) {
            drawClockDigit4x7Rotate180(m1, x3, y + 1 + digitOffset[3], symbolColor[3]);
            drawClockDigit4x7Rotate180(m2, x4, y + digitOffset[4], symbolColor[4]);
        } else if (funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
            drawClockDigit4x7UpsideDown(m1, x3, y + 1 + digitOffset[3], symbolColor[3]);
            drawClockDigit4x7UpsideDown(m2, x4, y + digitOffset[4], symbolColor[4]);
        } else {
            drawClockDigit4x7(m1, x3, y + 1 + digitOffset[3], symbolColor[3]);
            drawClockDigit4x7(m2, x4, y + digitOffset[4], symbolColor[4]);
        }
        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
            drawClockColonClassicUpsideDown(x5, y + digitOffset[5], colon, symbolColor[5]);
        } else {
            drawClockColonClassic(x5, y + digitOffset[5], colon, symbolColor[5]);
        }
        if (funClockState.activeEffect == FUN_CLOCK_EFFECT_MIRROR) {
            drawClockDigit4x7Mirrored(s1, x6, y + 1 + digitOffset[6], symbolColor[6]);
            drawClockDigit4x7Mirrored(s2, x7, y + digitOffset[7], symbolColor[7]);
        } else if (funClockState.activeEffect == FUN_CLOCK_EFFECT_ROTATE_180) {
            drawClockDigit4x7Rotate180(s1, x6, y + 1 + digitOffset[6], symbolColor[6]);
            drawClockDigit4x7Rotate180(s2, x7, y + digitOffset[7], symbolColor[7]);
        } else if (funClockState.activeEffect == FUN_CLOCK_EFFECT_UPSIDE_DOWN) {
            drawClockDigit4x7UpsideDown(s1, x6, y + 1 + digitOffset[6], symbolColor[6]);
            drawClockDigit4x7UpsideDown(s2, x7, y + digitOffset[7], symbolColor[7]);
        } else {
            drawClockDigit4x7(s1, x6, y + 1 + digitOffset[6], symbolColor[6]);
            drawClockDigit4x7(s2, x7, y + digitOffset[7], symbolColor[7]);
        }
        return;
    }

#if CLOCK_FONT_STYLE == 2

    const int16_t digitW = 6;
    const int16_t gap = 1;
    const int16_t colonW = 2;
    const int16_t totalW = digitW + gap + digitW + gap + colonW + gap + digitW + gap + digitW;
    int16_t x = (LED_WIDTH > totalW) ? ((LED_WIDTH - totalW) / 2) : 0;
    int16_t y = 0;

    drawClockDigit3x4_2x2(h1, x, y, clock_color);
    x += digitW + gap;
    drawClockDigit3x4_2x2(h2, x, y, clock_color);
    x += digitW + gap;
    drawClockColon(x, y, colon, clock_color);
    x += colonW + gap;
    drawClockDigit3x4_2x2(m1, x, y, clock_color);
    x += digitW + gap;
    drawClockDigit3x4_2x2(m2, x, y, clock_color);

#else

    const int16_t digitW = 4;
    const int16_t gap = 1;
    const int16_t colonW = 1;
    const int16_t totalW = digitW + gap + digitW + gap + colonW + gap + digitW + gap + digitW;
    int16_t x = (LED_WIDTH > totalW) ? ((LED_WIDTH - totalW) / 2) : 0;
    int16_t y = (LED_HEIGHT > 7) ? ((LED_HEIGHT - 7) / 2) : 0;

    drawClockDigit4x7(h1, x, y, clock_color);
    x += digitW + gap;
    drawClockDigit4x7(h2, x, y, clock_color);
    x += digitW + gap;
    drawClockColonClassic(x, y, colon, clock_color);
    x += colonW + gap;
    drawClockDigit4x7(m1, x, y, clock_color);
    x += digitW + gap;
    drawClockDigit4x7(m2, x, y, clock_color);

#endif
}

void display_drawLamp() {
    fill_solid(leds, NUM_LEDS, globalColor);
}

void display_drawCornerCalibration() {
    display_clear();

    leds[XY(0, 0)] = CRGB::Red;
    leds[XY(LED_WIDTH - 1, 0)] = CRGB::Red;
    leds[XY(0, LED_HEIGHT - 1)] = CRGB::Green;
    leds[XY(LED_WIDTH - 1, LED_HEIGHT - 1)] = CRGB::Green;

    display_show();
}

// === Animation functions ===

void anim_rainbow() {
    display_clear();
    uint32_t now = millis();
    
    if (now - last_animation_update > 30 / animation_speed) {
        animation_hue += 0.5;
        if (animation_hue >= 255) animation_hue = 0;
        last_animation_update = now;
    }
    
    uint8_t hue_byte = (uint8_t)animation_hue;
    CHSV base = rgb2hsv_approximate(animation_color);
    uint8_t baseSat = base.s == 0 ? 255 : base.s;
    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t hue = (base.h + hue_byte + (i / 4)) % 256;
        leds[i] = CHSV(hue, baseSat, 255);
    }
}

void anim_fade() {
    display_clear();
    uint32_t now = millis();
    
    if (now - last_animation_update > 20 / animation_speed) {
        animation_hue += 1;
        if (animation_hue >= 255) animation_hue = 0;
        last_animation_update = now;
    }
    
    uint8_t brightness = 128 + (128 * sin(animation_hue / 255.0 * PI)) / 2;
    CRGB color = animation_color;
    color.nscale8_video(brightness);
    fill_solid(leds, NUM_LEDS, color);
}

void anim_wave() {
    display_clear();
    uint32_t now = millis();
    
    if (now - last_animation_update > 25 / animation_speed) {
        animation_hue += 2;
        if (animation_hue >= 255) animation_hue = 0;
        last_animation_update = now;
    }
    
    for (int x = 0; x < LED_WIDTH; x++) {
        uint8_t y_pos = LED_HEIGHT / 2 + (2 * sin((x + animation_hue) / 255.0 * PI * 2));
        if (y_pos < LED_HEIGHT) {
            leds[XY(x, y_pos)] = animation_color;
        }
    }
}

void anim_pulse() {
    display_clear();
    uint32_t now = millis();
    
    if (now - last_animation_update > 30 / animation_speed) {
        animation_hue += 3;
        if (animation_hue >= 255) animation_hue = 0;
        last_animation_update = now;
    }
    
    uint8_t brightness = 100 + (155 * abs(sin(animation_hue / 255.0 * PI)));
    CRGB color = animation_color;
    color.nscale8_video(brightness);
    fill_solid(leds, NUM_LEDS, color);
}

void anim_night() {
    display_clear();
    uint32_t now = millis();
    
    if (now - last_animation_update > 50 / animation_speed) {
        animation_hue += 1;
        if (animation_hue >= 255) animation_hue = 0;
        last_animation_update = now;
    }
    
    uint8_t brightness = 40;
    CRGB color = animation_color;
    color.nscale8_video(brightness);
    fill_solid(leds, NUM_LEDS, color);
}
