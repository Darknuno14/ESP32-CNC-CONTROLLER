#pragma once
#include <vector>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class SDMenagerStatus {
    OK,
    INIT_FAILED,
    DIRECTORY_CREATE_FAILED,
    DIRECTORY_OPEN_FAILED,
    MUTEX_CREATE_FAILED,
    FILE_OPEN_FAILED,
    CARD_NOT_INITIALIZED,
    UNKNOWN_ERROR  
};

 // Manages the SD card, which acts as a storage medium for project files and parameters.
 // Provides a mutex for synchronizing access to the card.
class SDCardManager {
private:
    // Track card initialization status
    bool cardInitialized{false};

    // Create the Projects directory on the SD card
    // true = directory creation was successful
    bool createProjectsDirectory(); 

    // Store project files names
    std::vector<std::string> projectFiles;  

    // Mutex for SD card access
    SemaphoreHandle_t sdMutex{};

    // Track if a project is selected
    bool projectIsSelected{false};
    
    // Store the selected project filename 
    std::string selectedProject{};

public:

    // Default constructor for SDCardManager
    SDCardManager() = default;

    // Initialize the SD card
    SDMenagerStatus init();

    // Check if the SD card is initialized
    bool isCardInitialized() const;

    //Update the project files vector
    SDMenagerStatus listProjectFiles();
    
    //Get the Project Files vector
    std::vector<std::string> getProjectFiles() const;

    // Take the SD card for exclusive access
    bool takeSD();

    // Give the SD card back for other tasks to use
    void giveSD();

    // Check if a project is selected
    bool isProjectSelected() const;

    // Get the selected project filename
    const std::string& getSelectedProject();

    // Set the selected project filename
    void setSelectedProject(const std::string& filename);

    // Clear the selected project filename
    void clearSelectedProject();
};

