#include "FSManager.h"

bool FSManager::init() {
    // Try to mount the LittleFS filesystem with format_if_failed=true
    if (!LittleFS.begin(true)) {
        // Print error message if mounting fails
        Serial.print("ERROR: Failed to mount LittleFS");
        return false;
    }
    // Return true if filesystem mounted successfully
    return true;
}