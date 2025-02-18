#pragma once

// Used to track (and handle) various error conditions that may occur
enum class FSManagerStatus {
    OK,
    MOUNT_FAILED
};


// Menages the LittleFS filesystem operations for the ESP32 CNC Controller.
// It handles mounting, initialization and provides filesystem access for web server files.
class FSManager {
public:

    // Initializes FSManager instance without mounting the filesystem.
    // Call init() separately to mount the filesystem.
    FSManager() = default;

    // This function attempts to mount the LittleFS filesystem.
    // The filesystem is crucial for storing web server files (HTML, JS, CSS).
    FSManagerStatus init();
}; 
