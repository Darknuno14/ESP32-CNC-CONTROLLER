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

// Kolejki do komunikacji między zadaniami
QueueHandle_t stateQueue;   // Od CNC do Control (informacje o stanie)
QueueHandle_t commandQueue; // Od Control do CNC (komendy sterujące)

/* --FUNCTION PROTOTYPES-- */

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager);
void connectToWiFi(WiFiManager* wifiManager);
void startWebServer(WebServerManager* webServerManager);
float getParameter(const String& line, char param);
bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state, MachineConfig& config);
uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, 
    volatile const bool& pauseCondition, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state);
void updateOutputs(const MachineState& state);

/*-- Tasks --*/

void taskControl(void * parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager, configManager, commandQueue); 

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager);
    connectToWiFi(wifiManager);
    startWebServer(webServerManager);

    // Bufor na przychodzący stan maszyny
    MachineState currentState {};

    // Przechowujemy ostatni stan maszyny
    MachineState lastState {};
    memset(&lastState, 0, sizeof(MachineState));

    unsigned long lastWebUpdateTime {0};
    constexpr unsigned long WEB_UPDATE_INTERVAL {200}; // ms
    
    while (true) {
        // 1. Odbieranie statusu maszyny z kolejki
        if (xQueueReceive(stateQueue, &currentState, 0) == pdTRUE) {
            #ifdef DEBUG_CONTROL_TASK
                Serial.printf("STATUS: Received machine state. Position: X=%.2f, Y=%.2f, State=%d\n", 
                    currentState.currentX, currentState.currentY, (int)currentState.state);
            #endif
            
            // Zapamiętaj ostatni stan
            lastState = currentState; 
            
            // Wysyłaj aktualizacje przez EventSource jeśli jest istotna zmiana lub minął interwał
            if (millis() - lastWebUpdateTime > WEB_UPDATE_INTERVAL || 
                currentState.state != lastState.state || 
                fabs(currentState.jobProgress - lastState.jobProgress) > 0.5) {
                
                // Generuj dane JSON
                char buffer[256];
                snprintf(buffer, sizeof(buffer), 
                    "{\"state\":%d,\"currentX\":%.3f,\"currentY\":%.3f,\"spindleOn\":%s,\"fanOn\":%s,"
                    "\"jobProgress\":%.1f,\"currentLine\":%d,\"currentProject\":\"%s\",\"jobRunTime\":%lu,\"isPaused\":%s}",
                    (int)currentState.state, 
                    currentState.currentX, currentState.currentY,
                    currentState.hotWireOn ? "true" : "false", 
                    currentState.fanOn ? "true" : "false",
                    currentState.jobProgress,
                    currentState.currentLine,
                    currentState.currentProject.c_str(),
                    currentState.jobRunTime,
                    currentState.isPaused ? "true" : "false");
                
                // Wyślij zdarzenie do klientów webowych
                webServerManager->sendEvent("machine-status", buffer);
                
                lastWebUpdateTime = millis();
            }
        }
                        
        vTaskDelay(pdMS_TO_TICKS(10)); // delay to prevent watchdog timeouts
    }
}

 
void taskCNC(void * parameter) {
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(10000));

    MachineState state {};
    state.state = CNCState::IDLE;
    state.relativeMode = false;
    state.isPaused = false;
    state.hotWireOn = false;
    state.fanOn = false;
    state.currentX = 0.0f;
    state.currentY = 0.0f;
    state.jobProgress = 0.0f;
    state.currentLine = 0;
    state.jobStartTime = 0;
    state.jobRunTime = 0;
    state.hasError = false;
    state.errorCode = 0;
    state.currentProject = "";

    bool processingStopped = false;

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
    SPI.begin(CONFIG::SD_CLK_PIN, CONFIG::SD_MISO_PIN, CONFIG::SD_MOSI_PIN);
    delay(200);

    pinMode(CONFIG::WIRE_RELAY_PIN, OUTPUT); digitalWrite(CONFIG::WIRE_RELAY_PIN, LOW);
    pinMode(CONFIG::FAN_RELAY_PIN, OUTPUT); digitalWrite(CONFIG::FAN_RELAY_PIN, LOW);

    // Inicjalizacja kolejek do komunikacji między zadaniami
    stateQueue = xQueueCreate(5, sizeof(MachineState));
    commandQueue = xQueueCreate(10, sizeof(WebserverCommand));

    Serial.println("Creating Control task...");
    xTaskCreatePinnedToCore(taskControl,  // Task function
                            "Control",  // Task name
                            CONFIG::CONTROLTASK_STACK_SIZE,  // Stack size
                            NULL,   // Parameters
                            CONFIG::CONTROLTASK_PRIORITY,      // Priority
                            NULL,   // Task handle
                            CONFIG::CORE_1 // Core
                            );

    delay(200);

    Serial.println("Creating CNC task...");
    xTaskCreatePinnedToCore(taskCNC, 
                            "CNC", 
                            CONFIG::CNCTASK_STACK_SIZE, 
                            NULL,
                            CONFIG::CNCTASK_PRIORITY,
                            NULL, 
                            CONFIG::CORE_0);
    delay(200);
}

void loop() {
    // Empty loop


}

/*-- MISCELLANEOUS FUNCTIONS --*/

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager) {
    
    FSManagerStatus fsManagerStatus {fsManager->init()};
    SDManagerStatus sdManagerStatus {sdManager->init()};
    WiFiManagerStatus wifiManagerStatus {wifiManager->init()};
    WebServerStatus webServerManagerStatus {webServerManager->init()};
    ConfigManagerStatus configManagerStatus {configManager->init()};

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
    int index = line.indexOf(param);
    if (index == -1) return NAN;
    
    // Wyznaczanie początku tesktu z parametrem
    int valueStart = index + 1;
    int valueEnd = static_cast<int>(line.length());
    
    // Wyznaczanie końca tekstu z parametrem (następna spacja lub koniec linii)
    for (int i = valueStart; i < static_cast<int>(line.length()); ++i) {
        if (line[i] == ' ' || line[i] == '\t') {
            valueEnd = i;
            break;
        }
    }

    return line.substring(valueStart, valueEnd).toFloat();
}

bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state, MachineConfig& config) {
    #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC GCODE: Processing: %s\n", line.c_str());
    #endif
    
    // Usuwanie białcyh znaków i pustych linii
    line.trim();
    if (line.length() == 0) return true;
    
    // Usuwanie komentarzy
    int commentIndex {line.indexOf(';')};
    if (commentIndex != -1) {
        line = line.substring(0, commentIndex);
        line.trim();
        if (line.length() == 0) return true;
    }
    
    // Wyodrębnienie polecenia
    int spaceIndex {line.indexOf(' ')};
    String command {(spaceIndex != -1) ? line.substring(0, spaceIndex) : line};
    command.toUpperCase();  // Ujednolicenie wielkości liter
    
    if (command.startsWith("G")) {
        int gCode {command.substring(1).toInt()};
        
        switch (gCode) {
            case 0:  // G0: Szybki ruch
            case 1:  // G1: Ruch liniowy
                {
                    float targetX {getParameter(line, 'X')};
                    float targetY {getParameter(line, 'Y')};
                    float feedRate {getParameter(line, 'F')};
                                
                    // Oblicz nowe pozycje
                    float newX {isnan(targetX) ? state.currentX : 
                                (state.relativeMode ? state.currentX + targetX : targetX)};
                    float newY {isnan(targetY) ? state.currentY : 
                                (state.relativeMode ? state.currentY + targetY : targetY)};
                    
                    // Oblicz prędkość ruchu w zależności od komendy ruchu
                    float moveSpeedX {(gCode == 0) ? config.xAxis.rapidFeedRate : 
                                     (config.useGCodeFeedRate && !isnan(feedRate)) ? 
                                     min(feedRate, config.xAxis.workFeedRate) : config.xAxis.workFeedRate};
                    float moveSpeedY {(gCode == 0) ? config.yAxis.rapidFeedRate : 
                                     (config.useGCodeFeedRate && !isnan(feedRate)) ? 
                                     min(feedRate, config.yAxis.workFeedRate) : config.yAxis.workFeedRate};
                    
                    // Oblicz ruch w krokach
                    long targetStepsX {static_cast<long>(newX * config.xAxis.stepsPerMM)};
                    long targetStepsY {static_cast<long>(newY * config.yAxis.stepsPerMM)};
                    
                    #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC MOVE: X%.3f Y%.3f Vx%.3f Vy%.3f \n", newX, newY, moveSpeedX, moveSpeedY);
                    #endif
                    
                    // Prędkości silników
                    float stepsXPerSec {moveSpeedX / 60.0f * config.xAxis.stepsPerMM}; // 60.0f - przelicznik na sekundy
                    float stepsYPerSec {moveSpeedY / 60.0f * config.yAxis.stepsPerMM};
                    stepperX.setMaxSpeed(stepsXPerSec);
                    stepperY.setMaxSpeed(stepsYPerSec);
                    
                    // Przyspieszenie - tymczasowo jako dwukrotna prędkość
                    stepperX.setAcceleration(config.xAxis.rapidAcceleration * config.xAxis.stepsPerMM);
                    stepperY.setAcceleration(config.yAxis.rapidAcceleration * config.yAxis.stepsPerMM);
                    
                    // Ustaw pozycje docelowe
                    stepperX.moveTo(targetStepsX);
                    stepperY.moveTo(targetStepsY);
                    
                    // Wykonanie ruchu bez blokowania
                    bool moving {true};
                    unsigned long lastProgressUpdate {millis()};
                    
                    while (moving) {
                        // Sprawdź, czy nie ma polecenia zatrzymania
                        WebserverCommand cmd;
                        if (xQueuePeek(commandQueue, &cmd, 0) == pdTRUE) {
                            if (cmd.type == CommandType::STOP) {
                                // Zatrzymaj ruch silników
                                stepperX.stop();
                                stepperY.stop();
                                
                                // Usuń komendę z kolejki
                                xQueueReceive(commandQueue, &cmd, 0);
                                return false; // Przerwij przetwarzanie
                            }
                        }
                        
                        // Uruchom silniki
                        stepperX.run();
                        stepperY.run();
                        
                        // Sprawdź czy ruch jest zakończony
                        if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                            moving = false;
                        }
                        
                        // Raportuj postęp co 100ms
                        if (millis() - lastProgressUpdate > 100) {
                            state.currentX = stepperX.currentPosition() / config.xAxis.stepsPerMM;
                            state.currentY = stepperY.currentPosition() / config.yAxis.stepsPerMM;
                            xQueueSend(stateQueue, &state, 0);
                            lastProgressUpdate = millis();
                        }
                        
                        vTaskDelay(10);
                    }
                    
                    // Zaktualizuj obecną pozycję
                    state.currentX = newX;
                    state.currentY = newY; 
                }
                break;
                
            case 28:  // G28: Bazowanie
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC HOME: Homing X and Y axes");
                #endif
                
                // Resetuj pozycję
                stepperX.setCurrentPosition(0);
                stepperY.setCurrentPosition(0);
                state.currentX = 0;
                state.currentY = 0;
                break;
                
            case 90:  // G90: Pozycjonowanie absolutne
                state.relativeMode = false;
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC MODE: Absolute positioning");
                #endif
                break;
                
            case 91:  // G91: Pozycjonowanie względne
                state.relativeMode = true;
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC MODE: Relative positioning");
                #endif
                break;
                
            default:
                #ifdef DEBUG_CNC_TASK
                    Serial.printf("WARNING: Unsupported G-code: G%d\n", gCode);
                #endif
                break;
        }
    } 
    else if (command.startsWith("M")) {
        int mCode = command.substring(1).toInt();
        
        switch (mCode) {
            case 0:  // M0: Pauza programu
            case 1:  // M1: Opcjonalna pauza programu
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC PAUSE: Program paused, waiting for resume");
                #endif
                state.isPaused = true;
                
                // Czekaj na wznowienie poprzez komendę z kolejki
                while (state.isPaused) {
                    // Sprawdź, czy nie ma polecenia wznowienia lub zatrzymania
                    WebserverCommand cmd;
                    if (xQueuePeek(commandQueue, &cmd, 0) == pdTRUE) {
                        if (cmd.type == CommandType::PAUSE) {
                            state.isPaused = false;
                            xQueueReceive(commandQueue, &cmd, 0); // Usuń komendę z kolejki
                        } else if (cmd.type == CommandType::STOP) {
                            xQueueReceive(commandQueue, &cmd, 0); // Usuń komendę z kolejki
                            return false; // Przerwij przetwarzanie
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;
                
            case 3:  // M3: Włączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC WIRE: Wire ON");
                #endif
                state.hotWireOn = true;
                updateOutputs(state);
                break;
                
            case 5:  // M5: Wyłączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC WIRE: Wire OFF");
                #endif
                state.hotWireOn = false;
                updateOutputs(state);
                break;
                
            default:
                #ifdef DEBUG_CNC_TASK
                    Serial.printf("WARNING: Unsupported M-code: M%d\n", mCode);
                #endif
                break;
        }
    }
    else {
        #ifdef DEBUG_CNC_TASK
            Serial.printf("WARNING: Unrecognized command: %s\n", command.c_str());
        #endif
        return false;
    }
    
    return true;
}

void updateOutputs(MachineState& state) {
    digitalWrite(CONFIG::WIRE_RELAY_PIN, state.hotWireOn ? HIGH : LOW);
    digitalWrite(CONFIG::FAN_RELAY_PIN, state.fanOn ? HIGH : LOW);
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

    // Pobranie konfiguracji
    MachineConfig config;
    if (configManager->isConfigLoaded()) {
        config = configManager->getConfig();
    } else {
        // Domyślne wartości jeśli konfiguracja nie jest dostępna
        config.xAxis.stepsPerMM = CONFIG::X_STEPS_PER_MM;
        config.yAxis.stepsPerMM = CONFIG::Y_STEPS_PER_MM;
        config.xAxis.rapidFeedRate = CONFIG::X_RAPID_FEEDRATE;
        config.xAxis.rapidAcceleration = CONFIG::X_RAPID_ACCELERATION;
        config.yAxis.rapidFeedRate = CONFIG::Y_RAPID_FEEDRATE;
        config.yAxis.rapidAcceleration = CONFIG::Y_RAPID_ACCELERATION;
        config.xAxis.workFeedRate = CONFIG::X_WORK_FEEDRATE;
        config.xAxis.workAcceleration = CONFIG::X_WORK_ACCELERATION;
        config.yAxis.workFeedRate = CONFIG::Y_WORK_FEEDRATE;
        config.yAxis.workAcceleration = CONFIG::Y_WORK_ACCELERATION;
        config.useGCodeFeedRate = CONFIG::USE_GCODE_FEEDRATE;
        config.deactivateESTOP = CONFIG::DEACTIVATE_ESTOP;
        config.deactivateLimitSwitches = CONFIG::DEACTIVATE_LIMIT_SWITCHES;
        config.limitSwitchType = CONFIG::LIMIT_SWITCH_TYPE;
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
        if (!processGCodeLine(line, stepperX, stepperY, state, config)) {
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