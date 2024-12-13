#include <Arduino.h>

#include "credentials.h"
#include "WiFiManager.h"
#include "FSManager.h"
#include "WebServerManager.h"

// Create manager instances
FSManager* fsManager{nullptr};
WiFiManager* wifiManager{nullptr};
WebServerManager* webServerManager{nullptr};

void task1(void * parameter) {
    // Log task start on core 0
    Serial.println("STATUS: Handle task started on core 0");

    // Initialize filesystem manager
    fsManager = new FSManager();
    if (fsManager->init()) {
        Serial.println("STATUS: Filesystem initialized");
    }

    // Initialize WiFi connection
    wifiManager = new WiFiManager();
    if (wifiManager->connect(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("STATUS: WiFi connected");
    }

    // Initialize and start web server
    webServerManager = new WebServerManager();
    if (webServerManager->init()) {
        Serial.println("STATUS: Web server initialized");
    }
    if (webServerManager->begin()) {
        Serial.println("STATUS: Web server started");
    }

    // Main task loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Add delay to prevent watchdog timeouts
        //  WORK IN PROGRESS
    }
}


void task2(void * parameter) {
    // Log task start on core 1
    Serial.println("STATUS: Program task started on core 1");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Add delay to prevent watchdog timeouts
        // WORK IN PROGRESS
    }
}


void setup() {
    Serial.begin(115200);
    delay(1000);

    xTaskCreatePinnedToCore(task1, "HandleTask", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(task2, "ProgramTask", 8192, NULL, 1, NULL, 1);
}

void loop() {
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
// }