#include "mqtt_manager.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "clock.h"
#include "display.h"

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
static String mqttNegativeStateTopic;
static String mqttNegativeCommandTopic;

static bool mqttLampConfigLoaded = false;
static bool mqttLampEnabled = false;
static uint8_t mqttLampBrightness = 180;
static CRGB mqttLampColor = CRGB::White;

static bool mqttDiscoveryPublished = false;
static uint32_t mqttLastConnectAttemptMs = 0;
static uint32_t mqttLastStatePublishMs = 0;

static void mqttPublishLightState();
static void mqttPublishExtraStates();
static void mqttPublishLampState();
static void mqttPublishModeState();
static void mqttPublishNegativeState();
static void mqttPublishDiscoveryCleanup();

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
    bool fxUpsideDown = prefs.getUChar("fxUpsideDown", 1) == 1;
    bool fxRotate180 = prefs.getUChar("fxRotate180", 1) == 1;
    bool fxFullRotate = prefs.getUChar("fxFullRotate", 1) == 1;
    bool fxMiddleSwap = prefs.getUChar("fxMiddleSwap", 1) == 1;
    bool fxNegative = prefs.getUChar("displayNegative", 0) == 1;
    prefs.end();

    display_setFunClockEffectsEnabled(
        fxMove,
        fxMirror,
        fxRainbow,
        fxHoursSlide,
        fxMatrixFont,
        fxUpsideDown,
        fxRotate180,
        fxFullRotate,
        fxMiddleSwap,
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
    mqttNegativeStateTopic = mqttBaseTopic + "/negative_random/state";
    mqttNegativeCommandTopic = mqttBaseTopic + "/negative_random/set";
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

static void mqttPublishNegativeState() {
    if (!mqttClient.connected()) return;
    Preferences prefs;
    prefs.begin("wifi", true);
    bool enabled = prefs.getUChar("displayNegative", 0) == 1;
    prefs.end();
    mqttClient.publish(mqttNegativeStateTopic.c_str(), enabled ? "ON" : "OFF", true);
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
            display_enabled = true;
            display_mode = DISPLAY_MODE_LAMP;
            display_setBrightness(mqttLampBrightness);
            globalColor = mqttLampColor;
            display_setColor(mqttLampColor);
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
            animation_mode = ANIM_FADE;
            display_mode = DISPLAY_MODE_ANIMATION;
            mqttPublishLampState();
        } else {
            mqttLoadLampConfigIfNeeded();
            mqttLampEnabled = false;
            mqttSaveLampConfig();
            display_mode = DISPLAY_MODE_CLOCK;
            mqttRestoreClockVisualsFromPrefs();
            mqttPublishLampState();
        }

        mqttPublishModeState();
        mqttPublishLightState();
        mqttPublishExtraStates();
        mqttPublishState();
        return;
    }

    if (topicStr == mqttNegativeCommandTopic) {
        char temp[12];
        unsigned int copyLen = length < (sizeof(temp) - 1) ? length : (sizeof(temp) - 1);
        memcpy(temp, payload, copyLen);
        temp[copyLen] = '\0';

        String requested = String(temp);
        requested.trim();
        requested.toUpperCase();
        bool enabled = (requested == "ON" || requested == "1" || requested == "TRUE");

        Preferences prefs;
        prefs.begin("wifi", false);
        prefs.putUChar("displayNegative", enabled ? 1 : 0);
        prefs.end();

        mqttApplyFunClockEffectsFromPrefs();
        mqttPublishNegativeState();
        mqttPublishState();
        return;
    }

    if (topicStr == mqttLampCommandTopic) {
        mqttLoadLampConfigIfNeeded();

        StaticJsonDocument<320> doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err) return;

        bool changed = false;

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

        if (changed) {
            mqttSaveLampConfig();

            if (mqttLampEnabled) {
                display_enabled = true;
                display_mode = DISPLAY_MODE_LAMP;
                display_setBrightness(mqttLampBrightness);
                globalColor = mqttLampColor;
                display_setColor(mqttLampColor);
            } else {
                display_mode = DISPLAY_MODE_CLOCK;
                mqttRestoreClockVisualsFromPrefs();
            }

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
    mqttClient.publish((discoveryBase + "/number/" + mqttDeviceId + "_lamp_brightness/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/text/" + mqttDeviceId + "_lamp_color_hex/config").c_str(), "", true);
    mqttClient.publish((discoveryBase + "/switch/" + mqttDeviceId + "_lamp_mode/config").c_str(), "", true);
}

static void mqttPublishDiscovery() {
    if (!mqttClient.connected()) return;

    String discoveryBase = mqttNormalizePrefix(mqttDiscoveryPrefix);

    {
        String topic = discoveryBase + "/sensor/" + mqttDeviceId + "_status/config";
        StaticJsonDocument<640> doc;
        doc["name"] = "LED Matrix Clock Status";
        doc["uniq_id"] = mqttDeviceId + "_status";
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
        doc["uniq_id"] = mqttDeviceId + "_rssi";
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
        doc["uniq_id"] = mqttDeviceId + "_light";
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
        doc["uniq_id"] = mqttDeviceId + "_lamp";
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
        doc["uniq_id"] = mqttDeviceId + "_brightness";
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
        doc["uniq_id"] = mqttDeviceId + "_color_hex";
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
        String topic = discoveryBase + "/select/" + mqttDeviceId + "_mode/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Tryb";
        doc["uniq_id"] = mqttDeviceId + "_mode";
        doc["cmd_t"] = mqttModeCommandTopic;
        doc["stat_t"] = mqttModeStateTopic;
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["ic"] = "mdi:view-dashboard";

        JsonArray options = doc.createNestedArray("options");
        options.add("clock");
        options.add("lamp");
        options.add("animation");

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
        String topic = discoveryBase + "/switch/" + mqttDeviceId + "_negative_random/config";
        StaticJsonDocument<768> doc;
        doc["name"] = "LED Matrix Negatyw (losowanie)";
        doc["uniq_id"] = mqttDeviceId + "_negative_random";
        doc["cmd_t"] = mqttNegativeCommandTopic;
        doc["stat_t"] = mqttNegativeStateTopic;
        doc["pl_on"] = "ON";
        doc["pl_off"] = "OFF";
        doc["stat_on"] = "ON";
        doc["stat_off"] = "OFF";
        doc["avty_t"] = mqttAvailabilityTopic;
        doc["ic"] = "mdi:invert-colors";

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
        mqttPublishNegativeState();
        mqttClient.subscribe(mqttLightCommandTopic.c_str());
        mqttClient.subscribe(mqttLampCommandTopic.c_str());
        mqttClient.subscribe(mqttBrightnessCommandTopic.c_str());
        mqttClient.subscribe(mqttColorCommandTopic.c_str());
        mqttClient.subscribe(mqttModeCommandTopic.c_str());
        mqttClient.subscribe(mqttNegativeCommandTopic.c_str());
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
        mqttPublishNegativeState();
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
    mqttPublishNegativeState();
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
