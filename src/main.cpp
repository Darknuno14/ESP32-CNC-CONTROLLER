#include <Arduino.h>
#include <vector>
#include <string>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "CONFIGURATION.h"
#include "credentials.h"

#include "FSManager.h"
#include "SDManager.h"
#include "WiFiManager.h"
#include "WebServerManager.h"


/*-- GLOBAL SCOPE --*/

SDCardManager* sdManager{new SDCardManager()}; // It is a global instance, so it can be accessed from multiple tasks

volatile bool commandStart{false}; // Used to communicate between tasks, Task1 sets this flag to true, based on.. 
volatile bool commandStop{false};  // ..webserver activity and then Task2 reads this flag and stops processing the file

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager);
void processGCodeFile(const char* filename, const bool stopCondition, const bool pauseCondition);

/*-- Tasks --*/

void task1(void * parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager(); // Allocate FSManager
    WiFiManager* wifiManager = new WiFiManager(); // Allocate WiFiManager
    WebServerManager* webServerManager = new WebServerManager(sdManager); // Allocate WebServerManager

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager);
    
    while (true) {
        commandStart = webServerManager->getStartCommand();
        commandStop = webServerManager->getStopCommand();
        vTaskDelay(pdMS_TO_TICKS(10)); // delay to prevent watchdog timeouts
    }
}
 
void task2(void * parameter) {
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());

    enum class TaskState {
        WAITING_FOR_PROJECT,
        IDLE,
        PROCESSING,
        STOPPED
    };

    TaskState state{TaskState::WAITING_FOR_PROJECT};
    
    while (true) {

        if (sdManager == NULL) {
            state = TaskState::WAITING_FOR_PROJECT;
        } else {
            switch (state) {
                case TaskState::WAITING_FOR_PROJECT:
                    if (sdManager->isProjectSelected()) {
                        #ifdef DEBUG_CNC
                            Serial.println("DEBUG CNC STATUS: Switching to idle state - project selected");
                        #endif
                        state = TaskState::IDLE;
                    }
                    break;
        
                case TaskState::IDLE:
                    if (commandStart) {
                        #ifdef DEBUG_CNC
                            Serial.println("DEBUG CNC STATUS: Switching to processing state - start command received");
                        #endif
                        state = TaskState::PROCESSING;
                    }
                    break;
        
                case TaskState::PROCESSING:
                    if (commandStop) {
                        #ifdef DEBUG_CNC
                            Serial.println("DEBUG CNC STATUS: Switching to paused state - stop command received");
                        #endif
                        state = TaskState::STOPPED;
                    }
                    processGCodeFile(sdManager->getSelectedProject().c_str(), commandStop, false);
                    state = TaskState::IDLE;
        
                    break;
        
                case TaskState::STOPPED:
                    if(true) {
                        #ifdef DEBUG_CNC
                            Serial.println("DEBUG CNC STATUS: Switching to waiting state - project stopped");
                        #endif
                        state = TaskState::WAITING_FOR_PROJECT;
                    }
                    break;
        
                default:
                    break;
                }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*-- Main Program --*/

void setup() {
    Serial.begin(115200);
    delay(1000);
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN);

    // Task configuration
    constexpr uint32_t WEBSERVER_STACK_SIZE = 8192;
    constexpr uint32_t CNC_STACK_SIZE = 8192;
    constexpr uint8_t WEBSERVER_TASK_PRIORITY = 1;
    constexpr uint8_t CNC_TASK_PRIORITY = 1;
    constexpr BaseType_t CORE_0 = 0;
    constexpr BaseType_t CORE_1 = 1; 
    
    xTaskCreatePinnedToCore(task1, 
                            "WebServer", 
                            8192, 
                            NULL,   // Parameters
                            WEBSERVER_TASK_PRIORITY,      // Priority
                            NULL,   // Task handle
                            CORE_0);

    xTaskCreatePinnedToCore(task2, 
                           "CNC", 
                           8192, 
                           NULL,
                           CNC_TASK_PRIORITY, 
                           NULL, 
                           CORE_1);
}

void loop() {
    // Serial.printf("Current core: %d\n", xPortGetCoreID());
}

/*-- MISCELLANEOUS FUNCTIONS --*/

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager) {
// Initialize filesystem manager
    switch(fsManager->init()) {
    case FSManagerError::OK:
        Serial.println("STATUS: Filesystem initialized");
        break;
    case FSManagerError::MOUNT_FAILED:
        Serial.println("ERROR: Filesystem mount failed");
        break;
    default:
        Serial.println("ERROR: Unknown error during initialization");
        break;
    }

    // Initialize SD card manager
    switch (sdManager->init()) {
    case SDCardError::OK:
      Serial.println("STATUS: SD card initialized");
      break;
    case SDCardError::INIT_FAILED:
      Serial.println("ERROR: SD card initialization failed");
      break;
    case SDCardError::DIRECTORY_CREATE_FAILED:
      Serial.println("ERROR: Projects directory creation failed");
      break;
    default:
      Serial.println("ERROR: Unknown error during initialization");
    }

    if (wifiManager->connect(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("STATUS: WiFi connected");
    }

    switch(webServerManager->init()) {
    case WebServerError::OK:
        Serial.println("STATUS: Web server initialized");
        break;
    case WebServerError::ALREADY_INITIALIZED:
        Serial.println("ERROR: Web server already initialized");
        break;
    case WebServerError::SERVER_ALLOCATION_FAILED:
        Serial.println("ERROR: Web server allocation failed");
        break;
    case WebServerError::EVENT_SOURCE_FAILED:
        Serial.println("ERROR: Event source failed");
        break;
    default:
        Serial.println("ERROR: Unknown error during initialization");
        break;
    }

    switch (webServerManager->begin())
   {
   case WebServerError::OK:
        Serial.println("STATUS: Web server started");
        break;
    case WebServerError::ALREADY_INITIALIZED:
        Serial.println("ERROR: Web server already started");
        break;   
   default:
        Serial.println("ERROR: Unknown error during initialization");
        break;
   }
}

void processGCodeFile(const char* filename, const bool stopCondition, const bool pauseCondition) {
    #ifdef DEBUG_CNC
        Serial.println("DEBUG CNC STATUS: Attempting to process file");
    #endif

    if (sdManager->takeSD()) {
        String filePath = String(sdManager->projectsDirectory) + "/" + String(filename);
        File file = SD.open(filePath);
        
        if (!file) {
            #ifdef DEBUG_CNC
                Serial.println("DEBUG CNC ERROR: Failed to open file");
            #endif
            sdManager->giveSD();
            return;
        } else {
            #ifdef DEBUG_CNC
                Serial.println("DEBUG CNC STATUS: Processing file: " + filePath + "\n");
            #endif
        }

        while (file.available() && !stopCondition) {
            if (!pauseCondition) {
                String line = file.readStringUntil('\n');
                Serial.printf("Processing line: %s\n", line.c_str());
                vTaskDelay(pdMS_TO_TICKS(100)); // Simulate processing time
            }
        }

        file.close();
        sdManager->giveSD();
        #ifdef DEBUG_CNC
            Serial.println("DEBUG CNC STATUS: Processing complete");
        #endif
                       
        if (stopCondition)
        {
            #ifdef DEBUG_CNC
                Serial.println("DEBUG CNC STATUS: Processing stopped via stop condition");
            #endif
            sdManager->giveSD();
            file.close();
            return;
        }
    }
}