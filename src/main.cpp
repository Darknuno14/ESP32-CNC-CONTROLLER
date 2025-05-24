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
SDCardManager* sdManager {}; // Do zarządzania kartą SD
ConfigManager* configManager {}; // Do zarządzania konfiguracją (parametrami) - odczyt/zapis z/do pliku na karcie SD

// Kolejki do komunikacji między zadaniami
QueueHandle_t stateQueue {};   // Od CNC do Control (informacje o stanie)
QueueHandle_t commandQueue {}; // Od Control do CNC (komendy sterujące)

bool systemInitialized { false }; // Flaga do sprawdzania, czy system został zainicjalizowany

/*
* ------------------------------------------------------------------------------------------------------------
* --- FUNCTION PROTOTYPES ---
* ------------------------------------------------------------------------------------------------------------
*/

bool initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager);
bool connectToWiFi(WiFiManager* wifiManager);
bool startWebServer(WebServerManager* webServerManager);

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

    sdManager = new SDCardManager();
    configManager = new ConfigManager(sdManager);

    // Inicjalizacja kolejek do komunikacji między zadaniami
    stateQueue = xQueueCreate(1, sizeof(MachineState));
    commandQueue = xQueueCreate(3, sizeof(WebserverCommand));

    if (!stateQueue) {
        Serial.println("SYSTEM ERROR: stateQueue not created!");
    }
    if (!commandQueue) {
        Serial.println("SYSTEM ERROR: commandQueue not created!");
    }

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

    while (!systemInitialized) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Oczekiwanie na inicjalizację systemu");
        #endif
        vTaskDelay(pdMS_TO_TICKS(1000)); // Odczekaj chwilę przed ponowną próbą
    }

    // Parametry (konfiguracja) maszyny
    MachineConfig config {};
    ConfigManagerStatus configStatus {};
    do {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Próba wczytania konfiguracji...");
        #endif
        if (configManager != nullptr) {
            configStatus = configManager->getConfig(config);
        }
        else {
            #ifdef DEBUG_CNC_TASK
            Serial.println("ERROR CNC: Nie wczytano managera konfiguracji.");
            #endif
            configStatus = ConfigManagerStatus::MANAGER_NOT_INITIALIZED;
        }
    } while (configStatus != ConfigManagerStatus::OK);

    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);

    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };

        stepperX.run();
        stepperY.run();

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

        if (commandPending && commandData.type == CommandType::RELOAD_CONFIG) {
            // Przeładuj konfigurację
            ConfigManagerStatus reloadStatus = configManager->getConfig(config);
            if (reloadStatus == ConfigManagerStatus::OK) {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Konfiguracja przeładowana pomyślnie.");
                #endif
            }
            else {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Błąd podczas przeładowania konfiguracji.");
                #endif
            }
            commandPending = false;
        }


        switch (cncState.state) {
            case CNCState::IDLE:
                // W stanie IDLE nic nie robimy, czekamy na komendy
                if (commandPending) {
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
                }
                else {
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

    bool managersInitialized { initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager) };
    bool connectedToWifi { connectToWiFi(wifiManager) };
    bool startedWebServer { startWebServer(webServerManager) };

    systemInitialized = managersInitialized && connectedToWifi && startedWebServer;

    if(!systemInitialized) {
        ESP.restart(); // Restart systemu, jeśli inicjalizacja nie powiodła się
    }
    MachineState receivedState {};
    TickType_t lastStatusUpdateTime { 0 };
    TickType_t lastDebugTime { 0 };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(500) };
    const TickType_t debugUpdateInterval { pdMS_TO_TICKS(1000) };

    while (true) {

        TickType_t currentTime { xTaskGetTickCount() };

        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {
            if (!webServerManager->isBusy()) {
                webServerManager->broadcastMachineStatus();
            }
            lastStatusUpdateTime = currentTime;
        }

        // W taskControl, co jakiś czas:
        if ((currentTime - lastDebugTime) >= debugUpdateInterval) {
            // Serial.printf("Free heap: %d bytes, Min free: %d bytes\n",
            //     ESP.getFreeHeap(), ESP.getMinFreeHeap());
            lastDebugTime = currentTime;
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
bool initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager) {

    if (fsManager != nullptr) {
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji FSManager...\n");
        FSManagerStatus fsManagerStatus { fsManager->init() };
        if (fsManagerStatus != FSManagerStatus::OK) {
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować FSManager: %d\n", static_cast<int>(fsManagerStatus));
            return false;
        }
    }
    else {
        Serial.printf("SYSTEM ERROR: FSManager jest pustym wskaźnikiem\n");
        return false;
    }

    if (sdManager != nullptr) {
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji SDCardManager...\n");
        SDManagerStatus sdManagerStatus { sdManager->init() };
        if (sdManagerStatus != SDManagerStatus::OK) {
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować SDCardManager: %d\n", static_cast<int>(sdManagerStatus));
            return false;
        }
    }
    else {
        Serial.printf("SYSTEM ERROR: SDCardManager jest pustym wskaźnikiem\n");
        return false;
    }

    if (configManager != nullptr) {
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji ConfigManager...\n");
        ConfigManagerStatus configManagerStatus { configManager->init() };
        if (configManagerStatus != ConfigManagerStatus::OK) {
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować ConfigManager: %d\n", static_cast<int>(configManagerStatus));
            return false;
        }
    }
    else {
        Serial.printf("SYSTEM ERROR: ConfigManager jest pustym wskaźnikiem\n");
        return false;
    }

    if (wifiManager != nullptr) {
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji WiFiManager...\n");
        WiFiManagerStatus wifiManagerStatus { wifiManager->init() };
        if (wifiManagerStatus != WiFiManagerStatus::OK) {
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować WiFiManager: %d\n", static_cast<int>(wifiManagerStatus));
            return false;
        }
    }
    else {
        Serial.printf("SYSTEM ERROR: WiFiManager jest pustym wskaźnikiem\n");
        return false;
    }

    if (webServerManager != nullptr) {
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji WebServerManager...\n");
        WebServerStatus webServerStatus { webServerManager->init() };
        if (webServerStatus != WebServerStatus::OK) {
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować WebServerManager: %d\n", static_cast<int>(webServerStatus));
            return false;
        }
    }
    else {
        Serial.printf("SYSTEM ERROR: WebServerManager jest pustym wskaźnikiem\n");
        return false;
    }
    return true;
}

// Nawiązuje połączenie WiFi wykorzystując dane z credentials.h
bool connectToWiFi(WiFiManager* wifiManager) {
    WiFiManagerStatus wifiManagerStatus { wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, CONFIG::MAX_CONNECTION_TIME) };
    if (wifiManagerStatus == WiFiManagerStatus::OK) {
        Serial.println("STATUS: Connected to WiFi.");
        return true;
    }
    else {
        Serial.println("ERROR: Failed to connect to WiFi.");
        return false;
    }
}

// Uruchamia serwer WWW do sterowania i monitorowania maszyny
bool startWebServer(WebServerManager* webServerManager) {
    WebServerStatus webServerStatus = webServerManager->begin();
    if (webServerStatus == WebServerStatus::OK) {
        Serial.println("STATUS: Web server started.");
        return true;
    }
    else {
        Serial.println("ERROR: Web server failed to start.");
        return false;
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

float getParameter(const String& line, char param) {
    int index = line.indexOf(param);
    if (index == -1) return NAN;

    int valueStart = index + 1;
    int valueEnd = line.length();

    // Szukaj końca liczby (spacja, tab, koniec linii)
    for (int i = valueStart; i < line.length(); ++i) {
        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n') {
            valueEnd = i;
            break;
        }
    }

    String valueStr = line.substring(valueStart, valueEnd);
    valueStr.trim(); // Usuwa ewentualne białe znaki

    return valueStr.toFloat();
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

void processGCodeStateMachine(MachineState& cncState, GCodeProcessingState& gCodeState, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {
    // Jeśli plik nie jest otwarty lub już skończony, nic nie rób
    if (!gCodeState.fileOpen || !gCodeState.currentFile) {
        gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
        return;
    }

    // Sprawdź czy poprzedni ruch się skończył
    if (gCodeState.movementInProgress) {
        if (stepperX.isRunning() || stepperY.isRunning()) {
            return; // Czekaj na zakończenie ruchu
        }
        gCodeState.movementInProgress = false;
    }

    // Pobierz SD tylko gdy trzeba
    if (!sdManager->takeSD()) {
        return; // Spróbuj ponownie w następnym cyklu
    }

    // Czytaj jedną linię na wywołanie
    String line = gCodeState.currentFile.readStringUntil('\n');
    sdManager->giveSD(); // Natychmiast zwolnij
    if (line.length() == 0) {
        gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
        cncState.hotWireOn = false;
        cncState.fanOn = false;
        return;
    }

    // Parsuj i wykonaj ruch
    float xPos = getParameter(line, 'X');
    float yPos = getParameter(line, 'Y');

    if (!isnan(xPos) || !isnan(yPos)) {
        // Ustaw target positions
        if (!isnan(xPos)) {
            stepperX.moveTo(xPos * config.X.stepsPerMM);
        }
        if (!isnan(yPos)) {
            stepperY.moveTo(yPos * config.Y.stepsPerMM);
        }
        gCodeState.movementInProgress = true;
    }

    cncState.hotWireOn = true;
    cncState.fanOn = true;
    gCodeState.lineNumber++;

    vTaskDelay(pdMS_TO_TICKS(50));

    // Wypisz linię na Serial (testowo)
    Serial.printf("GCODE LINE %lu: %s\n", gCodeState.lineNumber, line.c_str());
}

