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

/*
* ------------------------------------------------------------------------------------------------------------
* --- GLOBAL SCOPE ---
* ------------------------------------------------------------------------------------------------------------
*/

// Instancje menadżerów systemu wykorzystywane w obu Taskach
SDCardManager* sdManager { new SDCardManager() }; // Do zarządzania kartą SD
ConfigManager* configManager { new ConfigManager(sdManager) }; // Do zarządzania konfiguracją (parametrami) - odczyt/zapis z/do pliku na karcie SD

// Kolejki do komunikacji między zadaniami
QueueHandle_t stateQueue {};   // Od CNC do Control (informacje o stanie)
QueueHandle_t commandQueue {}; // Od Control do CNC (komendy sterujące)

/*
* ------------------------------------------------------------------------------------------------------------
* --- FUNCTION PROTOTYPES ---
* ------------------------------------------------------------------------------------------------------------
*/

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager);
void connectToWiFi(WiFiManager* wifiManager);
void startWebServer(WebServerManager* webServerManager);

void updateIO(MachineState& cncState, const MachineConfig& config);
bool loadConfig(MachineConfig& config);

float getParameter(const String& line, char param);
bool updateMotorSpeed(const char axis, const bool useRapid, AccelStepper& stepper, const MachineConfig& config);
bool updateMotorSpeed(const char axis, const float feedRate, const float accelMultiplier, AccelStepper& stepper, const MachineConfig& config);
bool initializeGCodeProcessing(MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config);
void processGCodeStateMachine(MachineState& cncState, GCodeProcessingState& gCodeState, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config);
bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config);

void taskCNC(void* parameter);
void taskControl(void* parameter);

/*
* ------------------------------------------------------------------------------------------------------------
* --- Main Controller Program ---
* ------------------------------------------------------------------------------------------------------------
*/

void setup() {
    // ============================================================================
    // Port szeregowy do debugowania
    Serial.begin(115200);

    // SPI dla karty SD
    SPI.begin(PINCONFIG::SD_CLK_PIN, PINCONFIG::SD_MISO_PIN, PINCONFIG::SD_MOSI_PIN);

    // Odczekanie chwili na poprawne uruchomienie protokołu SPI
    delay(200);

    // ============================================================================
    // I/O
    pinMode(PINCONFIG::LIMIT_X_PIN, INPUT_PULLUP);
    pinMode(PINCONFIG::LIMIT_Y_PIN, INPUT_PULLUP);
    pinMode(PINCONFIG::ESTOP_PIN, INPUT_PULLUP);

    pinMode(PINCONFIG::WIRE_RELAY_PIN, OUTPUT);
    digitalWrite(PINCONFIG::WIRE_RELAY_PIN, LOW);

    pinMode(PINCONFIG::FAN_RELAY_PIN, OUTPUT);
    digitalWrite(PINCONFIG::FAN_RELAY_PIN, LOW);

    // Konfiguracja PWM
    ledcSetup(PINCONFIG::WIRE_PWM_CHANNEL, PINCONFIG::PWM_FREQ, PINCONFIG::PWM_RESOLUTION);
    ledcAttachPin(PINCONFIG::WIRE_PWM_PIN, PINCONFIG::WIRE_PWM_CHANNEL);
    ledcWrite(PINCONFIG::WIRE_PWM_CHANNEL, 0);

    ledcSetup(PINCONFIG::FAN_PWM_CHANNEL, PINCONFIG::PWM_FREQ, PINCONFIG::PWM_RESOLUTION);
    ledcAttachPin(PINCONFIG::FAN_PWM_PIN, PINCONFIG::FAN_PWM_CHANNEL);
    ledcWrite(PINCONFIG::FAN_PWM_CHANNEL, 0);

    // ============================================================================
    // Inicjalizacja kolejek do komunikacji między zadaniami
    stateQueue = xQueueCreate(1, sizeof(MachineState));
    commandQueue = xQueueCreate(3, sizeof(WebserverCommand));

    // Stworzenie zadań
    Serial.println("Creating Control task...");
    xTaskCreatePinnedToCore(taskControl,    // Task function
        "Control",                          // Task name
        CONFIG::CONTROLTASK_STACK_SIZE,     // Stack size
        NULL,                               // Parameters
        CONFIG::CONTROLTASK_PRIORITY,       // Priority
        NULL,                               // Task handle
        CONFIG::CORE_1                      // Core
    );

    delay(200); // Odczekanie chwili na poprawne uruchomienie pierwszego zadania

    Serial.println("Creating CNC task...");
    xTaskCreatePinnedToCore(taskCNC,        // Task function
        "CNC",                              // Task name
        CONFIG::CNCTASK_STACK_SIZE,         // Stack size
        NULL,                               // Parameters
        CONFIG::CNCTASK_PRIORITY,           // Priority
        NULL,                               // Task handle
        CONFIG::CORE_0                      // Core
    );

    delay(200); // Odczekanie chwili na poprawne uruchomienie drugiego zadania
}

void loop() {
    // Brak potrzeby implementacji pętli głównej
}


/*
* ------------------------------------------------------------------------------------------------------------
* --- Tasks ---
* ------------------------------------------------------------------------------------------------------------
*/

// Zadanie obsługi ruchu CNC - wykonywanie poleceń i kontrola fizycznych wejść/wyjść
void taskCNC(void* parameter) {
    #ifdef DEBUG_CNC_TASK
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());
    #endif

    // Stany maszyny
    MachineState cncState {};
    GCodeProcessingState gCodeState {};

    // Komendy odbierane z interfejsu webowego
    WebserverCommand commandData {};
    bool commandPending { false }; // Flaga do sprawdzania, czy komenda została odebrana

    // Komunikacja między zadaniami
    TickType_t lastCommandProcessTime { 0 };
    TickType_t lastStatusUpdateTime { 0 };
    const TickType_t commandProcessInterval { pdMS_TO_TICKS(500) };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(100) };

    // Silniki krokowe
    AccelStepper stepperX(AccelStepper::DRIVER, PINCONFIG::STEP_X_PIN, PINCONFIG::DIR_X_PIN);
    AccelStepper stepperY(AccelStepper::DRIVER, PINCONFIG::STEP_Y_PIN, PINCONFIG::DIR_Y_PIN);

    // Parametry (konfiguracja) maszyny
    MachineConfig config {};
    if (!loadConfig(config)) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("CNC ERROR: Nie można załadować konfiguracji maszyny, wczytanie wartości domyślnych.");
        #endif

        config.X.stepsPerMM = DEFAULTS::X_STEPS_PER_MM;
        config.X.rapidFeedRate = DEFAULTS::X_RAPID_FEEDRATE;
        config.X.rapidAcceleration = DEFAULTS::X_RAPID_ACCELERATION;
        config.X.workFeedRate = DEFAULTS::X_WORK_FEEDRATE;
        config.X.workAcceleration = DEFAULTS::X_WORK_ACCELERATION;
        config.X.offset = DEFAULTS::X_OFFSET;

        config.Y.stepsPerMM = DEFAULTS::Y_STEPS_PER_MM;
        config.Y.rapidFeedRate = DEFAULTS::Y_RAPID_FEEDRATE;
        config.Y.rapidAcceleration = DEFAULTS::Y_RAPID_ACCELERATION;
        config.Y.workFeedRate = DEFAULTS::Y_WORK_FEEDRATE;
        config.Y.workAcceleration = DEFAULTS::Y_WORK_ACCELERATION;
        config.Y.offset = DEFAULTS::Y_OFFSET;

        config.useGCodeFeedRate = DEFAULTS::USE_GCODE_FEEDRATE;
        config.delayAfterStartup = DEFAULTS::DELAY_AFTER_STARTUP;
        config.deactivateESTOP = DEFAULTS::DEACTIVATE_ESTOP;
        config.deactivateLimitSwitches = DEFAULTS::DEACTIVATE_LIMIT_SWITCHES;
        config.limitSwitchType = DEFAULTS::LIMIT_SWITCH_TYPE;
    };

    updateMotorSpeed('X', true, stepperX, config);
    updateMotorSpeed('Y', true, stepperY, config);
    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);

    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };

        // Odbieranie komend z kolejki
        if ((currentTime - lastCommandProcessTime) >= commandProcessInterval) {
            if (xQueueReceive(commandQueue, &commandData, 0) == pdTRUE) {
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: Otrzymano komendę typu %d\n", static_cast<int>(commandData.type));
                #endif
                commandPending = true;
            }
            lastCommandProcessTime = currentTime;
        }

        // Wysłasnie statusu maszyny
        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {

            if (xQueueOverwrite(stateQueue, &cncState) != pdPASS) {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Nie można wysłać statusu maszyny.");
                #endif
            }

            lastStatusUpdateTime = currentTime;
        }


        switch (cncState.state) {
            case CNCState::IDLE:
                // W stanie IDLE nic nie robimy, czekamy na komendy
                if(commandPending) {
                    commandPending = false;
                    switch (commandData.type) {
                        case CommandType::START:
                            // Rozpoczęcie przetwarzania G-code
                            if (initializeGCodeProcessing(cncState, gCodeState, config)) {
                                cncState.state = CNCState::RUNNING;
                            }
                            break;

                        case CommandType::HOME:
                            // Rozpoczęcie bazowania
                            cncState.state = CNCState::HOMING;
                            break;

                        case CommandType::JOG:
                            // Rozpoczęcie ruchu ręcznego
                            cncState.state = CNCState::JOG;
                            break;

                        default:
                            break;
                    }
                }
                break;

            case CNCState::RUNNING:
                // Przetwarzanie G-code
                if (commandPending) {
                    commandPending = false;
                    switch (commandData.type) {
                        case CommandType::PAUSE:
                            gCodeState.pauseRequested = !gCodeState.pauseRequested;
                            break;
                        case CommandType::STOP:
                            gCodeState.stopRequested = true;
                            break;
                        default:
                            break;
                    }
                }

                if (gCodeState.stopRequested) {
                    // Zatrzymaj przetwarzanie G-code
                    if (gCodeState.fileOpen && gCodeState.currentFile) {
                        gCodeState.currentFile.close();
                        gCodeState.fileOpen = false;
                    }
                    cncState.state = CNCState::STOPPED;
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Przetwarzanie wstrzymane");
                    #endif
                    break;
                }

                if (gCodeState.pauseRequested) {
                    cncState.isPaused = true;
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Przetwarzanie wznowione");
                    #endif
                    break;
                } else {
                    cncState.isPaused = false;
                }
            
                if (!cncState.isPaused) {
                    processGCodeStateMachine(cncState, gCodeState, stepperX, stepperY, config);
            
                    // Aktualizacja postępu
                    cncState.currentLine = gCodeState.lineNumber;
                    cncState.jobProgress = gCodeState.totalLines > 0
                        ? (100.0f * gCodeState.lineNumber / gCodeState.totalLines)
                        : 0.0f;
                    cncState.jobRunTime = millis() - cncState.jobStartTime;
            
                    // Sprawdź czy zakończono plik
                    if (gCodeState.stage == GCodeProcessingState::ProcessingStage::FINISHED) {
                        if (gCodeState.fileOpen && gCodeState.currentFile) {
                            gCodeState.currentFile.close();
                            gCodeState.fileOpen = false;
                        }
                        cncState.state = CNCState::IDLE;
                        #ifdef DEBUG_CNC_TASK
                        Serial.println("DEBUG CNC: Przetwarzanie pliku zakończone");
                        #endif
                    }
                    // Obsługa błędu
                    if (gCodeState.stage == GCodeProcessingState::ProcessingStage::ERROR) {
                        cncState.state = CNCState::ERROR;
                        #ifdef DEBUG_CNC_TASK
                        Serial.printf("CNC ERROR: %s\n", gCodeState.errorMessage.c_str());
                        #endif
                    }
                }

                break;

            case CNCState::JOG:
                // Wykonanie jednego kroku JOG
                break;

            case CNCState::HOMING:
                // Obsługa bazowania
                break;

            case CNCState::STOPPED:
            case CNCState::ERROR:
                break;
        }

        updateIO(cncState, config);

        vTaskDelay(pdMS_TO_TICKS(10));


    }
}

// Zadanie kontroli i sterowania maszyną - obsługa interfejsu webowego
void taskControl(void* parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager, configManager, commandQueue, stateQueue);

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager);
    connectToWiFi(wifiManager);
    startWebServer(webServerManager);

    MachineState receivedState {};

    TickType_t lastStatusUpdateTime { 0 };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(20000) };

    while (true) {

        TickType_t currentTime { xTaskGetTickCount() };

        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {

            // Odbieranie statusu maszyny z kolejki
            if (xQueuePeek(stateQueue, &receivedState, 0) == pdTRUE) {
                #ifdef DEBUG_CONTROL_TASK
                Serial.printf(
                    "DEBUG CONTROL: Otrzymano status maszyny: {"
                    "\"state\":%d,"
                    "\"isPaused\":%s,"
                    "\"errorID\":%d,"
                    "\"currentX\":%.2f,"
                    "\"currentY\":%.2f,"
                    "\"relativeMode\":%s,"
                    "\"hotWireOn\":%s,"
                    "\"fanOn\":%s,"
                    "\"hotWirePower\":%d,"
                    "\"fanPower\":%d,"
                    "\"currentProject\":\"%s\","
                    "\"jobProgress\":%.2f,"
                    "\"currentLine\":%lu,"
                    "\"totalLines\":%lu,"
                    "\"jobStartTime\":%lu,"
                    "\"jobRunTime\":%lu,"
                    "\"estopOn\":%s,"
                    "\"limitXOn\":%s,"
                    "\"limitYOn\":%s"
                    "}\n",
                    static_cast<int>(receivedState.state),
                    receivedState.isPaused ? "true" : "false",
                    receivedState.errorID,
                    receivedState.currentX,
                    receivedState.currentY,
                    receivedState.relativeMode ? "true" : "false",
                    receivedState.hotWireOn ? "true" : "false",
                    receivedState.fanOn ? "true" : "false",
                    receivedState.hotWirePower,
                    receivedState.fanPower,
                    receivedState.currentProject,
                    receivedState.jobProgress,
                    receivedState.currentLine,
                    receivedState.totalLines,
                    receivedState.jobStartTime,
                    receivedState.jobRunTime,
                    receivedState.estopOn ? "true" : "false",
                    receivedState.limitXOn ? "true" : "false",
                    receivedState.limitYOn ? "true" : "false"
                );
                #endif
            }
            else {
                #ifdef DEBUG_CONTROL_TASK
                Serial.println("DEBUG CONTROL: Brak statusu maszyny.");
                #endif
            }
            lastStatusUpdateTime = currentTime;
        }
        // Krótkie opóźnienie dla oszczędzania energii i zapobiegania watchdog timeouts
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- MISCELLANEOUS FUNCTIONS ---
* ------------------------------------------------------------------------------------------------------------
*/

// SYSYTEM

// Inicjalizuje wszystkie obiekty zarządzające systemem
void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager) {

    FSManagerStatus fsManagerStatus { fsManager->init() };
    SDManagerStatus sdManagerStatus { sdManager->init() };
    WiFiManagerStatus wifiManagerStatus { wifiManager->init() };
    WebServerStatus webServerManagerStatus { webServerManager->init() };
    ConfigManagerStatus configManagerStatus { configManager->init() };

    #ifdef DEBUG_CONTROL_TASK
    switch (fsManagerStatus) {
        case FSManagerStatus::OK:
            Serial.println("STATUS: FS Manager initialized successfully.");
            break;
        case FSManagerStatus::MOUNT_FAILED:
            Serial.println("ERROR: FS Manager initialization failed.");
            break;
        default:
            Serial.println("ERROR: FS Manager unknown error.");
            break;
    }

    switch (sdManagerStatus) {
        case SDManagerStatus::OK:
            Serial.println("STATUS: SD Manager initialized successfully.");
            break;
        case SDManagerStatus::INIT_FAILED:
            Serial.println("ERROR: SD Manager initialization failed.");
            break;
        case SDManagerStatus::DIRECTORY_CREATE_FAILED:
            Serial.println("ERROR: SD Manager directory creation failed.");
            break;
        case SDManagerStatus::DIRECTORY_OPEN_FAILED:
            Serial.println("ERROR: SD Manager directory open failed.");
            break;
        case SDManagerStatus::MUTEX_CREATE_FAILED:
            Serial.println("ERROR: SD Manager mutex creation failed.");
            break;
        case SDManagerStatus::FILE_OPEN_FAILED:
            Serial.println("ERROR: SD Manager file open failed.");
            break;
        case SDManagerStatus::CARD_NOT_INITIALIZED:
            Serial.println("ERROR: SD Manager card not initialized.");
            break;
        default:
            Serial.println("ERROR: SD Manager unknown error.");
            break;
    }

    switch (wifiManagerStatus) {
        case WiFiManagerStatus::OK:
            Serial.println("STATUS: WiFi Manager initialized successfully.");
            break;
        case WiFiManagerStatus::STA_MODE_FAILED:
            Serial.println("ERROR: WiFi Manager STA mode failed.");
            break;
        case WiFiManagerStatus::WIFI_NO_CONNECTION:
            Serial.println("ERROR: WiFi Manager no WiFi connection.");
            break;
        default:
            Serial.println("ERROR: WiFi Manager unknown error.");
            break;
    }

    switch (webServerManagerStatus) {
        case WebServerStatus::OK:
            Serial.println("STATUS: Web Server Manager initialized successfully.");
            break;
        case WebServerStatus::ALREADY_INITIALIZED:
            Serial.println("ERROR: Web Server Manager already initialized.");
            break;
        case WebServerStatus::SERVER_ALLOCATION_FAILED:
            Serial.println("ERROR: Web Server Manager server allocation failed.");
            break;
        case WebServerStatus::EVENT_SOURCE_FAILED:
            Serial.println("ERROR: Web Server Manager event source failed.");
            break;
        case WebServerStatus::UNKNOWN_ERROR:
            Serial.println("ERROR: Web Server Manager unknown error.");
            break;
        default:
            Serial.println("ERROR: Web Server Manager unknown error.");
            break;
    }

    switch (configManagerStatus) {
        case ConfigManagerStatus::OK:
            Serial.println("STATUS: Config Manager initialized successfully.");
            break;
        case ConfigManagerStatus::FILE_OPEN_FAILED:
            Serial.println("WARNING: Config file not found, using defaults.");
            break;
        case ConfigManagerStatus::FILE_WRITE_FAILED:
            Serial.println("ERROR: Config Manager file write failed.");
            break;
        case ConfigManagerStatus::JSON_PARSE_ERROR:
            Serial.println("ERROR: Config Manager JSON parse error.");
            break;
        case ConfigManagerStatus::JSON_SERIALIZE_ERROR:
            Serial.println("ERROR: Config Manager JSON serialize error.");
            break;
        case ConfigManagerStatus::SD_ACCESS_ERROR:
            Serial.println("ERROR: Config Manager SD access error.");
            break;
        default:
            Serial.println("ERROR: Config Manager unknown error.");
            break;
    }
    #endif
}

// Nawiązuje połączenie WiFi wykorzystując dane z credentials.h
void connectToWiFi(WiFiManager* wifiManager) {
    WiFiManagerStatus wifiManagerStatus { wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, CONFIG::MAX_CONNECTION_TIME) };
    if (wifiManagerStatus == WiFiManagerStatus::OK) {
        Serial.println("STATUS: Connected to WiFi.");
    }
    else {
        Serial.println("ERROR: Failed to connect to WiFi.");
    }
}

// Uruchamia serwer WWW do sterowania i monitorowania maszyny
void startWebServer(WebServerManager* webServerManager) {
    WebServerStatus webServerStatus = webServerManager->begin();
    if (webServerStatus == WebServerStatus::OK) {
        Serial.println("STATUS: Web server started.");
    }
    else {
        Serial.println("ERROR: Web server failed to start.");
    }
}

// IO

// Aktualizuje stan wyjść fizycznych na podstawie stanu maszyny
void updateIO(MachineState& cncSate, const MachineConfig& config) {

    digitalWrite(PINCONFIG::WIRE_RELAY_PIN, cncSate.hotWireOn ? HIGH : LOW);
    digitalWrite(PINCONFIG::FAN_RELAY_PIN, cncSate.fanOn ? HIGH : LOW);
    ledcWrite(PINCONFIG::WIRE_PWM_CHANNEL, cncSate.hotWirePower);
    ledcWrite(PINCONFIG::FAN_PWM_CHANNEL, cncSate.fanPower);

    if (config.deactivateLimitSwitches) {
        cncSate.limitXOn = false;
        cncSate.limitYOn = false;
    }
    else {
        cncSate.limitXOn = digitalRead(PINCONFIG::LIMIT_X_PIN);
        cncSate.limitYOn = digitalRead(PINCONFIG::LIMIT_Y_PIN);
    }

    if (config.deactivateESTOP) {
        cncSate.estopOn = false;
    }
    else {
        cncSate.estopOn = digitalRead(PINCONFIG::ESTOP_PIN);
    }

}

// Wczytane parametrów z pliku konfiguracyjnego
bool loadConfig(MachineConfig& config) {

    // TODO: Dodać obsługę błędów i wrzytywanie domyślnych wartości 
    config = configManager->getConfig();
    #ifdef DEBUG_CNC_TASK
    Serial.println("DEBUG CNC: Konfiguacja załadowana");
    #endif
    return true;

    // // Domyślne wartości jeśli konfiguracja nie jest dostępna
    // config.xAxis.stepsPerMM = CONFIG::X_STEPS_PER_MM;
    // config.xAxis.rapidAcceleration = CONFIG::X_RAPID_ACCELERATION;
    // config.xAxis.rapidFeedRate = CONFIG::X_RAPID_FEEDRATE;
    // config.xAxis.workFeedRate = CONFIG::X_WORK_FEEDRATE;
    // config.xAxis.workAcceleration = CONFIG::X_WORK_ACCELERATION;

    // config.yAxis.stepsPerMM = CONFIG::Y_STEPS_PER_MM;
    // config.yAxis.rapidFeedRate = CONFIG::Y_RAPID_FEEDRATE;
    // config.yAxis.rapidAcceleration = CONFIG::Y_RAPID_ACCELERATION;
    // config.yAxis.workFeedRate = CONFIG::Y_WORK_FEEDRATE;
    // config.yAxis.workAcceleration = CONFIG::Y_WORK_ACCELERATION;

    // config.offsetX = CONFIG::X_OFFSET;
    // config.offsetY = CONFIG::Y_OFFSET;

    // config.useGCodeFeedRate = CONFIG::USE_GCODE_FEEDRATE;
    // config.deactivateESTOP = CONFIG::DEACTIVATE_ESTOP;
    // config.deactivateLimitSwitches = CONFIG::DEACTIVATE_LIMIT_SWITCHES;
    // config.limitSwitchType = CONFIG::LIMIT_SWITCH_TYPE;
    // config.delayAfterStartup = CONFIG::DELAY_AFTER_STARTUP;

}

/**
 * Aktualizacja dynamiki silnika krokowego (dane z konfiguracji)
 * @param axis 0 = X, 1 = Y
 * @param useRapid true = rapid (G0), false = work (G1)
 */
bool updateMotorSpeed(const char axis, const bool useRapid, AccelStepper& stepper, const MachineConfig& config) {
    float stepsPerMM {};
    float feedRate {};
    float acceleration {};
    switch (axis) {
        case 'X':
            stepsPerMM = config.X.stepsPerMM;
            feedRate = useRapid ? config.X.rapidFeedRate : config.X.workFeedRate;
            acceleration = useRapid ? config.X.rapidAcceleration : config.X.workAcceleration;
            break;
        case 'Y':
            stepsPerMM = config.Y.stepsPerMM;
            feedRate = useRapid ? config.Y.rapidFeedRate : config.Y.workFeedRate;
            acceleration = useRapid ? config.Y.rapidAcceleration : config.Y.workAcceleration;
            break;
        default:
            return false; // Nieprawidłowa oś
    }

    stepper.setMaxSpeed(feedRate / 60.0f * stepsPerMM); // Konwersja z mm/min na steps/s
    stepper.setAcceleration(acceleration * stepsPerMM);

    return true;
}

/**
 * Aktualizacja dynamiki silnika krokowego (dane z pliku)
 * @param axis 'X' lub 'Y'
 * @param feedRate Prędkośc w steps/s
 * @param accelMultiplier Acceleration multiplier -> accel = feed * multiplier
 */
bool updateMotorSpeed(const char axis, const float feedRate, const float accelMultiplier, AccelStepper& stepper, const MachineConfig& config) {
    float stepsPerMM {};
    switch (axis) {
        case 'X':
            stepsPerMM = config.X.stepsPerMM;
            break;
        case 'Y':
            stepsPerMM = config.Y.stepsPerMM;
            break;
        default:
            return false; // Nieprawidłowa oś
    }

    stepper.setMaxSpeed(feedRate / 60.0f * stepsPerMM);
    stepper.setAcceleration(feedRate * accelMultiplier * stepsPerMM);

    return true;
}

bool initializeGCodeProcessing(MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config) {
    // Pobierz nazwę wybranego projektu z SDManagera
    std::string filename {};
    sdManager->getSelectedProject(filename);

    if (filename.empty()) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC ERROR: Brak wybranego pliku projektu.");
        #endif
        return false;
    }

    // Zamknij poprzedni plik, jeśli był otwarty
    if (gCodeState.fileOpen && gCodeState.currentFile) {
        gCodeState.currentFile.close();
        gCodeState.fileOpen = false;
    }

    // Resetuj flagi i stan przetwarzania
    gCodeState.lineNumber = 0;
    gCodeState.stopRequested = false;
    gCodeState.pauseRequested = false;
    gCodeState.stage = GCodeProcessingState::ProcessingStage::INITIALIZING;
    gCodeState.movementInProgress = false;
    gCodeState.errorMessage = "";
    gCodeState.currentLine = "";
    gCodeState.heatingStartTime = 0;
    gCodeState.heatingDuration = config.delayAfterStartup;

    // Inicjalizacja pliku
    if (!sdManager->takeSD()) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC ERROR: Nie udało się zablokować karty SD");
        #endif
        return false;
    }

    std::string filePath = CONFIG::PROJECTS_DIR + filename;
    gCodeState.currentFile = SD.open(filePath.c_str());
    if (!gCodeState.currentFile) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC ERROR: Nie udało się otworzyć pliku %s\n", filePath.c_str());
        #endif
        sdManager->giveSD();
        return false;
    }

    // Oszaconowano 24 bajtów na linię
    // plik 139 648 bajtów ma 5727 linii -> 139648 / 5727 = 24.4
    // plik 289 824 bajtów ma 11854 linii -> 289824 / 11854 = 24.4
    // Uproszczono ze względu na niepotrzebne komplikowanie obliczeń
    gCodeState.totalLines = gCodeState.currentFile.size() / 24;

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: Otwarto plik %s, liczba linii: %d\n",
        filePath.c_str(), gCodeState.totalLines);
    #endif

    gCodeState.fileOpen = true;
    sdManager->giveSD();

    // Inicjalizacja stanu maszyny
    strncpy(cncState.currentProject, filename.c_str(), sizeof(cncState.currentProject) - 1);
    cncState.currentProject[sizeof(cncState.currentProject) - 1] = '\0';
    cncState.jobStartTime = millis();
    cncState.jobProgress = 0.0f;
    cncState.currentLine = 0;
    cncState.totalLines = gCodeState.totalLines;

    return true;
}


