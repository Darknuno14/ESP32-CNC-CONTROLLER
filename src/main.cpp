#include <Arduino.h>
#include <vector>
#include <string>

#include "credentials.h"
#include "WiFiManager.h"
#include "FSManager.h"
#include "WebServerManager.h"
#include "SDManager.h"

// Create manager instances
FSManager* fsManager{nullptr};
WiFiManager* wifiManager{nullptr};
WebServerManager* webServerManager{nullptr};
SDCardManager* sdManager{nullptr};

// TEMP
bool allInitialized = false;

void task1(void * parameter) {
    // Log task start on core 0
    Serial.println("STATUS: Handle task started on core 0");

    // Initialize filesystem manager
    fsManager = new FSManager();
    if (fsManager->init()) {
        Serial.println("STATUS: Filesystem initialized");
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
    if (webServerManager->init()) {
        Serial.println("STATUS: Web server initialized");
    }
    if (webServerManager->begin()) {
        Serial.println("STATUS: Web server started");
    }

    allInitialized = true;

    // Main task loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Add delay to prevent watchdog timeouts
    }
}



void task2(void * parameter) {
    // Log task start on core 1
    Serial.println("STATUS: Program task started on core 1");
    while (true) {
        if (allInitialized){
        sdManager->listProjectFiles(); // Wywołujemy funkcję listującą pliki na karcie SD
        Serial.println("STATUS: Listing files on SD card");
        std::vector<std::string> pliki = sdManager->getProjectFiles(); // Pobieramy wektor plików z karty SD
        for (const auto& plik : pliki) { // Iterujemy po wektorze i wyświetlamy każdy element
                Serial.println(plik.c_str()); // Konwersja std::string na const char* dla Serial.println
            }
 
    }
           vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void setup() {
    Serial.begin(115200);
    delay(1000);
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN);

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
//     if(sdManager){
//         delete sdManager;
//         sdManager = nullptr;
//     }
// }