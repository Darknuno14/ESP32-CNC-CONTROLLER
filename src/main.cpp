#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AccelStepper.h>

#include <vector>
#include <string>

#include "CONFIGURATION.h"
#include "credentials.h"

#include "SharedTypes.h"
#include "FSManager.h"
#include "SDManager.h"
#include "WiFiManager.h"
#include "WebServerManager.h"


/*-- GLOBAL SCOPE --*/

// Globalna instancja, aby był dostęp dla wszystkich zadań
SDCardManager* sdManager{new SDCardManager()};
ConfigManager* configManager{new ConfigManager(sdManager)};

/* --FUNCTION PROTOTYPES-- */

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager);
void connectToWiFi(WiFiManager* wifiManager);
void startWebServer(WebServerManager* webServerManager);
float getParameter(const String& line, char param);
bool processGCodeLine(String line, MachineState& state, AccelStepper& stepperX, AccelStepper& stepperY);
uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, 
    volatile const bool& pauseCondition, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state);

/*-- Tasks --*/

void taskControl(void * parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager, configManager); 

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager);
    connectToWiFi(wifiManager);
    startWebServer(webServerManager);

    unsigned long lastWebUpdateTime = 0;
    constexpr unsigned long WEB_UPDATE_INTERVAL = 200; // ms
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // delay to prevent watchdog timeouts
    }
}
 
void taskCNC(void * parameter) {
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());

    CNCState currentState {CNCState::IDLE};

    bool projectReady {false}; // Czy projekt jest wybrany
    std::string projectName {""}; // Nazwa wybranego projektu

    unsigned long lastStatusUpdateTime {0}; // Ostatnia aktualizacja statusu
    constexpr unsigned long STATUS_UPDATE_INTERVAL {500}; // Interwał aktualizacji statusu (500ms)

    AccelStepper stepperX(AccelStepper::DRIVER, CONFIG::STEP_X_PIN, CONFIG::DIR_X_PIN);
    AccelStepper stepperY(AccelStepper::DRIVER, CONFIG::STEP_Y_PIN, CONFIG::DIR_Y_PIN);

    while (true) {
        // Krótkie opóźnienie, aby zapobiec przekroczeniu czasu watchdoga i umożliwić działanie innych zadań
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*-- Main Program --*/

void setup() {
    Serial.begin(115200);
    delay(1000);

    SPI.begin(CONFIG::SD_CLK_PIN, CONFIG::SD_MISO_PIN, CONFIG::SD_MOSI_PIN);

    pinMode(CONFIG::WIRE_RELAY_PIN, OUTPUT); digitalWrite(CONFIG::WIRE_RELAY_PIN, LOW);
    pinMode(CONFIG::FAN_RELAY_PIN, OUTPUT); digitalWrite(CONFIG::FAN_RELAY_PIN, LOW);

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

void loop() {}

/*-- MISCELLANEOUS FUNCTIONS --*/

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager) {
    
    FSManagerStatus fsManagerStatus = fsManager->init();
    SDManagerStatus sdManagerStatus = sdManager->init();
    WiFiManagerStatus wifiManagerStatus = wifiManager->init();
    WebServerStatus webServerManagerStatus = webServerManager->init();
    ConfigManagerStatus configManagerStatus = configManager->init();

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
            case SDManagerStatus::OK: 
                Serial.println("STATUS: SD Manager initialized successfully."); break;
            case SDManagerStatus::INIT_FAILED:
                Serial.println("ERROR: SD Manager initialization failed."); break;
            case SDManagerStatus::DIRECTORY_CREATE_FAILED:
                Serial.println("ERROR: SD Manager directory creation failed."); break;
            case SDManagerStatus::DIRECTORY_OPEN_FAILED:
                Serial.println("ERROR: SD Manager directory open failed."); break;
            case SDManagerStatus::MUTEX_CREATE_FAILED:
                Serial.println("ERROR: SD Manager mutex creation failed."); break;
            case SDManagerStatus::FILE_OPEN_FAILED:
                Serial.println("ERROR: SD Manager file open failed."); break;
            case SDManagerStatus::CARD_NOT_INITIALIZED:
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

        switch(configManagerStatus) {
            case ConfigManagerStatus::OK: 
                Serial.println("STATUS: Config Manager initialized successfully."); break;
            case ConfigManagerStatus::FILE_OPEN_FAILED:
                Serial.println("WARNING: Config file not found, using defaults."); break;
            case ConfigManagerStatus::FILE_WRITE_FAILED:
                Serial.println("ERROR: Config Manager file write failed."); break;
            case ConfigManagerStatus::JSON_PARSE_ERROR:
                Serial.println("ERROR: Config Manager JSON parse error."); break;
            case ConfigManagerStatus::JSON_SERIALIZE_ERROR:
                Serial.println("ERROR: Config Manager JSON serialize error."); break;
            case ConfigManagerStatus::SD_ACCESS_ERROR:
                Serial.println("ERROR: Config Manager SD access error."); break;
            default:
                Serial.println("ERROR: Config Manager unknown error."); break;
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

float getParameter(const String& line, char param) {
    int index {line.indexOf(param)};
    if (index == -1) return NAN;
    
    // Wyznaczanie początku tesktu z parametrem
    int valueStart {index + 1};
    int valueEnd {line.length()};
    
    // Wyznaczanie końca tekstu z parametrem (następna spacja lub koniec linii)
    for (int i{valueStart}; i < line.length(); ++i) {
        if (line[i] == ' ' || line[i] == '\t') {
            valueEnd = i;
            break;
        }
    }

    return line.substring(valueStart, valueEnd).toFloat();
}

bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state) {
    #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC GCODE: Processing: %s\n", line.c_str());
    #endif
}

uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, 
    volatile const bool& pauseCondition, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state) {
    #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC STATUS: Starting to process file");
    #endif
   
    // Przejęcie dostępu do karty SD
    if (!sdManager->takeSD()) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Nie udało się zablokować karty SD");
        #endif
        return 2; // Błąd: nie można zablokować karty SD
    }
    
    // Otwarcie pliku
    std::string filePath {CONFIG::PROJECTS_DIR + filename};
    File file {SD.open(filePath.c_str())};
    
    if (!file) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Nie udało się otworzyć pliku");
        #endif
        sdManager->giveSD();
        return 2; // Błąd: otwarcie pliku nie powiodło się
    }
    
    #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC STATUS: Przetwarzanie pliku: %s\n", filePath.c_str());
    #endif
        
    // Przetwarzanie pliku linia po linii
    uint32_t lineNumber {0};
    bool processingError {false};
    
    while (file.available() && !stopCondition) {
        if (pauseCondition) {
            // Jeśli wstrzymano, czekaj bez przetwarzania
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Odczyt linii z pliku
        String line = file.readStringUntil('\n');
        lineNumber++;
        
        #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC FILE READ: %d: %s\n", lineNumber, line.c_str());
        #endif
        
        // Przetwarzanie linii
        if (!processGCodeLine(line, stepperX, stepperY, state)) {
            #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC ERROR: Błąd przetwarzania linii %d\n", lineNumber);
            #endif
            processingError = true;
            break;
        }
        
        // Krótkie opóźnienie, aby zapobiec przekroczeniu czasu watchdoga
        vTaskDelay(10);
    }
        
    // Zamknij plik i zwolnij kartę SD
    file.close();
    sdManager->giveSD();
        
    if (stopCondition) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC STATUS: Przetwarzanie zatrzymane przez warunek stopu");
        #endif
        return 1; // Zatrzymano przez użytkownika
    } else if (processingError) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Błąd przetwarzania");
        #endif
        return 2; // Błąd podczas przetwarzania
    } else {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC STATUS: Przetwarzanie zakończone pomyślnie");
        #endif
        return 0; // Sukces
    }
}