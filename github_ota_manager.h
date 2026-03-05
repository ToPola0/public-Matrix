#ifndef GITHUB_OTA_MANAGER_H
#define GITHUB_OTA_MANAGER_H

#include <Arduino.h>
#include "ota_manager.h"

class GitHubOTAManager {
public:
    GitHubOTAManager();

    void begin(bool enabled,
               const char* currentVersion,
               const char* versionUrl,
               const char* firmwareUrl,
               uint32_t checkIntervalMs,
               uint32_t bootDelayMs);

    void loop(bool wifiConnected, bool otherOtaBusy);

    bool isInProgress() const;
    bool isConfigured() const;

    const String& getCurrentVersion() const;
    const String& getRemoteVersion() const;
    const String& getLastError() const;

    void setProgressCallback(OTA_ProgressCallback cb);
    void setStatusCallback(OTA_StatusCallback cb);

private:
    bool checkAndUpdate();
    bool fetchRemoteVersion(String& outVersion);
    bool performUpdate(const String& targetVersion);
    bool isRemoteVersionNewer(const String& remote, const String& local) const;
    int parseVersionPart(const String& v, int partIndex) const;

    bool _enabled = false;
    bool _configured = false;
    bool _started = false;
    bool _inProgress = false;

    String _currentVersion;
    String _remoteVersion;
    String _versionUrl;
    String _firmwareUrl;
    String _lastError;

    uint32_t _checkIntervalMs = 3600000UL;
    uint32_t _bootDelayMs = 30000UL;
    uint32_t _startMs = 0;
    uint32_t _lastCheckMs = 0;

    OTA_ProgressCallback _progressCb = nullptr;
    OTA_StatusCallback _statusCb = nullptr;
};

extern GitHubOTAManager githubOtaManager;

#endif // GITHUB_OTA_MANAGER_H
