#pragma once
#include <LittleFS.h>

/**
 * @brief Enumeration of possible FSManager error states
 * 
 * Used to track and handle various error conditions that may occur
 */
enum class FSManagerError {
    OK,
    MOUNT_FAILED
};

/**
 * @brief File System Manager for ESP32 CNC Controller
 * 
 * This class manages the LittleFS filesystem operations for the ESP32 CNC Controller.
 * It handles mounting, initialization, and provides filesystem access for web server files.
 * 
 * @endcode
 */
class FSManager {
public:
    /**
     * @brief Default constructor
     * 
     * Initializes FSManager instance without mounting the filesystem.
     * Call init() separately to mount the filesystem.
     */
    FSManager() = default;

    /**
     * @brief Initialize and mount the LittleFS filesystem
     * 
     * This function attempts to mount the LittleFS filesystem.
     * 
     * @note The filesystem is crucial for storing web server files (HTML, JS, CSS)
     * 
     * @return bool Returns true if filesystem mounted successfully, false if mounting failed
     * 
    */
    FSManagerError init();
}; 