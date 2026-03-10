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
    bool isEnabled() const;

    const String& getCurrentVersion() const;
    const String& getRemoteVersion() const;
    const String& getLastError() const;
    bool isUpdateAvailable() const;

    bool checkNow();
    bool installNow();
    bool startInstallAsync();
    uint32_t getLastCheckMs() const;
    uint32_t getCheckIntervalMs() const;
    uint32_t getWrittenBytes() const;
    uint32_t getTotalBytes() const;
    uint8_t getPhase() const;

    void setProgressCallback(OTA_ProgressCallback cb);
    void setStatusCallback(OTA_StatusCallback cb);

private:
    bool checkOnly();
    bool checkAndInstall();
    bool fetchRemoteVersion(String& outVersion);
    bool performUpdate(const String& targetVersion);
    bool installInternal();
    bool isRemoteVersionNewer(const String& remote, const String& local) const;
    int parseVersionPart(const String& v, int partIndex) const;
    static void installTaskEntry(void* arg);

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
    uint32_t _lastVersionCheckMs = 0;  // Track when we last fetched remote version for display
    uint32_t _lastRetryMs = 0;
    uint32_t _updateStartMs = 0;
    uint8_t _retryCount = 0;
    bool _checkedToday = false;
    volatile uint32_t _writtenBytes = 0;
    volatile uint32_t _totalBytes = 0;
    volatile bool _installTaskRunning = false;
    volatile uint8_t _phase = 0;

    OTA_ProgressCallback _progressCb = nullptr;
    OTA_StatusCallback _statusCb = nullptr;
};

extern GitHubOTAManager githubOtaManager;

#endif // GITHUB_OTA_MANAGER_H
