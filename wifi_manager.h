#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>

#define WIFI_AP_SSID "LED-Matrix-Setup"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_IP IPAddress(192,168,4,1)

struct WifiConfig {
    String ssid;
    String password;
};

class WifiManager {
public:
    WifiManager();
    void begin(WebServer* webServer = nullptr);
    void loop();
    bool isConnected();
    String getLocalIP();
    String getStatusHTML();
    void setExternalOtaActive(bool active);
    void resetWifiConfig();
    void restartESP();
    WifiConfig getConfig();
    bool isAPMode();
    bool isOtaUpdating();
private:
    void setupAP();
    void setupStation();
    void setupWebServer(WebServer* webServer);
    void handleRoot();
    void handleSave();
    void handleSaveTime();
    void handleSaveAnimations();
    void handleSaveSchedule();
    void handleResetWifi();
    void handleForgetWifi();
    void handleRestart();
    void handleApiStatus();
    void handleApiQuotes();
    void handleApiSchedule();
    void handleApiNetworkInfo();
    void handleApiWifiScan();
    void handleApiMqttConfig();
    void handleApiLampConfig();
    void handleApiQuotesList();
    void handleApiNtpConfig();
    void handleSaveQuote();
    void handleSaveMqtt();
    void handleSaveLampConfig();
    void handleTriggerQuote();
    void handleTriggerClockAnimation();
    void handleTriggerClockMirror();
    void handleTriggerClockRainbow();
    void handleTriggerClockHoursSlide();
    void handleTriggerClockMatrixFont();
    void handleTriggerClockUpsideDown();
    void handleTriggerClockRotate180();
    void handleTriggerClockFullRotate();
    void handleTriggerClockMiddleSwap();
    void handleDeleteQuote();
    void handleExportQuotes();
    void handleImportQuotes();
    void handleSaveQuotesEnabled();
    void handleApiQuotesConfig();
    void handleApiAnimationsConfig();
    void handleApiOtaStatus();
    void handleOtaUpload();
    void handleApiColorsPalette();
    bool ensureAuthenticated();
    void loadApPassword();
    void loadConfig();
    void saveConfig(const WifiConfig& config);
    void reconnectIfNeeded();

    Preferences preferences;
    WebServer* server;
    WifiConfig config;
    unsigned long lastReconnectAttempt;
    bool apEnabled;
    bool externalOtaActive = false;
    unsigned long staConnectedTime = 0;
    bool apDisabledAfterSta = false;
    size_t otaProgress = 0;
    size_t otaTotal = 0;
    bool otaUpdating = false;
    String apPassword;
};

#endif // WIFI_MANAGER_H
