#pragma once

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

#include "WiFiManager.h"
#include "FSManager.h"
#include "WebServerManager.h"
#include "SDManager.h"

// ============================================================================
// Global Variables:

SDCardManager* sdManager{nullptr};  // Create manager instances - Used in both tasks

bool allInitialized = false; // TEMP


// Task 1: Handle filesystem, WiFi, and web server
void task1(void * parameter) {
    // Log task start on core 0
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());
    FSManager* fsManager{nullptr};
    WiFiManager* wifiManager{nullptr};
    WebServerManager* webServerManager{nullptr};

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
    sdManager = new SDCardManager();
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

    // Initialize WiFi connection
    wifiManager = new WiFiManager();
    if (wifiManager->connect(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("STATUS: WiFi connected");
    }

    // Initialize and start web server
    webServerManager = new WebServerManager(sdManager);
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

    // TEMP
    allInitialized = true;

    // Main task loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Add delay to prevent watchdog timeouts
    }
}
 
// ============================================================================
// Task 2: Program task
void task2(void * parameter) {
    // Log task start on core 0
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());

    // Main task loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Add delay to prevent watchdog timeouts
    }
}

// ============================================================================
// Setup function:
void setup() {
    Serial.begin(115200);
    delay(1000);
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN);

    xTaskCreatePinnedToCore(task1, "HandleTask", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(task2, "ProgramTask", 8192, NULL, 1, NULL, 1);
}

// ============================================================================
// Loop function:
void loop() {
    // Serial.printf("Current core: %d\n", xPortGetCoreID());
    // Empty loop
}

// MIGHT BE IMPLEMENTED IN THE FUTURE

// void cleanup(){
//         if(fsManager){
//         delete fsManager;
//         fsManager = nullptr;
//     }
//     if(wifiManager){
//         delete wifiManager;
//         wifiManager = nullptr;
//     }
//     if(webServerManager){
//         delete webServerManager;
//         webServerManager = nullptr;
//     }
//     if(sdManager){
//         delete sdManager;
//         sdManager = nullptr;
//     }
// }