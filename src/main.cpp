#include <Arduino.h>
#include <vector>
#include <string>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

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
uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, const bool& pauseCondition);
void connectToWiFi(WiFiManager* wifiManager);
void startWebServer(WebServerManager* webServerManager);

/*-- Tasks --*/

void taskControl(void * parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager); 

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager);
    connectToWiFi(wifiManager);
    startWebServer(webServerManager);
    
    while (true) {
        commandStart = webServerManager->getStartCommand();
        commandStop = webServerManager->getStopCommand();
        vTaskDelay(pdMS_TO_TICKS(10)); // delay to prevent watchdog timeouts
    }
}
 
void taskCNC(void * parameter) {
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());


    enum class TaskState {
        IDLE,
        RUNNING,
        STOPPED,
        JOG,
        HOMMING
    };

    TaskState state{TaskState::IDLE};
    
    while (true) {
        switch (state) {
            case TaskState::IDLE:
                #ifdef DEBUG_CNC_TASK
                    // Serial.println("DEBUG CNC STATUS: CNC is in idle state.");
                #endif
                if (commandStart) {
                    state = TaskState::RUNNING;
                }
                break;
            case TaskState::RUNNING: {
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC STATUS: CNC is running.");
                #endif
                uint8_t programResult{ 0 };

                programResult = processGCodeFile(sdManager->getSelectedProject(), commandStop, false);
                
                switch (programResult) {
                    case 0:
                        #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG CNC STATUS: Program completed successfully.");
                        #endif
                        state = TaskState::IDLE;
                        break;
                    case 1: // Stop condition triggered
                        #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG CNC STATUS: Program stopped.");
                        #endif
                        state = TaskState::STOPPED;
                        break;
                    case 2: // Error
                        #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG CNC ERROR: Program error.");
                        #endif
                        state = TaskState::STOPPED;
                        break;
                    default:
                        break;
                }
                break;
            }
            case TaskState::STOPPED:
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC STATUS: CNC is stopped.");
                #endif
                if (commandStart) {
                    state = TaskState::RUNNING;
                }
                break;
            case TaskState::JOG:
                // Implement as needed
                break;
            case TaskState::HOMMING:
                // Implement as needed
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while(true) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*-- Main Program --*/

void setup() {
    Serial.begin(115200);
    delay(1000);
    SPI.begin(CONFIG::SD_CLK_PIN, CONFIG::SD_MISO_PIN, CONFIG::SD_MOSI_PIN);

    
    xTaskCreatePinnedToCore(taskControl,  // Task function
                            "Control",  // Task name
                            CONFIG::CONTROLTASK_STACK_SIZE,  // Stack size
                            NULL,   // Parameters
                            CONFIG::CONTROLTASK_PRIORITY,      // Priority
                            NULL,   // Task handle
                            CONFIG::CORE_0 // Core
                            );

    xTaskCreatePinnedToCore(taskCNC, 
                           "CNC", 
                           CONFIG::CNCTASK_STACK_SIZE, 
                           NULL,
                           CONFIG::CNCTASK_PRIORITY, 
                           NULL, 
                           CONFIG::CORE_1);
}

void loop() {
    // Serial.printf("Current core: %d\n", xPortGetCoreID());
}

/*-- MISCELLANEOUS FUNCTIONS --*/

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager) {
    
    FSManagerStatus fsManagerStatus = fsManager->init();
    SDMenagerStatus sdManagerStatus = sdManager->init();
    WiFiManagerStatus wifiManagerStatus = wifiManager->init();
    WebServerStatus webServerManagerStatus = webServerManager->init();

    #ifdef DEBUG_CONTROL_TASK
    switch(fsManagerStatus) {
        case FSManagerStatus::OK: 
            Serial.println("STATUS: FS Manager initialized successfully."); break;
        case FSManagerStatus::MOUNT_FAILED:
            Serial.println("ERROR: FS Manager initialization failed."); break;
        default:
            Serial.println("ERROR: FS Manager unknown error."); break;
    }

    switch(sdManagerStatus) {
        case SDMenagerStatus::OK: 
            Serial.println("STATUS: SD Manager initialized successfully."); break;
        case SDMenagerStatus::INIT_FAILED:
            Serial.println("ERROR: SD Manager initialization failed."); break;
        case SDMenagerStatus::DIRECTORY_CREATE_FAILED:
            Serial.println("ERROR: SD Manager directory creation failed."); break;
        case SDMenagerStatus::DIRECTORY_OPEN_FAILED:
            Serial.println("ERROR: SD Manager directory open failed."); break;
        case SDMenagerStatus::MUTEX_CREATE_FAILED:
            Serial.println("ERROR: SD Manager mutex creation failed."); break;
        case SDMenagerStatus::FILE_OPEN_FAILED:
            Serial.println("ERROR: SD Manager file open failed."); break;
        case SDMenagerStatus::CARD_NOT_INITIALIZED:
            Serial.println("ERROR: SD Manager card not initialized."); break;
        default:
            Serial.println("ERROR: SD Manager unknown error."); break;
    }

    switch(wifiManagerStatus){
        case WiFiManagerStatus::OK:
            Serial.println("STATUS: WiFi Manager initialized successfully."); break;
        case WiFiManagerStatus::STA_MODE_FAILED:
            Serial.println("ERROR: WiFi Manager STA mode failed."); break;
        case WiFiManagerStatus::WIFI_NO_CONNECTION:
            Serial.println("ERROR: WiFi Manager no WiFi connection."); break;
        default:
            Serial.println("ERROR: WiFi Manager unknown error."); break;
    }

    switch(webServerManagerStatus){
        case WebServerStatus::OK:
            Serial.println("STATUS: Web Server Manager initialized successfully."); break;
        case WebServerStatus::ALREADY_INITIALIZED:
            Serial.println("ERROR: Web Server Manager already initialized."); break;
        case WebServerStatus::SERVER_ALLOCATION_FAILED:
            Serial.println("ERROR: Web Server Manager server allocation failed."); break;
        case WebServerStatus::EVENT_SOURCE_FAILED:
            Serial.println("ERROR: Web Server Manager event source failed."); break;
        case WebServerStatus::UNKNOWN_ERROR:
            Serial.println("ERROR: Web Server Manager unknown error."); break;
        default:
            Serial.println("ERROR: Web Server Manager unknown error."); break;
    }
    #endif

}

void connectToWiFi(WiFiManager* wifiManager) {
    WiFiManagerStatus wifiManagerStatus = wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, 10000);
    if (wifiManagerStatus == WiFiManagerStatus::OK) {
        Serial.println("STATUS: Connected to WiFi.");
    } else {
        Serial.println("ERROR: Failed to connect to WiFi.");
    }
}

void startWebServer(WebServerManager* webServerManager) {
    WebServerStatus webServerStatus = webServerManager->begin();
    if (webServerStatus == WebServerStatus::OK) {
        Serial.println("STATUS: Web server started.");
    } else {
        Serial.println("ERROR: Web server failed to start.");
    }
}


uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, const bool& pauseCondition) {
    #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC STATUS: Attempting to process file");
    #endif

    if(!sdManager->takeSD()) {
        return 2; // Error: unable to lock SD card
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC STATUS: Failed to lock SD card");
        #endif
    }

    std::string filePath = CONFIG::PROJECTS_DIR + filename;
    File file {SD.open(filePath.c_str())};
    if(!file) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Failed to open file");
        #endif
        sdManager->giveSD();
        return 2;
    } else {
        #ifdef DEBUG_CNC_TASK
            Serial.println(("DEBUG CNC STATUS: Processing file: " + filePath).c_str());
        #endif
    }

    while(file.available() && !stopCondition) {
        if (!pauseCondition) {
            String line = file.readStringUntil('\n');
            Serial.printf("Processing line: %s\n", line.c_str());
        }
    }

    if(stopCondition) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC STATUS: Processing stopped via stop condition");
        #endif
        file.close();
        sdManager->giveSD();
        return 1;
    }

    #ifdef DEBUG_CNC_TASK
    Serial.println("DEBUG CNC STATUS: Processing complete");
    #endif

    file.close();
    sdManager->giveSD();
    return 0;
}