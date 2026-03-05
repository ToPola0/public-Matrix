#include "github_ota_manager.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

#include "app_logger.h"

GitHubOTAManager githubOtaManager;

GitHubOTAManager::GitHubOTAManager() {
}

void GitHubOTAManager::begin(bool enabled,
                             const char* currentVersion,
                             const char* versionUrl,
                             const char* firmwareUrl,
                             uint32_t checkIntervalMs,
                             uint32_t bootDelayMs) {
    _enabled = enabled;
    _currentVersion = currentVersion ? String(currentVersion) : String("0.0.0");
    _versionUrl = versionUrl ? String(versionUrl) : String();
    _firmwareUrl = firmwareUrl ? String(firmwareUrl) : String();
    _checkIntervalMs = checkIntervalMs;
    _bootDelayMs = bootDelayMs;
    _startMs = millis();
    _lastCheckMs = 0;
    _started = true;

    _configured = _enabled && _versionUrl.length() > 0 && _firmwareUrl.length() > 0;
    if (_enabled && !_configured) {
        _lastError = "GitHub OTA wlaczone, ale URL nie skonfigurowane";
        app_logf("[GitHubOTA] %s", _lastError.c_str());
    }

    if (_configured) {
        app_logf("[GitHubOTA] Init: current=%s interval=%lu ms", _currentVersion.c_str(), (unsigned long)_checkIntervalMs);
    } else {
        app_log("[GitHubOTA] Disabled or not configured");
    }
}

void GitHubOTAManager::setProgressCallback(OTA_ProgressCallback cb) {
    _progressCb = cb;
}

void GitHubOTAManager::setStatusCallback(OTA_StatusCallback cb) {
    _statusCb = cb;
}

bool GitHubOTAManager::isInProgress() const {
    return _inProgress;
}

bool GitHubOTAManager::isConfigured() const {
    return _configured;
}

const String& GitHubOTAManager::getCurrentVersion() const {
    return _currentVersion;
}

const String& GitHubOTAManager::getRemoteVersion() const {
    return _remoteVersion;
}

const String& GitHubOTAManager::getLastError() const {
    return _lastError;
}

void GitHubOTAManager::loop(bool wifiConnected, bool otherOtaBusy) {
    if (!_started || !_configured || !_enabled) return;
    if (!wifiConnected) return;
    if (otherOtaBusy || _inProgress) return;

    uint32_t now = millis();
    if ((now - _startMs) < _bootDelayMs) return;

    if (_lastCheckMs != 0 && (now - _lastCheckMs) < _checkIntervalMs) {
        return;
    }

    _lastCheckMs = now;
    checkAndUpdate();
}

bool GitHubOTAManager::checkAndUpdate() {
    _lastError = "";
    String remote;
    if (!fetchRemoteVersion(remote)) {
        return false;
    }

    _remoteVersion = remote;
    app_logf("[GitHubOTA] Current=%s, Remote=%s", _currentVersion.c_str(), _remoteVersion.c_str());

    if (!isRemoteVersionNewer(_remoteVersion, _currentVersion)) {
        app_log("[GitHubOTA] Brak nowej wersji");
        return true;
    }

    app_logf("[GitHubOTA] Nowa wersja wykryta: %s", _remoteVersion.c_str());
    return performUpdate(_remoteVersion);
}

bool GitHubOTAManager::fetchRemoteVersion(String& outVersion) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, _versionUrl)) {
        _lastError = "Nie mozna polaczyc z version URL";
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        return false;
    }

    http.setTimeout(12000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastError = String("version GET HTTP ") + code;
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        http.end();
        return false;
    }

    outVersion = http.getString();
    outVersion.trim();
    http.end();

    if (outVersion.length() == 0) {
        _lastError = "Pusty version.txt";
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        return false;
    }

    return true;
}

bool GitHubOTAManager::performUpdate(const String& targetVersion) {
    _inProgress = true;
    if (_statusCb) _statusCb(OTA_STATUS_DOWNLOADING);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, _firmwareUrl)) {
        _lastError = "Nie mozna polaczyc z firmware URL";
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        _inProgress = false;
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
        return false;
    }

    http.setTimeout(20000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastError = String("firmware GET HTTP ") + code;
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        http.end();
        _inProgress = false;
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
        return false;
    }

    int totalSize = http.getSize();
    size_t beginSize = (totalSize > 0) ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(beginSize)) {
        _lastError = String("Update.begin error: ") + Update.errorString();
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        http.end();
        _inProgress = false;
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
        return false;
    }

    if (_statusCb) _statusCb(OTA_STATUS_WRITING);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t writtenTotal = 0;
    unsigned long lastDataMs = millis();

    while (http.connected() && (totalSize < 0 || writtenTotal < (size_t)totalSize)) {
        size_t availableBytes = stream->available();
        if (availableBytes > 0) {
            size_t toRead = availableBytes;
            if (toRead > sizeof(buffer)) toRead = sizeof(buffer);
            int bytesRead = stream->readBytes(buffer, toRead);
            if (bytesRead > 0) {
                size_t written = Update.write(buffer, (size_t)bytesRead);
                if (written != (size_t)bytesRead) {
                    _lastError = String("Update.write error: ") + Update.errorString();
                    app_logf("[GitHubOTA] %s", _lastError.c_str());
                    Update.abort();
                    http.end();
                    _inProgress = false;
                    if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
                    return false;
                }

                writtenTotal += written;
                lastDataMs = millis();
                if (_progressCb) {
                    uint32_t progress = (uint32_t)writtenTotal;
                    uint32_t total = (totalSize > 0) ? (uint32_t)totalSize : (uint32_t)(writtenTotal + 1);
                    _progressCb(progress, total);
                }
            }
        } else {
            if ((millis() - lastDataMs) > 15000UL) {
                _lastError = "Timeout transferu firmware";
                app_logf("[GitHubOTA] %s", _lastError.c_str());
                Update.abort();
                http.end();
                _inProgress = false;
                if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
                return false;
            }
            delay(1);
        }
    }

    if (!Update.end(true)) {
        _lastError = String("Update.end error: ") + Update.errorString();
        app_logf("[GitHubOTA] %s", _lastError.c_str());
        http.end();
        _inProgress = false;
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_VALIDATION);
        return false;
    }

    http.end();

    if (_statusCb) _statusCb(OTA_STATUS_VALIDATING);
    otaManager.armRollbackGuardForNextBoot();

    _currentVersion = targetVersion;
    _inProgress = false;

    if (_statusCb) _statusCb(OTA_STATUS_SUCCESS);
    app_logf("[GitHubOTA] Aktualizacja OK do %s, restart...", targetVersion.c_str());
    delay(1200);
    ESP.restart();
    return true;
}

int GitHubOTAManager::parseVersionPart(const String& v, int partIndex) const {
    int currentPart = 0;
    String token;

    for (uint16_t i = 0; i < v.length(); i++) {
        char c = v.charAt(i);
        if (c == '.') {
            if (currentPart == partIndex) {
                break;
            }
            token = "";
            currentPart++;
            continue;
        }

        if (c >= '0' && c <= '9') {
            token += c;
        } else if (token.length() > 0) {
            // Stop at first non-digit after number, e.g. 1.2.3-beta
            break;
        }
    }

    if (currentPart != partIndex || token.length() == 0) return 0;
    return token.toInt();
}

bool GitHubOTAManager::isRemoteVersionNewer(const String& remote, const String& local) const {
    for (int i = 0; i < 3; i++) {
        int r = parseVersionPart(remote, i);
        int l = parseVersionPart(local, i);
        if (r > l) return true;
        if (r < l) return false;
    }
    return false;
}
