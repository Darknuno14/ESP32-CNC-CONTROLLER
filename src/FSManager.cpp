#include "FSManager.h"


FSManagerError FSManager::init() {
    // Attempt to mount LittleFS
    // The 'false' parameter disables automatic formatting if mounting fails
    if (!LittleFS.begin(false)) {
        // If mounting fails, log error and return false
        return FSManagerError::MOUNT_FAILED;
    }
    
    // Filesystem mounted successfully
    return FSManagerError::OK;
}