#ifndef WEB_PANEL_H
#define WEB_PANEL_H

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Konfiguracja
#define CONFIG_FILE "/config.json"
#define FIRMWARE_VERSION "1.0.0"

// Deklaracje funkcji
void webPanel_setup();
void webPanel_loop();
void webPanel_handle();
void webPanel_setHostname(const char* hostname);

// Struktury konfiguracyjne
struct WifiPanelConfig {
    char ssid[32];
    char password[64];
    bool dhcp;
    char ip[16];
    char gateway[16];
    char dns[16];
};

struct MqttConfig {
    char broker[64];
    uint16_t port;
    char user[32];
    char password[32];
    char clientId[32];
    char topicPub[64];
    char topicSub[64];
};

struct DisplayConfig {
    uint8_t brightness;
    bool autoBrightness;
    bool hour24;
    bool showSeconds;
    char ntpServer[64];
    int16_t timezone;  // UTC offset in seconds (e.g., 3600 for UTC+1)
};

// === SCHEDULER ===
struct AnimationInRotation {
    bool enabled;       // czy ta animacja aktywna w rotacji?
    uint8_t type;       // 0-4 (rainbow, fade, wave, pulse, night)
    uint16_t duration;  // ile sekund pokazywać tę animację
};

struct ScheduleAnimationConfig {
    bool enabled;                          // czy włączona rotacja?
    uint16_t rotation_interval;            // co ile minut zmiana animacji (1-1440)
    uint8_t num_animations;                // ile animacji w rotacji (1-5)
    AnimationInRotation animations[5];     // lista aktywnych animacji
};

struct ScheduledMessage {
    bool enabled;                   // czy ta wiadomość aktywna?
    uint8_t hour;                   // 0-23 (godzina)
    uint8_t minute;                 // 0-59 (minuta)
    char text[128];                 // treść wiadomości
    uint16_t duration;              // ile sekund wyświetlać (1-300)
};

struct ScheduleConfig {
    ScheduleAnimationConfig animation;                // rotacja animacji
    ScheduledMessage messages[10];                    // max 10 zaplanowanych wiadomości
    uint8_t num_messages;                             // ile wiadomości zaplanowanych
    
    bool random_quotes_enabled;                       // losowe cytaty?
    uint16_t random_quotes_interval;                  // co ile minut
    uint8_t random_quotes_start_hour;                 // od której godziny
    uint8_t random_quotes_end_hour;                   // do której godziny
};

// Główna struktura konfiguracyjna
struct MainConfig {
    WifiPanelConfig wifi;
    MqttConfig mqtt;
    DisplayConfig display;
    ScheduleConfig schedule;
};

extern MainConfig mainConfig;
extern WebServer webServer;
extern unsigned long bootMillis;

// Funkcje do obsługi konfiguracji
bool loadConfig();
bool saveConfig();
void resetConfig();
void exportConfig();
void importConfig();

// Funkcje pomocnicze
String getStatusJson();
String getUptime();
String getFreeHeap();
String getRssi();
String getIp();
String getMqttStatus();
String getCurrentTime();
void restartDevice();
void factoryReset();
void handleOtaUpdate();

#endif // WEB_PANEL_H
