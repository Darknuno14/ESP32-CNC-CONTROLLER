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

// Inicjalizuje wszystkie obiekty zarządzające systemem
void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager,
    WebServerManager* webServerManager, ConfigManager* configManager);

// Nawiązuje połączenie WiFi wykorzystując dane z credentials.h
void connectToWiFi(WiFiManager* wifiManager);

// Uruchamia serwer WWW do sterowania i monitorowania maszyny
void startWebServer(WebServerManager* webServerManager);

// Wyciąga wartość parametru z linii G-code dla określonego oznaczenia (np. 'X', 'Y', 'F')
float getParameter(const String& line, char param);

// Aktualizuje stan wyjść fizycznych na podstawie stanu maszyny
void updateOutputs(const MachineState& cncState);

// Wczytane parametrów z pliku konfiguracyjnego
bool loadConfig(MachineConfig& config);

/**
 * Aktualizacja dynamiki silnika krokowego (dane z konfiguracji)
 * @param axis 0 = X, 1 = Y
 * @param useRapid true = rapid (G0), false = work (G1)
 */
bool updateMotorSpeed(const int axis, const bool useRapid, AccelStepper& stepper, const MachineConfig& config);

/**
 * Aktualizacja dynamiki silnika krokowego (dane z pliku)
 * @param axis 0 = X, 1 = Y
 * @param feedRate Feed rate in mm/min
 * @param accelMultiplier Acceleration multiplier -> accel = feed * multiplier
 */
bool updateMotorSpeed(const int axis, const float feedRate, const float accelMultiplier, AccelStepper& stepper, const MachineConfig& config);



void handleCNCStateMachine(MachineState& cncState, bool commandPending, WebserverCommand& cmd,
    AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config);
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

    vTaskDelay(pdMS_TO_TICKS(10000)); // Opóźnienie na uruchomienie systemu

    // Komunikacja między zadaniami
    TickType_t lastCommandProcessTime { 0 };
    TickType_t lastStatusUpdateTime { 0 };
    const TickType_t commandProcessInterval { pdMS_TO_TICKS(200) };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(5000) };

    // Stan maszyny
    MachineState cncState {};

    // Parametry (konfiguracja) maszyny
    MachineConfig config {};
    loadConfig(config);

    // Silniki krokowe
    AccelStepper stepperX(AccelStepper::DRIVER, CONFIG::STEP_X_PIN, CONFIG::DIR_X_PIN);
    AccelStepper stepperY(AccelStepper::DRIVER, CONFIG::STEP_Y_PIN, CONFIG::DIR_Y_PIN);

    updateMotorSpeed(0, false, stepperX, config);
    updateMotorSpeed(1, false, stepperY, config);
    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);

    // Aktualna komenda
    WebserverCommand cmd {};
    bool commandPending { false }; // Flaga do sprawdzania, czy komenda została odebrana

    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };

        // Odbieranie komend z kolejki
        if ((currentTime - lastCommandProcessTime) >= commandProcessInterval) {
            if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: Otrzymano komendę typu %d\n", static_cast<int>(cmd.type));
                #endif
                commandPending = true;
            }
            lastCommandProcessTime = currentTime;
        }

        // Wysłasnie statusu maszyny
        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {

            // TODO: ZAIMPLEMENTOWAĆ, BO POPTRZEDNIA WERSJA WYSYŁANIA KOLEJKI RESTARTUJE ESP - BYĆ MOŻE PROBLEMEM JEST STRING W NAZWIE PROJEKTU

            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: Stan wysłany.\n");
            #endif

            lastStatusUpdateTime = currentTime;
        }

        handleCNCStateMachine(cncState, commandPending, cmd, stepperX, stepperY, config);

        updateOutputs(cncState);

        // Krótka przerwa, aby zapobiec przekroczeniu czasu watchdoga
        if (cncState.state == CNCState::RUNNING) {
            taskYIELD();
        }
        else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// Zadanie kontroli i sterowania maszyną - obsługa interfejsu webowego
void taskControl(void* parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager, configManager, commandQueue);

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager);
    connectToWiFi(wifiManager);
    startWebServer(webServerManager);

    while (true) {
        // TODO: Zaimpelementować odbieranie statusu maszyny z kolejki

        // Krótkie opóźnienie dla oszczędzania energii i zapobiegania watchdog timeouts
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
/*
* ------------------------------------------------------------------------------------------------------------
* --- Main Controller Program ---
* ------------------------------------------------------------------------------------------------------------
*/


void setup() {
    // Port szeregowy do debugowania
    Serial.begin(115200);

    // SPI dla karty SD
    SPI.begin(CONFIG::SD_CLK_PIN, CONFIG::SD_MISO_PIN, CONFIG::SD_MOSI_PIN);

    delay(200); // Odczekanie chwili na poprawne uruchomienie protokołu SPI

    // Ustawienie pinów dla wejść/wyjść kontrolowanych ręcznie - bez bibliotek
    pinMode(CONFIG::WIRE_RELAY_PIN, OUTPUT);
    digitalWrite(CONFIG::WIRE_RELAY_PIN, LOW);
    pinMode(CONFIG::FAN_RELAY_PIN, OUTPUT);
    digitalWrite(CONFIG::FAN_RELAY_PIN, LOW);

    // Inicjalizacja kolejek do komunikacji między zadaniami
    stateQueue = xQueueCreate(5, sizeof(MachineState));
    commandQueue = xQueueCreate(10, sizeof(WebserverCommand));

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
    xTaskCreatePinnedToCore(taskCNC,
        "CNC",
        CONFIG::CNCTASK_STACK_SIZE,
        NULL,
        CONFIG::CNCTASK_PRIORITY,
        NULL,
        CONFIG::CORE_0);

    delay(200); // Odczekanie chwili na poprawne uruchomienie drugiego zadania
}

void loop() {
    // Brak potrzeby implementacji pętli głównej
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- MISCELLANEOUS FUNCTIONS ---
* ------------------------------------------------------------------------------------------------------------
*/

// SYSYTEM

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

void connectToWiFi(WiFiManager* wifiManager) {
    WiFiManagerStatus wifiManagerStatus { wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, CONFIG::MAX_CONNECTION_TIME) };
    if (wifiManagerStatus == WiFiManagerStatus::OK) {
        Serial.println("STATUS: Connected to WiFi.");
    }
    else {
        Serial.println("ERROR: Failed to connect to WiFi.");
    }
}

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

void updateOutputs(const MachineState& cncSate) {
    digitalWrite(CONFIG::WIRE_RELAY_PIN, cncSate.hotWireOn ? HIGH : LOW);
    digitalWrite(CONFIG::FAN_RELAY_PIN, cncSate.fanOn ? HIGH : LOW);
}

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

bool updateMotorSpeed(const int axis, const bool useRapid, AccelStepper& stepper, const MachineConfig& config) {
    const auto& axisConfig = (axis == 0) ? config.xAxis : config.yAxis;
    const float stepsPerMM = axisConfig.stepsPerMM;

    const float feedRate = useRapid ? axisConfig.rapidFeedRate : axisConfig.workFeedRate;
    const float acceleration = useRapid ? axisConfig.rapidAcceleration : axisConfig.workAcceleration;

    stepper.setMaxSpeed(feedRate / 60.0f * stepsPerMM); // Konwersja z mm/min na steps/s
    stepper.setAcceleration(acceleration * stepsPerMM);

    return true;
}

bool updateMotorSpeed(const int axis, const float feedRate, const float accelMultiplier, AccelStepper& stepper, const MachineConfig& config) {
    const auto& axisConfig = (axis == 0) ? config.xAxis : config.yAxis;
    const float stepsPerMM = axisConfig.stepsPerMM;

    stepper.setMaxSpeed(feedRate / 60.0f * stepsPerMM);
    stepper.setAcceleration(feedRate * accelMultiplier * stepsPerMM);

    return true;
}

// CNC
void handleCNCStateMachine(MachineState& cncState, bool commandPending, WebserverCommand& cmd,
    AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {
    GCodeProcessingState gCodeState {};

    switch (cncState.state) {
        case CNCState::IDLE:
            // Oczekiwanie na polecenia
            if (commandPending) {
                commandPending = false;
            }

            break;

        case CNCState::RUNNING:
            // Przetwarzanie G-code
            if (commandPending) {
                commandPending = false;
            }

            break;

        case CNCState::HOMING:
            // Bazowanie
            if (commandPending) {
                commandPending = false;
            }

            break;

        case CNCState::STOPPED:
        case CNCState::ERROR:
            // Zatrzymanie, oczekiwanie na RESET
            // Błąd, oczekiwanie na RESET
            if (commandPending) {
                commandPending = false;
            }

    }
}

// GCODE

bool initializeGCodeProcessing(const std::string& filename, MachineState& state,
    GCodeProcessingState& gCodeState, MachineConfig& config) {
    // Zamknij poprzedni plik, jeśli był otwarty
    if (gCodeState.fileOpen && gCodeState.currentFile) {
        gCodeState.currentFile.close();
        gCodeState.fileOpen = false;
    }

    // Resetuj wszystkie flagi i stan
    gCodeState.lineNumber = 0;
    gCodeState.stopRequested = false;
    gCodeState.pauseRequested = false;
    gCodeState.stage = GCodeProcessingState::ProcessingStage::IDLE;
    gCodeState.movementInProgress = false;

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

    sdManager->giveSD();

    // Oszaconowano 24 bajtów na linię
    // plik 139 648 bajtów ma 5727 linii -> 139648 / 5727 = 24.4
    // plik 289 824 bajtów ma 11854 linii -> 289824 / 11854 = 24.4
    // Uproszczono ze względu na niepotrzebne komplikowanie obliczeń
    gCodeState.totalLines = gCodeState.currentFile.size() / 24;

    gCodeState.fileOpen = true;

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: Otwarto plik %s, liczba linii: %d\n",
        filePath.c_str(), gCodeState.totalLines);
    #endif

    // Inicjalizacja stanu maszyny
    state.currentProject = filename;
    state.jobStartTime = millis();
    state.jobProgress = 0.0f;
    state.currentLine = 0;
    updateOutputs(state);
    return true;
}

void processGCodeStateMachine(MachineState& cncState, GCodeProcessingState& gCodeState,
    AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {

    // Sprawdź warunek zatrzymania
    if (gCodeState.stopRequested) {
        if (gCodeState.fileOpen) {
            // Zamknij plik
            sdManager->takeSD();
            gCodeState.currentFile.close();
            gCodeState.fileOpen = false;
            sdManager->giveSD();
        }

        // Aktualizuj stan
        cncState.state = CNCState::STOPPED;
        gCodeState.stage = GCodeProcessingState::ProcessingStage::IDLE;

        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Przetwarzanie zatrzymane");
        #endif
        return;
    }

    // Sprawdź pauzę
    if (gCodeState.pauseRequested) {
        // 
        return;
    }

    // TODO: Zaimplementować
    // Główna maszyna stanowa dla przetwarzania G-code
    // Obsługuje tylko jedną operację na iterację pętli
    // Dzięki temu nie blokuje pętli podczas przetwarzania G-code
    switch (gCodeState.stage) {
        case GCodeProcessingState::ProcessingStage::IDLE:
            // Oczekiwanie na rozpoczęcie przetwarzania
            break;

        case GCodeProcessingState::ProcessingStage::HEATING:
            // Rozgrzewanie drutu
            break;

        case GCodeProcessingState::ProcessingStage::MOVING_TO_OFFSET:
            // Przemieszczenie o offset z bieżącej pozycji
            break;

        case GCodeProcessingState::ProcessingStage::READING_FILE:
            // Odczyt linii G-code z pliku
            break;

        case GCodeProcessingState::ProcessingStage::PROCESSING_LINE:
            // Przetwarzanie linii G-code
            break;

        case GCodeProcessingState::ProcessingStage::EXECUTING_MOVEMENT:
            // Wykonywanie ruchu
            break;

        case GCodeProcessingState::ProcessingStage::FINISHED:
            // Zakończono przetwarzanie pliku G-code
            break;

        case GCodeProcessingState::ProcessingStage::ERROR:
            // Błąd przetwarzania
            break;

        default:
            // Stan nieznany
            break;
    }
}

bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY,
    MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config) {

    // Usuń komentarze i białe znaki
    line.trim();
    if (line.length() == 0)
        return true;

    int commentIndex { line.indexOf(';') };
    if (commentIndex != -1) {
        line = line.substring(0, commentIndex);
        line.trim();
        if (line.length() == 0)
            return true;
    }

    // Parsuj komendę
    int spaceIndex { line.indexOf(' ') };
    String command { (spaceIndex != -1) ? line.substring(0, spaceIndex) : line };
    command.toUpperCase();

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC GCODE: Przetwarzam linię: %s\n", line.c_str());
    #endif

    // Przetwarzanie komend G-code bez blokowania
    if (command.startsWith("G")) {
        int gCode { command.substring(1).toInt() };

        switch (gCode) {
            case 0: // G0: Szybki ruch
            case 1: // G1: Ruch liniowy
                {
                    float targetX { getParameter(line, 'X') };
                    float targetY { getParameter(line, 'Y') };
                    float feedRate { getParameter(line, 'F') };

                    // Oblicz nowe pozycje
                    float newX { isnan(targetX) ? cncState.currentX : (cncState.relativeMode ? cncState.currentX + targetX : targetX) };
                    float newY { isnan(targetY) ? cncState.currentY : (cncState.relativeMode ? cncState.currentY + targetY : targetY) };

                    // Oblicz prędkości
                    if (!isnan(feedRate)) {
                        if (config.useGCodeFeedRate) {
                            updateMotorSpeed(0, feedRate, 2.0f, stepperX, config);
                            updateMotorSpeed(1, feedRate, 2.0f, stepperY, config);
                        }
                        else {
                            updateMotorSpeed(0, gCode == 0, stepperX, config);
                            updateMotorSpeed(1, gCode == 0, stepperY, config);
                        }
                    }

                    // Zapamiętaj cel ruchu
                    gCodeState.targetX = newX;
                    gCodeState.targetY = newY;
                    gCodeState.movementInProgress = true;

                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC MOVE: X=%.3f Y=%.3f ", newX, newY);
                    #endif
                }
                break;

            case 28: // G28: Bazowanie
                // Uproszczona wersja bazowania
                // Przyjęcie, że maszyna bierze obecną pozycję jako 0,0
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC HOME: Bazowanie osi X i Y");
                #endif

                // Resetuj pozycję
                stepperX.setCurrentPosition(0);
                stepperY.setCurrentPosition(0);
                cncState.currentX = 0;
                cncState.currentY = 0;
                break;

            case 90: // G90: Pozycjonowanie absolutne
                cncState.relativeMode = false;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC MODE: Pozycjonowanie absolutne");
                #endif
                break;

            case 91: // G91: Pozycjonowanie względne
                cncState.relativeMode = true;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC MODE: Pozycjonowanie względne");
                #endif
                break;

            default:
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC WARNING: Nieobsługiwany kod G: G%d\n", gCode);
                #endif
                break;
        }
    }
    else if (command.startsWith("M")) {
        int mCode { command.substring(1).toInt() };

        switch (mCode) {
            case 0: // M0: Pauza programu
            case 1: // M1: Opcjonalna pauza programu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC PAUSE: Program wstrzymany, oczekiwanie na wznowienie");
                #endif

                cncState.isPaused = true;
                break;

            case 3: // M3: Włączenie drutu (CW dla wrzeciona)
            case 4: // M4 : Włączenie drutu (CCW dla wrzeciona)
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC WIRE: Drut włączony");
                #endif

                cncState.hotWireOn = true;
                break;

            case 5: // M5: Wyłączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC WIRE: Drut wyłączony");
                #endif

                cncState.hotWireOn = false;
                break;

            case 30: // M30: Zakończenie programu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC PROGRAM: Program zakończony");
                #endif

            case 106: // M106: Włączenie wentylatora
                // Brak obsługi prędkości wentylatora 
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC FAN: Wentylator włączony");
                #endif

                cncState.fanOn = true;
                break;

            case 107: // M107: Wyłączenie wentylatora
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC FAN: Wentylator wyłączony");
                #endif

                cncState.fanOn = false;
                break;

            default:
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC WARNING: Nieobsługiwany kod M: M%d\n", mCode);
                #endif
                break;
        }
    }
    else if (command.startsWith("F")) {
        float feedRate { getParameter(line, 'F') };

        // Oblicz prędkości
        if (!isnan(feedRate) && config.useGCodeFeedRate) {
            updateMotorSpeed(0, feedRate, 2.0f, stepperX, config);
            updateMotorSpeed(1, feedRate, 2.0f, stepperY, config);
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC GCODE: Zaktualizowano prędkość \n");
            #endif
        }

    }
    else {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC WARNING: Nierozpoznana komenda: %s\n", command.c_str());
        #endif
    }

    return true;
}




