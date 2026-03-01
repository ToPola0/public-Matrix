#include "mqtt_manager.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "clock.h"
#include "display.h"
#include "effects.h"
#include "quotes.h"

static const char* kDefaultApPassword = "12345678";
static const char* kHiddenApPasswordPlaceholder = "********";

static WiFiClient mqttNetClient;
static PubSubClient mqttClient(mqttNetClient);

static bool mqttEnabled = false;
static String mqttHost;
static uint16_t mqttPort = 1883;
static String mqttUser;
static String mqttPassword;
static String mqttDiscoveryPrefix = "homeassistant";

static String mqttDeviceId;
static String mqttClientId;
static String mqttBaseTopic;
static String mqttAvailabilityTopic;
static String mqttStateTopic;
static String mqttLightStateTopic;
static String mqttLightCommandTopic;
static String mqttBrightnessStateTopic;
static String mqttBrightnessCommandTopic;
static String mqttColorStateTopic;
static String mqttColorCommandTopic;
static String mqttLampStateTopic;
static String mqttLampCommandTopic;
static String mqttModeStateTopic;
static String mqttModeCommandTopic;
static String mqttApPasswordShowStateTopic;
static String mqttApPasswordShowCommandTopic;
static String mqttApPasswordStateTopic;
static String mqttApPasswordCommandTopic;
static String mqttQuoteTriggerCommandTopic;

struct MqttHaEntityConfig {
    bool enabled;
    String entity;
    String topic;
    String displayName;
    String unit;
    uint16_t durationSec;
    String lastValue;
    bool hasValue;
};

static const uint8_t kMaxHaEntities = 10;
static MqttHaEntityConfig mqttHaEntities[kMaxHaEntities];
static uint8_t mqttHaEntityCount = 0;
static uint8_t mqttHaRotationIndex = 0;
static uint32_t mqttHaNextDisplayMs = 0;

static bool mqttLampConfigLoaded = false;
static bool mqttLampEnabled = false;
static uint8_t mqttLampBrightness = 180;
static CRGB mqttLampColor = CRGB::White;
static bool mqttApPasswordVisible = false;
static String mqttApPasswordCached = String(kDefaultApPassword);

static bool mqttDiscoveryPublished = false;
static uint32_t mqttLastConnectAttemptMs = 0;
static uint32_t mqttLastStatePublishMs = 0;

static void mqttPublishLightState();
static void mqttPublishExtraStates();
static void mqttPublishLampState();
static void mqttPublishModeState();
static void mqttPublishApPasswordShowState();
static void mqttPublishApPasswordState();
static void mqttPublishDiscoveryCleanup();
static void mqttLoadApPasswordConfig();
static void mqttRefreshApPasswordCache();
static bool mqttTriggerQuoteTest();
static void mqttLoadHaEntitiesConfigFromPrefs();
static bool mqttApplyHaEntitiesConfigJson(const String& json, bool persistToPrefs);
static void mqttSubscribeHaEntityTopics();
static bool mqttTopicMatchesHaEntity(const String& topic, uint8_t& outIndex);
static bool mqttHasAnyHaEntityWithValue();
static String mqttResolveHaEntityTopic(const String& entityName, const String& fallbackTopic);

static bool mqttTriggerQuoteTest() {
    if (numQuotes == 0) {
        return false;
    }

    char* selectedQuote = quotes_getRandom();
    if (!selectedQuote || selectedQuote[0] == '\0') {
        return false;
    }

    message_active = false;
    display_mode = DISPLAY_MODE_QUOTE;
    effects_quotes(selectedQuote);
    display_enabled = true;

    Serial.printf("[MQTT] Trigger quote test: %s\n", selectedQuote);
    return true;
}

static bool mqttIsApPasswordDisplayEnabled() {
    return mqttApPasswordVisible;
}

static String mqttGetApPassword() {
    return mqttApPasswordCached;
}

static void mqttRefreshApPasswordCache() {
    Preferences prefs;
    if (!prefs.begin("wifi", true)) {
        mqttApPasswordCached = String(kDefaultApPassword);
        return;
    }
    String password = prefs.getString("apPassword", kDefaultApPassword);
    prefs.end();

    password.trim();
    if (password.length() < 8 || password.length() > 63) {
        password = String(kDefaultApPassword);
    }
    mqttApPasswordCached = password;
}

static bool mqttApplyHaEntitiesConfigJson(const String& json, bool persistToPrefs) {
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonArray>()) {
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    uint8_t count = 0;
    for (JsonObject item : arr) {
        if (count >= kMaxHaEntities) break;

        String entityName = String((const char*)(item["entity"] | ""));
        if (entityName.length() == 0) {
            entityName = String((const char*)(item["topic"] | ""));
        }
        entityName.trim();

        String fallbackTopic = String((const char*)(item["topic"] | ""));
        fallbackTopic.trim();
        String topic = mqttResolveHaEntityTopic(entityName, fallbackTopic);
        if (topic.length() == 0) continue;

        mqttHaEntities[count].enabled = (item["enabled"] | true);
        mqttHaEntities[count].entity = entityName;
        mqttHaEntities[count].topic = topic;
        mqttHaEntities[count].displayName = String((const char*)(item["displayName"] | item["name"] | ""));
        mqttHaEntities[count].unit = String((const char*)(item["unit"] | item["suffix"] | ""));
        mqttHaEntities[count].unit.trim();
        mqttHaEntities[count].durationSec = (uint16_t)constrain((int)(item["durationSec"] | 8), 2, 120);
        mqttHaEntities[count].lastValue = "";
        mqttHaEntities[count].hasValue = false;
        count++;
    }

    mqttHaEntityCount = count;
    mqttHaRotationIndex = 0;
    mqttHaNextDisplayMs = 0;

    if (persistToPrefs) {
        Preferences prefs;
        if (prefs.begin("wifi", false)) {
            prefs.putString("haEntitiesCfg", json);
            prefs.end();
        }
    }

    if (mqttClient.connected()) {
        mqttSubscribeHaEntityTopics();
    }
    return true;
}

static String mqttResolveHaEntityTopic(const String& entityName, const String& fallbackTopic) {
    String topic = fallbackTopic;
    topic.trim();
    if (topic.length() > 0) {
        return topic;
    }

    String entity = entityName;
    entity.trim();
    if (entity.length() == 0) {
        return "";
    }

    if (entity.indexOf('/') >= 0) {
        return entity;
    }

    if (entity.indexOf('.') >= 0) {
        String converted = entity;
        converted.replace('.', '/');
        return String("homeassistant/") + converted + "/state";
    }

    return entity;
}

static void mqttLoadHaEntitiesConfigFromPrefs() {
    Preferences prefs;
    String json = "[]";
    if (prefs.begin("wifi", true)) {
        json = prefs.getString("haEntitiesCfg", "[]");
        prefs.end();
    }
    if (!mqttApplyHaEntitiesConfigJson(json, false)) {
        mqttHaEntityCount = 0;
    }
}


static bool mqttTopicMatchesHaEntity(const String& topic, uint8_t& outIndex) {
    for (uint8_t i = 0; i < mqttHaEntityCount; i++) {
        if (mqttHaEntities[i].topic == topic || mqttHaEntities[i].entity == topic) {
            outIndex = i;
            return true;
        }
    }
    return false;
}

static void mqttSubscribeHaEntityTopics() {
    if (!mqttClient.connected()) return;

    for (uint8_t i = 0; i < mqttHaEntityCount; i++) {
        if (!mqttHaEntities[i].enabled) continue;
        if (mqttHaEntities[i].topic.length() == 0) continue;
        mqttClient.subscribe(mqttHaEntities[i].topic.c_str());
        if (mqttHaEntities[i].entity.length() > 0 && mqttHaEntities[i].entity != mqttHaEntities[i].topic) {
            mqttClient.subscribe(mqttHaEntities[i].entity.c_str());
        }
    }
}

static bool mqttHasAnyHaEntityWithValue() {
    for (uint8_t i = 0; i < mqttHaEntityCount; i++) {
        if (mqttHaEntities[i].enabled && mqttHaEntities[i].hasValue) {
            return true;
        }
    }
    return false;
}

static void mqttLoadApPasswordConfig() {
    Preferences prefs;
    if (!prefs.begin("wifi", true)) {
        mqttApPasswordVisible = false;
        mqttApPasswordCached = String(kDefaultApPassword);
        return;
    }

    mqttApPasswordVisible = prefs.getUChar("mqttShowApPassword", 0) == 1;
    String password = prefs.getString("apPassword", kDefaultApPassword);
    prefs.end();

    password.trim();
    if (password.length() < 8 || password.length() > 63) {
        password = String(kDefaultApPassword);
    }
    mqttApPasswordCached = password;
}

static bool mqttParseHexColor(const String& color, CRGB& outColor) {
    const char* raw = color.c_str();
    if (!raw) return false;

    const char* hex = raw;
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return false;

    char* endPtr = nullptr;
    unsigned long rgb = strtoul(hex, &endPtr, 16);
    if (!endPtr || *endPtr != '\0') return false;

    outColor = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

static String mqttColorToHex(const CRGB& color) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", color.r, color.g, color.b);
    return String(hex);
}

static void mqttLoadLampConfigIfNeeded() {
    if (mqttLampConfigLoaded) return;

    Preferences prefs;
    prefs.begin("wifi", true);
    mqttLampEnabled = prefs.getUChar("displayLampMode", 0) == 1;
    mqttLampBrightness = (uint8_t)constrain(prefs.getString("lampBrightness", "180").toInt(), 1, 255);
    String lampColor = prefs.getString("lampColor", "#FFFFFF");
    prefs.end();

    CRGB parsed;
    if (mqttParseHexColor(lampColor, parsed)) {
        mqttLampColor = parsed;
    } else {
        mqttLampColor = CRGB::White;
    }

    mqttLampConfigLoaded = true;
}

static void mqttSaveLampConfig() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putUChar("displayLampMode", mqttLampEnabled ? 1 : 0);
    prefs.putString("lampBrightness", String((int)mqttLampBrightness));
    prefs.putString("lampColor", mqttColorToHex(mqttLampColor));
    prefs.end();
}

static void mqttLoadAnimVisualsFromPrefs(uint8_t& outBrightness, CRGB& outColor) {
    Preferences prefs;
    prefs.begin("wifi", true);
    outBrightness = (uint8_t)constrain(prefs.getString("animBrightness", "200").toInt(), 1, 255);
    String animColor = prefs.getString("animColor", "#FF0000");
    prefs.end();

    CRGB parsed;
    if (mqttParseHexColor(animColor, parsed)) {
        outColor = parsed;
    } else {
        outColor = CRGB::Red;
    }
}

static void mqttApplyFunClockEffectsFromPrefs() {
    Preferences prefs;
    prefs.begin("wifi", true);
    bool fxMove = prefs.getUChar("fxMove", 1) == 1;
    bool fxMirror = prefs.getUChar("fxMirror", 1) == 1;
    bool fxRainbow = prefs.getUChar("fxRainbow", 1) == 1;
    bool fxHoursSlide = prefs.getUChar("fxHoursSlide", 1) == 1;
    bool fxMatrixFont = prefs.getUChar("fxMatrixFont", 1) == 1;
    bool fxMatrixSideways = prefs.getUChar("fxMatrixSideways", 1) == 1;
    bool fxUpsideDown = prefs.getUChar("fxUpsideDown", 1) == 1;
    bool fxRotate180 = prefs.getUChar("fxRotate180", 1) == 1;
    bool fxFullRotate = prefs.getUChar("fxFullRotate", 1) == 1;
    bool fxMiddleSwap = prefs.getUChar("fxMiddleSwap", 1) == 1;
    bool fxSplitHalves = prefs.getUChar("fxSplitHalves", 1) == 1;
    bool fxTetris = prefs.getUChar("fxTetris", 1) == 1;
    bool fxPileup = prefs.getUChar("fxPileup", 1) == 1;
    bool fxNegative = prefs.getUChar("displayNegative", 0) == 1;
    prefs.end();

    display_setFunClockEffectsEnabled(
        fxMove,
        fxMirror,
        fxRainbow,
        fxHoursSlide,
        fxMatrixFont,
        fxMatrixSideways,
        fxUpsideDown,
        fxRotate180,
        fxFullRotate,
        fxMiddleSwap,
        fxSplitHalves,
        fxTetris,
        fxPileup,
        fxNegative);
    display_setNegative(false);
}

static String mqttBuildDeviceId() {
    uint64_t mac = ESP.getEfuseMac();
    char id[17];
    snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
    return String("ledmatrixclock_") + String(id);
}

static String mqttNormalizePrefix(const String& prefix) {
    String out = prefix;
    out.trim();
    if (out.length() == 0) out = "homeassistant";
    while (out.endsWith("/")) {
        out.remove(out.length() - 1);
    }
    return out;
}

static void mqttRebuildTopics() {
    if (mqttDeviceId.length() == 0) {
        mqttDeviceId = mqttBuildDeviceId();
    }
    mqttClientId = mqttDeviceId;
    mqttBaseTopic = String("ledmatrixclock/") + mqttDeviceId;
    mqttAvailabilityTopic = mqttBaseTopic + "/availability";
    mqttStateTopic = mqttBaseTopic + "/status";
    mqttLightStateTopic = mqttBaseTopic + "/light/state";
    mqttLightCommandTopic = mqttBaseTopic + "/light/set";
    mqttBrightnessStateTopic = mqttBaseTopic + "/brightness/state";
    mqttBrightnessCommandTopic = mqttBaseTopic + "/brightness/set";
    mqttColorStateTopic = mqttBaseTopic + "/color/state";
    mqttColorCommandTopic = mqttBaseTopic + "/color/set";
    mqttLampStateTopic = mqttBaseTopic + "/lamp/state";
    mqttLampCommandTopic = mqttBaseTopic + "/lamp/set";
    mqttModeStateTopic = mqttBaseTopic + "/mode/state";
    mqttModeCommandTopic = mqttBaseTopic + "/mode/set";
    mqttApPasswordShowStateTopic = mqttBaseTopic + "/ap_password_show/state";
    mqttApPasswordShowCommandTopic = mqttBaseTopic + "/ap_password_show/set";
    mqttApPasswordStateTopic = mqttBaseTopic + "/ap_password/state";
    mqttApPasswordCommandTopic = mqttBaseTopic + "/ap_password/set";
    mqttQuoteTriggerCommandTopic = mqttBaseTopic + "/quote_trigger/set";
}

static const char* mqttModeToString() {
    if (display_mode == DISPLAY_MODE_LAMP) return "lamp";
    if (display_mode == DISPLAY_MODE_ANIMATION) return "animation";
    return "clock";
}

static void mqttPublishState() {
    if (!mqttClient.connected()) return;

    StaticJsonDocument<384> doc;
    doc["state"] = "online";
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["heap"] = ESP.getFreeHeap();

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u", currentHour, currentMinute, currentSecond);
    doc["time"] = String(timeBuf);

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(mqttStateTopic.c_str(), payload.c_str(), true);
}

static void mqttPublishLightState() {
    if (!mqttClient.connected()) return;

    StaticJsonDocument<192> doc;
    doc["state"] = display_enabled ? "ON" : "OFF";
    doc["brightness"] = globalBrightness;

    JsonObject color = doc.createNestedObject("color");
    color["r"] = globalColor.r;
    color["g"] = globalColor.g;
    color["b"] = globalColor.b;

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(mqttLightStateTopic.c_str(), payload.c_str(), true);
}

static void mqttPublishExtraStates() {
    if (!mqttClient.connected()) return;

    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", globalColor.r, globalColor.g, globalColor.b);

    char b[6];
    snprintf(b, sizeof(b), "%u", (unsigned)globalBrightness);
    mqttClient.publish(mqttBrightnessStateTopic.c_str(), b, true);
    mqttClient.publish(mqttColorStateTopic.c_str(), hex, true);
}

static void mqttPublishLampState() {
    if (!mqttClient.connected()) return;
    mqttLoadLampConfigIfNeeded();

    StaticJsonDocument<192> doc;
    doc["state"] = mqttLampEnabled ? "ON" : "OFF";
    doc["brightness"] = mqttLampBrightness;

    JsonObject color = doc.createNestedObject("color");
    color["r"] = mqttLampColor.r;
    color["g"] = mqttLampColor.g;
    color["b"] = mqttLampColor.b;

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(mqttLampStateTopic.c_str(), payload.c_str(), true);
}

static void mqttPublishModeState() {
    if (!mqttClient.connected()) return;
    mqttClient.publish(mqttModeStateTopic.c_str(), mqttModeToString(), true);
}

static void mqttPublishApPasswordShowState() {
    if (!mqttClient.connected()) return;
    mqttClient.publish(mqttApPasswordShowStateTopic.c_str(), mqttIsApPasswordDisplayEnabled() ? "ON" : "OFF", true);
}

static void mqttPublishApPasswordState() {
    if (!mqttClient.connected()) return;
    if (!mqttIsApPasswordDisplayEnabled()) {
        mqttClient.publish(mqttApPasswordStateTopic.c_str(), kHiddenApPasswordPlaceholder, true);
        return;
    }
    mqttRefreshApPasswordCache();
    String apPassword = mqttGetApPassword();
    mqttClient.publish(mqttApPasswordStateTopic.c_str(), apPassword.c_str(), true);
}

static void mqttApplyColor(uint8_t r, uint8_t g, uint8_t b) {
    CRGB newColor(r, g, b);
    animation_color = newColor;
    clock_color = newColor;
    quote_color = newColor;
    message_color = newColor;
    globalColor = newColor;
    display_setColor(newColor);
}

static void mqttRestoreClockVisualsFromPrefs() {
    Preferences prefs;
    prefs.begin("wifi", true);
    int animBrightness = constrain(prefs.getString("animBrightness", "200").toInt(), 1, 255);
    String animColor = prefs.getString("animColor", "#FF0000");
    prefs.end();

    display_setBrightness((uint8_t)animBrightness);

    CRGB parsed;
    if (mqttParseHexColor(animColor, parsed)) {
        mqttApplyColor(parsed.r, parsed.g, parsed.b);
    }
}

static bool mqttCanApplyBaseDisplayModeChange() {
    if (display_mode == DISPLAY_MODE_QUOTE) return false;
    if (message_active) return false;
    return true;
}

static void mqttApplyLampModeFromCurrentConfig() {
    if (mqttLampEnabled) {
        if (mqttCanApplyBaseDisplayModeChange()) {
            display_mode = DISPLAY_MODE_LAMP;
        }
        display_setBrightness(mqttLampBrightness);
        globalColor = mqttLampColor;
        display_setColor(mqttLampColor);
        return;
    }

    if (mqttCanApplyBaseDisplayModeChange()) {
        display_mode = DISPLAY_MODE_CLOCK;
    }
    mqttRestoreClockVisualsFromPrefs();
}

static void mqttOnMessage(char* topic, byte* payload, unsigned int length) {
    if (!topic || length == 0) return;
    String topicStr(topic);

    if (topicStr == mqttModeCommandTopic) {
        char temp[24];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';

        String requested = String(temp);
        requested.trim();
        requested.toLowerCase();

        if (requested == "lamp") {
            mqttLoadLampConfigIfNeeded();
            mqttLampEnabled = true;
            mqttSaveLampConfig();
            mqttApplyLampModeFromCurrentConfig();
            mqttPublishLampState();
        } else if (requested == "animation") {
            mqttLoadLampConfigIfNeeded();
            mqttLampEnabled = false;
            mqttSaveLampConfig();
            uint8_t animBrightness;
            CRGB animColor;
            mqttLoadAnimVisualsFromPrefs(animBrightness, animColor);
            display_setBrightness(animBrightness);
            mqttApplyColor(animColor.r, animColor.g, animColor.b);
            display_mode = DISPLAY_MODE_ANIMATION;
            mqttPublishLampState();
        } else {
            mqttLoadLampConfigIfNeeded();
            mqttLampEnabled = false;
            mqttSaveLampConfig();
            mqttApplyLampModeFromCurrentConfig();
            mqttPublishLampState();
        }

        mqttPublishModeState();
        mqttPublishLightState();
        mqttPublishExtraStates();
        mqttPublishState();
        return;
    }

    if (topicStr == mqttApPasswordShowCommandTopic) {
        char temp[12];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';

        String requested = String(temp);
        requested.trim();
        requested.toUpperCase();
        bool enabled = (requested == "ON" || requested == "1" || requested == "TRUE");
        Serial.printf("[MQTT] Pokaz haslo AP command: %s -> %s\n", requested.c_str(), enabled ? "ON" : "OFF");
        mqttApPasswordVisible = enabled;

        Preferences prefs;
        if (prefs.begin("wifi", false)) {
            prefs.putUChar("mqttShowApPassword", enabled ? 1 : 0);
            prefs.end();
        }

        mqttPublishApPasswordShowState();
        mqttPublishApPasswordState();
        mqttPublishState();
        return;
    }

    if (topicStr == mqttApPasswordCommandTopic) {
        char temp[96];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';

        String requested = String(temp);
        requested.trim();

        if (requested.length() >= 8 && requested.length() <= 63) {
            mqttApPasswordCached = requested;
            Preferences prefs;
            if (prefs.begin("wifi", false)) {
                prefs.putString("apPassword", requested);
                prefs.end();
            }
            Serial.println("[MQTT] AP haslo zapisane z encji tekstowej (aktywne po restarcie)");
        }

        mqttPublishApPasswordState();
        mqttPublishState();
        return;
    }

    if (topicStr == mqttQuoteTriggerCommandTopic) {
        mqttTriggerQuoteTest();
        mqttPublishState();
        mqttPublishModeState();
        return;
    }

    uint8_t haIndex = 0;
    if (mqttTopicMatchesHaEntity(topicStr, haIndex)) {
        char temp[192];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';
        mqttHaEntities[haIndex].lastValue = String(temp);
        mqttHaEntities[haIndex].lastValue.trim();
        mqttHaEntities[haIndex].hasValue = mqttHaEntities[haIndex].lastValue.length() > 0;
        return;
    }

    if (topicStr == mqttLampCommandTopic) {
        mqttLoadLampConfigIfNeeded();

        StaticJsonDocument<320> doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        bool changed = false;

        if (err) {
            char temp[16];
            unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
            memcpy(temp, payload, copyLen);
            temp[copyLen] = '\0';

            String state = String(temp);
            state.trim();
            state.toUpperCase();
            if (state == "ON") {
                mqttLampEnabled = true;
                changed = true;
            } else if (state == "OFF") {
                mqttLampEnabled = false;
                changed = true;
            } else {
                return;
            }
        } else {
            if (doc["state"].is<const char*>()) {
                String state = doc["state"].as<const char*>();
                state.toUpperCase();
                if (state == "ON") {
                    mqttLampEnabled = true;
                    changed = true;
                } else if (state == "OFF") {
                    mqttLampEnabled = false;
                    changed = true;
                }
            }

            if (!doc["brightness"].isNull()) {
                int requested = constrain((int)doc["brightness"], 1, 255);
                mqttLampBrightness = (uint8_t)requested;
                changed = true;
            }

            if (doc["color"].is<JsonObject>()) {
                JsonObject color = doc["color"].as<JsonObject>();
                mqttLampColor.r = (uint8_t)constrain((int)(color["r"] | mqttLampColor.r), 0, 255);
                mqttLampColor.g = (uint8_t)constrain((int)(color["g"] | mqttLampColor.g), 0, 255);
                mqttLampColor.b = (uint8_t)constrain((int)(color["b"] | mqttLampColor.b), 0, 255);
                changed = true;
            }

            if (doc["rgb_color"].is<JsonArray>()) {
                JsonArray rgb = doc["rgb_color"].as<JsonArray>();
                if (rgb.size() >= 3) {
                    mqttLampColor.r = (uint8_t)constrain((int)rgb[0], 0, 255);
                    mqttLampColor.g = (uint8_t)constrain((int)rgb[1], 0, 255);
                    mqttLampColor.b = (uint8_t)constrain((int)rgb[2], 0, 255);
                    changed = true;
                }
            }
        }

        if (changed) {
            mqttSaveLampConfig();
            mqttApplyLampModeFromCurrentConfig();

            mqttPublishLampState();
            mqttPublishModeState();
            mqttPublishState();
            mqttPublishLightState();
            mqttPublishExtraStates();
        }
        return;
    }

    if (topicStr == mqttBrightnessCommandTopic) {
        char temp[12];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';

        int requested = atoi(temp);
        requested = constrain(requested, 1, 255);
        display_setBrightness((uint8_t)requested);
        mqttPublishLightState();
        mqttPublishState();
        mqttPublishExtraStates();
        return;
    }

    if (topicStr == mqttColorCommandTopic) {
        char temp[20];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';

        const char* hex = temp;
        if (hex[0] == '#') hex++;
        if (strlen(hex) == 6) {
            char* endPtr = nullptr;
            unsigned long rgb = strtoul(hex, &endPtr, 16);
            if (endPtr && *endPtr == '\0') {
                uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
                uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
                uint8_t b = (uint8_t)(rgb & 0xFF);
                mqttApplyColor(r, g, b);
                mqttPublishLightState();
                mqttPublishState();
                mqttPublishExtraStates();
            }
        }
        return;
    }

    if (topicStr != mqttLightCommandTopic) return;

    StaticJsonDocument<320> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        return;
    }

    bool publishAfter = false;

    if (doc["state"].is<const char*>()) {
        String state = doc["state"].as<const char*>();
        state.toUpperCase();
        if (state == "ON") {
            display_enabled = true;
            publishAfter = true;
        } else if (state == "OFF") {
            display_enabled = false;
            publishAfter = true;
        }
    }

    if (!doc["brightness"].isNull()) {
        int requested = (int)doc["brightness"];
        requested = constrain(requested, 1, 255);
        display_setBrightness((uint8_t)requested);
        publishAfter = true;
    }

    if (doc["color"].is<JsonObject>()) {
        JsonObject color = doc["color"].as<JsonObject>();
        uint8_t r = (uint8_t)constrain((int)(color["r"] | globalColor.r), 0, 255);
        uint8_t g = (uint8_t)constrain((int)(color["g"] | globalColor.g), 0, 255);
        uint8_t b = (uint8_t)constrain((int)(color["b"] | globalColor.b), 0, 255);
        mqttApplyColor(r, g, b);
        publishAfter = true;
    }

    if (doc["rgb_color"].is<JsonArray>()) {
        JsonArray rgb = doc["rgb_color"].as<JsonArray>();
        if (rgb.size() >= 3) {
            uint8_t r = (uint8_t)constrain((int)rgb[0], 0, 255);
            uint8_t g = (uint8_t)constrain((int)rgb[1], 0, 255);
            uint8_t b = (uint8_t)constrain((int)rgb[2], 0, 255);
            mqttApplyColor(r, g, b);
            publishAfter = true;
        }
    }

    if (publishAfter) {
        mqttPublishLightState();
        mqttPublishModeState();
        mqttPublishState();
        mqttPublishExtraStates();
    }
}

static void mqttPublishDiscoveryCleanup() {
    if (!mqttClient.connected()) return;

    String discoveryBase = mqttNormalizePrefix(mqttDiscoveryPrefix);
    mqttClient.publish((discoveryBase + "/select/" + mqttDeviceId + "_mode/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/number/" + mqttDeviceId + "_lamp_brightness/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/text/" + mqttDeviceId + "_lamp_color_hex/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/switch/" + mqttDeviceId + "_lamp_mode/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/switch/" + mqttDeviceId + "_ap_password_show/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/text/" + mqttDeviceId + "_ap_password_text/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/sensor/" + mqttDeviceId + "_ap_password/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/text/" + mqttDeviceId + "_ap_password/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/button/" + mqttDeviceId + "_quote_trigger/config").c_str(), "", true);
}

static void mqttPublishDiscovery() {
    if (!mqttClient.connected()) return;

    String discoveryBase = mqttNormalizePrefix(mqttDiscoveryPrefix);

    {
        String topic = discoveryBase + "/sensor/" + mqttDeviceId + "_status/config";
        StaticJsonDocument<640> doc;
        doc["name"] = "LED Matrix Clock Status";
        doc["unique_id"] = mqttDeviceId + "_status";
        doc["stat_t"] = mqttStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["val_tpl"] = "{{ value_json.state }}";
        doc["json_attr_t"] = mqttStateTopic;
        doc["ic"] = "mdi:clock-digital";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/sensor/" + mqttDeviceId + "_rssi/config";
        StaticJsonDocument<512> doc;
        doc["name"] = "LED Matrix Clock RSSI";
        doc["unique_id"] = mqttDeviceId + "_rssi";
        doc["stat_t"] = mqttStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["val_tpl"] = "{{ value_json.rssi }}";
        doc["unit_of_meas"] = "dBm";
        doc["dev_cla"] = "signal_strength";
        doc["stat_cla"] = "measurement";
        doc["ic"] = "mdi:wifi";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/light/" + mqttDeviceId + "_light/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Clock";
        doc["unique_id"] = mqttDeviceId + "_light";
        doc["schema"] = "json";
        doc["cmd_t"] = mqttLightCommandTopic;
        doc["stat_t"] = mqttLightStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["pl_avail"] = "online";
        doc["pl_not_avail"] = "offline";
        doc["brightness"] = true;
        doc["rgb"] = true;
        doc["ic"] = "mdi:led-strip-variant";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/light/" + mqttDeviceId + "_lamp/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Lampa";
        doc["unique_id"] = mqttDeviceId + "_lamp";
        doc["schema"] = "json";
        doc["cmd_t"] = mqttLampCommandTopic;
        doc["stat_t"] = mqttLampStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["pl_avail"] = "online";
        doc["pl_not_avail"] = "offline";
        doc["brightness"] = true;
        doc["rgb"] = true;
        doc["ic"] = "mdi:lightbulb-on-outline";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/number/" + mqttDeviceId + "_brightness/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Clock Jasność";
        doc["unique_id"] = mqttDeviceId + "_brightness";
        doc["cmd_t"] = mqttBrightnessCommandTopic;
        doc["stat_t"] = mqttBrightnessStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["min"] = 1;
        doc["max"] = 255;
        doc["step"] = 1;
        doc["mode"] = "slider";
        doc["unit_of_meas"] = "lvl";
        doc["ic"] = "mdi:brightness-6";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/text/" + mqttDeviceId + "_color_hex/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Clock Kolor HEX";
        doc["unique_id"] = mqttDeviceId + "_color_hex";
        doc["cmd_t"] = mqttColorCommandTopic;
        doc["stat_t"] = mqttColorStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["min"] = 7;
        doc["max"] = 7;
        doc["pattern"] = "^#[0-9A-Fa-f]{6}$";
        doc["mode"] = "text";
        doc["ic"] = "mdi:palette";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/switch/" + mqttDeviceId + "_ap_password_show/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Pokaz haslo AP";
        doc["unique_id"] = mqttDeviceId + "_ap_password_show";
        doc["command_topic"] = mqttApPasswordShowCommandTopic;
        doc["state_topic"] = mqttApPasswordShowStateTopic;
        doc["payload_on"] = "ON";
        doc["payload_off"] = "OFF";
        doc["state_on"] = "ON";
        doc["state_off"] = "OFF";
        doc["availability_topic"] = mqttAvailabilityTopic;
        doc["enabled_by_default"] = true;
        doc["icon"] = "mdi:eye-outline";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/text/" + mqttDeviceId + "_ap_password_text/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Haslo AP";
        doc["unique_id"] = mqttDeviceId + "_ap_password_text";
        doc["command_topic"] = mqttApPasswordCommandTopic;
        doc["state_topic"] = mqttApPasswordStateTopic;
        doc["availability_topic"] = mqttAvailabilityTopic;
        doc["enabled_by_default"] = true;
        doc["entity_category"] = "config";
        doc["mode"] = "text";
        doc["min"] = 8;
        doc["max"] = 63;
        doc["icon"] = "mdi:key-variant";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    {
        String topic = discoveryBase + "/button/" + mqttDeviceId + "_quote_trigger/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Wyzwol cytat";
        doc["unique_id"] = mqttDeviceId + "_quote_trigger";
        doc["command_topic"] = mqttQuoteTriggerCommandTopic;
        doc["payload_press"] = "PRESS";
        doc["availability_topic"] = mqttAvailabilityTopic;
        doc["enabled_by_default"] = true;
        doc["icon"] = "mdi:format-quote-close";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(mqttDeviceId);
        dev["name"] = "LED Matrix Clock";
        dev["mdl"] = "ESP32-S3 N16R8";
        dev["mf"] = "DIY";

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    mqttDiscoveryPublished = true;
}

void mqtt_manager_begin() {
    mqttClient.setBufferSize(1024);
    mqttClient.setKeepAlive(30);
    mqttClient.setCallback(mqttOnMessage);
    mqttRebuildTopics();
    mqttLoadLampConfigIfNeeded();
    mqttLoadApPasswordConfig();
    mqttLoadHaEntitiesConfigFromPrefs();
}

void mqtt_manager_configure(bool enabled,
                            const char* host,
                            uint16_t port,
                            const char* user,
                            const char* password,
                            const char* discoveryPrefix) {
    mqttEnabled = enabled;
    mqttHost = host ? host : "";
    mqttPort = (port == 0) ? 1883 : port;
    mqttUser = user ? user : "";
    mqttPassword = password ? password : "";
    mqttDiscoveryPrefix = discoveryPrefix ? discoveryPrefix : "homeassistant";

    mqttDiscoveryPrefix = mqttNormalizePrefix(mqttDiscoveryPrefix);
    mqttRebuildTopics();

    mqttClient.setServer(mqttHost.c_str(), mqttPort);
    mqttDiscoveryPublished = false;

    if (!mqttEnabled && mqttClient.connected()) {
        mqttClient.publish(mqttAvailabilityTopic.c_str(), "offline", true);
        mqttClient.disconnect();
    }
}

void mqtt_manager_loop() {
    if (!mqttEnabled) return;
    if (mqttHost.length() == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();

    if (!mqttClient.connected()) {
        if ((now - mqttLastConnectAttemptMs) < 8000U) {
            return;
        }
        mqttLastConnectAttemptMs = now;

        bool connected = false;
        if (mqttUser.length() > 0) {
            connected = mqttClient.connect(
                mqttClientId.c_str(),
                mqttUser.c_str(),
                mqttPassword.c_str(),
                mqttAvailabilityTopic.c_str(),
                0,
                true,
                "offline");
        } else {
            connected = mqttClient.connect(
                mqttClientId.c_str(),
                mqttAvailabilityTopic.c_str(),
                0,
                true,
                "offline");
        }

        if (!connected) {
            return;
        }

        mqttClient.publish(mqttAvailabilityTopic.c_str(), "online", true);
        mqttPublishDiscoveryCleanup();
        mqttPublishDiscovery();
        mqttPublishState();
        mqttPublishLightState();
        mqttPublishLampState();
        mqttPublishModeState();
        mqttPublishApPasswordShowState();
        mqttPublishApPasswordState();
        mqttClient.subscribe(mqttLightCommandTopic.c_str());
        mqttClient.subscribe(mqttLampCommandTopic.c_str());
        mqttClient.subscribe(mqttBrightnessCommandTopic.c_str());
        mqttClient.subscribe(mqttColorCommandTopic.c_str());
        mqttClient.subscribe(mqttModeCommandTopic.c_str());
        mqttClient.subscribe(mqttApPasswordShowCommandTopic.c_str());
        mqttClient.subscribe(mqttApPasswordCommandTopic.c_str());
        mqttClient.subscribe(mqttQuoteTriggerCommandTopic.c_str());
        mqttSubscribeHaEntityTopics();
        mqttPublishExtraStates();
        mqttLastStatePublishMs = now;
    }

    mqttClient.loop();

    if (!mqttDiscoveryPublished) {
        mqttPublishDiscovery();
    }

    if ((now - mqttLastStatePublishMs) >= 30000U) {
        mqttPublishState();
        mqttPublishLightState();
        mqttPublishLampState();
        mqttPublishModeState();
        mqttPublishApPasswordShowState();
        mqttPublishApPasswordState();
        mqttPublishExtraStates();
        mqttLastStatePublishMs = now;
    }
}

void mqtt_manager_publish_now() {
    if (!mqttEnabled) return;
    if (!mqttClient.connected()) return;
    mqttPublishState();
    mqttPublishLightState();
    mqttPublishLampState();
    mqttPublishModeState();
    mqttPublishApPasswordShowState();
    mqttPublishApPasswordState();
    mqttPublishExtraStates();
}

bool mqtt_manager_isEnabled() {
    return mqttEnabled;
}

bool mqtt_manager_isConnected() {
    return mqttClient.connected();
}

String mqtt_manager_getStatus() {
    if (!mqttEnabled) return "wyłączony";
    if (mqttHost.length() == 0) return "brak hosta";
    return mqttClient.connected() ? "połączony" : "rozłączony";
}

void mqtt_manager_setHaEntitiesConfig(const String& configJson) {
    mqttApplyHaEntitiesConfigJson(configJson, true);
}

String mqtt_manager_getHaEntitiesConfigJson() {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < mqttHaEntityCount; i++) {
        JsonObject item = arr.createNestedObject();
        item["enabled"] = mqttHaEntities[i].enabled;
        item["entity"] = mqttHaEntities[i].entity;
        item["displayName"] = mqttHaEntities[i].displayName;
        item["unit"] = mqttHaEntities[i].unit;
        item["topic"] = mqttHaEntities[i].topic;
        item["durationSec"] = mqttHaEntities[i].durationSec;
    }

    String out;
    serializeJson(arr, out);
    return out;
}

void mqtt_manager_tryDisplayHaEntity() {
    if (!mqttEnabled) return;
    if (!mqttClient.connected()) return;
    if (mqttHaEntityCount == 0) return;
    if (!mqttHasAnyHaEntityWithValue()) return;
    if (message_active) return;
    if (display_mode == DISPLAY_MODE_QUOTE || display_mode == DISPLAY_MODE_LAMP) return;

    uint32_t now = millis();
    if (now < mqttHaNextDisplayMs) return;

    for (uint8_t step = 0; step < mqttHaEntityCount; step++) {
        uint8_t idx = (uint8_t)((mqttHaRotationIndex + step) % mqttHaEntityCount);
        MqttHaEntityConfig& item = mqttHaEntities[idx];
        if (!item.enabled || !item.hasValue) continue;

        String label = item.displayName;
        if (label.length() == 0) {
            label = item.entity;
        }
        String text = "";
        if (label.length() > 0) {
            text = label + ": ";
        }
        text += item.lastValue;
        if (item.unit.length() > 0) {
            text += " ";
            text += item.unit;
        }
        text.trim();
        if (text.length() == 0) continue;

        strlcpy(message_text, text.c_str(), sizeof(message_text));
        message_active = true;
        message_offset = LED_WIDTH;
        message_speed = 1;
        message_color = CRGB::Cyan;
        message_start_time = now;
        message_time_left = (uint32_t)item.durationSec * 1000U;

        mqttHaRotationIndex = (uint8_t)((idx + 1) % mqttHaEntityCount);
        mqttHaNextDisplayMs = now + message_time_left;
        return;
    }

    mqttHaNextDisplayMs = now + 5000U;
}

void mqtt_manager_setHaEntitiesDisplayEnabled(bool enabled) {
    (void)enabled;
}

bool mqtt_manager_triggerHaEntityDisplay(const String& entityName) {
    String target = entityName;
    target.trim();
    if (target.length() == 0) return false;

    for (uint8_t i = 0; i < mqttHaEntityCount; i++) {
        MqttHaEntityConfig& item = mqttHaEntities[i];
        if (item.entity != target) continue;
        if (!item.hasValue || item.lastValue.length() == 0) {
            return false;
        }

        String label = item.displayName;
        if (label.length() == 0) label = item.entity;
        String text = "";
        if (label.length() > 0) text = label + ": ";
        text += item.lastValue;
        if (item.unit.length() > 0) {
            text += " ";
            text += item.unit;
        }
        text.trim();
        if (text.length() == 0) return false;

        uint32_t now = millis();
        strlcpy(message_text, text.c_str(), sizeof(message_text));
        message_active = true;
        message_offset = LED_WIDTH;
        message_speed = 1;
        message_color = CRGB::Cyan;
        message_start_time = now;
        message_time_left = (uint32_t)item.durationSec * 1000U;
        mqttHaNextDisplayMs = now + message_time_left;
        return true;
    }

    return false;
}
