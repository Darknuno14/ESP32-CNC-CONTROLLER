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

// Przetwarza pojedynczą linię G-code i wykonuje odpowiednie działania maszynowe
bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY,
    MachineState& state, MachineConfig& config);

// Przetwarza plik G-code z karty SD - wyłusuje linie i przekazuje do przetwarzania
uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition,
    volatile const bool& pauseCondition, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& state);

// Aktualizuje stan wyjść fizycznych na podstawie stanu maszyny
void updateOutputs(const MachineState& state);

// Wykonuje ruch manualny (jog) na podstawie poleceń z interfejsu webowego
bool executeJogMovement(AccelStepper& stepperX, AccelStepper& stepperY, float xOffset, float yOffset, float speed,
    const MachineConfig& config, MachineState& state);

// Wykonuje sekwencję bazowania osi (homing)
bool executeHomingSequence(AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config,
    MachineState& state);

/*
* ------------------------------------------------------------------------------------------------------------
* --- Tasks ---
* ------------------------------------------------------------------------------------------------------------
*/

// Zadanie obsługi ruchu CNC - wykonywanie poleceń i kontrola fizycznych wejść/wyjść
void taskCNC(void* parameter) {
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

    // Flagi kontrolne
    bool cuttingActive = false;       // Czy drut jest w trybie cięcia
    bool processingStopped = false;   // Flaga zatrzymania przetwarzania
    bool processingPaused = false;    // Flaga wstrzymania przetwarzania
    bool homingInProgress = false;    // Czy bazowanie jest w trakcie
    bool jogInProgress = false;       // Czy jog jest w trakcie

    unsigned long lastStatusUpdateTime { 0 };                 // Ostatnia aktualizacja statusu
    constexpr unsigned long STATUS_UPDATE_INTERVAL { 10000 };   // Interwał aktualizacji statusu
    // TODO: zmienić czas z 10s
    unsigned long lastCommandProcessTime = 0;              // Czas ostatniego przetwarzania komendy
    constexpr unsigned long COMMAND_PROCESS_INTERVAL = 10; // Interwał przetwarzania komend (10ms)

    AccelStepper stepperX(AccelStepper::DRIVER, CONFIG::STEP_X_PIN, CONFIG::DIR_X_PIN);
    AccelStepper stepperY(AccelStepper::DRIVER, CONFIG::STEP_Y_PIN, CONFIG::DIR_Y_PIN);

    // Pobierz konfigurację (parametry)
    MachineConfig config {};
    if (configManager->isConfigLoaded()) {
        config = configManager->getConfig();

        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Konfiguacja załadowana");
        #endif
    }
    else {
        // Jeśli nie udało się załadować konfiguracji, użyj domyślnej
        config.xAxis.stepsPerMM = CONFIG::X_STEPS_PER_MM;
        config.xAxis.workFeedRate = CONFIG::X_WORK_FEEDRATE;
        config.xAxis.workAcceleration = CONFIG::X_WORK_ACCELERATION;
        config.xAxis.rapidFeedRate = CONFIG::X_RAPID_FEEDRATE;
        config.xAxis.rapidAcceleration = CONFIG::X_RAPID_ACCELERATION;

        config.yAxis.stepsPerMM = CONFIG::Y_STEPS_PER_MM;
        config.yAxis.workFeedRate = CONFIG::Y_WORK_FEEDRATE;
        config.yAxis.workAcceleration = CONFIG::Y_WORK_ACCELERATION;
        config.yAxis.rapidFeedRate = CONFIG::Y_RAPID_FEEDRATE;
        config.yAxis.rapidAcceleration = CONFIG::Y_RAPID_ACCELERATION;

        config.offsetX = CONFIG::X_OFFSET;
        config.offsetY = CONFIG::Y_OFFSET;

        config.useGCodeFeedRate = CONFIG::USE_GCODE_FEEDRATE;
        config.delayAfterStartup = CONFIG::DELAY_AFTER_STARTUP;

        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Używam domyślnej konfiguracji");
        #endif
    }

    // Konfiguracja silników
    stepperX.setMaxSpeed(config.xAxis.rapidFeedRate / 60.0f * config.xAxis.stepsPerMM); // konwersja z mm/min na kroki/s
    stepperX.setAcceleration(config.xAxis.rapidAcceleration * config.xAxis.stepsPerMM);
    stepperY.setMaxSpeed(config.yAxis.rapidFeedRate / 60.0f * config.yAxis.stepsPerMM);
    stepperY.setAcceleration(config.yAxis.rapidAcceleration * config.yAxis.stepsPerMM);

    // Wyzeruj silniki na pozycji początkowej
    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);

    // Aktualna komenda
    WebserverCommand cmd {};

    // Wynik ostatniego przetwarzania G-code
    uint8_t processResult { 0 };

    while (true) {
        // 1. Odbieranie komend z kolejki
        if (millis() - lastCommandProcessTime >= COMMAND_PROCESS_INTERVAL) {
            if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: Otrzymano komendę typu %d\n", static_cast<int>(cmd.type));
                #endif

                // Przetwarzanie komend w zależności od aktualnego stanu maszyny
                switch (state.state) {
                    case CNCState::IDLE:
                        // W stanie IDLE obsługujemy wszystkie komendy
                        switch (cmd.type) {
                            case CommandType::START:
                                if (sdManager->isProjectSelected()) {
                                    std::string projectName {};
                                    if (sdManager->getSelectedProject(projectName) == SDManagerStatus::OK) {
                                        state.currentProject = projectName;
                                        state.state = CNCState::RUNNING;
                                        state.isPaused = false;
                                        state.jobStartTime = millis();
                                        state.jobProgress = 0.0f;
                                        state.currentLine = 0;
                                        processingStopped = false;
                                        processingPaused = false;
                                        cuttingActive = true;

                                        #ifdef DEBUG_CNC_TASK
                                        Serial.printf("DEBUG CNC: Rozpoczynam przetwarzanie projektu: %s\n", projectName.c_str());
                                        #endif
                                    }
                                }
                                else {
                                    #ifdef DEBUG_CNC_TASK
                                    Serial.println("DEBUG CNC: Brak wybranego projektu");
                                    #endif
                                }
                                break;

                            case CommandType::JOG:
                                // Obsługa JOG w zależności od parametrów
                                if (cmd.param1 == -1 && cmd.param2 == -1) {
                                    // Komenda dla drutu grzejnego
                                    state.hotWireOn = (cmd.param3 > 0.5f);
                                    updateOutputs(state);
                                }
                                else if (cmd.param1 == -2 && cmd.param2 == -2) {
                                    // Komenda dla wentylatora
                                    state.fanOn = (cmd.param3 > 0.5f);
                                    updateOutputs(state);
                                }
                                else {
                                    // Normalny ruch JOG
                                    state.state = CNCState::JOG;
                                    jogInProgress = executeJogMovement(
                                        stepperX, stepperY,
                                        cmd.param1, cmd.param2, cmd.param3,
                                        config, state
                                    );
                                }
                                break;

                            case CommandType::HOMING:
                                state.state = CNCState::HOMING;
                                homingInProgress = executeHomingSequence(stepperX, stepperY, config, state);
                                break;

                            case CommandType::ZERO:
                                stepperX.setCurrentPosition(0);
                                stepperY.setCurrentPosition(0);
                                state.currentX = 0.0f;
                                state.currentY = 0.0f;
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Pozycja wyzerowana");
                                #endif
                                break;

                            default:
                                // Ignorujemy inne komendy w stanie IDLE
                                break;
                        }
                        break;

                    case CNCState::RUNNING:
                        // W stanie RUNNING obsługujemy komendy STOP i PAUSE
                        switch (cmd.type) {
                            case CommandType::STOP:
                                processingStopped = true;
                                state.state = CNCState::STOPPED;
                                cuttingActive = false;
                                state.hotWireOn = false;
                                updateOutputs(state);
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Zatrzymano przetwarzanie");
                                #endif
                                break;

                            case CommandType::PAUSE:
                                processingPaused = !processingPaused;
                                state.isPaused = processingPaused;
                                #ifdef DEBUG_CNC_TASK
                                Serial.printf("DEBUG CNC: %s przetwarzanie\n",
                                    processingPaused ? "Wstrzymano" : "Wznowiono");
                                #endif
                                break;

                            default:
                                // Ignorujemy inne komendy w stanie RUNNING
                                break;
                        }
                        break;

                    case CNCState::JOG:
                        // W stanie JOG obsługujemy głównie komendy JOG i STOP
                        switch (cmd.type) {
                            case CommandType::JOG:
                                // Obsługa JOG w zależności od parametrów
                                if (cmd.param1 == -1 && cmd.param2 == -1) {
                                    // Komenda dla drutu grzejnego
                                    state.hotWireOn = (cmd.param3 > 0.5f);
                                    updateOutputs(state);
                                }
                                else if (cmd.param1 == -2 && cmd.param2 == -2) {
                                    // Komenda dla wentylatora
                                    state.fanOn = (cmd.param3 > 0.5f);
                                    updateOutputs(state);
                                }
                                else {
                                    // Zatrzymujemy obecny ruch i rozpoczynamy nowy
                                    stepperX.stop();
                                    stepperY.stop();

                                    jogInProgress = executeJogMovement(
                                        stepperX, stepperY,
                                        cmd.param1, cmd.param2, cmd.param3,
                                        config, state
                                    );
                                }
                                break;

                            case CommandType::STOP:
                                stepperX.stop();
                                stepperY.stop();
                                jogInProgress = false;
                                state.state = CNCState::IDLE;
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Zatrzymano tryb JOG");
                                #endif
                                break;

                            default:
                                // Ignorujemy inne komendy w stanie JOG
                                break;
                        }
                        break;

                    case CNCState::HOMING:
                        // W stanie HOMING obsługujemy tylko STOP
                        if (cmd.type == CommandType::STOP) {
                            stepperX.stop();
                            stepperY.stop();
                            homingInProgress = false;
                            state.state = CNCState::IDLE;
                            #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG CNC: Zatrzymano bazowanie");
                            #endif
                        }
                        break;

                    case CNCState::STOPPED:
                    case CNCState::ERROR:
                        // W stanach STOPPED i ERROR obsługujemy tylko RESET
                        if (cmd.type == CommandType::RESET) {
                            state.state = CNCState::IDLE;
                            state.isPaused = false;
                            state.jobProgress = 0.0f;
                            state.currentLine = 0;
                            state.jobRunTime = 0;
                            state.hasError = false;
                            state.errorCode = 0;
                            processingStopped = false;
                            processingPaused = false;
                            #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG CNC: Zresetowano maszynę");
                            #endif
                        }
                        break;
                }

                // Wymuś natychmiastową aktualizację stanu
                lastStatusUpdateTime = 0;
            }

            lastCommandProcessTime = millis();
        }

        // 2. Wykonanie odpowiednich działań w zależności od stanu maszyny
        switch (state.state) {
            case CNCState::IDLE:
                // W stanie IDLE nic nie robimy, czekamy na komendy
                break;

            case CNCState::RUNNING:
                // W stanie RUNNING przetwarzamy plik G-code
                if (!processingPaused && !processingStopped && state.currentProject.length() > 0) {
                    // Jeśli to pierwszy przebieg po zmianie stanu, zaczynamy przetwarzanie
                    if (state.jobProgress == 0.0f) {
                        // Dodatkowe opóźnienie startowe z konfiguracji
                        if (config.delayAfterStartup > 0) {
                            vTaskDelay(pdMS_TO_TICKS(config.delayAfterStartup));
                        }

                        #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC: Rozpoczynam przetwarzanie pliku: %s\n",
                            state.currentProject.c_str());
                        #endif

                        // Przetwarzanie pliku G-code
                        processResult = processGCodeFile(
                            state.currentProject,
                            processingStopped,
                            processingPaused,
                            stepperX,
                            stepperY,
                            state
                        );

                        // Analiza wyniku
                        switch (processResult) {
                            case 0: // Sukces
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Przetwarzanie zakończone sukcesem");
                                #endif
                                state.state = CNCState::IDLE;
                                break;

                            case 1: // Zatrzymano przez użytkownika
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Przetwarzanie zatrzymane przez użytkownika");
                                #endif
                                state.state = CNCState::STOPPED;
                                break;

                            case 2: // Błąd
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Błąd podczas przetwarzania");
                                #endif
                                state.state = CNCState::ERROR;
                                state.hasError = true;
                                state.errorCode = 1; // Ustawienie kodu błędu
                                break;

                            default:
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC: Nieznany wynik przetwarzania");
                                #endif
                                state.state = CNCState::ERROR;
                                state.hasError = true;
                                state.errorCode = 2; // Inny kod błędu
                                break;
                        }

                        // Zakończenie przetwarzania
                        cuttingActive = false;
                        state.hotWireOn = false;
                        updateOutputs(state);
                    }
                }
                break;

            case CNCState::JOG:
                // W stanie JOG sprawdzamy, czy ruch został zakończony
                if (jogInProgress && stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                    jogInProgress = false;
                    state.state = CNCState::IDLE;
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Ruch JOG zakończony");
                    #endif
                }

                // Wykonanie ruchu
                if (jogInProgress) {
                    stepperX.run();
                    stepperY.run();

                    // Aktualizacja pozycji
                    state.currentX = static_cast<float>(stepperX.currentPosition()) / config.xAxis.stepsPerMM;
                    state.currentY = static_cast<float>(stepperY.currentPosition()) / config.yAxis.stepsPerMM;
                }
                break;

            case CNCState::HOMING:
                // W stanie HOMING sprawdzamy, czy bazowanie zostało zakończone
                if (homingInProgress && stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                    homingInProgress = false;
                    state.state = CNCState::IDLE;

                    // Ustawienie pozycji bazowej
                    stepperX.setCurrentPosition(0);
                    stepperY.setCurrentPosition(0);
                    state.currentX = 0.0f;
                    state.currentY = 0.0f;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Bazowanie zakończone");
                    #endif
                }

                // Wykonanie ruchu bazowania
                if (homingInProgress) {
                    stepperX.run();
                    stepperY.run();

                    // Aktualizacja pozycji
                    state.currentX = static_cast<float>(stepperX.currentPosition()) / config.xAxis.stepsPerMM;
                    state.currentY = static_cast<float>(stepperY.currentPosition()) / config.yAxis.stepsPerMM;
                }
                break;

            case CNCState::STOPPED:
                // Zatrzymany - nic nie robimy, czekamy na RESET
                break;

            case CNCState::ERROR:
                // Błąd - nic nie robimy, czekamy na RESET
                break;
        }

        // 3. Aktualizacja stanu wyjść
        updateOutputs(state);

        // 4. Aktualizacja czasu pracy
        if (state.state == CNCState::RUNNING && !state.isPaused) {
            state.jobRunTime = millis() - state.jobStartTime;
        }

        // 5. Wysyłanie stanu maszyny przez kolejkę
        if (millis() - lastStatusUpdateTime >= STATUS_UPDATE_INTERVAL) {

            // TODO: ZAIMPLEMENTOWAĆ, BO POPTRZEDNIA WERSJA WYSYŁANIA KOLEJKI RESTARTUJE ESP - BYĆ MOŻE PROBLEMEM JEST STRING W NAZWIE PROJEKTU

            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: Stan wysłany. X=%.2f, Y=%.2f, Stan=%d\n",
                state.currentX, state.currentY, static_cast<int>(state.state));
            #endif

            lastStatusUpdateTime = millis();
        }
        // Krótka przerwa, aby zapobiec przekroczeniu czasu watchdoga
        vTaskDelay(pdMS_TO_TICKS(10));
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

float getParameter(const String& line, char param) {
    int index { line.indexOf(param) };
    if (index == -1)
        return NAN;

    // Wyznaczanie początku tesktu z parametrem
    int valueStart { index + 1 };
    int valueEnd { static_cast<int>(line.length()) };

    // Wyznaczanie końca tekstu z parametrem (następna spacja lub koniec linii)
    for (int i { valueStart }; i < static_cast<int>(line.length()); ++i) {
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

    // Usuwanie białych znaków i pustych linii
    line.trim();
    if (line.length() == 0)
        return true;

    // Usuwanie komentarzy
    int commentIndex { line.indexOf(';') };
    if (commentIndex != -1) {
        line = line.substring(0, commentIndex);
        line.trim();
        if (line.length() == 0)
            return true;
    }

    // Wyodrębnienie polecenia
    int spaceIndex { line.indexOf(' ') };
    String command { (spaceIndex != -1) ? line.substring(0, spaceIndex) : line };
    command.toUpperCase(); // Ujednolicenie wielkości liter

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
                    float newX { isnan(targetX) ? state.currentX : (state.relativeMode ? state.currentX + targetX : targetX) };
                    float newY { isnan(targetY) ? state.currentY : (state.relativeMode ? state.currentY + targetY : targetY) };

                    // Oblicz prędkość ruchu w zależności od komendy ruchu
                    float moveSpeedX { (gCode == 0) ? config.xAxis.rapidFeedRate : (config.useGCodeFeedRate && !isnan(feedRate)) ? min(feedRate, config.xAxis.workFeedRate) : config.xAxis.workFeedRate };
                    float moveSpeedY { (gCode == 0) ? config.yAxis.rapidFeedRate : (config.useGCodeFeedRate && !isnan(feedRate)) ? min(feedRate, config.yAxis.workFeedRate) : config.yAxis.workFeedRate };

                    // Oblicz ruch w krokach
                    long targetStepsX { static_cast<long>(newX * config.xAxis.stepsPerMM) };
                    long targetStepsY { static_cast<long>(newY * config.yAxis.stepsPerMM) };

                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC MOVE: X%.3f Y%.3f Vx%.3f Vy%.3f \n", newX, newY, moveSpeedX, moveSpeedY);
                    #endif

                    // Prędkości silników
                    float stepsXPerSec { moveSpeedX / 60.0f * config.xAxis.stepsPerMM }; // 60.0f - przelicznik na sekundy
                    float stepsYPerSec { moveSpeedY / 60.0f * config.yAxis.stepsPerMM };
                    stepperX.setMaxSpeed(stepsXPerSec);
                    stepperY.setMaxSpeed(stepsYPerSec);

                    // Przyspieszenie - tymczasowo jako dwukrotna prędkość
                    stepperX.setAcceleration(config.xAxis.rapidAcceleration * config.xAxis.stepsPerMM);
                    stepperY.setAcceleration(config.yAxis.rapidAcceleration * config.yAxis.stepsPerMM);

                    // Ustaw pozycje docelowe
                    stepperX.moveTo(targetStepsX);
                    stepperY.moveTo(targetStepsY);

                    // Wykonanie ruchu bez blokowania
                    bool moving { true };
                    unsigned long lastProgressUpdate { millis() };

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

            case 28: // G28: Bazowanie
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC HOME: Homing X and Y axes");
                #endif

                // Resetuj pozycję
                stepperX.setCurrentPosition(0);
                stepperY.setCurrentPosition(0);
                state.currentX = 0;
                state.currentY = 0;
                break;

            case 90: // G90: Pozycjonowanie absolutne
                state.relativeMode = false;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC MODE: Absolute positioning");
                #endif
                break;

            case 91: // G91: Pozycjonowanie względne
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
            case 0: // M0: Pauza programu
            case 1: // M1: Opcjonalna pauza programu
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
                        }
                        else if (cmd.type == CommandType::STOP) {
                            xQueueReceive(commandQueue, &cmd, 0); // Usuń komendę z kolejki
                            return false;                         // Przerwij przetwarzanie
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;

            case 3: // M3: Włączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC WIRE: Wire ON");
                #endif
                state.hotWireOn = true;
                updateOutputs(state);
                break;

            case 5: // M5: Wyłączenie wrzeciona/drutu
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

void updateOutputs(const MachineState& state) {
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
    }
    else {
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
        config.offsetX = CONFIG::X_OFFSET;
        config.offsetY = CONFIG::Y_OFFSET;
        config.useGCodeFeedRate = CONFIG::USE_GCODE_FEEDRATE;
        config.deactivateESTOP = CONFIG::DEACTIVATE_ESTOP;
        config.deactivateLimitSwitches = CONFIG::DEACTIVATE_LIMIT_SWITCHES;
        config.limitSwitchType = CONFIG::LIMIT_SWITCH_TYPE;
    }

    // Otwarcie pliku
    std::string filePath { CONFIG::PROJECTS_DIR + filename };
    File file { SD.open(filePath.c_str()) };

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
    uint32_t lineNumber { 0 };
    bool processingError { false };

    if (config.offsetX != 0.0f || config.offsetY != 0.0f) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC: Przesunięcie do pozycji początkowej (offset): X=%.2f, Y=%.2f\n",
            config.offsetX, config.offsetY);
        #endif

        // Oblicz przesunięcie w krokach silnika
        long targetStepsX { static_cast<long>(config.offsetX * config.xAxis.stepsPerMM) };
        long targetStepsY { static_cast<long>(config.offsetY * config.yAxis.stepsPerMM) };

        // Ustaw parametry ruchu
        stepperX.setMaxSpeed(config.xAxis.rapidFeedRate / 60.0f * config.xAxis.stepsPerMM);
        stepperY.setMaxSpeed(config.yAxis.rapidFeedRate / 60.0f * config.yAxis.stepsPerMM);
        stepperX.setAcceleration(config.xAxis.rapidAcceleration * config.xAxis.stepsPerMM);
        stepperY.setAcceleration(config.yAxis.rapidAcceleration * config.yAxis.stepsPerMM);

        // Ustaw pozycje docelowe (ruch absolutny)
        stepperX.moveTo(targetStepsX);
        stepperY.moveTo(targetStepsY);

        // Wykonaj ruch do pozycji offsetu
        bool moving { true };
        while (moving && !stopCondition) {
            stepperX.run();
            stepperY.run();

            // Aktualizuj stan pozycji
            state.currentX = stepperX.currentPosition() / config.xAxis.stepsPerMM;
            state.currentY = stepperY.currentPosition() / config.yAxis.stepsPerMM;

            // Sprawdź, czy ruch został zakończony
            if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                moving = false;
            }

            // Wysyłanie aktualizacji statusu co pewien czas
            if (millis() % 100 == 0) {
                xQueueSend(stateQueue, &state, 0);
            }

            vTaskDelay(10); // Zapobieganie watchdog reset
        }

        // Jeśli zatrzymano podczas ruchu do pozycji startowej, przerwij
        if (stopCondition) {
            file.close();
            sdManager->giveSD();
            return 1; // Zatrzymano przez użytkownika
        }

        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Osiągnięto pozycję początkową");
        #endif
    }

    while (file.available() && !stopCondition) {
        if (pauseCondition) {
            // Jeśli wstrzymano, czekaj bez przetwarzania
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Odczyt linii z pliku
        String line { file.readStringUntil('\n') };
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
    }
    else if (processingError) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC ERROR: Błąd przetwarzania");
        #endif
        return 2; // Błąd podczas przetwarzania
    }
    else {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC STATUS: Przetwarzanie zakończone pomyślnie");
        #endif
        return 0; // Sukces
    }
}

bool executeJogMovement(AccelStepper& stepperX, AccelStepper& stepperY,
    float xOffset, float yOffset, float speed,
    const MachineConfig& config, MachineState& state) {
    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: JOG X=%.2f, Y=%.2f, Prędkość=%.2f\n",
        xOffset, yOffset, speed);
    #endif

    // Oblicz przesunięcie w krokach
    long stepsX = static_cast<long>(xOffset * config.xAxis.stepsPerMM);
    long stepsY = static_cast<long>(yOffset * config.yAxis.stepsPerMM);

    // Ustaw prędkość (konwersja mm/min na kroki/s)
    float speedX = (speed / 60.0f) * config.xAxis.stepsPerMM;
    float speedY = (speed / 60.0f) * config.yAxis.stepsPerMM;

    stepperX.setMaxSpeed(speedX);
    stepperY.setMaxSpeed(speedY);

    // Ustaw przyspieszenie
    stepperX.setAcceleration(config.xAxis.workAcceleration * config.xAxis.stepsPerMM);
    stepperY.setAcceleration(config.yAxis.workAcceleration * config.yAxis.stepsPerMM);

    // Ustaw ruch względny
    stepperX.move(stepsX);
    stepperY.move(stepsY);

    return true;
}

bool executeHomingSequence(AccelStepper& stepperX, AccelStepper& stepperY,
    const MachineConfig& config, MachineState& state) {
    #ifdef DEBUG_CNC_TASK
    Serial.println("DEBUG CNC: Rozpoczęcie procedury bazowania");
    #endif

    // Ustaw prędkość i przyspieszenie dla bazowania (niższe niż normalne)
    stepperX.setMaxSpeed(config.xAxis.workFeedRate / 60.0f * config.xAxis.stepsPerMM / 2);
    stepperY.setMaxSpeed(config.yAxis.workFeedRate / 60.0f * config.yAxis.stepsPerMM / 2);
    stepperX.setAcceleration(config.xAxis.workAcceleration * config.xAxis.stepsPerMM / 2);
    stepperY.setAcceleration(config.yAxis.workAcceleration * config.yAxis.stepsPerMM / 2);

    // TODO: ZAIMPLEMENTOWAĆ BAZOWANIE, TYMCZASOWO USTAWIANA JEST POZYCJA 0,0
    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);
    // TODO: ZAIMPLEMENTOWAĆ BAZOWANIE, TYMCZASOWO USTAWIANA JEST POZYCJA 0,0

    return true;
}