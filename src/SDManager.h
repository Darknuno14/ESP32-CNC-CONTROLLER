#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <string>

#include "CONFIGURATION.h"

/**
 * @brief Enumeration of possible SD card error states
 * 
 * Used to track and handle various error conditions that may occur
 * during SD card initialization and operation.
 */
enum class SDCardError {
    OK,
    INIT_FAILED,
    DIRECTORY_CREATE_FAILED,
    DIRECTORY_OPEN_FAILED,
    FILE_OPEN_FAILED,
    CARD_NOT_INITIALIZED,
    UNKNOWN_ERROR  
};

/**
 * @brief SD Card Manager for ESP32 CNC Controller
 * 
 * Manages the SD card filesystem, handling:
 * - Card initialization and status
 * - Project file listing
 * - File operations
 * 
 */
class SDCardManager {
private:
    bool cardInitialized{false}; // Flag to track card initialization status

    bool createProjectsDirectory(); 
    std::vector<std::string> projectFiles;  // Vector to store project files 
                                            // Według gemini String z Arduino może powodować fragmentację pamięci. Unikanie String z Arduino: Klasa String z Arduino alokuje pamięć dynamicznie, co w dłuższym okresie może prowadzić do fragmentacji pamięci i niestabilności systemu. 
    
    bool ProjectSelected{false};
    String selectedProjectName{};

    SemaphoreHandle_t sdMutex{};

public:
    /**
     * @brief Construct a new SD Card Manager
     * 
     */
    SDCardManager() = default;

    String projectsDirectory{"/Projects"};
    /**
     * @brief Initialize the SD card
     * 
     * @return SDCardError 
     */
    SDCardError init();
    /**
     * @brief Check if the SD card is initialized
     * 
     * @return true if the card is initialized
     * @return false if the card is not initialized
     */
    bool isCardInitialized();
    /**
     * @brief Update the project files vector
     * 
     * @return SDCardError 
     */
    SDCardError listProjectFiles();
    /**
     * @brief Get the Project Files vector
     * 
     * @return std::vector<std::string> 
     */
    std::vector<std::string> getProjectFiles() const;

    bool takeSD();
    void giveSD();

    bool isProjectSelected();

    String getSelectedProject();
    void setSelectedProject(String filename);
    void clearSelectedProject();
};
