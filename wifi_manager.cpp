#include "wifi_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "clock.h"
#include "display.h"
#include "effects.h"
#include "quotes.h"
#include "scheduler.h"
#include "web_panel.h"
#include "mqtt_manager.h"
#include "app_logger.h"
#include "ota_manager.h"
#include "github_ota_manager.h"
#include "instrukcja.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <esp_ota_ops.h>

static const uint8_t kLampDefaultBrightness = 64;
static const uint8_t kLampStartupBrightnessCap = 96;
static const bool kShowIpOnStaConnect = true;
static const char* kBirthdaysPrefKey = "birthdays";

static bool isLeapYearValue(int year) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

static bool isValidIsoDateYYYYMMDD(const String& date) {
    if (date.length() != 10) return false;
    for (uint8_t i = 0; i < 10; i++) {
        char c = date.charAt(i);
        if (i == 4 || i == 7) {
            if (c != '-') return false;
        } else {
            if (c < '0' || c > '9') return false;
        }
    }

    int year = date.substring(0, 4).toInt();
    int month = date.substring(5, 7).toInt();
    int day = date.substring(8, 10).toInt();

    if (year < 1900 || year > 2200) return false;
    if (month < 1 || month > 12) return false;

    static const uint8_t daysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t maxDay = daysInMonth[month - 1];
    if (month == 2 && isLeapYearValue(year)) {
        maxDay = 29;
    }
    return day >= 1 && day <= maxDay;
}

static bool parseHexColorString(const String& color, CRGB& outColor) {
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

static uint16_t loadStoredClockAnimIntervalSeconds(Preferences& preferences) {
    uint16_t intervalNum = preferences.getUShort("clockAnimInt", 0);
    if (intervalNum >= 10 && intervalNum <= 3600) {
        return intervalNum;
    }

    int intervalStr = preferences.getString("clockAnimInterval", "10").toInt();
    intervalStr = constrain(intervalStr, 10, 3600);
    return (uint16_t)intervalStr;
}

static bool canApplyBaseDisplayModeChange() {
    if (display_mode == DISPLAY_MODE_QUOTE) return false;
    if (message_active) return false;
    return true;
}

static void applyStoredDisplaySettings(Preferences& preferences) {
    int savedBrightness = preferences.getString("animBrightness", "200").toInt();
    savedBrightness = constrain(savedBrightness, 1, 255);
    int savedAnimSpeed = (int)preferences.getUChar("animSpeed", 4);
    savedAnimSpeed = constrain(savedAnimSpeed, 1, 10);
    animation_speed = (uint8_t)savedAnimSpeed;
    // Also restore message_speed
    int savedMsgSpeed = (int)preferences.getUChar("msgSpeed", 2);
    savedMsgSpeed = constrain(savedMsgSpeed, 1, 10);
    message_speed = (uint8_t)savedMsgSpeed;
    
    // Restore logs enabled state
    bool logsEnabled = preferences.getUChar("logsEnabled", 1) == 1;
    app_logger_set_enabled(logsEnabled);
    String savedColor = preferences.getString("animColor", "#FF0000");
    int lampBrightness = preferences.getString("lampBrightness", String((int)kLampDefaultBrightness)).toInt();
    lampBrightness = constrain(lampBrightness, 1, 255);
    String lampColor = preferences.getString("lampColor", "#FFFFFF");
    uint16_t clockAnimInterval = loadStoredClockAnimIntervalSeconds(preferences);
    display_setFunClockIntervalSeconds(clockAnimInterval);

    bool fxMirror = preferences.getUChar("fxMirror", 1) == 1;
    bool fxRainbow = preferences.getUChar("fxRainbow", 1) == 1;
    bool fxHoursSlide = preferences.getUChar("fxHoursSlide", 1) == 1;
    bool fxMatrixFont = preferences.getUChar("fxMatrixFont", 1) == 1;
    bool fxMatrixSideways = preferences.getUChar("fxMatrixSideways", 1) == 1;
    bool fxUpsideDown = preferences.getUChar("fxUpsideDown", 1) == 1;
    bool fxRotate180 = preferences.getUChar("fxRotate180", 1) == 1;
    bool fxFullRotate = preferences.getUChar("fxFullRotate", 1) == 1;
    bool fxMiddleSwap = preferences.getUChar("fxMiddleSwap", 1) == 1;
    bool fxTetris = preferences.getUChar("fxTetris", 1) == 1;
    bool fxPileup = preferences.getUChar("fxPileup", 1) == 1;
    bool fxRainbowBackground = preferences.getUChar("fxRainbowBackground", 1) == 1;
    bool displayRotate180 = preferences.getUChar("displayRotate180", 1) == 1;
    bool displayLampMode = preferences.getUChar("displayLampMode", 0) == 1;
    bool displayNegative = preferences.getUChar("displayNegative", 0) == 1;
    bool fxQuotes = preferences.getUChar("quotes_enabled", 1) == 1;
    display_setFunClockEffectsEnabled(fxMirror, fxRainbow, fxHoursSlide, fxMatrixFont, fxMatrixSideways, fxUpsideDown, fxRotate180, fxFullRotate, fxMiddleSwap, fxTetris, fxPileup, fxRainbowBackground, displayNegative);
    display_setNegative(false);
    display_setMatrixRotate180(displayRotate180);
    display_mode = displayLampMode ? DISPLAY_MODE_LAMP : DISPLAY_MODE_CLOCK;
    mainConfig.schedule.random_quotes_enabled = fxQuotes;

    CRGB parsedAnimColor = CRGB::White;
    bool hasAnimColor = parseHexColorString(savedColor, parsedAnimColor);
    if (hasAnimColor) {
        animation_color = parsedAnimColor;
        clock_color = parsedAnimColor;
        quote_color = parsedAnimColor;
        message_color = parsedAnimColor;
    }

    CRGB parsedLampColor = CRGB::White;
    bool hasLampColor = parseHexColorString(lampColor, parsedLampColor);

    if (displayLampMode && lampBrightness > (int)kLampStartupBrightnessCap) {
        lampBrightness = (int)kLampStartupBrightnessCap;
        preferences.putString("lampBrightness", String(lampBrightness));
    }

    uint8_t activeBrightness = displayLampMode ? (uint8_t)lampBrightness : (uint8_t)savedBrightness;
    CRGB activeColor = displayLampMode
        ? (hasLampColor ? parsedLampColor : CRGB::White)
        : (hasAnimColor ? parsedAnimColor : CRGB::White);

    display_setBrightness(activeBrightness);
    globalColor = activeColor;
    display_setColor(activeColor);

    if (displayLampMode && hasLampColor) {
        quote_color = parsedLampColor;
        message_color = parsedLampColor;
    }

    Serial.printf("[WiFi] Startup apply: brightness=%d speed=%d color=%s lampBrightness=%d lampColor=%s interval=%ds fx=%d%d%d%d%d%d%d%d%d%d%d%d rotMap=%d lamp=%d neg=%d quotes=%d\n",
        savedBrightness,
        savedAnimSpeed,
        savedColor.c_str(),
        lampBrightness,
        lampColor.c_str(),
        clockAnimInterval,
        fxMirror ? 1 : 0,
        fxRainbow ? 1 : 0,
        fxHoursSlide ? 1 : 0,
        fxMatrixFont ? 1 : 0,
        fxMatrixSideways ? 1 : 0,
        fxUpsideDown ? 1 : 0,
        fxRotate180 ? 1 : 0,
        fxFullRotate ? 1 : 0,
        fxMiddleSwap ? 1 : 0,
        fxTetris ? 1 : 0,
        fxPileup ? 1 : 0,
        displayRotate180 ? 1 : 0,
        displayLampMode ? 1 : 0,
        displayNegative ? 1 : 0,
        fxQuotes ? 1 : 0);
}

static void applyStoredMqttSettings(Preferences& preferences) {
    bool mqttEnabled = preferences.getUChar("mqttEnabled", 0) == 1;
    String mqttHost = preferences.getString("mqttHost", "");
    uint16_t mqttPort = preferences.getUShort("mqttPort", 1883);
    String mqttUser = preferences.getString("mqttUser", "");
    String mqttPassword = preferences.getString("mqttPass", "");
    String mqttPrefix = preferences.getString("mqttPrefix", "homeassistant");

    mqtt_manager_configure(
        mqttEnabled,
        mqttHost.c_str(),
        mqttPort,
        mqttUser.c_str(),
        mqttPassword.c_str(),
        mqttPrefix.c_str());

    Serial.printf("[MQTT] Startup apply: enabled=%d host=%s port=%u prefix=%s\n",
        mqttEnabled ? 1 : 0,
        mqttHost.c_str(),
        mqttPort,
        mqttPrefix.c_str());
}

bool wifi_manager_apply_lamp_config(bool lampEnabled,
                                    uint8_t lampBrightness,
                                    const String& lampColorInput,
                                    bool persistToPrefs,
                                    String* outNormalizedLampColor) {
    uint8_t normalizedBrightness = (uint8_t)constrain((int)lampBrightness, 1, 255);
    String normalizedLampColor = lampColorInput;
    normalizedLampColor.trim();

    CRGB parsedLampColor;
    if (!parseHexColorString(normalizedLampColor, parsedLampColor)) {
        normalizedLampColor = "#FFFFFF";
        parsedLampColor = CRGB::White;
    }

    if (outNormalizedLampColor) {
        *outNormalizedLampColor = normalizedLampColor;
    }

    Preferences prefs;
    bool prefsOpened = prefs.begin("wifi", false);
    if (persistToPrefs && prefsOpened) {
        prefs.putUChar("displayLampMode", lampEnabled ? 1 : 0);
        prefs.putString("lampBrightness", String(normalizedBrightness).c_str());
        prefs.putString("lampColor", normalizedLampColor.c_str());
    }

    bool modeApplied = false;
    if (lampEnabled) {
        if (canApplyBaseDisplayModeChange()) {
            display_mode = DISPLAY_MODE_LAMP;
            modeApplied = true;
        }
        display_setBrightness(normalizedBrightness);
        globalColor = parsedLampColor;
        display_setColor(parsedLampColor);
    } else {
        if (canApplyBaseDisplayModeChange()) {
            display_mode = DISPLAY_MODE_CLOCK;
            modeApplied = true;
        }

        int animBrightness = 200;
        String animColor = "#FF0000";
        if (prefsOpened) {
            animBrightness = constrain(prefs.getString("animBrightness", "200").toInt(), 1, 255);
            animColor = prefs.getString("animColor", "#FF0000");
        }

        display_setBrightness((uint8_t)animBrightness);
        CRGB parsedAnimColor;
        if (parseHexColorString(animColor, parsedAnimColor)) {
            animation_color = parsedAnimColor;
            clock_color = parsedAnimColor;
            quote_color = parsedAnimColor;
            message_color = parsedAnimColor;
            globalColor = parsedAnimColor;
            display_setColor(parsedAnimColor);
        }
    }

    if (prefsOpened) {
        prefs.end();
    }

    Serial.printf("[WiFi] Lamp config(shared): enabled=%d brightness=%d color=%s\n",
        lampEnabled ? 1 : 0,
        normalizedBrightness,
        normalizedLampColor.c_str());
    app_logf("LAMP shared: enabled=%d brightness=%u color=%s persist=%d modeApplied=%d",
             lampEnabled ? 1 : 0,
             normalizedBrightness,
             normalizedLampColor.c_str(),
             persistToPrefs ? 1 : 0,
             modeApplied ? 1 : 0);

    return modeApplied;
}

static void drawOtaProgressOnMatrix(size_t writtenBytes, size_t totalBytes, bool errorState = false, bool forceRedraw = false) {
    static int16_t lastPercentShown = -1;
    static bool lastErrorState = false;

    int16_t percent = -1;
    if (!errorState) {
        if (totalBytes > 0U) {
            uint32_t p = (uint32_t)((writtenBytes * 100UL) / totalBytes);
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
    String progressText;

    if (errorState) {
        progressText = "ERR";
    } else {
        progressText = String(percent) + "%";
    }

    int16_t textWidth = (int16_t)progressText.length() * 6 - 1;
    if (textWidth < 0) textWidth = 0;
    int16_t textX = (LED_WIDTH > textWidth) ? ((LED_WIDTH - textWidth) / 2) : 0;
    int16_t textY = (LED_HEIGHT > 7) ? ((LED_HEIGHT - 7) / 2) : 0;
    display_drawText(progressText.c_str(), textX, textColor);
    updateLEDs();
}


const char htmlPageAP[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang='pl'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>LED Matrix by ToPola - AP</title>
    <style>
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:Arial,sans-serif;background:#111;color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:12px}
        .card{width:100%;max-width:420px;background:#1b1b1b;border:1px solid #333;border-radius:10px;padding:14px}
        h1{text-align:center;font-size:1.15rem;margin-bottom:12px}
        .hint{font-size:.85rem;color:#aaa;margin-bottom:10px;text-align:center}
        input,select,button{width:100%;margin:6px 0;padding:10px;border-radius:8px;border:1px solid #444;background:#0f0f0f;color:#eee;font-size:.95rem}
        button{background:#1976d2;border-color:#1976d2;font-weight:600;cursor:pointer}
        button:hover{background:#1565c0}
        .secondary{background:#333;border-color:#555}
        .secondary:hover{background:#444}
        .danger{background:#7a1f1f;border-color:#a32b2b}
        .danger:hover{background:#8d2525}
        .status{font-size:.82rem;color:#9ec5ff;min-height:18px;margin:2px 0 4px}
        @media (max-width:360px){
            h1{font-size:1rem}
            input,select,button{padding:9px;font-size:.9rem}
        }
    </style>
</head>
<body>
    <div class='card'>
        <h1>Konfiguracja WiFi</h1>
        <div class='hint'>Tryb AP aktywny</div>
        <button type='button' class='secondary' onclick='scanWifi()'>Skanuj dostępne sieci</button>
        <div id='scan-status' class='status'>Naciśnij „Skanuj dostępne sieci”</div>
        <select id='wifi-select' onchange='selectScannedSsid()'>
            <option value=''>-- wybierz sieć z listy --</option>
        </select>
        <form method='POST' action='/save'>
            <input id='ssid-input' name='ssid' placeholder='Nazwa sieci (SSID)' required autofocus>
            <input name='password' type='password' placeholder='Hasło'>
            <button type='submit'>Połącz</button>
        </form>
        <form method='POST' action='/forget-wifi' onsubmit="return confirm('Usunąć zapisaną sieć WiFi?');">
            <button class='danger' type='submit'>Usuń zapisaną sieć</button>
        </form>
    </div>
    <script>
        async function scanWifi(){
            const status=document.getElementById('scan-status');
            const select=document.getElementById('wifi-select');
            status.textContent='Skanowanie...';
            select.innerHTML="<option value=''>-- skanowanie --</option>";
            try{
                const r=await fetch('/api/wifi-scan');
                const d=await r.json();
                if(!d.success){
                    status.textContent='Błąd skanowania';
                    select.innerHTML="<option value=''>-- brak wyników --</option>";
                    return;
                }
                const nets=Array.isArray(d.networks)?d.networks:[];
                if(nets.length===0){
                    status.textContent='Nie znaleziono sieci';
                    select.innerHTML="<option value=''>-- brak sieci --</option>";
                    return;
                }
                select.innerHTML="<option value=''>-- wybierz sieć z listy --</option>";
                nets.forEach(n=>{
                    const o=document.createElement('option');
                    o.value=n.ssid||'';
                    const sec=(n.secure===true)?'🔒':'🔓';
                    o.textContent=`${n.ssid} (${n.rssi} dBm) ${sec}`;
                    select.appendChild(o);
                });
                status.textContent=`Znaleziono sieci: ${nets.length}`;
            }catch(e){
                status.textContent='Błąd połączenia';
                select.innerHTML="<option value=''>-- brak wyników --</option>";
            }
        }

        function selectScannedSsid(){
            const select=document.getElementById('wifi-select');
            const ssidInput=document.getElementById('ssid-input');
            if(select && ssidInput && select.value){
                ssidInput.value=select.value;
            }
        }
    </script>
</body>
</html>)HTML";

const char htmlPageSTA[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang='pl'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>LED Matrix by ToPola</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial;background:#0b0f14;color:#e8edf4;padding:10px;line-height:1.35}
.c{max-width:820px;margin:0 auto}
h1{font-size:1.22em;margin:0 0 6px}
h2{font-size:1.08em;margin:0 0 8px;color:#f2f5fa}
label{display:block;margin-top:6px;color:#d5dde8}
input,select{width:100%;padding:9px;margin:4px 0;border:1px solid #3a4553;background:#121922;color:#e8edf4;border-radius:8px;outline:none;transition:border-color .15s, box-shadow .15s}
input:focus,select:focus{border-color:#5f7ea3;box-shadow:0 0 0 2px rgba(95,126,163,.25)}
button{width:100%;padding:9px 10px;margin:4px 0;background:#243241;color:#edf3fb;border:1px solid #3a4b5e;cursor:pointer;border-radius:8px;transition:background .15s,border-color .15s}
button:hover{background:#2d3f52;border-color:#4b6077}
.t{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 10px}
.tb{flex:1 1 calc(25% - 6px);min-width:96px;padding:7px 8px;background:#1d2834;border:1px solid #344354;color:#d9e4f0;cursor:pointer;font-size:0.84em;margin:0}
.tb.a{background:#38506a;border-color:#5a7a99;color:#fff}
.tc{display:none;padding:10px;background:#111923;border:1px solid #2c3a4a;border-radius:10px;margin-bottom:8px}
.tc.a{display:block}
.anim-test-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;margin-top:6px}
.anim-test-grid button{width:auto;margin:0;padding:6px 7px;font-size:.82em}
.fg{margin:2px 0}
.d{background:#582626;border-color:#8b3a3a}
.d:hover{background:#6c2f2f;border-color:#a84848}
.i{padding:7px 8px;margin:5px 0;background:#0d141d;border-left:3px solid #44596f;border-radius:6px;font-size:0.9em}
.lamp-row{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:4px 0 8px}
.lamp-status{font-size:.92em;color:#cfe0f4}
.b-toggle{width:auto;min-width:180px;margin:0}
.b-toggle.on{background:#2f5d2f;border-color:#4f8e4f;color:#eef9ee}
.b-toggle.off{background:#3e2e1f;border-color:#725436;color:#fff2e4}
.sys{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;margin-top:6px}
.sys .i{margin:0}
.pill{display:inline-block;padding:2px 8px;border-radius:999px;font-size:.78em;border:1px solid #3c4f64;background:#1a2430;color:#dbe7f4}
.pill.ok{border-color:#3d7d50;background:#1e3626;color:#d8f1de}
.pill.bad{border-color:#8b4a4a;background:#3b2323;color:#ffdede}
.toast-wrap{position:fixed;right:10px;bottom:10px;display:flex;flex-direction:column;gap:8px;z-index:9999;pointer-events:none}
.toast{min-width:220px;max-width:320px;padding:9px 10px;border-radius:8px;border-left:4px solid #4caf50;background:#15221a;color:#e9f8ed;box-shadow:0 6px 18px rgba(0,0,0,.35);opacity:0;transform:translateY(8px);transition:opacity .18s,transform .18s}
.toast.show{opacity:1;transform:translateY(0)}
.toast.err{border-left-color:#f44336;background:#2a1717;color:#ffe5e5}
.logs-box{margin-top:8px;background:#0b121b;border:1px solid #2c3a4a;border-radius:8px;padding:8px;height:220px;overflow:auto;font-family:Consolas,monospace;font-size:.78em;line-height:1.35;white-space:pre-wrap}
.logs-controls{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.ota-progress{height:14px;border:1px solid #2c3a4a;border-radius:999px;background:#0b121b;overflow:hidden;margin:8px 0}
.ota-progress>div{height:100%;width:0%;background:linear-gradient(90deg,#2f78c4,#4ea3ff);transition:width .2s}
.ota-progress.indeterminate>div{width:35% !important;animation:otaInd 1.1s linear infinite}
@keyframes otaInd{0%{transform:translateX(-120%)}100%{transform:translateX(360%)}}
.ota-row{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.ota-row button{flex:1 1 140px}
.ot{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0 10px}
.otb{background:#2a2a2a;border:1px solid #444;color:#eee;padding:8px 10px;border-radius:8px;cursor:pointer;flex:1 1 120px}
.otb.a{background:#1976d2;border-color:#1976d2;color:#fff}
.otc{display:none}
.otc.a{display:block}
@media (max-width:480px){
    body{padding:7px}
    h1{font-size:1.05em}
    h2{font-size:1em}
    .tb{flex:1 1 calc(50% - 6px);font-size:0.82em;padding:8px}
    input,select,button{padding:9px;font-size:0.95em}
    .lamp-row{flex-direction:column;align-items:stretch}
    .b-toggle{width:100%}
    .sys{grid-template-columns:1fr}
}
</style>
</head>
<body>
<div class='h'><h1>⚙️ LED Matrix by ToPola</h1></div>
<div class='c'>
<div class='t'>
<button class='tb a' onclick='showTab(0)'>Info</button>
<button class='tb' onclick='showTab(1)'>Czas</button>
<button class='tb' onclick='showTab(2)'>Anim</button>
<button class='tb' onclick='showTab(3)'>Lampa</button>
<button class='tb' onclick='showTab(4)'>Cytaty</button>
<button class='tb' onclick='showTab(5)'>Urodziny</button>
<button class='tb' onclick='showTab(6)'>Plan</button>
<button class='tb' onclick='showTab(7)'>Inne</button>
<button class='tb' onclick='showTab(8)' style='display:none'>Logi</button>
</div>

<!-- TAB 0: INFO -->
<div class='tc a'>
<h2>Info</h2>
<div class='i'><strong>IP:</strong> <span id='ipaddr'>--</span></div>
<div class='i'><strong>SSID:</strong> <span id='ssid-info'>--</span></div>
<div class='i'><strong>RSSI:</strong> <span id='rssi-info'>--</span></div>
<div class='i'><strong>Czas:</strong> <span id='current-time'>--</span></div>
<div class='i'><strong>Uptime:</strong> <span id='uptime'>--</span></div>
<div class='i'><strong>Flash:</strong> <span id='flash-info'>--</span></div>
<div class='sys'>
<div class='i'><strong>WiFi:</strong> <span id='sys-wifi' class='pill'>--</span></div>
<div class='i'><strong>MQTT:</strong> <span id='sys-mqtt' class='pill'>--</span></div>
<div class='i'><strong>OTA:</strong> <span id='sys-ota' class='pill'>--</span></div>
<div class='i'><strong>RAM:</strong> <span id='sys-ram' class='pill'>--</span></div>
</div>
<button type='button' onclick='showTab(8)'>Logi</button>
<button onclick='location.reload()'>Refresh</button>
<button class='d' onclick='restartDevice()'>Restart urządzenia</button>
<button onclick='logoutPanel()'>Wyloguj</button>
</div>

<!-- TAB 1: CZAS -->
<div class='tc'>
<h2>Czas</h2>
<form id='time-form' method='POST' action='/save-time'>
<label>NTP</label>
<input type='text' name='ntpServer' id='ntpServer-input' value='pool.ntp'>
<label>Strefa</label>
<select name='timezone' id='timezone-input'>
<option value='0'>UTC</option>
<option value='1' selected>UTC+1</option>
<option value='2'>UTC+2</option>
<option value='-5'>UTC-5</option>
<option value='-8'>UTC-8</option>
<option value='8'>UTC+8</option>
<option value='9'>UTC+9</option>
</select>
<div class='i'><span id='time-display' style='font-size:1.1em'>--:--</span></div>
<div id='time-save-status' class='i' style='display:none;margin-top:4px'>✓ Zapisano ustawienia czasu</div>
<button type='submit'>Zapisz</button>
</form>
</div>

<!-- TAB 2: ANIMACJE -->
<div class='tc'>
<h2>Anim</h2>
<form id='anim-form' onsubmit='return false'>
<label>Jasność: <span id='bv'>200</span></label>
<input type='range' name='animBrightness' id='ab' min='1' max='255' value='200' oninput='u();saveAnimationVisuals()'>
<label>Prędkość wyświetlania: <span id='asv'>4</span></label>
<input type='range' name='animSpeed' id='as' min='1' max='10' value='4' oninput='u();saveAnimationVisuals()'>
<label>Prędkość wiadomości: <span id='msv'>2</span></label>
<input type='range' name='msgSpeed' id='ms' min='1' max='10' value='2' oninput='u();saveAnimationVisuals()'>
<label>Czas między animacjami: <span id='afreqv'>10 s</span></label>
<input type='range' name='clockAnimInterval' id='afreq' min='10' max='3600' value='10' oninput='u();saveAnimationInterval()'>
<label><input type='checkbox' name='fxMirror' id='fxMirror' checked style='width:auto' onchange='saveAnimationSelection()'> Lustro</label>
<label><input type='checkbox' name='fxRainbow' id='fxRainbow' checked style='width:auto' onchange='saveAnimationSelection()'> Tęcza</label>
<label><input type='checkbox' name='fxHoursSlide' id='fxHoursSlide' checked style='width:auto' onchange='saveAnimationSelection()'> Godziny: wyjazd/lewy + powrót/prawy</label>
<label><input type='checkbox' name='fxMatrixFont' id='fxMatrixFont' checked style='width:auto' onchange='saveAnimationSelection()'> Predator GLYPH</label>
<label><input type='checkbox' name='fxMatrixSideways' id='fxMatrixSideways' checked style='width:auto' onchange='saveAnimationSelection()'> Matrix: bokiem, 2 rzędy w lewo</label>
<label><input type='checkbox' name='fxUpsideDown' id='fxUpsideDown' checked style='width:auto' onchange='saveAnimationSelection()'> Do góry nogami</label>
<label><input type='checkbox' name='fxRotate180' id='fxRotate180' checked style='width:auto' onchange='saveAnimationSelection()'> Obrót 180°</label>
<label><input type='checkbox' name='fxFullRotate' id='fxFullRotate' checked style='width:auto' onchange='saveAnimationSelection()'> Pełny obrót w prawo</label>
<label><input type='checkbox' name='fxMiddleSwap' id='fxMiddleSwap' checked style='width:auto' onchange='saveAnimationSelection()'> Wszystkie cyfry: naprzemienny przejazd</label>
<label><input type='checkbox' name='fxTetris' id='fxTetris' checked style='width:auto' onchange='saveAnimationSelection()'> Tetris: cyfry spadają z góry</label>
<label><input type='checkbox' name='fxPileup' id='fxPileup' checked style='width:auto' onchange='saveAnimationSelection()'> Karambol: cyfry i dwukropki na lewo</label>
<label><input type='checkbox' name='displayRotate180' id='displayRotate180' style='width:auto' onchange='saveMatrixRotateToggle()'> Obróć matrycę 180°</label>
<label><input type='checkbox' name='displayNegative' id='displayNegative' style='width:auto' onchange='saveNegativeToggle()'> Negatyw wyświetlania</label>
<label><input type='checkbox' name='fxRainbowBackground' id='fxRainbowBackground' checked style='width:auto' onchange='saveAnimationSelection()'> Tęczowe tło z cyframi</label>
<label><input type='checkbox' name='fxQuotes' id='fxQuotes' checked style='width:auto' onchange='saveQuotesToggle()'> Cytaty</label>
<label>Kolor</label>
<input type='color' name='animColor' id='ac' value='#FF0000' oninput='saveAnimationVisuals()' style='height:40px'>
<hex-color-picker id='acp' color='#FF0000' style='display:block;width:100%;height:72px;margin-bottom:4px'></hex-color-picker>
<div class='anim-test-grid'>
<button type='button' onclick='triggerClockAnimTest()' style='background:#1976d2;'>Test animacji cyfr</button>
<button type='button' onclick='triggerClockMirrorTest()' style='background:#444;'>Test lustra</button>
<button type='button' onclick='triggerClockRainbowTest()' style='background:#6a1b9a;'>Test animacji tęczy</button>
<button type='button' onclick='triggerClockHoursSlideTest()' style='background:#8d6e63;'>Test wyjazdu/wjazdu godzin</button>
<button type='button' onclick='triggerClockMatrixFontTest()' style='background:#2e7d32;'>Test Predator GLYPH</button>
<button type='button' onclick='triggerClockMatrixSidewaysTest()' style='background:#1b5e20;'>Test Matrix (2 rzędy, bokiem)</button>
<button type='button' onclick='triggerClockUpsideDownTest()' style='background:#455a64;'>Test do góry nogami</button>
<button type='button' onclick='triggerClockRotate180Test()' style='background:#5d4037;'>Test obrotu 180°</button>
<button type='button' onclick='triggerClockFullRotateTest()' style='background:#00695c;'>Test pełnego obrotu</button>
<button type='button' onclick='triggerClockMiddleSwapTest()' style='background:#3949ab;'>Test przejazdu wszystkich cyfr</button>
<button type='button' onclick='triggerClockTetrisTest()' style='background:#00897b;'>Test Tetris</button>
<button type='button' onclick='triggerClockPileupTest()' style='background:#4e342e;'>Test karambolu cyfr</button>
<button type='button' onclick='triggerClockNegativeTest()' style='background:#455a64;'>Test negatywu</button>
<button type='button' onclick='triggerClockRainbowBackgroundTest()' style='background:#9c27b0;'>Test tęczowego tła</button>
</div>
<div style='margin-top:12px; border-top:1px solid #555; padding-top:12px;'>
<h3>Wysłij wiadomość</h3>
<label>Tekst</label>
<input type='text' id='msgTextInput' placeholder='Wpisz wiadomość...' maxlength='127' style='margin-bottom:8px;'>
<button type='button' onclick='sendRuntimeMessage()' style='background:#ff9800; width:100%;'>➤ Wyślij wiadomość</button>
</div>
</form>
</div>

<!-- TAB 3: LAMPA -->
<div class='tc'>
<h2>Lampa</h2>
<form id='lamp-form' onsubmit='return false'>
<div class='lamp-row'>
<div class='lamp-status'>Status: <strong id='lampModeStatus'>Wyłączona</strong></div>
<button type='button' id='lampToggleBtn' class='b-toggle off' onclick='toggleLampMode()'>Włącz lampę</button>
</div>
<label>Jasność lampy: <span id='lbv'>180</span></label>
<input type='range' name='lampBrightness' id='lb' min='1' max='255' value='64' oninput='uLamp();saveLampConfigDebounced()'>
<label>Kolor lampy</label>
<input type='color' name='lampColor' id='lc' value='#FFFFFF' oninput='saveLampConfigDebounced()' style='height:40px'>
<hex-color-picker id='lcp' color='#FFFFFF' style='display:block;width:100%;height:160px'></hex-color-picker>
</form>
</div>

<!-- TAB 4: CYTATY -->
<div class='tc'>
<h2>Cytaty</h2>
<form method='POST' action='/save-quote' id='addQuoteForm'>
<input type='text' name='quote' placeholder='Cytat' maxlength='80'>
<button type='submit'>Dodaj</button>
</form>
<button type='button' onclick='triggerQuoteTest()' style='background:#1976d2;'>Wyzwól cytat (test)</button>
<button type='button' onclick='exportQuotes()' style='background:#333;flex:1'>Eksport</button>
<input type='file' id='importFile' style='display:none' accept='.json'>
<button type='button' onclick='document.getElementById("importFile").click()' style='background:#333;flex:1'>Import</button>
<div id='quotes-list'></div>
</div>

<!-- TAB 5: HARMONOGRAM -->
<div class='tc'>
<h2>Urodziny</h2>
<form id='birthday-form' onsubmit='return false'>
<label>Imię</label>
<input type='text' id='bd-name' maxlength='40' placeholder='Np. Anna'>
<label>Data wyświetlania</label>
<input type='date' id='bd-date'>
<button type='button' onclick='addBirthday()'>Dodaj urodziny</button>
<button type='button' onclick='triggerBirthdayTest()' style='background:#1976d2;'>Test życzeń</button>
</form>
<div id='birthdays-list' style='margin-top:8px'></div>
</div>

<!-- TAB 6: HARMONOGRAM -->
<div class='tc'>
<h2>Harmonogram jasności</h2>
<div id='schedule-list' style='display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:4px;margin-bottom:4px'></div>
<button type='button' onclick='addScheduleItem()' style='background:#555;margin-bottom:4px'>Dodaj okno</button>
<button type='button' onclick='saveSchedule()'>Zapisz</button>
</div>

<!-- TAB 7: INNE (MQTT + WiFi + OTA) -->
<div class='tc'>
<h2>Inne</h2>
<div class='ot'>
<button type='button' class='otb a' onclick='showOtherTab(0)'>MQTT</button>
<button type='button' class='otb' onclick='showOtherTab(1)'>WiFi</button>
<button type='button' class='otb' onclick='showOtherTab(2)'>OTA</button>
<button type='button' class='otb' onclick='showOtherTab(3)'>Instrukcja</button>
</div>

<div class='otc a'>
<h3>MQTT (Home Assistant)</h3>
<form id='mqtt-form' onsubmit='return false'>
<label><input type='checkbox' name='mqttEnabled' id='mqttEnabled' style='width:auto' onchange='saveMqttConfig()'> Włącz MQTT</label>
<label>Host (IP/Nazwa)</label>
<input type='text' name='mqttHost' id='mqttHost' placeholder='192.168.1.10'>
<label>Port</label>
<input type='number' name='mqttPort' id='mqttPort' min='1' max='65535' value='1883'>
<label>Użytkownik</label>
<input type='text' name='mqttUser' id='mqttUser' placeholder='opcjonalnie'>
<label>Hasło</label>
<input type='password' name='mqttPass' id='mqttPass' placeholder='opcjonalnie'>
<label>Prefix discovery</label>
<input type='text' name='mqttPrefix' id='mqttPrefix' value='homeassistant'>
<div class='i'><strong>Status:</strong> <span id='mqttStatus'>--</span></div>
<button type='button' onclick='saveMqttConfig()'>Zapisz MQTT</button>
</form>
</div>

<div class='otc'>
<h3>WiFi</h3>
<form method='POST' action='/save'>
<label>SSID</label>
<input type='text' name='ssid' id='ssid-input'>
<label>Hasło</label>
<input type='password' name='password' id='password-input'>
<label>Hasło AP / logowania WWW</label>
<input type='password' name='apPassword' id='apPassword-input' minlength='8' maxlength='63' placeholder='min. 8 znaków'>
<label>IP</label>
<input type='text' name='staticIP' id='staticIP-input'>
<label>GW</label>
<input type='text' name='gateway' id='gateway-input'>
<label>Subnet</label>
<input type='text' name='subnet' id='subnet-input'>
<label><input type='checkbox' name='dhcp' id='dhcp-input' checked style='width:auto'> DHCP</label>
<button type='submit'>Zapisz</button>
</form>
<form method='POST' action='/forget-wifi' onsubmit='return confirm("Usunąć zapisaną sieć WiFi?")'>
<button type='submit' class='d'>Usuń zapisaną sieć</button>
</form>
<button class='d' onclick='if(confirm("Wykonać pełny reset ustawień WiFi?"))location.href="/resetwifi"'>Reset WiFi (całość)</button>
</div>

<div class='otc'>
<h3>OTA firmware update</h3>
<div class='i'><strong>Status:</strong> <span id='ota-status-text'>Gotowy</span></div>
<div class='i'><strong>Wersja:</strong> <span id='ota-version-text'>--</span></div>
<div class='i'><strong>Kompilacja:</strong> <span id='ota-build-text'>--</span></div>
<div class='i'><strong>Rozmiar szkicu:</strong> <span id='ota-sketch-size'>--</span></div>
<div class='i'><strong>Wersja zdalna:</strong> <span id='ota-github-remote'>--</span></div>
<div class='i' id='ota-new-version-msg' style='display:none;color:#ff9800;font-weight:bold'>Jest nowa wersja, wkrótce będzie na twoim urządzeniu</div>
<div class='i'><strong>Etap zdalnej OTA:</strong> <span id='ota-github-phase'>--</span></div>
<div class='i'><strong>Błąd zdalnej OTA:</strong> <span id='ota-github-error'>--</span></div>
<div class='i'><strong>Aktywny slot:</strong> <span id='ota-active-slot'>--</span></div>
<div class='i'><strong>Następny slot:</strong> <span id='ota-next-slot'>--</span></div>
<label>Plik firmware (.bin)</label>
<input type='file' id='ota-file' accept='.bin'>
<div class='ota-progress'><div id='ota-progress-bar'></div></div>
<div class='i'><strong>Postęp:</strong> <span id='ota-progress-text'>0%</span></div>
<div class='i'><strong>Transfer:</strong> <span id='ota-transfer-text'>0 B / ?</span></div>
<div class='ota-row'>
<button type='button' id='ota-upload-btn' onclick='startOtaUpload()'>Wyślij firmware</button>
<button type='button' id='ota-switch-slot-btn' onclick='switchOtaSlot()'>Przełącz slot i restart</button>
<button type='button' id='ota-test-btn' onclick='startGithubOtaCheck()' style='background:#ff9800;display:none'>🧪 TEST: Sprawdź aktual. GitHub</button>
<button type='button' id='ota-refresh-btn' onclick='loadOtaStatus()'>Odśwież status</button>
</div>
<div class='i' style='font-size:.82em'>Po wysłaniu poprawnego pliku urządzenie zrestartuje się automatycznie.</div>
</div>

<div class='otc'>
<h3>Instrukcja</h3>
<div id='instrukcja-content' style='line-height:1.6;font-size:0.95em;color:#ddd'>Ładowanie instrukcji...</div>
</div>
</div>

<!-- TAB 8: LOGI -->
<div class='tc'>
<h2>Logi</h2>
<div class='logs-controls'>
<label><input type='checkbox' id='logsEnabled' style='width:auto' onchange='saveLogsConfig()'> Włącz logi</label>
<button type='button' onclick='loadLogsNow(true)'>Odśwież</button>
<button type='button' class='d' onclick='clearLogs()'>Wyczyść</button>
<label><input type='checkbox' id='logsAuto' style='width:auto' checked> Auto</label>
</div>
<div id='logsBox' class='logs-box'>Brak logów</div>
</div>
</div>
</body>
<div id='toast-wrap' class='toast-wrap'></div>
<script>
if(!customElements.get('hex-color-picker')){
const s=document.createElement('script');
s.type='module';
s.src='https://cdn.jsdelivr.net/npm/vanilla-colorful@0.7.2/lib/esm/entrypoints/index.js';
document.head.appendChild(s);
}

let lampModeEnabled=false;
let logsLastSeq=0;
let logsPollTimer=null;
let otaPollTimer=null;
let otaUploadActive=false;
let otaUiProgress=0;
let otaUiTargetProgress=0;
let otaUiProgressTimer=null;
let otherTabIndex=0;

function formatBytes(v){
const n=Number(v||0);
if(n<1024)return `${n} B`;
if(n<1024*1024)return `${(n/1024).toFixed(1)} KB`;
return `${(n/(1024*1024)).toFixed(2)} MB`;
}

function otaSetIndeterminate(on){
const wrap=document.querySelector('.ota-progress');
if(!wrap)return;
if(on){wrap.classList.add('indeterminate');}
else{wrap.classList.remove('indeterminate');}
}

function showOtherTab(i){
otherTabIndex=i;
document.querySelectorAll('.otc').forEach(e=>e.classList.remove('a'));
document.querySelectorAll('.otb').forEach(e=>e.classList.remove('a'));
const panes=document.querySelectorAll('.otc');
const btns=document.querySelectorAll('.otb');
if(panes[i])panes[i].classList.add('a');
if(btns[i])btns[i].classList.add('a');

if(i===0){
loadMqttConfig();
stopOtaStatusPolling();
}
if(i===1){
loadWifiConfig();
stopOtaStatusPolling();
}
if(i===2){
loadOtaStatus();
startOtaStatusPolling();
}
if(i===3){
stopOtaStatusPolling();
loadInstrukcja();
}
}

function showTab(i){
document.querySelectorAll('.tc').forEach(e=>e.classList.remove('a'));
document.querySelectorAll('.tb').forEach(e=>e.classList.remove('a'));
document.querySelectorAll('.tc')[i].classList.add('a');
document.querySelectorAll('.tb')[i].classList.add('a');
if(i===3)loadLampConfig();
if(i===4)loadQuotes();
if(i===5)loadBirthdays();
if(i===6)loadSchedule();
if(i===2)loadAnimationsConfig();
if(i===7){showOtherTab(otherTabIndex);}else{stopOtaStatusPolling();}
if(i===8){loadLogsNow(true);startLogsPolling();}else{stopLogsPolling();}
}

function logsAppend(items,replaceAll=false){
const box=document.getElementById('logsBox');
if(!box)return;
const atBottom=(box.scrollTop+box.clientHeight+8)>=box.scrollHeight;
if(replaceAll){box.textContent='';}
if(!Array.isArray(items)||items.length===0){
if(replaceAll && box.textContent.trim()==='')box.textContent='Brak logów';
return;
}
if(box.textContent==='Brak logów')box.textContent='';
items.forEach(it=>{
const ms=(it&&it.ms!==undefined)?it.ms:0;
const msg=(it&&it.msg)?String(it.msg):'';
box.textContent+=`[${ms}] ${msg}\n`;
});
if(atBottom){box.scrollTop=box.scrollHeight;}
}

function loadLogsNow(replaceAll=false){
const since=replaceAll?0:logsLastSeq;
const limit=replaceAll?120:40;
fetch(`/api/logs?since=${since}&limit=${limit}`).then(r=>r.json()).then(d=>{
if(document.getElementById('logsEnabled')){
document.getElementById('logsEnabled').checked=(d.enabled===true);
}
if(replaceAll){
logsLastSeq=0;
}
if(Array.isArray(d.logs) && d.logs.length>0){
logsAppend(d.logs,replaceAll);
const last=d.logs[d.logs.length-1];
if(last&&last.seq)logsLastSeq=last.seq;
}else if(replaceAll){
logsAppend([],true);
}
}).catch(e=>console.log('Error:',e));
}

function startLogsPolling(){
if(logsPollTimer)return;
logsPollTimer=setInterval(()=>{
const autoEl=document.getElementById('logsAuto');
if(autoEl && autoEl.checked===false)return;
const tabIndex=[...document.querySelectorAll('.tc')].findIndex(e=>e.classList.contains('a'));
if(tabIndex!==8)return;
loadLogsNow(false);
},1000);
}

function stopLogsPolling(){
if(!logsPollTimer)return;
clearInterval(logsPollTimer);
logsPollTimer=null;
}

function saveLogsConfig(){
const enabled=(document.getElementById('logsEnabled')?.checked===true);
const fd=new FormData();
fd.append('logsEnabled',enabled?'1':'0');
postJsonForm('/save-logs',fd).then(d=>{
if(!d.success){showToast('❌ Błąd zapisu logów',false);}
}).catch(e=>console.log('Error:',e));
}

function clearLogs(){
fetch('/clear-logs',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
logsLastSeq=0;
logsAppend([],true);
showToast('✓ Logi wyczyszczone',true);
}else{
showToast('❌ Błąd czyszczenia logów',false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia',false)});
}

function otaSetProgress(percent){
otaSetIndeterminate(false);
otaUiTargetProgress=Math.max(0,Math.min(100,Number(percent||0)));
if(!otaUiProgressTimer){
otaUiProgressTimer=setInterval(()=>{
const delta=otaUiTargetProgress-otaUiProgress;
if(Math.abs(delta)<0.2){
otaUiProgress=otaUiTargetProgress;
}else{
otaUiProgress+=delta*0.35;
}
const p=Math.max(0,Math.min(100,Math.round(otaUiProgress)));
const bar=document.getElementById('ota-progress-bar');
const txt=document.getElementById('ota-progress-text');
if(bar)bar.style.width=p+'%';
if(txt)txt.textContent=p+'%';
if(otaUiProgress===otaUiTargetProgress){
clearInterval(otaUiProgressTimer);
otaUiProgressTimer=null;
}
},50);
}
}

function loadOtaStatus(){
fetch('/api/ota-status').then(r=>r.json()).then(d=>{
const statusEl=document.getElementById('ota-status-text');
const versionEl=document.getElementById('ota-version-text');
const buildEl=document.getElementById('ota-build-text');
const sketchSizeEl=document.getElementById('ota-sketch-size');
const githubRemoteEl=document.getElementById('ota-github-remote');
const githubNewVersionMsgEl=document.getElementById('ota-new-version-msg');
const githubPhaseEl=document.getElementById('ota-github-phase');
const githubErrorEl=document.getElementById('ota-github-error');
const activeSlotEl=document.getElementById('ota-active-slot');
const nextSlotEl=document.getElementById('ota-next-slot');
const transferEl=document.getElementById('ota-transfer-text');
const uploadBtn=document.getElementById('ota-upload-btn');
const testBtn=document.getElementById('ota-test-btn');
if(statusEl)statusEl.textContent=d.status||'Gotowy';
if(versionEl)versionEl.textContent=d.version||'--';
if(buildEl)buildEl.textContent=(d.buildDate&&d.buildTime)?(d.buildDate+' '+d.buildTime):'--';
if(sketchSizeEl)sketchSizeEl.textContent=(d.sketchSize!==undefined)?(String(d.sketchSize)+' B'):'--';
if(githubRemoteEl)githubRemoteEl.textContent=d.githubRemoteVersion||'--';
if(githubNewVersionMsgEl)githubNewVersionMsgEl.style.display=d.githubUpdateAvailable===true?'block':'none';
if(githubPhaseEl)githubPhaseEl.textContent=d.githubPhaseText||'--';
if(githubErrorEl)githubErrorEl.textContent=d.githubLastError||'brak';
if(activeSlotEl)activeSlotEl.textContent=d.activeSlot||'--';
if(nextSlotEl)nextSlotEl.textContent=d.nextSlot||'--';
const total=Number(d.total||0);
const written=Number(d.written||0);
if(transferEl)transferEl.textContent=total>0?`${formatBytes(written)} / ${formatBytes(total)}`:`${formatBytes(written)} / ?`;
let percent=0;
if(total>0){
percent=(written*100.0)/total;
otaSetProgress(percent);
}else if(d.updating===true){
otaSetIndeterminate(true);
if(written===0){
otaSetProgress(1);
}
}else{
otaSetProgress(0);
}
if(uploadBtn)uploadBtn.disabled=(d.updating===true);
if(testBtn)testBtn.style.display=d.githubTestButtonEnabled===true?'inline-block':'none';
}).catch(e=>console.log('Error:',e));
}

// GitHub OTA Manual Check - Test button for triggering check
function startGithubOtaCheck(){
if(otaUploadActive){
showToast('❌ OTA upload w toku',false);
return;
}
const btn=document.querySelector('button[onclick*="startGithubOtaCheck"]');
if(btn)btn.disabled=true;
fetch('/api/github-ota-check',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
showToast('✓ '+(d.message||'Sprawdzono aktualizację'),true);
}else{
showToast('❌ '+(d.error||'Sprawdzenie zdalnej OTA nieudane'),false);
}
loadOtaStatus();
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia zdalnej OTA',false);})
.finally(()=>{if(btn)btn.disabled=false;});
}

function startGithubOtaInstall(){
if(otaUploadActive){
showToast('❌ OTA upload w toku',false);
return;
}
const btn=document.getElementById('ota-github-install-btn');
if(btn)btn.disabled=true;
fetch('/api/github-ota-install',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
showToast('✓ '+(d.message||'Pobieranie i instalacja OTA...'),true);
otaSetIndeterminate(true);
}else{
showToast('❌ '+(d.error||'Instalacja zdalnej OTA nieudana'),false);
}
loadOtaStatus();
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia zdalnej OTA',false);})
.finally(()=>{if(btn)btn.disabled=false;});
}

function switchOtaSlot(){
if(otaUploadActive){
showToast('❌ OTA upload w toku',false);
return;
}
if(!confirm('Przełączyć slot OTA i zrestartować urządzenie?'))return;
fetch('/api/ota-switch-slot',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
showToast('✓ Przełączono slot na '+(d.slot||'next')+', restart...',true);
}else{
showToast('❌ '+(d.error||'Nie udało się przełączyć slotu'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd przełączania slotu',false)});
}

function startOtaStatusPolling(){
if(otaPollTimer)return;
otaPollTimer=setInterval(()=>{
loadOtaStatus();
},1000);
}

function stopOtaStatusPolling(){
if(!otaPollTimer)return;
clearInterval(otaPollTimer);
otaPollTimer=null;
}

function startOtaUpload(){
if(otaUploadActive)return;
const fileInput=document.getElementById('ota-file');
const uploadBtn=document.getElementById('ota-upload-btn');
if(!fileInput||!fileInput.files||fileInput.files.length===0){
showToast('❌ Wybierz plik .bin',false);
return;
}
const fw=fileInput.files[0];
if(!String(fw.name||'').toLowerCase().endsWith('.bin')){
showToast('❌ Niepoprawny plik (wymagany .bin)',false);
return;
}
const fd=new FormData();
fd.append('firmware',fw);
otaUploadActive=true;
if(uploadBtn)uploadBtn.disabled=true;
otaSetProgress(0);

const xhr=new XMLHttpRequest();
xhr.open('POST','/ota-upload?fwsize='+encodeURIComponent(String(fw.size||0)),true);
xhr.setRequestHeader('X-Firmware-Size', String(fw.size||0));
xhr.upload.onprogress=(ev)=>{
if(ev.lengthComputable && ev.total>0){
otaSetProgress((ev.loaded*100.0)/ev.total);
}
};
xhr.onload=()=>{
otaUploadActive=false;
if(uploadBtn)uploadBtn.disabled=false;
let resp={};
try{resp=JSON.parse(xhr.responseText||'{}');}catch(_){resp={};}
if(xhr.status>=200 && xhr.status<300 && resp.success===true){
otaSetProgress(100);
showToast('✓ Firmware wysłany, restart urządzenia...',true);
}else{
const msg=resp.error||('HTTP '+xhr.status);
showToast('❌ OTA: '+msg,false);
loadOtaStatus();
}
};
xhr.onerror=()=>{
otaUploadActive=false;
if(uploadBtn)uploadBtn.disabled=false;
showToast('❌ Błąd połączenia podczas OTA',false);
loadOtaStatus();
};
xhr.send(fd);
}

const saveTimers={};
function debounceSave(key,fn,delayMs=350){
if(saveTimers[key])clearTimeout(saveTimers[key]);
saveTimers[key]=setTimeout(()=>{fn();delete saveTimers[key];},delayMs);
}
function postJsonForm(url,fd){
return fetch(url,{method:'POST',body:fd}).then(r=>r.json());
}
function showToast(text,ok=true){
const wrap=document.getElementById('toast-wrap');
if(!wrap)return;
const t=document.createElement('div');
t.className='toast'+(ok?'':' err');
t.textContent=text;
wrap.appendChild(t);
requestAnimationFrame(()=>t.classList.add('show'));
setTimeout(()=>{t.classList.remove('show');setTimeout(()=>t.remove(),220);},2200);
}
function showInlineStatus(id,text,ok=true,autoHideMs=1800){
const el=document.getElementById(id);
if(!el)return;
el.style.display='block';
el.textContent=text;
el.style.borderLeftColor=ok?'#4caf50':'#f44336';
if(autoHideMs>0){
setTimeout(()=>{if(el)el.style.display='none';},autoHideMs);
}
}

function loadQuotes(){
fetch('/api/quotes-list').then(r=>r.json()).then(q=>{
const c=document.getElementById('quotes-list');c.innerHTML='';
q.forEach((x,i)=>{c.insertAdjacentHTML('beforeend',`<div style='background:#222;padding:2px;margin:2px 0'>"${x}"<button type="button" onclick='deleteQuote(${i})' style='background:#600;padding:2px;font-size:0.75em;margin-top:2px;width:auto'>X</button></div>`)})}).catch(e=>console.log('Error:',e));
}
function triggerQuoteTest(){
fetch('/trigger-quote',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
showToast('✓ Cytat testowy wyświetlony',true);
}else{
showToast('❌ '+(d.error||'Brak cytatu'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia',false)});
}
function sendRuntimeMessage(){
const msgText=document.getElementById('msgTextInput');
if(!msgText.value.trim()){
showToast('❌ Wpisz wiadomość',false);
return;
}
const params=new URLSearchParams();
params.append('message',msgText.value);
params.append('message_time',5000);
params.append('message_color','00FF00');
fetch('/control_form',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()}).then(r=>r.json()).then(j=>{
if(j.ok){
showToast('✓ Wiadomość wysłana',true);
msgText.value='';
}else{
showToast('❌ Błąd: '+(j.error||'nieznany'),false);
}
}).catch(e=>showToast('❌ Błąd sieci: '+e,false));
}
function triggerClockAnimTest(){
fetch('/trigger-clock-anim',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Animacja uruchomiona',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd animacji'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockMirrorTest(){
fetch('/trigger-clock-mirror',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Odbicie uruchomione',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd lustra'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockRainbowTest(){
fetch('/trigger-clock-rainbow',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Tęcza uruchomiona',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd tęczy'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockHoursSlideTest(){
fetch('/trigger-clock-hours-slide',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Wyjazd/wjazd godzin uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd efektu godzin'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockMatrixFontTest(){
fetch('/trigger-clock-matrix-font',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Predator GLYPH uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd Predator GLYPH'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockMatrixSidewaysTest(){
fetch('/trigger-clock-matrix-sideways',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Matrix bokiem (2 rzędy) uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd Matrix bokiem'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockUpsideDownTest(){
fetch('/trigger-clock-upside-down',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Do góry nogami uruchomione',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd do góry nogami'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockRotate180Test(){
fetch('/trigger-clock-rotate-180',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Obrót 180° uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd obrotu 180°'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockFullRotateTest(){
fetch('/trigger-clock-full-rotate',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Pełny obrót uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd pełnego obrotu'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockMiddleSwapTest(){
fetch('/trigger-clock-middle-swap',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Efekt przejazdu wszystkich cyfr uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd efektu przejazdu wszystkich cyfr'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockTetrisTest(){
fetch('/trigger-clock-tetris',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Efekt Tetris uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd efektu Tetris'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockPileupTest(){
fetch('/trigger-clock-pileup',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Efekt karambolu uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd efektu karambolu'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockNegativeTest(){
fetch('/trigger-clock-negative',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Efekt negatywu uruchomiony',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd efektu negatywu'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function triggerClockRainbowBackgroundTest(){
fetch('/trigger-rainbow-background',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
setAnimTestStatus('✓ Tęczowe tło uruchomione',true);
}else{
setAnimTestStatus('❌ '+(d.error||'Błąd animacji'),false);
}
}).catch(e=>{console.log('Error:',e);setAnimTestStatus('❌ Błąd połączenia',false)});
}
function setAnimTestStatus(text,ok){
const el=document.getElementById('anim-test-status');
if(!el)return;
el.textContent=text;
el.style.borderLeftColor=ok?'#4caf50':'#f44336';
}
function deleteQuote(i){
if(confirm('Usunąć?'))fetch('/delete-quote?index='+i,{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
loadQuotes();
showToast('✓ Cytat usunięty',true);
}else{
showToast('❌ Błąd: '+(d.error||'Nieznany'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd usuwania cytatu',false)});
}
function exportQuotes(){
fetch('/api/quotes-export').then(r=>r.json()).then(data=>{
const text=JSON.stringify(data,null,2);
const blob=new Blob([text],{type:'application/json'});
const url=URL.createObjectURL(blob);
const a=document.createElement('a');
a.href=url;a.download='cytaty.json';a.click();
URL.revokeObjectURL(url);
alert('✓ Cytaty exportowane!');
showToast('✓ Cytaty wyeksportowane',true);
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd eksportu',false)});
}
function handleFileImport(e){
const file=e.target.files[0];
if(!file)return;
const reader=new FileReader();
reader.onload=ev=>{
try{
const data=JSON.parse(ev.target.result);
// Extract quotes array - handle both {"success":true,"quotes":[...]} and direct array
const quotesArr=Array.isArray(data)?data:data.quotes;
if(!Array.isArray(quotesArr))throw new Error('Invalid format');
const fd=new FormData();
fd.append('quotes',JSON.stringify(quotesArr));
fetch('/import-quotes',{method:'POST',body:fd}).then(r=>r.json()).then(d=>{
if(d.success){
loadQuotes();
showToast('✓ Cytaty zaimportowane',true);
}else{
showToast('❌ '+d.error,false);
}
document.getElementById('importFile').value='';
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd importu',false);document.getElementById('importFile').value=''});
}catch(err){
showToast('❌ Nieprawidłowy JSON',false);
document.getElementById('importFile').value='';
}
};
reader.readAsText(file);
}
document.getElementById('importFile')?.addEventListener('change',handleFileImport);
function escapeHtml(text){
return String(text)
.replace(/&/g,'&amp;')
.replace(/</g,'&lt;')
.replace(/>/g,'&gt;')
.replace(/\"/g,'&quot;')
.replace(/'/g,'&#39;');
}
function loadBirthdays(){
fetch('/api/birthdays').then(r=>r.json()).then(d=>{
const list=document.getElementById('birthdays-list');
if(!list)return;
const items=Array.isArray(d.birthdays)?d.birthdays:[];
if(items.length===0){
list.innerHTML="<div class='i'>Brak zapisanych urodzin</div>";
return;
}
list.innerHTML='';
items.forEach((it,idx)=>{
const name=escapeHtml(it.name||'');
const date=escapeHtml(it.date||'');
list.insertAdjacentHTML('beforeend',`<div class='i'><strong>${name}</strong><br><span>${date}</span><br><button type='button' onclick='deleteBirthday(${idx})' style='width:auto;padding:2px 8px;background:#600;margin-top:4px'>Usuń</button></div>`);
});
}).catch(e=>console.log('Error:',e));
}
function addBirthday(){
const nameEl=document.getElementById('bd-name');
const dateEl=document.getElementById('bd-date');
if(!nameEl||!dateEl)return;
const name=nameEl.value.trim();
const date=dateEl.value;
if(name.length===0){showToast('❌ Wpisz imię',false);return;}
if(!date){showToast('❌ Wybierz datę',false);return;}
const fd=new FormData();
fd.append('name',name);
fd.append('date',date);
fetch('/save-birthday',{method:'POST',body:fd}).then(r=>r.json()).then(d=>{
if(d.success){
nameEl.value='';
dateEl.value='';
loadBirthdays();
showToast('✓ Urodziny zapisane',true);
}else{
showToast('❌ '+(d.error||'Błąd zapisu'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia',false)});
}
function deleteBirthday(index){
if(!confirm('Usunąć urodziny?'))return;
fetch('/delete-birthday?index='+index,{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
loadBirthdays();
showToast('✓ Urodziny usunięte',true);
}else{
showToast('❌ '+(d.error||'Błąd usuwania'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia',false)});
}
function triggerBirthdayTest(){
fetch('/trigger-birthday-test',{method:'POST'}).then(r=>r.json()).then(d=>{
if(d.success){
showToast('✓ Test życzeń uruchomiony',true);
}else{
showToast('❌ '+(d.error||'Błąd testu'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia',false)});
}
function loadSchedule(){
return fetch('/api/schedule?t=' + Date.now()).then(r=>r.json()).then(d=>{
const l=document.getElementById('schedule-list');
l.innerHTML='';
const windows=Array.isArray(d.windows)?d.windows:[];
windows.forEach(w=>addScheduleItem(w));
}).catch(e=>console.log('Error:',e));
}
function addScheduleItem(existing){
const l=document.getElementById('schedule-list');
const id=Date.now()+'_'+Math.random().toString(36).substr(2,9);
const item=document.createElement('div');item.id='sch-'+id;
item.style.cssText='background:#111;padding:6px;margin:2px;border-left:2px solid #666;border-radius:4px';
item.innerHTML=`<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:4px'><label style='font-size:0.9em;margin:0'><input type='checkbox' class='sch-e' checked style='width:auto'> Aktywne</label><div style='display:flex;gap:4px;align-items:center'><input type='color' class='sch-c' value='#00ff00' style='width:40px;height:40px;border:none;padding:0;cursor:pointer;flex-shrink:0'><button type='button' onclick='document.getElementById("sch-${id}").remove()' style='width:24px;height:24px;padding:0;background:#600;border:none;color:#fff;cursor:pointer;border-radius:3px;font-size:0.8em'>X</button></div></div><div style='display:flex;gap:4px;margin-bottom:4px'><div style='flex:1'><label style='font-size:0.75em;display:block'>Od</label><input type='time' class='sch-start' value='07:00' style='width:100%;font-size:1.1em;padding:6px'></div><div style='flex:1'><label style='font-size:0.75em;display:block'>Do</label><input type='time' class='sch-end' value='22:00' style='width:100%;font-size:1.1em;padding:6px'></div></div><div style='margin-bottom:4px'><label style='font-size:0.75em;display:block'>Jasność: <span class='sch-bv'>120</span></label><input type='range' class='sch-b' min='1' max='255' value='120' style='width:100%;height:18px'></div>`;
if(existing){
item.querySelector('.sch-e').checked=(existing.enabled===1||existing.enabled===true);
item.querySelector('.sch-start').value=existing.start||'07:00';
item.querySelector('.sch-end').value=existing.end||'22:00';
const bVal=Math.min(255,Math.max(1,parseInt(existing.brightness)||120));
item.querySelector('.sch-b').value=bVal;
item.querySelector('.sch-bv').textContent=bVal;
item.querySelector('.sch-c').value=existing.color||'#00ff00';
}
const b=item.querySelector('.sch-b');
const bv=item.querySelector('.sch-bv');
b.addEventListener('input',()=>{bv.textContent=b.value;});
l.appendChild(item);
}
function saveSchedule(){
const items=document.querySelectorAll('[id^="sch-"]');
const windows=Array.from(items).map(item=>({
enabled:item.querySelector('.sch-e').checked?1:0,
start:item.querySelector('.sch-start').value||'00:00',
end:item.querySelector('.sch-end').value||'00:00',
brightness:parseInt(item.querySelector('.sch-b').value)||120,
color:item.querySelector('.sch-c').value||'#00ff00'
}));
console.log('Saving schedule, items count:', windows.length);
console.log('Schedule data:', JSON.stringify(windows));
fetch('/save-schedule',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(windows)}).then(r=>{
console.log('Response status:', r.status);
return r.json();
}).then(d=>{
console.log('Response data:', d);
if(d.success){
showToast('✓ Harmonogram jasności zapisany',true);
return loadSchedule();
}else{
showToast('❌ '+(d.error||'Błąd zapisu'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd połączenia',false)});
}
function loadWifiConfig(){
Promise.all([
fetch('/api/network-info').then(r=>r.json()).catch(()=>({})),
fetch('/api/quotes').then(r=>r.json()).catch(()=>({})),
fetch('/api/ntp-config').then(r=>r.json()).catch(()=>({}))
]).then(([net,cfg,ntp])=>{
document.getElementById('ssid-input').value=cfg.ssid||'';
document.getElementById('password-input').value=cfg.password||'';
document.getElementById('apPassword-input').value=cfg.apPassword||'';
// Jeśli DHCP włączony - pokazuj aktualny IP, jeśli wyłączony - static IP
const useDhcp=!net.useStaticIP;
document.getElementById('dhcp-input').checked=useDhcp;
const normNetVal=v=>(v===undefined||v===null||v==='--')?'':String(v);
const currentIP=normNetVal(net.currentIP);
const currentGateway=normNetVal(net.currentGateway);
const currentSubnet=normNetVal(net.currentSubnet);
const savedStaticIP=normNetVal(net.staticIP);
const savedStaticGateway=normNetVal(net.staticGateway);
const savedStaticSubnet=normNetVal(net.staticSubnet);
document.getElementById('staticIP-input').value=useDhcp?(currentIP||savedStaticIP):(savedStaticIP||currentIP);
document.getElementById('gateway-input').value=useDhcp?(currentGateway||savedStaticGateway):(savedStaticGateway||currentGateway);
document.getElementById('subnet-input').value=useDhcp?(currentSubnet||savedStaticSubnet):(savedStaticSubnet||currentSubnet);
const ipInput=document.getElementById('staticIP-input');
const gwInput=document.getElementById('gateway-input');
const snInput=document.getElementById('subnet-input');
let prevIsDhcp=useDhcp;
const updateInputs=()=>{
const isDhcp=document.getElementById('dhcp-input').checked;
ipInput.disabled=isDhcp;
gwInput.disabled=isDhcp;
snInput.disabled=isDhcp;
if(isDhcp){
ipInput.value=currentIP||savedStaticIP;
gwInput.value=currentGateway||savedStaticGateway;
snInput.value=currentSubnet||savedStaticSubnet;
}else{
if(prevIsDhcp){
if(!ipInput.value)ipInput.value=currentIP||savedStaticIP;
if(!gwInput.value)gwInput.value=currentGateway||savedStaticGateway;
if(!snInput.value)snInput.value=currentSubnet||savedStaticSubnet;
}
if(!ipInput.value)ipInput.value=savedStaticIP;
if(!gwInput.value)gwInput.value=savedStaticGateway||currentGateway;
if(!snInput.value)snInput.value=savedStaticSubnet||currentSubnet;
}
prevIsDhcp=isDhcp;
};
updateInputs();
document.getElementById('dhcp-input').addEventListener('change',updateInputs);
document.getElementById('ntpServer-input').value=ntp.ntpServer||'pool.ntp.org';
const tzSelect=document.getElementById('timezone-input');
const tzRaw=Number(ntp.timezone);
let tzValue='1';
if(Number.isFinite(tzRaw)){
const tzNormalized=(Math.abs(tzRaw)>24)?(tzRaw/3600):tzRaw;
if(Number.isInteger(tzNormalized)){
tzValue=String(tzNormalized);
}
}
if(tzSelect && Array.from(tzSelect.options).some(o=>o.value===tzValue)){
tzSelect.value=tzValue;
}
}).catch(e=>console.log('Error:',e));
}
function loadAnimationsConfig(){
fetch('/api/animations-config').then(r=>r.json()).then(cfg=>{
document.getElementById('ab').value=cfg.animBrightness||200;
document.getElementById('as').value=cfg.animSpeed||4;
document.getElementById('ms').value=cfg.msgSpeed||2;
document.getElementById('ac').value=cfg.animColor||'#FF0000';
document.getElementById('afreq').value=cfg.clockAnimInterval||10;
document.getElementById('fxMirror').checked=(cfg.fxMirror!==false);
document.getElementById('fxRainbow').checked=(cfg.fxRainbow!==false);
document.getElementById('fxHoursSlide').checked=(cfg.fxHoursSlide!==false);
document.getElementById('fxMatrixFont').checked=(cfg.fxMatrixFont!==false);
document.getElementById('fxMatrixSideways').checked=(cfg.fxMatrixSideways!==false);
document.getElementById('fxUpsideDown').checked=(cfg.fxUpsideDown!==false);
document.getElementById('fxRotate180').checked=(cfg.fxRotate180!==false);
document.getElementById('fxFullRotate').checked=(cfg.fxFullRotate!==false);
document.getElementById('fxMiddleSwap').checked=(cfg.fxMiddleSwap!==false);
document.getElementById('fxTetris').checked=(cfg.fxTetris!==false);
document.getElementById('fxPileup').checked=(cfg.fxPileup!==false);
document.getElementById('fxRainbowBackground').checked=(cfg.fxRainbowBackground!==false);
document.getElementById('displayRotate180').checked=(cfg.displayRotate180===true);
document.getElementById('displayNegative').checked=(cfg.displayNegative===true);
document.getElementById('fxQuotes').checked=(cfg.fxQuotes!==false);
const cp=document.getElementById('acp');
if(cp){cp.color=document.getElementById('ac').value;}
u();
}).catch(e=>console.log('Error:',e));
}

function loadLampConfig(){
fetch('/api/lamp-config').then(r=>r.json()).then(cfg=>{
lampModeEnabled=(cfg.enabled===true);
document.getElementById('lb').value=cfg.lampBrightness||64;
document.getElementById('lc').value=cfg.lampColor||'#FFFFFF';
const cp=document.getElementById('lcp');
if(cp){cp.color=document.getElementById('lc').value;}
updateLampUiState();
uLamp();
}).catch(e=>console.log('Error:',e));
}

function loadMqttConfig(){
fetch('/api/mqtt-config').then(r=>r.json()).then(cfg=>{
document.getElementById('mqttEnabled').checked=(cfg.enabled===true);
document.getElementById('mqttHost').value=cfg.host||'';
document.getElementById('mqttPort').value=cfg.port||1883;
document.getElementById('mqttUser').value=cfg.user||'';
document.getElementById('mqttPass').value=cfg.password||'';
document.getElementById('mqttPrefix').value=cfg.prefix||'homeassistant';
document.getElementById('mqttStatus').textContent=cfg.status||'--';
}).catch(e=>console.log('Error:',e));
}

function saveMqttConfig(){
const fd=new FormData(document.getElementById('mqtt-form'));
postJsonForm('/save-mqtt',fd)
.then(d=>{
if(d.success){
document.getElementById('mqttStatus').textContent=d.status||'zapisano';
showToast('✓ Zapisano ustawienia MQTT',true);
}else{
showToast('❌ '+(d.error||'Błąd MQTT'),false);
}
}).catch(e=>console.log('Error:',e));
}

function loadInstrukcja(){
const el=document.getElementById('instrukcja-content');
if(!el)return;
fetch('/api/instrukcja').then(r=>r.text()).then(html=>{
el.innerHTML=html;
}).catch(e=>{
el.innerHTML='<p style="color:#f00">Błąd ładowania instrukcji</p>';
console.log('Error loading instrukcja:',e);
});
}

// === ANIMACJE ===
function u(){
const b=document.getElementById('ab').value;
document.getElementById('bv').textContent=b;
const s=document.getElementById('as').value;
document.getElementById('asv').textContent=s;
const ms=document.getElementById('ms').value;
document.getElementById('msv').textContent=ms;
const f=parseInt(document.getElementById('afreq').value||'10',10);
const hrs=Math.floor(f/3600);
const mins=Math.floor((f%3600)/60);
const secs=f%60;
let label='';
if(hrs>0){
label=`${hrs} h`;
if(mins>0)label+=` ${mins} min`;
}else if(mins>0){
label=`${mins} min`;
if(secs>0)label+=` ${secs} s`;
}else{
label=`${secs} s`;
}
document.getElementById('afreqv').textContent=label;
}
function uLamp(){
const b=document.getElementById('lb').value;
document.getElementById('lbv').textContent=b;
}
function updateLampUiState(){
const status=document.getElementById('lampModeStatus');
const btn=document.getElementById('lampToggleBtn');
if(status){status.textContent=lampModeEnabled?'Włączona':'Wyłączona';}
if(btn){
btn.textContent=lampModeEnabled?'Wyłącz lampę':'Włącz lampę';
btn.classList.toggle('on',lampModeEnabled);
btn.classList.toggle('off',!lampModeEnabled);
}
}
function toggleLampMode(){
lampModeEnabled=!lampModeEnabled;
updateLampUiState();
saveLampConfig();
}
function saveAnimations(){
const fd=new FormData();
fd.append('animBrightness',document.getElementById('ab').value||'200');
fd.append('animSpeed',document.getElementById('as').value||'4');
fd.append('msgSpeed',document.getElementById('ms').value||'2');
fd.append('animColor',document.getElementById('ac').value||'#FF0000');
postJsonForm('/save-animations',fd)
.then(d=>{if(d.success)console.log('✓ '+(d.message||'Jasność zapisana'));else alert('❌ '+(d.message||'Błąd!'))}).catch(e=>console.log('Error:',e));
}
function saveAnimationVisuals(){debounceSave('animVisuals',saveAnimations,320);}
function saveAnimationSelection(){
const fd=new FormData();
['fxMirror','fxRainbow','fxHoursSlide','fxMatrixFont','fxMatrixSideways','fxUpsideDown','fxRotate180','fxFullRotate','fxMiddleSwap','fxTetris','fxPileup','fxRainbowBackground'].forEach(id=>{
const el=document.getElementById(id);
fd.append(id,(el&&el.checked)?'1':'0');
});
postJsonForm('/save-animations',fd)
.then(d=>{if(!d.success)console.log('❌ '+(d.message||'Błąd zapisu wyboru animacji'));}).catch(e=>console.log('Error:',e));
}
function saveNegativeToggle(){
const fd=new FormData();
const neg=document.getElementById('displayNegative');
fd.append('displayNegative',(neg&&neg.checked)?'1':'0');
postJsonForm('/save-animations',fd)
.then(d=>{if(!d.success)console.log('❌ '+(d.message||'Błąd zapisu negatywu'));}).catch(e=>console.log('Error:',e));
}
function saveMatrixRotateToggle(){
const fd=new FormData();
const rot=document.getElementById('displayRotate180');
fd.append('displayRotate180',(rot&&rot.checked)?'1':'0');
postJsonForm('/save-animations',fd)
.then(d=>{if(!d.success)console.log('❌ '+(d.message||'Błąd zapisu obrotu matrycy'));}).catch(e=>console.log('Error:',e));
}
function saveLampConfig(){
const fd=new FormData();
fd.append('displayLampMode',lampModeEnabled?'1':'0');
fd.append('lampBrightness',document.getElementById('lb').value||'180');
fd.append('lampColor',document.getElementById('lc').value||'#FFFFFF');
postJsonForm('/save-lamp',fd)
.then(d=>{if(!d.success)console.log('❌ '+(d.message||'Błąd zapisu lampy'));}).catch(e=>console.log('Error:',e));
}
function saveLampConfigDebounced(){debounceSave('lampConfig',saveLampConfig,320);}
function saveQuotesToggle(){
const fd=new FormData();
const q=document.getElementById('fxQuotes');
fd.append('fxQuotes',(q&&q.checked)?'1':'0');
postJsonForm('/save-animations',fd)
.then(d=>{if(!d.success)console.log('❌ '+(d.message||'Błąd zapisu cytatów'));}).catch(e=>console.log('Error:',e));
}
function saveAnimationInterval(){
const fd=new FormData();
fd.append('clockAnimInterval',document.getElementById('afreq').value||'10');
debounceSave('animInterval',()=>{
postJsonForm('/save-animations',fd)
.then(d=>{if(!d.success)console.log('❌ '+(d.message||'Błąd zapisu interwału'));}).catch(e=>console.log('Error:',e));
},320);
}
function restartDevice(){
if(confirm('Uruchomić ponownie?'))fetch('/restart');
}
function logoutPanel(){
if(!confirm('Wylogować z panelu?'))return;
fetch('/logout',{method:'POST',cache:'no-store'}).finally(()=>{
window.location.href='/?login='+Date.now();
});
}
document.addEventListener('DOMContentLoaded',()=>{
loadWifiConfig();loadQuotes();loadBirthdays();loadSchedule();loadAnimationsConfig();loadLampConfig();loadMqttConfig();
loadOtaStatus();
const cp=document.getElementById('acp');
const ac=document.getElementById('ac');
if(cp&&ac){
cp.addEventListener('color-changed',e=>{
ac.value=e.detail.value;
saveAnimationVisuals();
});
ac.addEventListener('input',()=>{
cp.color=ac.value;
saveAnimationVisuals();
});
}
const lcp=document.getElementById('lcp');
const lc=document.getElementById('lc');
if(lcp&&lc){
lcp.addEventListener('color-changed',e=>{
lc.value=e.detail.value;
saveLampConfigDebounced();
});
lc.addEventListener('input',()=>{
lcp.color=lc.value;
saveLampConfigDebounced();
});
}
const aqf=document.getElementById('addQuoteForm');
if(aqf)aqf.addEventListener('submit',(e)=>{
e.preventDefault();
const qInput=aqf.querySelector('input[name="quote"]');
const quote=qInput.value.trim();
if(quote.length===0){showToast('❌ Wpisz cytat',false);return}
const fd=new FormData();
fd.append('quote',quote);
fetch('/save-quote',{method:'POST',body:fd}).then(r=>r.json()).then(d=>{
if(d.success){
qInput.value='';
loadQuotes();
showToast('✓ Cytat dodany',true);
}else{
showToast('❌ Błąd: '+(d.error||'Nieznany'),false);
}
}).catch(e=>{console.log('Error:',e);showToast('❌ Błąd dodawania cytatu',false)});
});
const tf=document.getElementById('time-form');
if(tf)tf.addEventListener('submit',(e)=>{
e.preventDefault();
const fd=new FormData(tf);
postJsonForm('/save-time',fd).then(d=>{
if(d.success){
showInlineStatus('time-save-status','✓ '+(d.message||'Zapisano ustawienia czasu'),true,2000);
}else{
showInlineStatus('time-save-status','❌ '+(d.message||'Błąd zapisu czasu'),false,0);
}
}).catch(err=>{
console.log('Error:',err);
showInlineStatus('time-save-status','❌ Błąd połączenia',false,0);
});
});
u();
setInterval(()=>{
fetch('/api/status').then(r=>r.json()).then(d=>{
document.getElementById('ipaddr').textContent=d.ip||'--';
document.getElementById('ssid-info').textContent=d.ssid||'--';
document.getElementById('rssi-info').textContent=d.rssi||'--';
document.getElementById('current-time').textContent=d.time||'--:--:--';
document.getElementById('uptime').textContent=d.uptime||'--';
const flashInfo=document.getElementById('flash-info');
if(flashInfo){
const topPct=(typeof d.flashUsedPercent==='number')?Math.max(0,Math.min(100,d.flashUsedPercent)):null;
const topChipPct=(typeof d.flashChipUsedPercent==='number')?Math.max(0,Math.min(100,d.flashChipUsedPercent)):null;
if(topPct===null && topChipPct===null){
flashInfo.textContent='--';
}else{
const topSketchUsed=(topPct===null)?null:topPct;
const topSketchFree=(topPct===null)?null:(100-topPct);
const topChipUsed=(topChipPct===null)?null:topChipPct;
const topChipFree=(topChipPct===null)?null:(100-topChipPct);
const topSketchText=(topSketchUsed===null)?'Szkic --':'Szkic wolne '+topSketchFree+'% (zajęte '+topSketchUsed+'%)';
const topChipText=(topChipUsed===null)?'Kość --':'Kość wolne '+topChipFree+'% (zajęte '+topChipUsed+'%)';
flashInfo.textContent=topSketchText+' · '+topChipText;
}
}
document.getElementById('time-display').textContent=d.time||'--:--:--';
const wifiOk=(d.connected===true);
const mqttOk=((d.mqtt||'').toLowerCase()==='połączony');
const otaBusy=(d.ota===true);
const sysWifi=document.getElementById('sys-wifi');
const sysMqtt=document.getElementById('sys-mqtt');
const sysOta=document.getElementById('sys-ota');
const sysRam=document.getElementById('sys-ram');
if(sysWifi){sysWifi.textContent=wifiOk?'Połączone':'Offline';sysWifi.classList.toggle('ok',wifiOk);sysWifi.classList.toggle('bad',!wifiOk);}
if(sysMqtt){sysMqtt.textContent=mqttOk?'Połączony':'Rozłączony';sysMqtt.classList.toggle('ok',mqttOk);sysMqtt.classList.toggle('bad',!mqttOk);}
if(sysOta){sysOta.textContent=otaBusy?'Aktualizacja':'Gotowe';sysOta.classList.toggle('ok',!otaBusy);sysOta.classList.toggle('bad',otaBusy);}
if(sysRam){sysRam.textContent=(d.heap||'--')+' B';sysRam.classList.add('ok');sysRam.classList.remove('bad');}
}).catch(e=>console.log('Error:',e));
},5000);
});
</script>
</html>
)HTML";

WifiManager::WifiManager() : server(nullptr), lastReconnectAttempt(0), apEnabled(false), apPassword(WIFI_AP_PASSWORD) {}

void WifiManager::loadApPassword() {
    String stored = preferences.getString("apPassword", WIFI_AP_PASSWORD);
    stored.trim();
    if (stored.length() < 8 || stored.length() > 63) {
        stored = WIFI_AP_PASSWORD;
        preferences.putString("apPassword", stored);
    }
    apPassword = stored;
    Serial.print("[WiFi] AP/WWW haslo: ");
    Serial.println(apPassword);
}

bool WifiManager::ensureAuthenticated() {
    if (!server) return false;
    if (server->authenticate("admin", apPassword.c_str())) {
        return true;
    }
    server->requestAuthentication(BASIC_AUTH, "LED Matrix by ToPola", "Podaj haslo AP (uzytkownik: admin)");
    return false;
}

void WifiManager::handleLogout() {
    if (!server) return;
    if (server->method() == HTTP_GET) {
        server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server->sendHeader("Pragma", "no-cache");
        server->sendHeader("Expires", "0");
        server->sendHeader("Location", "/");
        server->send(302, "text/plain", "");
        return;
    }
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Expires", "0");
    server->sendHeader("WWW-Authenticate", "Basic realm=\"LED Matrix by ToPola\", charset=\"UTF-8\"");
    server->send(401, "text/plain; charset=utf-8", "Wylogowano z panelu.");
}

void WifiManager::begin(WebServer* webServer) {
    preferences.begin("wifi", false);
    bool logsEnabled = preferences.getUChar("logsEnabled", 1) == 1;
    app_logger_set_enabled(logsEnabled);
    app_logf("SYSTEM boot: logger=%s", logsEnabled ? "ON" : "OFF");
    loadConfig();
    applyStoredDisplaySettings(preferences);
    applyStoredMqttSettings(preferences);
    yield();
    delay(100);
    
    // Najpierw ustawić WiFi mode - przed WebServer
    Serial.println("[WiFi] Inicjowanie AP + STA mode");
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname("LedMatrixClock");  // Set hostname immediately after mode
    Serial.print("[WiFi] Hostname set to: ");
    Serial.println(WiFi.getHostname());
    yield();
    delay(200);
    
    setupAP();
    yield();
    delay(100);
    apEnabled = true;
    
    // Spróbuj STA connection jeśli mamy SSID
    if (config.ssid != "") {
        setupStation();
        yield();
        delay(100);
    }
    
    yield();
    delay(300);
    
    // Uruchamiaj web server dopiero po WiFi setup
    if (webServer) {
        setupWebServer(webServer);
        yield();
        delay(100);
    }
    
    Serial.println("[WiFi] Initialization complete");
    app_log("SYSTEM init complete");
}

void WifiManager::loop() {
    static bool staWasConnected = false;
    static bool ipShownEver = false;
    static uint32_t staConnectedSinceMs = 0;
    static bool mdnsBegan = false;

    if (server) {
        server->handleClient();
    }

    if (otaUpdating || externalOtaActive) {
        yield();
        delay(1);
        return;
    }

    bool staConnected = (WiFi.status() == WL_CONNECTED);
    uint32_t nowMs = millis();

    if (staConnected) {
        if (!staWasConnected) {
            staConnectedSinceMs = nowMs;
            
            // Initialize mDNS on first connection
            if (!mdnsBegan) {
                MDNS.begin("LedMatrixClock");
                mdnsBegan = true;
                Serial.println("[WiFi] mDNS started: LedMatrixClock.local");
            }
            
            app_logf("WIFI connected: ssid=%s ip=%s rssi=%d",
                     config.ssid.c_str(),
                     WiFi.localIP().toString().c_str(),
                     WiFi.RSSI());
        }

        // Show IP only once after boot (never again until restart)
        if (kShowIpOnStaConnect && !ipShownEver &&
            staConnectedSinceMs != 0 &&
            (nowMs - staConnectedSinceMs) >= 1200U) {
        String ipMsg = "IP: " + WiFi.localIP().toString();
        strlcpy(message_text, ipMsg.c_str(), sizeof(message_text));
        message_active = true;
        message_offset = LED_WIDTH;
        // Use saved message_speed from Preferences, default to 2 if not set
        message_speed = preferences.getUChar("msgSpeed", 2);
        if (message_speed < 1) message_speed = 1;
        if (message_speed > 10) message_speed = 10;
        uint16_t textPixels = (uint16_t)strlen(message_text) * 6U;
        uint16_t scrollPixels = (uint16_t)LED_WIDTH + textPixels + 1U;
        uint16_t perPixelMs = (message_speed > 0) ? (uint16_t)(30U / message_speed) : 30U;
        if (perPixelMs == 0) perPixelMs = 1;
        uint32_t fullScrollMs = (uint32_t)scrollPixels * (uint32_t)perPixelMs;
        message_time_left = fullScrollMs + 2500U;
        message_start_time = millis();
        message_color = CRGB::Cyan;
        Serial.printf("[WiFi] Showing IP on matrix: %s\n", ipMsg.c_str());
            ipShownEver = true;
        }
    } else {
        if (staWasConnected) {
            app_log("WIFI disconnected");
        }
        staConnectedSinceMs = 0;
    }
    staWasConnected = staConnected;
    
    // Auto-disable AP after 10 seconds of stable STA connection
    if (apEnabled && !apDisabledAfterSta && staConnected) {
        if (staConnectedTime == 0) {
            staConnectedTime = millis();
        } else if ((millis() - staConnectedTime) > 10000) {
            // Wyłącz AP mode po 10 sekundach stabilnego połączenia STA
            WiFi.mode(WIFI_STA);
            apEnabled = false;
            apDisabledAfterSta = true;
            Serial.println("[WiFi] AP disabled - stable STA connection maintained");
            app_log("WIFI AP disabled after stable STA");
        }
    }
    
    // Reset timer if connection lost
    if (!staConnected) {
        staConnectedTime = 0;
    }
    
    yield(); // Let FreeRTOS run other tasks
    delay(10); // Prevent watchdog issues
    reconnectIfNeeded();
}

bool WifiManager::isConnected() {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        static bool wasConnected = false;
        if (!wasConnected) {
            Serial.print("[WiFi] Połączono! IP: ");
            Serial.println(WiFi.localIP());
            wasConnected = true;
        }
    } else {
        static bool wasConnected = true;
        if (wasConnected) {
            Serial.println("[WiFi] Rozłączono!");
            wasConnected = false;
        }
    }
    return status == WL_CONNECTED;
}

String WifiManager::getLocalIP() {
    return WiFi.localIP().toString();
}

bool WifiManager::isOtaUpdating() {
    return otaUpdating || externalOtaActive;
}

void WifiManager::setExternalOtaActive(bool active) {
    externalOtaActive = active;
}

WifiConfig WifiManager::getConfig() {
    return config;
}

bool WifiManager::isAPMode() {
    return apEnabled; // AP jest zawsze włączony
}

void WifiManager::setupAP() {
    Serial.println("[WiFi] Setup AP (Access Point)");
    // WiFi.mode() już ustawiony w begin()
    WiFi.softAP(WIFI_AP_SSID, apPassword.c_str());
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_IP, IPAddress(255,255,255,0));
    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());
    app_logf("WIFI AP started: ssid=%s ip=%s", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void WifiManager::setupStation() {
    Serial.println("[WiFi] Konfiguracja Stacji (Station)");
    // Mode już ustawiony na WIFI_AP_STA w begin()
    // Hostname już ustawiony w begin() - nie powtarzamy
    Serial.print("[WiFi] Nazwa hosta: ");
    Serial.println(WiFi.getHostname());
    WiFi.begin(config.ssid.c_str(), config.password.c_str());
    Serial.print("[WiFi] Łączenie z SSID: ");
    Serial.println(config.ssid.c_str());
    app_logf("WIFI STA connect start: ssid=%s", config.ssid.c_str());
}

void WifiManager::setupWebServer(WebServer* webServer) {
    Serial.println("[WiFi] setupWebServer() called");
    server = webServer;
    const char* headerKeys[] = {"X-Firmware-Size", "Content-Length"};
    server->collectHeaders(headerKeys, 2);

    auto authWrap = [this](void (WifiManager::*handler)()) {
        return [this, handler]() {
            if (!ensureAuthenticated()) return;
            (this->*handler)();
        };
    };

    server->on("/", authWrap(&WifiManager::handleRoot));
    server->on("/save", HTTP_POST, authWrap(&WifiManager::handleSave));
    server->on("/logout", [this]() { handleLogout(); });
    server->on("/save-time", HTTP_POST, authWrap(&WifiManager::handleSaveTime));
    server->on("/save-animations", HTTP_POST, authWrap(&WifiManager::handleSaveAnimations));
    Serial.println("[WiFi] Registering /save-schedule endpoint");
    server->on("/save-schedule", HTTP_POST, authWrap(&WifiManager::handleSaveSchedule));
    server->on("/clear-schedule", HTTP_POST, authWrap(&WifiManager::handleClearSchedule));
    Serial.println("[WiFi] /save-schedule registered");
    server->on("/debug-schedule", [this]() { 
        Serial.println("[DEBUG] /debug-schedule endpoint called!");
        server->send(200, "text/plain", "Debug schedule endpoint works");
    });
    server->on("/resetwifi", authWrap(&WifiManager::handleResetWifi));
    server->on("/forget-wifi", HTTP_POST, authWrap(&WifiManager::handleForgetWifi));
    server->on("/restart", authWrap(&WifiManager::handleRestart));
    server->on("/api/status", authWrap(&WifiManager::handleApiStatus));
    server->on("/api/quotes", authWrap(&WifiManager::handleApiQuotes));
    server->on("/api/birthdays", authWrap(&WifiManager::handleApiBirthdays));
    server->on("/api/animations-config", authWrap(&WifiManager::handleApiAnimationsConfig));
    server->on("/api/schedule", authWrap(&WifiManager::handleApiSchedule));
    server->on("/api/quotes-list", authWrap(&WifiManager::handleApiQuotesList));
    server->on("/api/ntp-config", authWrap(&WifiManager::handleApiNtpConfig));
    server->on("/api/network-info", authWrap(&WifiManager::handleApiNetworkInfo));
    server->on("/api/wifi-scan", authWrap(&WifiManager::handleApiWifiScan));
    server->on("/api/mqtt-config", authWrap(&WifiManager::handleApiMqttConfig));
    server->on("/api/lamp-config", authWrap(&WifiManager::handleApiLampConfig));
    server->on("/api/logs", authWrap(&WifiManager::handleApiLogs));
    server->on("/save-logs-config", HTTP_POST, authWrap(&WifiManager::handleSaveLogsConfig));
    server->on("/api/ping", [this]() {
        if (!ensureAuthenticated()) return;
        server->send(200, "text/plain", "pong");
    });
    server->on("/save-quote", HTTP_POST, authWrap(&WifiManager::handleSaveQuote));
    server->on("/save-birthday", HTTP_POST, authWrap(&WifiManager::handleSaveBirthday));
    server->on("/save-mqtt", HTTP_POST, authWrap(&WifiManager::handleSaveMqtt));
    server->on("/save-logs", HTTP_POST, authWrap(&WifiManager::handleSaveLogsConfig));
    server->on("/save-lamp", HTTP_POST, authWrap(&WifiManager::handleSaveLampConfig));
    server->on("/clear-logs", HTTP_POST, authWrap(&WifiManager::handleClearLogs));
    server->on("/trigger-quote", HTTP_POST, authWrap(&WifiManager::handleTriggerQuote));
    server->on("/trigger-clock-anim", HTTP_POST, authWrap(&WifiManager::handleTriggerClockAnimation));
    server->on("/trigger-clock-mirror", HTTP_POST, authWrap(&WifiManager::handleTriggerClockMirror));
    server->on("/trigger-clock-rainbow", HTTP_POST, authWrap(&WifiManager::handleTriggerClockRainbow));
    server->on("/trigger-clock-hours-slide", HTTP_POST, authWrap(&WifiManager::handleTriggerClockHoursSlide));
    server->on("/trigger-clock-matrix-font", HTTP_POST, authWrap(&WifiManager::handleTriggerClockMatrixFont));
    server->on("/trigger-clock-matrix-sideways", HTTP_POST, authWrap(&WifiManager::handleTriggerClockMatrixSideways));
    server->on("/trigger-clock-upside-down", HTTP_POST, authWrap(&WifiManager::handleTriggerClockUpsideDown));
    server->on("/trigger-clock-rotate-180", HTTP_POST, authWrap(&WifiManager::handleTriggerClockRotate180));
    server->on("/trigger-clock-full-rotate", HTTP_POST, authWrap(&WifiManager::handleTriggerClockFullRotate));
    server->on("/trigger-clock-middle-swap", HTTP_POST, authWrap(&WifiManager::handleTriggerClockMiddleSwap));
    server->on("/trigger-clock-tetris", HTTP_POST, authWrap(&WifiManager::handleTriggerClockTetris));
    server->on("/trigger-clock-pileup", HTTP_POST, authWrap(&WifiManager::handleTriggerClockPileup));
    server->on("/trigger-clock-negative", HTTP_POST, authWrap(&WifiManager::handleTriggerClockNegative));
    server->on("/trigger-rainbow-background", HTTP_POST, authWrap(&WifiManager::handleTriggerRainbowBackground));
    server->on("/trigger-birthday-test", HTTP_POST, authWrap(&WifiManager::handleTriggerBirthdayTest));
    server->on("/delete-quote", HTTP_POST, authWrap(&WifiManager::handleDeleteQuote));
    server->on("/delete-birthday", HTTP_POST, authWrap(&WifiManager::handleDeleteBirthday));
    server->on("/api/quotes-export", authWrap(&WifiManager::handleExportQuotes));
    server->on("/import-quotes", HTTP_POST, authWrap(&WifiManager::handleImportQuotes));
    server->on("/save-quotes-enabled", HTTP_POST, authWrap(&WifiManager::handleSaveQuotesEnabled));
    server->on("/api/quotes-config", authWrap(&WifiManager::handleApiQuotesConfig));
    server->on("/api/ota-status", authWrap(&WifiManager::handleApiOtaStatus));
    server->on("/api/instrukcja", authWrap(&WifiManager::handleApiInstrukcja));
    server->on("/api/github-ota-check", HTTP_POST, authWrap(&WifiManager::handleApiGithubOtaCheck));
    server->on("/api/github-ota-install", HTTP_POST, authWrap(&WifiManager::handleApiGithubOtaInstall));
    server->on("/api/ota-switch-slot", HTTP_POST, authWrap(&WifiManager::handleApiOtaSwitchSlot));
    server->on("/api/colors-palette", authWrap(&WifiManager::handleApiColorsPalette));
    server->on("/ota-upload", HTTP_POST, [this]() {
        if (!ensureAuthenticated()) return;
    }, [this]() {
        if (!ensureAuthenticated()) return;
        handleOtaUpload();
    });
    server->begin();
}

void WifiManager::handleRoot() {
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->sendHeader("Expires", "0");

    // Jeśli nie ma połączenia STA - pokaż formularz AP (tylko WiFi)
    if (WiFi.status() != WL_CONNECTED) {
        String page = FPSTR(htmlPageAP);
        server->send(200, "text/html; charset=utf-8", page);
    } else {
        // Jeśli jest STA - pokaż pełny formularz bez zmian
        String page = FPSTR(htmlPageSTA);
        server->send(200, "text/html; charset=utf-8", page);
    }
}

void WifiManager::handleSave() {
    // Checkbox wysyła pole tylko gdy jest zaznaczony.
    // Brak pola "dhcp" = użytkownik wyłączył DHCP i chce statyczny IP.
    bool dhcpEnabled = server->hasArg("dhcp");
    String useStatic = dhcpEnabled ? "0" : "1";
    String requestedApPassword = server->arg("apPassword");
    requestedApPassword.trim();
    if (requestedApPassword.length() == 0) {
        requestedApPassword = apPassword;
    }

    if (requestedApPassword.length() < 8 || requestedApPassword.length() > 63) {
        server->send(400, "text/html", "<html><body><h1>Błąd: hasło AP musi mieć 8-63 znaki.</h1></body></html>");
        return;
    }

    if (requestedApPassword != apPassword) {
        apPassword = requestedApPassword;
        preferences.putString("apPassword", apPassword);
        Serial.print("[WiFi] AP/WWW haslo zmienione na: ");
        Serial.println(apPassword);
        app_log("WIFI AP/WWW password changed");
    }
    
    auto sendRestartPage = [this](const char* title) {
        String html = "<!DOCTYPE html><html lang='pl'><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<meta http-equiv='refresh' content='8;url=/'>";
        html += "<title>LED Matrix by ToPola</title>";
        html += "<style>body{margin:0;background:#0b0f14;color:#e8edf4;font-family:Arial,sans-serif;display:flex;min-height:100vh;align-items:center;justify-content:center;padding:16px}.card{max-width:420px;width:100%;background:#111923;border:1px solid #2c3a4a;border-radius:12px;padding:18px}.ok{color:#7ee787;font-weight:700;font-size:1.05em;margin-bottom:8px}.sub{color:#b8c7d9;font-size:.95em}</style></head><body>";
        html += "<div class='card'><div class='ok'>✓ ";
        html += title;
        html += "</div><div class='sub'>Urządzenie uruchamia się ponownie... Za chwilę wróć do panelu.</div></div></body></html>";
        server->send(200, "text/html; charset=utf-8", html);
    };

    String requestedStaticIP = server->arg("staticIP");
    String requestedStaticGateway = server->arg("staticGateway");
    String requestedStaticSubnet = server->arg("staticSubnet");

    if (useStatic == "1") {
        if (requestedStaticIP.length() == 0 || requestedStaticIP == "--") {
            IPAddress ip = WiFi.localIP();
            if (ip != INADDR_NONE && ip[0] != 0) requestedStaticIP = ip.toString();
        }
        if (requestedStaticGateway.length() == 0 || requestedStaticGateway == "--") {
            IPAddress gw = WiFi.gatewayIP();
            if (gw != INADDR_NONE && gw[0] != 0) requestedStaticGateway = gw.toString();
        }
        if (requestedStaticSubnet.length() == 0 || requestedStaticSubnet == "--") {
            IPAddress sn = WiFi.subnetMask();
            if (sn != INADDR_NONE && sn[0] != 0) requestedStaticSubnet = sn.toString();
        }
    }

    // Jeśli nie ma połączenia STA - zapisz tylko WiFi i restart
    if (WiFi.status() != WL_CONNECTED) {
        WifiConfig newConfig;
        newConfig.ssid = server->arg("ssid");
        newConfig.password = server->arg("password");
        saveConfig(newConfig);
        
        // Save IP settings
        preferences.putString("useStaticIP", useStatic);
        preferences.putString("staticIP", requestedStaticIP);
        preferences.putString("staticGateway", requestedStaticGateway);
        preferences.putString("staticSubnet", requestedStaticSubnet);
        
        Serial.print("[WiFi] DHCP: ");
        Serial.print(dhcpEnabled ? "on" : "off");
        Serial.print(" AP haslo: ");
        Serial.print(apPassword);
        Serial.print(" Static IP: ");
        Serial.print(requestedStaticIP.c_str());
        Serial.print(" Gateway: ");
        Serial.print(requestedStaticGateway.c_str());
        Serial.print(" Subnet: ");
        Serial.println(requestedStaticSubnet.c_str());
        
        app_logf("WIFI save+restart (offline): ssid=%s dhcp=%d staticIP=%s gw=%s",
             newConfig.ssid.c_str(),
             dhcpEnabled ? 1 : 0,
             requestedStaticIP.c_str(),
             requestedStaticGateway.c_str());

        sendRestartPage("WiFi zapisano");
        delay(500);
        restartESP();
    } else {
        // Jeśli jest STA - zapisz wszystko
        WifiConfig newConfig;
        newConfig.ssid = server->arg("ssid");
        newConfig.password = server->arg("password");
        saveConfig(newConfig);
        
        // Save IP settings
        preferences.putString("useStaticIP", useStatic);
        preferences.putString("staticIP", requestedStaticIP);
        preferences.putString("staticGateway", requestedStaticGateway);
        preferences.putString("staticSubnet", requestedStaticSubnet);

        Serial.print("[WiFi] DHCP: ");
        Serial.println(dhcpEnabled ? "on" : "off");
        
        Serial.print("[WiFi] Static IP: ");
        Serial.print(requestedStaticIP.c_str());
        Serial.print(" Gateway: ");
        Serial.print(requestedStaticGateway.c_str());
        Serial.print(" Subnet: ");
        Serial.println(requestedStaticSubnet.c_str());
        
        app_logf("WIFI save+restart: ssid=%s dhcp=%d staticIP=%s gw=%s",
                 newConfig.ssid.c_str(),
                 dhcpEnabled ? 1 : 0,
                 requestedStaticIP.c_str(),
                 requestedStaticGateway.c_str());

        sendRestartPage("Ustawienia zapisane");
        delay(500);
        restartESP();
    }
}

void WifiManager::handleResetWifi() {
    Serial.println("[WiFi] Reset konfiguracji WiFi!");
    app_log("WIFI full reset requested");
    preferences.clear();
    server->send(200, "text/html", "<html><body><h1>WiFi reset. Restart...</h1></body></html>");
    delay(500);
    restartESP();
}

void WifiManager::handleForgetWifi() {
    Serial.println("[WiFi] Usuwanie zapisanej sieci WiFi");
    app_log("WIFI forget saved network requested");

    preferences.remove("ssid");
    preferences.remove("password");
    preferences.remove("useStaticIP");
    preferences.remove("staticIP");
    preferences.remove("staticGateway");
    preferences.remove("staticSubnet");

    config.ssid = "";
    config.password = "";
    WiFi.disconnect(true, true);

    server->send(200, "text/html", "<html><body><h1>Usunięto zapisaną sieć. Restart...</h1></body></html>");
    delay(500);
    restartESP();
}

void WifiManager::handleRestart() {
    app_log("SYSTEM restart requested from WWW");
    server->send(200, "text/html", "<html><body><h1>Restart ESP...</h1></body></html>");
    delay(500);
    restartESP();
}

void WifiManager::restartESP() {
    ESP.restart();
}

void WifiManager::loadConfig() {
    config.ssid = preferences.getString("ssid", "");
    config.password = preferences.getString("password", "");
    loadApPassword();
}

void WifiManager::saveConfig(const WifiConfig& cfg) {
    preferences.putString("ssid", cfg.ssid);
    preferences.putString("password", cfg.password);
}

void WifiManager::resetWifiConfig() {
    Serial.println("[WiFi] Reset konfiguracji WiFi!");
    preferences.clear();
}

void WifiManager::reconnectIfNeeded() {
    if (otaUpdating || externalOtaActive) {
        return;
    }

    if (config.ssid != "" && WiFi.status() != WL_CONNECTED) {
        // Re-enable AP if connection was lost (for recovery)
        if (apDisabledAfterSta && !apEnabled) {
            WiFi.mode(WIFI_AP_STA);
            apEnabled = true;
            apDisabledAfterSta = false;
            Serial.println("[WiFi] STA connection lost - AP mode re-enabled for recovery");
            app_log("WIFI AP re-enabled after STA loss");
        }
        
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 5000) {
            Serial.println("[WiFi] Utracono połączenie STA. Próba ponownego połączenia...");
            WiFi.begin(config.ssid.c_str(), config.password.c_str());
            Serial.print("[WiFi] Ponowne łączenie z SSID: ");
            Serial.println(config.ssid.c_str());
            app_logf("WIFI reconnect attempt: ssid=%s", config.ssid.c_str());
            lastReconnectAttempt = now;
        }
    }
}

String WifiManager::getStatusHTML() {
    String html = "<html><body style='background:#222;color:#eee;font-family:Arial'><div style='max-width:400px;margin:40px auto;background:#333;padding:24px;border-radius:8px'><h1>Status LED Matrix</h1>";
    html += "<b>AP IP:</b> " + WiFi.softAPIP().toString() + "<br>";
    if (WiFi.status() == WL_CONNECTED) {
        html += "<b>STA IP:</b> " + getLocalIP() + "<br>";
        html += "<b>Status STA:</b> Połączony<br>";
    } else {
        html += "<b>Status STA:</b> Rozłączony<br>";
    }
    html += "<b>Tryb:</b> AP + STA (Dual Mode)<br>";
    html += "<b>AP SSID:</b> " + String(WIFI_AP_SSID) + "<br>";
    // Dodaj status MQTT (placeholder)
    html += "<b>Status MQTT:</b> TODO<br>";
    html += "<hr><a href='/resetwifi'><button style='background:#d32f2f;width:100%'>Reset WiFi</button></a>";
    html += "<a href='/restart'><button style='background:#388e3c;width:100%'>Restart ESP</button></a>";
    html += "</div></body></html>";
    return html;
}

void WifiManager::handleApiStatus() {
    String json = "{";
    
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    if (isConnected) {
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"ssid\":\"" + String(config.ssid.c_str()) + "\",";
        int rssi = WiFi.RSSI();
        json += "\"rssi\":" + String(rssi) + ",";
    } else {
        json += "\"ip\":\"--\",";
        json += "\"ssid\":\"--\",";
        json += "\"rssi\":0,";
    }
    
    unsigned long ms = millis();
    unsigned long uptime_sec = ms / 1000;
    unsigned long hours = uptime_sec / 3600;
    unsigned long minutes = (uptime_sec % 3600) / 60;
    unsigned long seconds = uptime_sec % 60;
    char uptimeStr[20];
    sprintf(uptimeStr, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    json += "\"uptime\":\"" + String(uptimeStr) + "\",";
    
    // Czas już zawiera offset ustawiony w NTPClient
    uint8_t h = timeClient.getHours();
    char timeStr[20];
    sprintf(timeStr, "%02u:%02u:%02u", h, timeClient.getMinutes(), timeClient.getSeconds());
    uint32_t sketchUsed = ESP.getSketchSize();
    uint32_t sketchTotal = sketchUsed + ESP.getFreeSketchSpace();
    uint32_t flashChipSize = ESP.getFlashChipSize();
    uint8_t flashUsedPercent = 0;
    uint8_t flashChipUsedPercent = 0;
    if (sketchTotal > 0U) {
        uint32_t pct = (sketchUsed * 100UL) / sketchTotal;
        if (pct > 100UL) pct = 100UL;
        flashUsedPercent = (uint8_t)pct;
    }
    if (flashChipSize > 0U) {
        uint32_t chipPct = (sketchUsed * 100UL) / flashChipSize;
        if (chipPct > 100UL) chipPct = 100UL;
        flashChipUsedPercent = (uint8_t)chipPct;
    }
    json += "\"time\":\"" + String(timeStr) + "\",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"flashUsedPercent\":" + String(flashUsedPercent) + ",";
    json += "\"flashUsedBytes\":" + String(sketchUsed) + ",";
    json += "\"flashTotalBytes\":" + String(sketchTotal) + ",";
    json += "\"flashChipUsedPercent\":" + String(flashChipUsedPercent) + ",";
    json += "\"flashChipBytes\":" + String(flashChipSize) + ",";
    json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"mqtt\":\"" + mqtt_manager_getStatus() + "\",";
    json += "\"ntp\":\"OK\",";
    json += "\"ota\":" + String(isOtaUpdating() ? "true" : "false") + ",";
    json += "\"connected\":" + String(isConnected ? "true" : "false");
    json += "}";
    
    server->send(200, "application/json", json);
}

void WifiManager::handleApiQuotes() {
    // Zwraca aktualne wartości WiFi w JSON
    String json = "{";
    json += "\"ssid\":\"" + String(config.ssid.c_str()) + "\",";
    json += "\"password\":\"" + String(config.password.c_str()) + "\",";
    json += "\"apPassword\":\"" + apPassword + "\"";
    json += "}";
    
    server->send(200, "application/json", json);
}

void WifiManager::handleApiAnimationsConfig() {
    String animBrightness = preferences.getString("animBrightness", "200");
    uint8_t animSpeed = (uint8_t)constrain((int)preferences.getUChar("animSpeed", 4), 1, 10);
    uint8_t msgSpeed = (uint8_t)constrain((int)preferences.getUChar("msgSpeed", 2), 1, 10);
    String animColor = preferences.getString("animColor", "#FF0000");
    uint16_t clockAnimInterval = loadStoredClockAnimIntervalSeconds(preferences);
    bool fxMirror = preferences.getUChar("fxMirror", 1) == 1;
    bool fxRainbow = preferences.getUChar("fxRainbow", 1) == 1;
    bool fxHoursSlide = preferences.getUChar("fxHoursSlide", 1) == 1;
    bool fxMatrixFont = preferences.getUChar("fxMatrixFont", 1) == 1;
    bool fxMatrixSideways = preferences.getUChar("fxMatrixSideways", 1) == 1;
    bool fxUpsideDown = preferences.getUChar("fxUpsideDown", 1) == 1;
    bool fxRotate180 = preferences.getUChar("fxRotate180", 1) == 1;
    bool fxFullRotate = preferences.getUChar("fxFullRotate", 1) == 1;
    bool fxMiddleSwap = preferences.getUChar("fxMiddleSwap", 1) == 1;
    bool fxTetris = preferences.getUChar("fxTetris", 1) == 1;
    bool fxPileup = preferences.getUChar("fxPileup", 1) == 1;
    bool fxRainbowBackground = preferences.getUChar("fxRainbowBackground", 1) == 1;
    bool displayRotate180 = preferences.getUChar("displayRotate180", 1) == 1;
    bool displayNegative = preferences.getUChar("displayNegative", 0) == 1;
    bool fxQuotes = preferences.getUChar("quotes_enabled", 1) == 1;
    String json = "{";
    json += "\"animBrightness\":" + animBrightness + ",";
    json += "\"animSpeed\":" + String(animSpeed) + ",";
    json += "\"msgSpeed\":" + String(msgSpeed) + ",";
    json += "\"animColor\":\"" + animColor + "\",";
    json += "\"clockAnimInterval\":" + String(clockAnimInterval) + ",";
    json += "\"fxMirror\":" + String(fxMirror ? "true" : "false") + ",";
    json += "\"fxRainbow\":" + String(fxRainbow ? "true" : "false") + ",";
    json += "\"fxHoursSlide\":" + String(fxHoursSlide ? "true" : "false") + ",";
    json += "\"fxMatrixFont\":" + String(fxMatrixFont ? "true" : "false") + ",";
    json += "\"fxMatrixSideways\":" + String(fxMatrixSideways ? "true" : "false") + ",";
    json += "\"fxUpsideDown\":" + String(fxUpsideDown ? "true" : "false") + ",";
    json += "\"fxRotate180\":" + String(fxRotate180 ? "true" : "false") + ",";
    json += "\"fxFullRotate\":" + String(fxFullRotate ? "true" : "false") + ",";
    json += "\"fxMiddleSwap\":" + String(fxMiddleSwap ? "true" : "false") + ",";
    json += "\"fxTetris\":" + String(fxTetris ? "true" : "false") + ",";
    json += "\"fxPileup\":" + String(fxPileup ? "true" : "false") + ",";
    json += "\"fxRainbowBackground\":" + String(fxRainbowBackground ? "true" : "false") + ",";
    json += "\"displayRotate180\":" + String(displayRotate180 ? "true" : "false") + ",";
    json += "\"displayNegative\":" + String(displayNegative ? "true" : "false") + ",";
    json += "\"fxQuotes\":" + String(fxQuotes ? "true" : "false");
    json += "}";
    server->send(200, "application/json", json);
}

void WifiManager::handleApiLampConfig() {
    bool lampEnabled = preferences.getUChar("displayLampMode", 0) == 1;
    int lampBrightness = constrain(preferences.getString("lampBrightness", String((int)kLampDefaultBrightness)).toInt(), 1, 255);
    String lampColor = preferences.getString("lampColor", "#FFFFFF");

    String json = "{";
    json += "\"enabled\":" + String(lampEnabled ? "true" : "false") + ",";
    json += "\"lampBrightness\":" + String(lampBrightness) + ",";
    json += "\"lampColor\":\"" + lampColor + "\"";
    json += "}";
    server->send(200, "application/json", json);
}

void WifiManager::handleApiLogs() {
    uint32_t since = 0;
    if (server->hasArg("since")) {
        since = (uint32_t)server->arg("since").toInt();
    }

    uint16_t limit = 80;
    if (server->hasArg("limit")) {
        int requested = server->arg("limit").toInt();
        requested = constrain(requested, 1, 200);
        limit = (uint16_t)requested;
    }

    String json;
    app_logger_build_json(since, limit, json);
    server->send(200, "application/json", json);
}

void WifiManager::handleSaveLogsConfig() {
    auto boolArgValue = [this](const char* key, bool fallback) -> bool {
        if (!server->hasArg(key)) return fallback;
        String v = server->arg(key);
        v.toLowerCase();
        return (v == "1" || v == "true" || v == "on" || v == "yes");
    };

    bool enabled = boolArgValue("logsEnabled", app_logger_is_enabled());
    if (!enabled) {
        app_log("LOGS config changed: OFF");
    }

    preferences.putUChar("logsEnabled", enabled ? 1 : 0);
    app_logger_set_enabled(enabled);

    if (enabled) {
        app_log("LOGS config changed: ON");
    }

    String response = "{\"success\":true,\"enabled\":";
    response += (enabled ? "true" : "false");
    response += "}";
    server->send(200, "application/json", response);
}

void WifiManager::handleClearLogs() {
    app_log("LOGS clear requested from WWW");
    app_logger_clear();
    server->send(200, "application/json", "{\"success\":true}");
}

void WifiManager::handleApiNetworkInfo() {
    // Zwraca info o sieci + ustawienia static IP
    String json = "{";
    
    // Pobierz aktualne IP (jeśli połączone) i preferencesx (szybkie odczyt)
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    String staticIP = preferences.getString("staticIP", "");
    String staticGateway = preferences.getString("staticGateway", "");
    String staticSubnet = preferences.getString("staticSubnet", "");
    String useStaticStr = preferences.getString("useStaticIP", "0");
    int useStaticValue = (useStaticStr == "1") ? 1 : 0;
    
    // Pokaż aktualny IP (z WiFi) lub "--" jeśli nie połączone
    String currentIP = "--";
    String currentGateway = "--";
    String currentSubnet = "--";
    
    if (isConnected) {
        currentIP = WiFi.localIP().toString();
        currentGateway = WiFi.gatewayIP().toString();
        currentSubnet = WiFi.subnetMask().toString();
    }
    
    json += "\"currentIP\":\"" + currentIP + "\",";
    json += "\"currentGateway\":\"" + currentGateway + "\",";
    json += "\"currentSubnet\":\"" + currentSubnet + "\",";
    json += "\"useStaticIP\":" + String(useStaticValue) + ",";
    json += "\"staticIP\":\"" + staticIP + "\",";
    json += "\"staticGateway\":\"" + staticGateway + "\",";
    json += "\"staticSubnet\":\"" + staticSubnet + "\"";
    json += "}";
    
    server->send(200, "application/json", json);
}

void WifiManager::handleApiWifiScan() {
    DynamicJsonDocument doc(4096);
    doc["success"] = false;
    JsonArray networks = doc.createNestedArray("networks");

    WiFi.scanDelete();
    int found = WiFi.scanNetworks(false, true);

    if (found < 0) {
        doc["error"] = "scan_failed";
        String out;
        serializeJson(doc, out);
        server->send(500, "application/json", out);
        return;
    }

    const int maxResults = 20;
    int added = 0;
    for (int i = 0; i < found && added < maxResults; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;

        JsonObject entry = networks.createNestedObject();
        entry["ssid"] = ssid;
        entry["rssi"] = WiFi.RSSI(i);
        entry["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        added++;
    }

    WiFi.scanDelete();

    doc["success"] = true;
    doc["count"] = added;
    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

void WifiManager::handleApiMqttConfig() {
    String host = preferences.getString("mqttHost", "");
    uint16_t port = preferences.getUShort("mqttPort", 1883);
    String user = preferences.getString("mqttUser", "");
    String pass = preferences.getString("mqttPass", "");
    String prefix = preferences.getString("mqttPrefix", "homeassistant");
    bool enabled = preferences.getUChar("mqttEnabled", 0) == 1;

    String json = "{";
    json += "\"enabled\":" + String(enabled ? "true" : "false") + ",";
    json += "\"host\":\"" + host + "\",";
    json += "\"port\":" + String(port) + ",";
    json += "\"user\":\"" + user + "\",";
    json += "\"password\":\"" + pass + "\",";
    json += "\"prefix\":\"" + prefix + "\",";
    json += "\"status\":\"" + mqtt_manager_getStatus() + "\"";
    json += "}";
    server->send(200, "application/json", json);
}

void WifiManager::handleSaveMqtt() {
    bool enabled = server->hasArg("mqttEnabled");
    String host = server->arg("mqttHost");
    uint16_t port = (uint16_t)constrain(server->arg("mqttPort").toInt(), 1, 65535);
    if (port == 0) port = 1883;
    String user = server->arg("mqttUser");
    String pass = server->arg("mqttPass");
    String prefix = server->arg("mqttPrefix");
    if (prefix.length() == 0) prefix = "homeassistant";

    preferences.putUChar("mqttEnabled", enabled ? 1 : 0);
    preferences.putString("mqttHost", host);
    preferences.putUShort("mqttPort", port);
    preferences.putString("mqttUser", user);
    preferences.putString("mqttPass", pass);
    preferences.putString("mqttPrefix", prefix);

    mqtt_manager_configure(enabled, host.c_str(), port, user.c_str(), pass.c_str(), prefix.c_str());
    mqtt_manager_publish_now();
    app_logf("MQTT config saved: enabled=%d host=%s port=%u prefix=%s user=%s",
             enabled ? 1 : 0,
             host.c_str(),
             port,
             prefix.c_str(),
             user.c_str());

    String json = "{";
    json += "\"success\":true,";
    json += "\"message\":\"Ustawienia MQTT zapisane\",";
    json += "\"status\":\"" + mqtt_manager_getStatus() + "\"";
    json += "}";
    server->send(200, "application/json", json);
}

void WifiManager::handleApiQuotesList() {
    String quotesJson = quotes_getJson();
    // Extract quotes array
    DynamicJsonDocument doc(QUOTES_JSON_DOC_SIZE);
    DeserializationError error = deserializeJson(doc, quotesJson);
    
    if (error) {
        Serial.printf("[WiFi] JSON parse error: %s\n", error.c_str());
        server->send(500, "application/json", "{\"error\":\"JSON parse failed\"}");
        return;
    }
    
    JsonArray arr = doc["quotes"];
    if (!arr) {
        server->send(500, "application/json", "{\"error\":\"No quotes array\"}");
        return;
    }
    
    // Use DynamicJsonDocument for proper JSON serialization with escaping
    DynamicJsonDocument responseDoc(QUOTES_JSON_DOC_SIZE);
    JsonArray respArr = responseDoc.createNestedArray();
    
    for (JsonVariant v : arr) {
        respArr.add(v.as<const char*>());
    }
    
    String response;
    serializeJson(respArr, response);
    server->send(200, "application/json; charset=utf-8", response);
}

void WifiManager::handleApiBirthdays() {
    String birthdaysJson = preferences.getString(kBirthdaysPrefKey, "[]");

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, birthdaysJson);
    if (err || !doc.is<JsonArray>()) {
        birthdaysJson = "[]";
    }

    String response = "{\"birthdays\":" + birthdaysJson + "}";
    server->send(200, "application/json; charset=utf-8", response);
}

void WifiManager::handleSaveQuote() {
    if (server->hasArg("quote")) {
        String newQuote = server->arg("quote");
        if (newQuote.length() > 0 && newQuote.length() <= MAX_QUOTE_LENGTH - 1) {
            if (quotes_add(newQuote.c_str())) {
                Serial.printf("[WiFi] Dodano cytat: %s\n", newQuote.c_str());
                server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
            } else {
                server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Quote storage full\"}");
            }
        } else {
            server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Quote too long\"}");
        }
    } else {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Missing quote text\"}");
    }
}

void WifiManager::handleSaveBirthday() {
    if (!server->hasArg("name") || !server->hasArg("date")) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Brak danych\"}");
        return;
    }

    String name = server->arg("name");
    String date = server->arg("date");
    name.trim();
    date.trim();

    if (name.length() == 0 || name.length() > 40) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Nieprawidłowe imię\"}");
        return;
    }
    if (!isValidIsoDateYYYYMMDD(date)) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Nieprawidłowa data\"}");
        return;
    }

    String birthdaysJson = preferences.getString(kBirthdaysPrefKey, "[]");
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, birthdaysJson);
    if (err || !doc.is<JsonArray>()) {
        doc.clear();
        doc.to<JsonArray>();
    }

    JsonArray arr = doc.as<JsonArray>();
    JsonObject entry = arr.createNestedObject();
    entry["name"] = name;
    entry["date"] = date;

    String out;
    serializeJson(arr, out);
    preferences.putString(kBirthdaysPrefKey, out.c_str());

    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerQuote() {
    if (numQuotes == 0) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Brak cytatów\"}");
        return;
    }

    char* selectedQuote = quotes_getRandom();
    if (!selectedQuote || selectedQuote[0] == '\0') {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Nie udało się pobrać cytatu\"}");
        return;
    }

    message_active = false;
    display_mode = DISPLAY_MODE_QUOTE;
    effects_quotes(selectedQuote);
    display_enabled = true;

    Serial.printf("[WiFi] Trigger quote test: %s\n", selectedQuote);
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockAnimation() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    if (!display_triggerFunClockAnyEnabled()) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Brak aktywnych efektów cyfr\"}");
        return;
    }
    Serial.println("[WiFi] Trigger clock animation test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockMirror() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockMirror();
    Serial.println("[WiFi] Trigger clock mirror test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockRainbow() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockRainbow();
    Serial.println("[WiFi] Trigger clock rainbow test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockHoursSlide() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockHoursSlide();
    Serial.println("[WiFi] Trigger clock hours slide test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockMatrixFont() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockMatrixFont();
    Serial.println("[WiFi] Trigger clock Predator GLYPH test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockMatrixSideways() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockMatrixSideways();
    Serial.println("[WiFi] Trigger clock Matrix sideways test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockUpsideDown() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockUpsideDown();
    Serial.println("[WiFi] Trigger clock upside-down test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockRotate180() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockRotate180();
    Serial.println("[WiFi] Trigger clock rotate-180 test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockFullRotate() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockFullRotate();
    Serial.println("[WiFi] Trigger clock full-rotate test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockMiddleSwap() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockMiddleSwap();
    Serial.println("[WiFi] Trigger clock middle-swap test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockTetris() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockTetris();
    Serial.println("[WiFi] Trigger clock Tetris test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockPileup() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockPileup();
    Serial.println("[WiFi] Trigger clock pileup test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerClockNegative() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockNegative();
    Serial.println("[WiFi] Trigger clock negative test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerRainbowBackground() {
    display_mode = DISPLAY_MODE_CLOCK;
    scheduler_snoozeQuotes(45000U);
    display_triggerFunClockRainbowBackground();
    Serial.println("[WiFi] Trigger rainbow background test");
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleTriggerBirthdayTest() {
    String birthdaysJson = preferences.getString(kBirthdaysPrefKey, "[]");

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, birthdaysJson);
    if (err || !doc.is<JsonArray>()) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Brak listy urodzin\"}");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Dodaj najpierw urodziny\"}");
        return;
    }

    JsonObject selected = arr[(size_t)random(0, (long)arr.size())];
    const char* rawName = selected["name"] | "Solenizant";

    static const char* templates[] = {
        "Sto Lat %s!",
        "Wszystkiego Najlepszego %s!",
        "Szczęścia i Zdrowia %s!",
        "Dużo Radości %s!",
        "Spełnienia Marzeń %s!",
        "Najlepsze Życzenia %s!"
    };
    uint8_t tplIdx = (uint8_t)random(0, (long)(sizeof(templates) / sizeof(templates[0])));

    char greeting[sizeof(message_text)];
    snprintf(greeting, sizeof(greeting), templates[tplIdx], rawName ? rawName : "Solenizant");
    greeting[sizeof(greeting) - 1] = '\0';

    strlcpy(message_text, greeting, sizeof(message_text));
    message_active = true;
    message_offset = LED_WIDTH;
    message_time_left = (uint32_t)random(9000UL, 15001UL);
    message_start_time = millis();
    message_color = CHSV((uint8_t)random(0, 256), 230, 255);
    display_mode = DISPLAY_MODE_MESSAGE;
    display_enabled = true;
    scheduler_snoozeQuotes(45000U);

    Serial.printf("[WiFi] Trigger birthday test: %s\n", message_text);
    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleDeleteQuote() {
    if (server->hasArg("index")) {
        int index = server->arg("index").toInt();
        if (index >= 0 && index < numQuotes) {
            if (quotes_remove(index)) {
                Serial.printf("[WiFi] Usunięto cytat o indeksie %d\n", index);
                server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
            } else {
                server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Failed to remove quote\"}");
            }
        } else {
            server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Invalid index\"}");
        }
    } else {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Missing index\"}");
    }
}

void WifiManager::handleDeleteBirthday() {
    if (!server->hasArg("index")) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Brak indeksu\"}");
        return;
    }

    int index = server->arg("index").toInt();
    String birthdaysJson = preferences.getString(kBirthdaysPrefKey, "[]");

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, birthdaysJson);
    if (err || !doc.is<JsonArray>()) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Brak listy urodzin\"}");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (index < 0 || index >= (int)arr.size()) {
        server->send(400, "application/json; charset=utf-8", "{\"success\":false,\"error\":\"Nieprawidłowy indeks\"}");
        return;
    }

    arr.remove((size_t)index);
    String out;
    serializeJson(arr, out);
    preferences.putString(kBirthdaysPrefKey, out.c_str());

    server->send(200, "application/json; charset=utf-8", "{\"success\":true}");
}

void WifiManager::handleSaveTime() {
    if (server->hasArg("ntpServer") && server->hasArg("timezone")) {
        String ntpServer = server->arg("ntpServer");
        int32_t tzRaw = server->arg("timezone").toInt();
        int32_t tzSeconds = clock_normalizeTimezoneOffset(tzRaw);
        
        // Zapisz do preferences (seconds)
        preferences.putString("ntpServer", ntpServer.c_str());
        preferences.putString("timezone", String(tzSeconds).c_str());

        // Zapisz do głównej konfiguracji używanej przez zegar LED
        strlcpy(mainConfig.display.ntpServer, ntpServer.c_str(), sizeof(mainConfig.display.ntpServer));
        mainConfig.display.timezone = (int16_t)tzSeconds;
        ::saveConfig();

        // Zastosuj natychmiast bez restartu
        timeClient.setPoolServerName(mainConfig.display.ntpServer);
        timeClient.setTimeOffset(mainConfig.display.timezone);
        timeClient.update();
        
        Serial.printf("[WiFi] NTP Server: %s\n", ntpServer.c_str());
        Serial.printf("[WiFi] Timezone raw=%ld, applied=%lds\n", (long)tzRaw, (long)tzSeconds);
        app_logf("TIME config saved: ntp=%s tz_raw=%ld tz_sec=%ld",
             ntpServer.c_str(),
             (long)tzRaw,
             (long)tzSeconds);
        mqtt_manager_publish_now();
        
        // Zwróć JSON zamiast HTML
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Ustawienia czasu zapisane\"}");
    } else {
        server->send(400, "application/json", "{\"success\":false,\"message\":\"Brak wymaganych pól\"}");
    }
}

void WifiManager::handleApiNtpConfig() {
    String json = "{";
    
    String ntpServer = preferences.getString("ntpServer", "pool.ntp.org");
    String timezone = preferences.getString("timezone", String(mainConfig.display.timezone));
    
    json += "\"ntpServer\":\"" + ntpServer + "\",";
    json += "\"timezone\":\"" + timezone + "\"";
    
    json += "}";
    
    server->send(200, "application/json", json);
}

void WifiManager::handleSaveAnimations() {
    auto boolArgValue = [this](const char* key, bool fallback) -> bool {
        if (!server->hasArg(key)) return fallback;
        String v = server->arg(key);
        v.toLowerCase();
        return (v == "1" || v == "true" || v == "on" || v == "yes");
    };

    bool hasBrightnessArg = server->hasArg("animBrightness");
    bool hasAnimSpeedArg = server->hasArg("animSpeed") || server->hasArg("animation_speed");
    bool hasMsgSpeedArg = server->hasArg("msgSpeed");
    bool hasColorArg = server->hasArg("animColor");
    bool hasAnyAnimToggleArg =
        server->hasArg("fxMirror") ||
        server->hasArg("fxRainbow") ||
        server->hasArg("fxHoursSlide") ||
        server->hasArg("fxMatrixFont") ||
        server->hasArg("fxMatrixSideways") ||
        server->hasArg("fxUpsideDown") ||
        server->hasArg("fxRotate180") ||
        server->hasArg("fxFullRotate") ||
        server->hasArg("fxMiddleSwap") ||
        server->hasArg("fxTetris") ||
        server->hasArg("fxPileup") ||
        server->hasArg("fxRainbowBackground") ||
        server->hasArg("displayRotate180") ||
        server->hasArg("displayNegative") ||
        server->hasArg("fxQuotes");
    bool fullAnimFormUpdate = server->hasArg("fullAnimForm") || hasBrightnessArg || hasColorArg || hasAnyAnimToggleArg;

    int brightness = hasBrightnessArg
        ? constrain(server->arg("animBrightness").toInt(), 1, 255)
        : constrain(preferences.getString("animBrightness", "200").toInt(), 1, 255);
    int animSpeed = hasAnimSpeedArg
        ? constrain((server->hasArg("animSpeed") ? server->arg("animSpeed").toInt() : server->arg("animation_speed").toInt()), 1, 10)
        : constrain((int)preferences.getUChar("animSpeed", 4), 1, 10);
    int msgSpeed = hasMsgSpeedArg
        ? constrain(server->arg("msgSpeed").toInt(), 1, 10)
        : constrain((int)preferences.getUChar("msgSpeed", 2), 1, 10);
    int clockAnimInterval = server->hasArg("clockAnimInterval")
        ? constrain(server->arg("clockAnimInterval").toInt(), 10, 3600)
        : (int)loadStoredClockAnimIntervalSeconds(preferences);
    bool fxMirror = fullAnimFormUpdate ? boolArgValue("fxMirror", preferences.getUChar("fxMirror", 1) == 1) : (preferences.getUChar("fxMirror", 1) == 1);
    bool fxRainbow = fullAnimFormUpdate ? boolArgValue("fxRainbow", preferences.getUChar("fxRainbow", 1) == 1) : (preferences.getUChar("fxRainbow", 1) == 1);
    bool fxHoursSlide = fullAnimFormUpdate ? boolArgValue("fxHoursSlide", preferences.getUChar("fxHoursSlide", 1) == 1) : (preferences.getUChar("fxHoursSlide", 1) == 1);
    bool fxMatrixFont = fullAnimFormUpdate ? boolArgValue("fxMatrixFont", preferences.getUChar("fxMatrixFont", 1) == 1) : (preferences.getUChar("fxMatrixFont", 1) == 1);
    bool fxMatrixSideways = fullAnimFormUpdate ? boolArgValue("fxMatrixSideways", preferences.getUChar("fxMatrixSideways", 1) == 1) : (preferences.getUChar("fxMatrixSideways", 1) == 1);
    bool fxUpsideDown = fullAnimFormUpdate ? boolArgValue("fxUpsideDown", preferences.getUChar("fxUpsideDown", 1) == 1) : (preferences.getUChar("fxUpsideDown", 1) == 1);
    bool fxRotate180 = fullAnimFormUpdate ? boolArgValue("fxRotate180", preferences.getUChar("fxRotate180", 1) == 1) : (preferences.getUChar("fxRotate180", 1) == 1);
    bool fxFullRotate = fullAnimFormUpdate ? boolArgValue("fxFullRotate", preferences.getUChar("fxFullRotate", 1) == 1) : (preferences.getUChar("fxFullRotate", 1) == 1);
    bool fxMiddleSwap = fullAnimFormUpdate ? boolArgValue("fxMiddleSwap", preferences.getUChar("fxMiddleSwap", 1) == 1) : (preferences.getUChar("fxMiddleSwap", 1) == 1);
    bool fxTetris = fullAnimFormUpdate ? boolArgValue("fxTetris", preferences.getUChar("fxTetris", 1) == 1) : (preferences.getUChar("fxTetris", 1) == 1);
    bool fxPileup = fullAnimFormUpdate ? boolArgValue("fxPileup", preferences.getUChar("fxPileup", 1) == 1) : (preferences.getUChar("fxPileup", 1) == 1);
    bool fxRainbowBackground = fullAnimFormUpdate ? boolArgValue("fxRainbowBackground", preferences.getUChar("fxRainbowBackground", 1) == 1) : (preferences.getUChar("fxRainbowBackground", 1) == 1);
    bool displayRotate180 = fullAnimFormUpdate ? boolArgValue("displayRotate180", preferences.getUChar("displayRotate180", 1) == 1) : (preferences.getUChar("displayRotate180", 1) == 1);
    bool displayNegative = fullAnimFormUpdate ? boolArgValue("displayNegative", preferences.getUChar("displayNegative", 0) == 1) : (preferences.getUChar("displayNegative", 0) == 1);
    bool fxQuotes = fullAnimFormUpdate ? boolArgValue("fxQuotes", preferences.getUChar("quotes_enabled", 1) == 1) : (preferences.getUChar("quotes_enabled", 1) == 1);
    bool lampEnabled = preferences.getUChar("displayLampMode", 0) == 1;
    String animColor = hasColorArg ? server->arg("animColor") : preferences.getString("animColor", "#FF0000");

    if (hasBrightnessArg) {
        preferences.putString("animBrightness", String(brightness).c_str());
    }
    if (hasAnimSpeedArg) {
        preferences.putUChar("animSpeed", (uint8_t)animSpeed);
    }
    if (hasMsgSpeedArg) {
        preferences.putUChar("msgSpeed", (uint8_t)msgSpeed);
    }
    if (hasColorArg) {
        preferences.putString("animColor", animColor.c_str());
    }
    preferences.putString("clockAnimInterval", String(clockAnimInterval).c_str());
    preferences.putUShort("clockAnimInt", (uint16_t)clockAnimInterval);

    if (fullAnimFormUpdate) {
        preferences.putUChar("fxMirror", fxMirror ? 1 : 0);
        preferences.putUChar("fxRainbow", fxRainbow ? 1 : 0);
        preferences.putUChar("fxHoursSlide", fxHoursSlide ? 1 : 0);
        preferences.putUChar("fxMatrixFont", fxMatrixFont ? 1 : 0);
        preferences.putUChar("fxMatrixSideways", fxMatrixSideways ? 1 : 0);
        preferences.putUChar("fxUpsideDown", fxUpsideDown ? 1 : 0);
        preferences.putUChar("fxRotate180", fxRotate180 ? 1 : 0);
        preferences.putUChar("fxFullRotate", fxFullRotate ? 1 : 0);
        preferences.putUChar("fxMiddleSwap", fxMiddleSwap ? 1 : 0);
        preferences.putUChar("fxTetris", fxTetris ? 1 : 0);
        preferences.putUChar("fxPileup", fxPileup ? 1 : 0);
        preferences.putUChar("fxRainbowBackground", fxRainbowBackground ? 1 : 0);
        preferences.putUChar("displayRotate180", displayRotate180 ? 1 : 0);
        preferences.putUChar("displayNegative", displayNegative ? 1 : 0);
        preferences.putUChar("quotes_enabled", fxQuotes ? 1 : 0);
    }

    if (hasBrightnessArg && !lampEnabled) {
        display_setBrightness((uint8_t)brightness);
    }
    animation_speed = (uint8_t)animSpeed;
    message_speed = (uint8_t)msgSpeed;
    display_setFunClockIntervalSeconds((uint16_t)clockAnimInterval);
    display_setFunClockEffectsEnabled(fxMirror, fxRainbow, fxHoursSlide, fxMatrixFont, fxMatrixSideways, fxUpsideDown, fxRotate180, fxFullRotate, fxMiddleSwap, fxTetris, fxPileup, fxRainbowBackground, displayNegative);
    display_setNegative(false);
    display_setMatrixRotate180(displayRotate180);
    mainConfig.schedule.random_quotes_enabled = fxQuotes;

    CRGB parsedColor;
    if (hasColorArg && parseHexColorString(animColor, parsedColor)) {
        display_suppressFunClockEffects(1200);
        animation_color = parsedColor;
        clock_color = parsedColor;
        quote_color = parsedColor;
        message_color = parsedColor;
        if (!lampEnabled) {
            globalColor = parsedColor;
            display_setColor(parsedColor);
        }
    }

    bool modeApplied = false;
    if (canApplyBaseDisplayModeChange()) {
        display_mode = lampEnabled ? DISPLAY_MODE_LAMP : DISPLAY_MODE_CLOCK;
        modeApplied = true;
    }

    Serial.printf("[WiFi] Brightness applied: %d, Speed: %d, Color: %s, ClockAnimInterval: %ds, fx=%d%d%d%d%d%d%d%d%d%d%d%d, rotMap=%d, lamp=%d, neg=%d, quotes=%d\n",
        brightness,
        animSpeed,
        animColor.c_str(),
        clockAnimInterval,
        fxMirror ? 1 : 0,
        fxRainbow ? 1 : 0,
        fxHoursSlide ? 1 : 0,
        fxMatrixFont ? 1 : 0,
        fxMatrixSideways ? 1 : 0,
        fxUpsideDown ? 1 : 0,
        fxRotate180 ? 1 : 0,
        fxFullRotate ? 1 : 0,
        fxMiddleSwap ? 1 : 0,
        fxTetris ? 1 : 0,
        fxPileup ? 1 : 0,
        fxRainbowBackground ? 1 : 0,
        displayRotate180 ? 1 : 0,
        lampEnabled ? 1 : 0,
        displayNegative ? 1 : 0,
        fxQuotes ? 1 : 0);
    String animResponse = "{\"success\":true,\"message\":\"Ustawienia animacji zapisane\",\"modeApplied\":";
    animResponse += (modeApplied ? "true" : "false");
    animResponse += "}";
    mqtt_manager_publish_now();
    server->send(200, "application/json", animResponse);
}

void WifiManager::handleSaveLampConfig() {
    auto boolArgValue = [this](const char* key, bool fallback) -> bool {
        if (!server->hasArg(key)) return fallback;
        String v = server->arg(key);
        v.toLowerCase();
        return (v == "1" || v == "true" || v == "on" || v == "yes");
    };

    bool lampEnabled = boolArgValue("displayLampMode", preferences.getUChar("displayLampMode", 0) == 1);
    int lampBrightness = server->hasArg("lampBrightness")
        ? constrain(server->arg("lampBrightness").toInt(), 1, 255)
        : constrain(preferences.getString("lampBrightness", String((int)kLampDefaultBrightness)).toInt(), 1, 255);
    String lampColor = server->hasArg("lampColor") ? server->arg("lampColor") : preferences.getString("lampColor", "#FFFFFF");
    String normalizedLampColor;
    bool modeApplied = wifi_manager_apply_lamp_config(lampEnabled,
                                                      (uint8_t)lampBrightness,
                                                      lampColor,
                                                      true,
                                                      &normalizedLampColor);

    Serial.printf("[WiFi] Lamp config: enabled=%d brightness=%d color=%s\n",
        lampEnabled ? 1 : 0,
        lampBrightness,
        normalizedLampColor.c_str());
    app_logf("WWW /save-lamp: enabled=%d brightness=%d color=%s modeApplied=%d",
             lampEnabled ? 1 : 0,
             lampBrightness,
             normalizedLampColor.c_str(),
             modeApplied ? 1 : 0);
    String lampResponse = "{\"success\":true,\"message\":\"Ustawienia lampy zapisane\",\"modeApplied\":";
    lampResponse += (modeApplied ? "true" : "false");
    lampResponse += "}";
    mqtt_manager_publish_now();
    server->send(200, "application/json", lampResponse);
}

void WifiManager::handleSaveSchedule() {
    if (!ensureAuthenticated()) return;
    
    Serial.println("[WiFi] handleSaveSchedule() called");
    
    if (server->method() != HTTP_POST) {
        Serial.println("[WiFi] ERROR: Not HTTP_POST");
        server->send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
        return;
    }

    String scheduleJSON = server->arg("plain");
    Serial.print("[WiFi] Received schedule JSON length: ");
    Serial.println(scheduleJSON.length());
    
    if (scheduleJSON.length() == 0) {
        Serial.println("[WiFi] ERROR: Empty body");
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Empty body\"}");
        return;
    }

    Serial.print("[WiFi] Schedule JSON: ");
    Serial.println(scheduleJSON);

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, scheduleJSON);
    if (err || !doc.is<JsonArray>()) {
        Serial.print("[WiFi] JSON parse error: ");
        Serial.println(err.c_str());
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON array\"}");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    DynamicJsonDocument sanitizedDoc(4096);
    JsonArray out = sanitizedDoc.to<JsonArray>();

    for (JsonVariant v : arr) {
        if (!v.is<JsonObject>()) continue;
        JsonObject src = v.as<JsonObject>();
        JsonObject dst = out.createNestedObject();

        bool enabled = (src["enabled"].as<int>() != 0);
        const char* start = src["start"] | "00:00";
        const char* end = src["end"] | "00:00";
        int brightness = src["brightness"].as<int>();
        brightness = constrain(brightness, 1, 255);
        String color = src["color"] | "#00ff00";
        CRGB parsed;
        if (!parseHexColorString(color, parsed)) {
            color = "#00ff00";
        }

        dst["enabled"] = enabled ? 1 : 0;
        dst["start"] = start;
        dst["end"] = end;
        dst["brightness"] = brightness;
        dst["color"] = color;
    }

    String sanitized;
    serializeJson(out, sanitized);
    
    app_logf("[SCHEDULE] Serialized data length: %d", sanitized.length());
    app_logf("[SCHEDULE] Serialized data: %s", sanitized.c_str());
    
    // Write to separate "schedule" namespace to avoid conflicts with global "wifi" namespace
    Preferences prefs;
    prefs.begin("schedule", false);
    app_logf("[SCHEDULE] Writing to 'schedule' namespace...");
    size_t written = prefs.putString("windows", sanitized.c_str());
    app_logf("[SCHEDULE] putString returned: %d bytes", written);
    prefs.end();
    app_logf("[SCHEDULE] Schedule namespace closed");

    Serial.print("[WiFi] Harmonogram jasności zapisany. Rozmiar: ");
    Serial.print(sanitized.length());
    Serial.println(" bajtów");

    server->send(200, "application/json", "{\"success\":true,\"windows\":" + String(out.size()) + "}");
}

// Clear entire schedule
void WifiManager::handleClearSchedule() {
    Serial.println("[WiFi] handleClearSchedule() called");
    
    // Remove from separate "schedule" namespace
    app_logf("[SCHEDULE] Clearing entire schedule...");
    Preferences prefs;
    prefs.begin("schedule", false);
    prefs.remove("windows");
    prefs.end();
    
    app_logf("[SCHEDULE] Schedule cleared from 'schedule' namespace");
    Serial.println("[WiFi] handleClearSchedule() completed");
    
    server->send(200, "application/json", "{\"success\":true}");
}

// New API endpoint to retrieve saved schedule
void WifiManager::handleApiSchedule() {
    // Read from separate "schedule" namespace
    Preferences fresh;
    fresh.begin("schedule", true);  // read-only
    String windowsJSON = fresh.getString("windows", "[]");
    fresh.end();
    
    app_logf("[SCHEDULE] API read from 'schedule' namespace: %s", windowsJSON.c_str());
    
    String response = "{\"windows\":" + windowsJSON + "}";
    server->send(200, "application/json", response);
}

// Export quotes as JSON array
void WifiManager::handleApiQuotesConfig() {
    String response = "{\"enabled\":" + String(mainConfig.schedule.random_quotes_enabled ? "true" : "false") + ",\"count\":" + String(numQuotes) + "}";
    server->send(200, "application/json", response);
}

// Export quotes as JSON array
void WifiManager::handleExportQuotes() {
    String quotesJson = quotes_getJson();
    // quotes_getJson() returns {"quotes":[...]}, we need only the array
    DynamicJsonDocument doc(QUOTES_JSON_DOC_SIZE);
    deserializeJson(doc, quotesJson);
    JsonArray arr = doc["quotes"];
    
    DynamicJsonDocument responseDoc(QUOTES_JSON_DOC_SIZE);
    responseDoc["success"] = true;
    JsonArray respArr = responseDoc.createNestedArray("quotes");
    for (JsonVariant v : arr) {
        respArr.add(v.as<const char*>());
    }
    
    String response;
    serializeJson(responseDoc, response);
    server->send(200, "application/json", response);
}

// Import quotes from JSON file
void WifiManager::handleImportQuotes() {
    if (!server->hasArg("quotes")) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Missing quotes data\"}");
        return;
    }
    
    String quotesJson = server->arg("quotes");
    
    // Parse JSON array
    DynamicJsonDocument doc(QUOTES_JSON_DOC_SIZE);
    DeserializationError error = deserializeJson(doc, quotesJson);
    
    if (error) {
        Serial.printf("[WiFi] Import JSON error: %s\n", error.c_str());
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON format\"}");
        return;
    }
    
    if (!doc.is<JsonArray>()) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Data must be array\"}");
        return;
    }
    
    JsonArray arr = doc.as<JsonArray>();
    
    // Clear existing quotes in memory
    numQuotes = 0;
    memset(quotes, 0, sizeof(quotes));
    
    for (size_t i = 0; i < arr.size() && i < MAX_QUOTES; i++) {
        if (arr[i].is<const char*>() || arr[i].is<String>()) {
            const char* quote = arr[i].as<const char*>();
            if (quote && strlen(quote) > 0 && strlen(quote) <= MAX_QUOTE_LENGTH - 1) {
                if (!quotes_add(quote)) {
                    Serial.printf("[WiFi] Nie mogę dodać cytatu %d\n", i);
                    break; // Stop if memory full
                }
            }
        }
    }
    
    // Save to file
    quotes_save();
    
    Serial.printf("[WiFi] Zaimportowano %d cytatów\n", numQuotes);
    server->send(200, "application/json", "{\"success\":true,\"count\":" + String(numQuotes) + "}");
}

// Save quotes enabled/disabled status
void WifiManager::handleSaveQuotesEnabled() {
    if (!server->hasArg("enabled")) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Missing enabled flag\"}");
        return;
    }
    
    int enabled = server->arg("enabled").toInt();
    mainConfig.schedule.random_quotes_enabled = (enabled == 1);
    
    // Save to preferences
    preferences.begin("wifi", false);
    preferences.putUChar("quotes_enabled", mainConfig.schedule.random_quotes_enabled ? 1 : 0);
    preferences.end();
    
    Serial.printf("[WiFi] Cytaty %s\n", mainConfig.schedule.random_quotes_enabled ? "włączone" : "wyłączone");
    server->send(200, "application/json", "{\"success\":true}");
}

// OTA Status endpoint
void WifiManager::handleApiOtaStatus() {
    // Always check for new remote version immediately when panel is loaded/refreshed
    githubOtaManager.checkNow();
    
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(runningPartition);
    const char* bootLabel = (runningPartition && runningPartition->label) ? runningPartition->label : "unknown";
    const char* nextLabel = (nextPartition && nextPartition->label) ? nextPartition->label : "unknown";

    bool githubUpdating = githubOtaManager.isInProgress();
    bool anyUpdating = otaUpdating || githubUpdating;
    uint32_t writtenBytes = otaUpdating
        ? (uint32_t)otaProgress
        : (githubUpdating ? githubOtaManager.getWrittenBytes() : (uint32_t)otaProgress);
    uint32_t totalBytes = otaUpdating
        ? (uint32_t)otaTotal
        : (githubUpdating ? githubOtaManager.getTotalBytes() : (uint32_t)otaTotal);

    String json = "{";
    json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"buildDate\":\"" + String(__DATE__) + "\",";
    json += "\"buildTime\":\"" + String(__TIME__) + "\",";
    json += "\"sketchSize\":" + String((unsigned long)ESP.getSketchSize()) + ",";
    json += "\"githubBuildEnabled\":" + String(githubOtaManager.isEnabled() ? "true" : "false") + ",";
    json += "\"githubEnabled\":" + String(githubOtaManager.isEnabled() ? "true" : "false") + ",";
    json += "\"githubConfigured\":" + String(githubOtaManager.isConfigured() ? "true" : "false") + ",";
    json += "\"githubTestButtonEnabled\":" + String(GITHUB_OTA_TEST_BUTTON ? "true" : "false") + ",";
    json += "\"githubCurrentVersion\":\"" + githubOtaManager.getCurrentVersion() + "\",";
    json += "\"githubRemoteVersion\":\"" + githubOtaManager.getRemoteVersion() + "\",";
    json += "\"githubUpdateAvailable\":" + String(githubOtaManager.isUpdateAvailable() ? "true" : "false") + ",";
    uint8_t ghPhase = githubOtaManager.getPhase();
    String ghPhaseText = "idle";
    if (ghPhase == 1) ghPhaseText = "laczenie";
    else if (ghPhase == 2) ghPhaseText = "pobieranie";
    else if (ghPhase == 3) ghPhaseText = "zapis flash";
    else if (ghPhase == 4) ghPhaseText = "walidacja";
    else if (ghPhase == 5) ghPhaseText = "zakonczono";
    else if (ghPhase == 6) ghPhaseText = "blad";
    json += "\"githubPhase\":" + String((unsigned long)ghPhase) + ",";
    json += "\"githubPhaseText\":\"" + ghPhaseText + "\",";
    json += "\"githubLastError\":\"" + githubOtaManager.getLastError() + "\",";
    json += "\"githubLastCheckMs\":" + String((unsigned long)githubOtaManager.getLastCheckMs()) + ",";
    json += "\"githubCheckIntervalMs\":" + String((unsigned long)githubOtaManager.getCheckIntervalMs()) + ",";
    json += "\"status\":\"";
    if (otaUpdating) {
        json += "Aktualizacja WWW";
    } else if (githubUpdating) {
        json += "Aktualizacja zdalna";
    } else {
        json += "Gotowy";
    }
    json += "\",";
    json += "\"updating\":" + String(anyUpdating ? "true" : "false") + ",";
    json += "\"written\":" + String((unsigned long)writtenBytes) + ",";
    json += "\"total\":" + String((unsigned long)totalBytes) + ",";
    json += "\"activeSlot\":\"" + String(bootLabel) + "\",";
    json += "\"nextSlot\":\"" + String(nextLabel) + "\"";
    json += "}";
    server->send(200, "application/json", json);
}

void WifiManager::handleApiInstrukcja() {
    String html = getInstrukcjaHtml();
    server->send(200, "text/html; charset=UTF-8", html);
}

void WifiManager::handleApiGithubOtaCheck() {
    if (otaUpdating || externalOtaActive || otaManager.isInProgress() || githubOtaManager.isInProgress()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Inny OTA jest w toku\"}");
        return;
    }
    if (!isConnected()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Brak polaczenia WiFi\"}");
        return;
    }
    if (!githubOtaManager.isEnabled()) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Zdalna OTA jest wylaczona w config.h\"}");
        return;
    }
    if (!githubOtaManager.isConfigured()) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Brak konfiguracji URL zdalnej OTA\"}");
        return;
    }

    app_log("[RemoteOTA] Manual check requested from WWW");
    bool ok = githubOtaManager.checkNow();
    if (ok) {
        String msg = githubOtaManager.isUpdateAvailable()
            ? "Nowa wersja jest dostępna"
            : "Brak nowej wersji";
        server->send(200, "application/json", "{\"success\":true,\"message\":\"" + msg + "\"}");
    } else {
        String err = githubOtaManager.getLastError();
        if (err.length() == 0) err = "Sprawdzenie nieudane";
        server->send(500, "application/json", "{\"success\":false,\"error\":\"" + err + "\"}");
    }
}

void WifiManager::handleApiGithubOtaInstall() {
    if (otaUpdating || externalOtaActive || otaManager.isInProgress() || githubOtaManager.isInProgress()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Inny OTA jest w toku\"}");
        return;
    }
    if (!isConnected()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Brak polaczenia WiFi\"}");
        return;
    }
    if (!githubOtaManager.isEnabled()) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Zdalna OTA jest wylaczona w config.h\"}");
        return;
    }
    if (!githubOtaManager.isConfigured()) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Brak konfiguracji URL zdalnej OTA\"}");
        return;
    }

    app_log("[RemoteOTA] Manual install requested from WWW");
    bool ok = githubOtaManager.startInstallAsync();
    if (ok) {
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Rozpoczeto pobieranie i instalacje\"}");
    } else {
        String err = githubOtaManager.getLastError();
        if (err.length() == 0) err = "Instalacja nieudana";
        server->send(500, "application/json", "{\"success\":false,\"error\":\"" + err + "\"}");
    }
}

void WifiManager::handleApiOtaSwitchSlot() {
    if (otaUpdating || externalOtaActive) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"OTA in progress\"}");
        return;
    }

    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(bootPartition);
    if (!nextPartition) {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"No next slot\"}");
        return;
    }

    esp_err_t err = esp_ota_set_boot_partition(nextPartition);
    if (err != ESP_OK) {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Switch failed\"}");
        return;
    }

    const char* nextLabel = (nextPartition->label) ? nextPartition->label : "unknown";
    app_logf("OTA manual slot switch requested: new_boot=%s", nextLabel);

    server->send(200, "application/json", "{\"success\":true,\"slot\":\"" + String(nextLabel) + "\",\"restarting\":true}");
    delay(400);
    ESP.restart();
}

// OTA Upload handler - receive firmware file and update ESP32
void WifiManager::handleOtaUpload() {
    // Check if file was uploaded using streaming API
    HTTPUpload& upload = server->upload();
    
    // Handle initiation of upload
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        
        // Validate file extension
        if (!filename.endsWith(".bin")) {
            Serial.println("[OTA] ❌ Niepoprawne rozszerzenie pliku");
            return;
        }
        
        Serial.printf("[OTA] 📥 Rozpoczęto: %s\n", filename.c_str());
        otaUpdating = true;
        externalOtaActive = true;
        message_active = false;
        display_suppressFunClockEffects(180000);
        display_setNegative(false);
        WiFi.setSleep(false);
        FastLED.setDither(0);
        otaProgress = 0;
        otaTotal = 0;
        if (server->hasArg("fwsize")) {
            long argSize = server->arg("fwsize").toInt();
            if (argSize > 0) {
                otaTotal = (size_t)argSize;
            }
        }
        if (server->hasHeader("X-Firmware-Size")) {
            long declaredSize = server->header("X-Firmware-Size").toInt();
            if (declaredSize > 0) {
                otaTotal = (size_t)declaredSize;
            }
        }
        if (otaTotal == 0 && server->hasHeader("Content-Length")) {
            long contentLen = server->header("Content-Length").toInt();
            if (contentLen > 0) {
                otaTotal = (size_t)contentLen;
            }
        }
        if (otaTotal == 0 && upload.totalSize > 0) {
            otaTotal = upload.totalSize;
        }
        drawOtaProgressOnMatrix(0, otaTotal, false, true);
        
        // Begin OTA update with max available sketch space
        size_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace, U_FLASH)) {
            Serial.printf("[OTA] ❌ Update.begin() failed\n");
            otaUpdating = false;
            externalOtaActive = false;
            FastLED.setDither(1);
            drawOtaProgressOnMatrix(0, 1, true, true);
            server->send(500, "application/json", "{\"success\":false,\"error\":\"Update begin failed\"}");
            return;
        }
    }
    
    // Handle data chunks during upload
    else if (upload.status == UPLOAD_FILE_WRITE) {
        // Write received chunk to flash
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Serial.printf("[OTA] ❌ Write failed\n");
            Update.abort();
            otaUpdating = false;
            externalOtaActive = false;
            FastLED.setDither(1);
            drawOtaProgressOnMatrix(0, 1, true, true);
            server->send(500, "application/json", "{\"success\":false,\"error\":\"Write failed\"}");
            return;
        }
        
        otaProgress += upload.currentSize;
        // IMPORTANT: in HTTPUpload, totalSize can be running uploaded bytes.
        // Do not overwrite otaTotal here, otherwise progress can stick at 100%.
        if (otaTotal == 0 && upload.totalSize > otaProgress + upload.currentSize) {
            otaTotal = upload.totalSize;
        }

        static uint32_t lastDrawMs = 0;
        uint32_t now = millis();
        if (now - lastDrawMs >= 40) {
            drawOtaProgressOnMatrix(otaProgress, otaTotal, false);
            lastDrawMs = now;
        }
        
        // Feed watchdog & yield
        yield();
        delay(1);
    }
    
    // Handle completion of upload
    else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("[OTA] ✓ Aktualizacja zakończona: %d bajtów\n", otaProgress);
            otaManager.armRollbackGuardForNextBoot();
            otaUpdating = false;
            externalOtaActive = false;
            FastLED.setDither(1);
            drawOtaProgressOnMatrix(otaTotal > 0 ? otaTotal : otaProgress, otaTotal > 0 ? otaTotal : otaProgress, false, true);
            
            // Send success response
            server->send(200, "application/json", "{\"success\":true,\"written\":" + String(otaProgress) + "}");
            
            // Schedule restart after brief delay
            delay(500);
            ESP.restart();
        } else {
            Serial.printf("[OTA] ❌ Update.end() failed\n");
            otaUpdating = false;
            externalOtaActive = false;
            FastLED.setDither(1);
            drawOtaProgressOnMatrix(0, 1, true, true);
            server->send(500, "application/json", "{\"success\":false,\"error\":\"Update failed\"}");
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
        Serial.println("[OTA] ❌ Upload aborted");
        Update.abort();
        otaUpdating = false;
        externalOtaActive = false;
        FastLED.setDither(1);
        drawOtaProgressOnMatrix(0, 1, true, true);
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Upload aborted\"}");
    }
}

void WifiManager::handleApiColorsPalette() {
    Preferences prefs;
    prefs.begin("app", true);
    
    String paletteStr = prefs.getString("colorPalette", "[]");
    
    prefs.end();
    
    server->send(200, "application/json", paletteStr);
}
