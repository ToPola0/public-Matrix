#include "ota_manager.h"
#include <ArduinoOTA.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <Preferences.h>
#include "app_logger.h"

OTAManager otaManager;

// Global state
static OTA_Status gOtaStatus = OTA_STATUS_IDLE;
static uint32_t gLastError = 0;
static bool gOtaInitialized = false;
static OTA_ProgressCallback gProgressCallback = nullptr;
static OTA_StatusCallback gStatusCallback = nullptr;

// OTA boot guard state
static bool gBootGuardArmed = false;
static uint32_t gBootGuardStartMs = 0;
static const uint32_t kOtaBootConfirmMs = 10000;

static void clearBootGuardState(Preferences& prefs) {
    prefs.putUChar("pending", 0);
    prefs.putUChar("attempts", 0);
    prefs.putString("prev", "");
    prefs.putString("target", "");
}

static const esp_partition_t* findAppPartitionByLabel(const char* label) {
    if (!label || !label[0]) return nullptr;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
}

OTAManager::OTAManager() {
}

void OTAManager::setProgressCallback(OTA_ProgressCallback cb) {
    gProgressCallback = cb;
}

void OTAManager::setStatusCallback(OTA_StatusCallback cb) {
    gStatusCallback = cb;
}

OTA_Status OTAManager::getStatus() const {
    return gOtaStatus;
}

uint32_t OTAManager::getLastError() const {
    return gLastError;
}

bool OTAManager::isInProgress() const {
    return (gOtaStatus == OTA_STATUS_DOWNLOADING || gOtaStatus == OTA_STATUS_WRITING);
}

const char* OTAManager::getBootPartitionName() {
    const esp_partition_t* partition = esp_ota_get_boot_partition();
    return partition ? (const char*)(partition->label) : "unknown";
}

const char* OTAManager::getNextPartitionName() {
    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    if (!bootPartition) return "unknown";
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(bootPartition);
    return nextPartition ? (const char*)(nextPartition->label) : "unknown";
}

void OTAManager::begin(const char* hostname, uint16_t port, const char* password) {
    if (gOtaInitialized) return;
    
    app_log("[OTA] Starting A/B OTA system (WiFi only, USB disabled)");
    app_logf("[OTA] Boot partition: %s | Next update: %s", getBootPartitionName(), getNextPartitionName());
    
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPort(port);
    ArduinoOTA.setTimeout(120000); // 2 minutes
    
    if (password && strlen(password) > 0) {
        ArduinoOTA.setPassword(password);
    }
    
    // === OTA Callbacks ===
    ArduinoOTA.onStart([]() {
        gOtaStatus = OTA_STATUS_DOWNLOADING;
        if (gStatusCallback) gStatusCallback(OTA_STATUS_DOWNLOADING);
        app_log("[OTA] Update started");
    });
    
    ArduinoOTA.onEnd([]() {
        gOtaStatus = OTA_STATUS_VALIDATING;
        if (gStatusCallback) gStatusCallback(OTA_STATUS_VALIDATING);
        app_log("[OTA] Download complete, validating...");
        delay(500);

        // Arm rollback guard: new firmware must survive 10 seconds after reboot.
        otaManager.armRollbackGuardForNextBoot();
        
        gOtaStatus = OTA_STATUS_SUCCESS;
        if (gStatusCallback) gStatusCallback(OTA_STATUS_SUCCESS);
        app_log("[OTA] SUCCESS! Restarting in 5 seconds...");
        delay(5000);
        ESP.restart();
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPercent = 255;
        uint8_t percent = (uint8_t)((progress * 100U) / total);
        
        gOtaStatus = OTA_STATUS_WRITING;
        if (gProgressCallback) gProgressCallback(progress, total);
        
        if (percent != lastPercent && (percent % 10 == 0 || percent == 99)) {
            app_logf("[OTA] Progress: %u%%", percent);
            lastPercent = percent;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        gLastError = error;
        gOtaStatus = OTA_STATUS_ERROR_WRITE;
        if (gStatusCallback) gStatusCallback(OTA_STATUS_ERROR_WRITE);
        
        const char* errorMsg = "UNKNOWN";
        if (error == OTA_AUTH_ERROR) errorMsg = "Auth failed";
        else if (error == OTA_BEGIN_ERROR) errorMsg = "Begin failed";
        else if (error == OTA_CONNECT_ERROR) errorMsg = "Connect failed";
        else if (error == OTA_RECEIVE_ERROR) errorMsg = "Receive failed";
        else if (error == OTA_END_ERROR) errorMsg = "End failed";
        
        app_logf("[OTA] ERROR: %s (code %u)", errorMsg, error);
    });
    
    ArduinoOTA.begin();
    gOtaInitialized = true;
    app_logf("[OTA] Ready: %s:%u", hostname, port);
}

void OTAManager::handle() {
    if (!gOtaInitialized) return;
    ArduinoOTA.handle();
}

void OTAManager::armRollbackGuardForNextBoot() {
    Preferences prefs;
    if (!prefs.begin("ota_guard", false)) {
        app_log("[OTA] Guard arm failed: NVS open error");
        return;
    }

    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(bootPartition);
    const char* prevLabel = (bootPartition && bootPartition->label) ? bootPartition->label : "";
    const char* targetLabel = (nextPartition && nextPartition->label) ? nextPartition->label : "";

    prefs.putUChar("pending", 1);
    prefs.putUChar("attempts", 0);
    prefs.putString("prev", prevLabel);
    prefs.putString("target", targetLabel);
    prefs.end();

    app_logf("[OTA] Guard armed: prev=%s target=%s", prevLabel, targetLabel);
}

void OTAManager::handleBootGuardOnStartup() {
    Preferences prefs;
    if (!prefs.begin("ota_guard", false)) {
        return;
    }

    uint8_t pending = prefs.getUChar("pending", 0);
    if (pending == 0) {
        prefs.end();
        return;
    }

    String prevSlot = prefs.getString("prev", "");
    String targetSlot = prefs.getString("target", "");
    uint8_t attempts = prefs.getUChar("attempts", 0);

    const esp_partition_t* currentBoot = esp_ota_get_boot_partition();
    const char* currentLabel = (currentBoot && currentBoot->label) ? currentBoot->label : "";

    // If we're not running target slot anymore, clear stale guard data.
    if (targetSlot.length() == 0 || targetSlot != String(currentLabel)) {
        clearBootGuardState(prefs);
        prefs.end();
        app_log("[OTA] Guard cleared: not running target slot");
        return;
    }

    attempts++;
    prefs.putUChar("attempts", attempts);
    prefs.end();

    if (attempts > 1) {
        const esp_partition_t* rollbackPartition = findAppPartitionByLabel(prevSlot.c_str());
        if (!rollbackPartition) {
            app_log("[OTA] Guard rollback failed: previous slot not found");
            Preferences clearPrefs;
            if (clearPrefs.begin("ota_guard", false)) {
                clearBootGuardState(clearPrefs);
                clearPrefs.end();
            }
            return;
        }

        esp_err_t err = esp_ota_set_boot_partition(rollbackPartition);
        if (err == ESP_OK) {
            Preferences clearPrefs;
            if (clearPrefs.begin("ota_guard", false)) {
                clearBootGuardState(clearPrefs);
                clearPrefs.end();
            }
            gOtaStatus = OTA_STATUS_ROLLED_BACK;
            app_logf("[OTA] Guard rollback to %s (boot in <10s failed)", prevSlot.c_str());
            delay(200);
            ESP.restart();
        } else {
            app_logf("[OTA] Guard rollback failed, err=0x%X", (unsigned)err);
        }
        return;
    }

    gBootGuardArmed = true;
    gBootGuardStartMs = millis();
    app_logf("[OTA] Guard active on %s: confirm in %lu ms", currentLabel, (unsigned long)kOtaBootConfirmMs);
}

void OTAManager::processBootGuard() {
    if (!gBootGuardArmed) return;
    if ((millis() - gBootGuardStartMs) < kOtaBootConfirmMs) return;

    Preferences prefs;
    if (!prefs.begin("ota_guard", false)) {
        return;
    }

    clearBootGuardState(prefs);
    prefs.end();
    gBootGuardArmed = false;
    app_log("[OTA] Guard confirmed: firmware alive for 10s");
}
