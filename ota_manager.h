#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

// OTA Status codes
enum OTA_Status {
    OTA_STATUS_IDLE,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_WRITING,
    OTA_STATUS_VALIDATING,
    OTA_STATUS_SUCCESS,
    OTA_STATUS_ERROR_VALIDATION,
    OTA_STATUS_ERROR_WRITE,
    OTA_STATUS_ERROR_BOOT,
    OTA_STATUS_ROLLED_BACK
};

// OTA Event callback
typedef void (*OTA_ProgressCallback)(uint32_t progress, uint32_t total);
typedef void (*OTA_StatusCallback)(OTA_Status status);

class OTAManager {
public:
    OTAManager();
    
    // Initialize OTA manager (called in loop when WiFi is connected)
    void begin(const char* hostname, uint16_t port, const char* password);
    
    // Handle OTA events (called in loop)
    void handle();

    // Boot safety guard: call once in setup and continuously in loop
    void handleBootGuardOnStartup();
    void processBootGuard();

    // Arm 10-second rollback guard for next boot after successful OTA write
    void armRollbackGuardForNextBoot();
    
    // Get current OTA status
    OTA_Status getStatus() const;
    
    // Get last error code
    uint32_t getLastError() const;
    
    // Check if OTA is in progress
    bool isInProgress() const;
    
    // Register progress callback
    void setProgressCallback(OTA_ProgressCallback cb);
    
    // Register status callback
    void setStatusCallback(OTA_StatusCallback cb);
    
    // Get partition info
    static const char* getBootPartitionName();
    static const char* getNextPartitionName();
};

extern OTAManager otaManager;

#endif // OTA_MANAGER_H
