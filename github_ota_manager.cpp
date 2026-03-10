#include "github_ota_manager.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ctime>

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
        _lastError = "Zdalna OTA wlaczona, ale URL nie skonfigurowane";
        app_logf("[RemoteOTA] %s", _lastError.c_str());
    }

    if (_configured) {
        app_logf("[RemoteOTA] Init: current=%s interval=%lu ms", _currentVersion.c_str(), (unsigned long)_checkIntervalMs);
    } else {
        app_log("[RemoteOTA] Disabled or not configured");
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

bool GitHubOTAManager::isEnabled() const {
    return _enabled;
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

bool GitHubOTAManager::checkNow() {
    if (!_started || !_enabled || !_configured || _inProgress) {
        return false;
    }
    _lastCheckMs = millis();
    return checkOnly();
}

bool GitHubOTAManager::installNow() {
    return installInternal();
}

bool GitHubOTAManager::startInstallAsync() {
    if (!_started || !_enabled || !_configured || _inProgress || _installTaskRunning) {
        return false;
    }

    _installTaskRunning = true;
    _phase = 1;
    _writtenBytes = 0;
    _totalBytes = 0;
    BaseType_t rc = xTaskCreate(
        GitHubOTAManager::installTaskEntry,
        "gh_ota",
        12288,
        this,
        1,
        nullptr);

    if (rc != pdPASS) {
        _installTaskRunning = false;
        _lastError = "Nie mozna uruchomic task OTA";
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        return false;
    }

    return true;
}

uint32_t GitHubOTAManager::getWrittenBytes() const {
    return _writtenBytes;
}

uint32_t GitHubOTAManager::getTotalBytes() const {
    return _totalBytes;
}

uint8_t GitHubOTAManager::getPhase() const {
    return _phase;
}

bool GitHubOTAManager::installInternal() {
    if (!_started || !_enabled || !_configured || _inProgress) {
        return false;
    }

    _lastError = "";
    _phase = 1;
    _writtenBytes = 0;
    _totalBytes = 0;

    // Always refresh remote version right before install.
    String remote;
    if (!fetchRemoteVersion(remote)) {
        _phase = 6;
        return false;
    }
    _remoteVersion = remote;

    if (!isRemoteVersionNewer(_remoteVersion, _currentVersion)) {
        _lastError = "Brak nowej wersji do instalacji";
        app_log("[RemoteOTA] Install skipped: no newer version");
        _phase = 0;
        return false;
    }

    app_logf("[RemoteOTA] Manual install requested: %s -> %s", _currentVersion.c_str(), _remoteVersion.c_str());
    return performUpdate(_remoteVersion);
}

void GitHubOTAManager::installTaskEntry(void* arg) {
    GitHubOTAManager* self = static_cast<GitHubOTAManager*>(arg);
    if (self) {
        self->installInternal();
        self->_installTaskRunning = false;
    }
    vTaskDelete(nullptr);
}

uint32_t GitHubOTAManager::getLastCheckMs() const {
    return _lastCheckMs;
}

uint32_t GitHubOTAManager::getCheckIntervalMs() const {
    return _checkIntervalMs;
}

bool GitHubOTAManager::isUpdateAvailable() const {
    if (_remoteVersion.length() == 0) return false;
    return isRemoteVersionNewer(_remoteVersion, _currentVersion);
}

void GitHubOTAManager::loop(bool wifiConnected, bool otherOtaBusy) {
    if (!_started || !_configured || !_enabled) return;
    if (!wifiConnected) return;
    if (otherOtaBusy || _inProgress) return;

    uint32_t now = millis();
    if ((now - _startMs) < _bootDelayMs) return;

    // Fetch remote version every hour just for display (regardless of installation)
    if ((now - _lastVersionCheckMs) >= 3600000UL) {  // Every 1 hour
        _lastVersionCheckMs = now;
        String remote;
        if (fetchRemoteVersion(remote)) {
            _remoteVersion = remote;
            app_logf("[RemoteOTA] Updated remote version display: %s", _remoteVersion.c_str());
        }
    }

    // Check daily at 03:XX (03:00 - 03:59)
    time_t timeNow = time(nullptr);
    struct tm* timeInfo = localtime(&timeNow);
    int currentHour = timeInfo->tm_hour;
    
    // Jeśli jesteśmy w godzinie 3 i jeszcze nie sprawdzaliśmy dziś
    if (currentHour == 3 && !_checkedToday) {
        _checkedToday = true;
        _lastCheckMs = now;
        _retryCount = 0;
        _lastRetryMs = 0;
        checkAndInstall();
    }
    
    // Resetuj flagę gdy przejdziemy poza godzinę 3
    else if (currentHour != 3 && _checkedToday) {
        _checkedToday = false;
        _retryCount = 0;
    }
    
    // Retry logic: jeśli ostatnia próba się nie powiodła i minęło dość czasu
    if (!_checkedToday && _retryCount > 0 && _retryCount < GITHUB_OTA_MAX_RETRIES) {
        if ((now - _lastRetryMs) >= GITHUB_OTA_RETRY_INTERVAL_MS) {
            _lastRetryMs = now;
            app_logf("[RemoteOTA] Retry attempt %d/%d", _retryCount + 1, GITHUB_OTA_MAX_RETRIES);
            checkAndInstall();
        }
    }
}

bool GitHubOTAManager::checkAndInstall() {
    _lastError = "";
    String remote;
    if (!fetchRemoteVersion(remote)) {
        // Increment retry counter on failure
        if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
            _retryCount++;
            _lastRetryMs = millis();
            app_logf("[RemoteOTA] Fetch failed, will retry (attempt %d/%d)", _retryCount, GITHUB_OTA_MAX_RETRIES);
        } else {
            app_logf("[RemoteOTA] Fetch failed, max retries reached");
        }
        return false;
    }

    _remoteVersion = remote;
    app_logf("[RemoteOTA] Daily check at 03:10: Current=%s, Remote=%s", _currentVersion.c_str(), _remoteVersion.c_str());

    if (!isRemoteVersionNewer(_remoteVersion, _currentVersion)) {
        app_log("[RemoteOTA] No newer version available");
        return true;
    }

    app_logf("[RemoteOTA] New version detected: %s - starting automatic installation", _remoteVersion.c_str());
    return performUpdate(_remoteVersion);
}

bool GitHubOTAManager::checkOnly() {
    _lastError = "";
    String remote;
    if (!fetchRemoteVersion(remote)) {
        return false;
    }

    _remoteVersion = remote;
    app_logf("[RemoteOTA] Current=%s, Remote=%s", _currentVersion.c_str(), _remoteVersion.c_str());

    if (!isRemoteVersionNewer(_remoteVersion, _currentVersion)) {
        app_log("[RemoteOTA] Brak nowej wersji");
        return true;
    }

    app_logf("[RemoteOTA] Nowa wersja wykryta: %s", _remoteVersion.c_str());
    return true;
}

bool GitHubOTAManager::fetchRemoteVersion(String& outVersion) {
    WiFiClientSecure client;
    client.setInsecure();

    String requestUrl = _versionUrl;
    requestUrl += (_versionUrl.indexOf('?') >= 0) ? "&" : "?";
    requestUrl += "ts=";
    requestUrl += String((unsigned long)millis());

    HTTPClient http;
    if (!http.begin(client, requestUrl)) {
        _lastError = "Nie mozna polaczyc z version URL";
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        return false;
    }

    http.addHeader("Cache-Control", "no-cache, no-store, max-age=0");
    http.addHeader("Pragma", "no-cache");
    http.setTimeout(12000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastError = String("version GET HTTP ") + code;
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        http.end();
        return false;
    }

    outVersion = http.getString();
    outVersion.trim();
    http.end();

    if (outVersion.length() == 0) {
        _lastError = "Pusty version.txt";
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        return false;
    }

    return true;
}

bool GitHubOTAManager::performUpdate(const String& targetVersion) {
    _inProgress = true;
    _writtenBytes = 0;
    _totalBytes = 0;
    _phase = 1; // connecting
    _updateStartMs = millis();  // Track operation start for timeout enforcement
    if (_statusCb) _statusCb(OTA_STATUS_DOWNLOADING);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, _firmwareUrl)) {
        _lastError = "Nie mozna polaczyc z firmware URL";
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        _inProgress = false;
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
        return false;
    }

    http.setTimeout(60000);  // Increased from 20s to 60s for larger firmware files
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastError = String("firmware GET HTTP ") + code;
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        http.end();
        _inProgress = false;
        
        // Increment retry counter on failure
        if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
            _retryCount++;
            _lastRetryMs = millis();
        }
        
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
        return false;
    }

    int totalSize = http.getSize();
    _phase = 2; // downloading headers/stream
    if (totalSize > 0) {
        _totalBytes = (uint32_t)totalSize;
    }
    size_t beginSize = (totalSize > 0) ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(beginSize)) {
        _lastError = String("Update.begin error: ") + Update.errorString();
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        http.end();
        _inProgress = false;
        
        // Increment retry counter on failure
        if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
            _retryCount++;
            _lastRetryMs = millis();
        }
        
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
        return false;
    }

    if (_statusCb) _statusCb(OTA_STATUS_WRITING);
    _phase = 3; // writing flash

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t writtenTotal = 0;
    unsigned long lastDataMs = millis();

    while (http.connected() && (totalSize < 0 || writtenTotal < (size_t)totalSize)) {
        // Global timeout check: abort if operation exceeds total timeout
        if ((millis() - _updateStartMs) > GITHUB_OTA_TOTAL_TIMEOUT_MS) {
            _lastError = "Calkowity timeout operacji update";
            app_logf("[RemoteOTA] %s", _lastError.c_str());
            Update.abort();
            http.end();
            _inProgress = false;
            _phase = 6;
            if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
            
            // Increment retry counter for next attempt
            if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
                _retryCount++;
                _lastRetryMs = millis();
            }
            return false;
        }
        
        // WiFi connectivity check during streaming
        if (!WiFi.isConnected()) {
            _lastError = "WiFi disconnected during firmware transfer";
            app_logf("[RemoteOTA] %s", _lastError.c_str());
            Update.abort();
            http.end();
            _inProgress = false;
            _phase = 6;
            if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
            
            // Increment retry counter for next attempt
            if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
                _retryCount++;
                _lastRetryMs = millis();
            }
            return false;
        }
        
        size_t availableBytes = stream->available();
        if (availableBytes > 0) {
            size_t toRead = availableBytes;
            if (toRead > sizeof(buffer)) toRead = sizeof(buffer);
            int bytesRead = stream->readBytes(buffer, toRead);
            if (bytesRead > 0) {
                size_t written = Update.write(buffer, (size_t)bytesRead);
                if (written != (size_t)bytesRead) {
                    _lastError = String("Update.write error: ") + Update.errorString();
                    app_logf("[RemoteOTA] %s", _lastError.c_str());
                    Update.abort();
                    http.end();
                    _inProgress = false;
                    _phase = 6;
                    
                    // Increment retry counter on failure
                    if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
                        _retryCount++;
                        _lastRetryMs = millis();
                    }
                    
                    if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
                    return false;
                }

                writtenTotal += written;
                _writtenBytes = (uint32_t)writtenTotal;
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
                app_logf("[RemoteOTA] %s", _lastError.c_str());
                Update.abort();
                http.end();
                _inProgress = false;
                _phase = 6;
                
                // Increment retry counter on failure
                if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
                    _retryCount++;
                    _lastRetryMs = millis();
                }
                
                if (_statusCb) _statusCb(OTA_STATUS_ERROR_WRITE);
                return false;
            }
            delay(1);
        }
    }

    if (!Update.end(true)) {
        _lastError = String("Update.end error: ") + Update.errorString();
        app_logf("[RemoteOTA] %s", _lastError.c_str());
        http.end();
        _inProgress = false;
        _phase = 6;
        
        // Increment retry counter on failure
        if (_retryCount < GITHUB_OTA_MAX_RETRIES) {
            _retryCount++;
            _lastRetryMs = millis();
        }
        
        if (_statusCb) _statusCb(OTA_STATUS_ERROR_VALIDATION);
        return false;
    }

    http.end();

    _writtenBytes = _totalBytes;

    _phase = 4; // validating
    if (_statusCb) _statusCb(OTA_STATUS_VALIDATING);
    otaManager.armRollbackGuardForNextBoot();

    _currentVersion = targetVersion;
    _inProgress = false;
    _phase = 5; // done

    if (_statusCb) _statusCb(OTA_STATUS_SUCCESS);
    app_logf("[RemoteOTA] Aktualizacja OK do %s, restart...", targetVersion.c_str());
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
