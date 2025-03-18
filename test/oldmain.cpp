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

    // Flagi
    static bool processingStop { false };   // Przetwarzanie G-code zatrzymane
    static bool processingPaused { false };    // Przetwarzanie G-code wstrzymane

    // Komunikacja między zadaniami
    TickType_t lastCommandProcessTime = 0;
    TickType_t lastStatusUpdateTime = 0;
    const TickType_t commandProcessInterval = pdMS_TO_TICKS(200);
    const TickType_t statusUpdateInterval = pdMS_TO_TICKS(5000);

    // Stan maszyny
    MachineState state {};

    // Parametry / Konfiguracja maszyny
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

    // Wynik ostatniego przetwarzania G-code
    uint8_t processResult { 0 };

    while (true) {
        TickType_t currentTime = xTaskGetTickCount();

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


        // Wykonanie odpowiednich działań w zależności od stanu maszyny
        switch (state.state) {
            case CNCState::IDLE:
                if (commandPending) {
                    switch (cmd.type) {
                        ;
                    }
                    commandPending = false;
                }

                break;

            case CNCState::RUNNING:
                // W stanie RUNNING przetwarzamy plik G-code
                break;

            case CNCState::HOMING:
                // W stanie HOMING sprawdzamy, czy bazowanie zostało zakończone
                break;

            case CNCState::STOPPED:
                // Zatrzymany - nic nie robimy, czekamy na RESET
                break;

            case CNCState::ERROR:
                // Błąd - nic nie robimy, czekamy na RESET
                break;
        }


        // Aktualizacja stanu wyjść
        updateOutputs(state);

        // Krótka przerwa, aby zapobiec przekroczeniu czasu watchdoga
        if (state.state == CNCState::RUNNING) {
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

void updateOutputs(const MachineState& state) {
    digitalWrite(CONFIG::WIRE_RELAY_PIN, state.hotWireOn ? HIGH : LOW);
    digitalWrite(CONFIG::FAN_RELAY_PIN, state.fanOn ? HIGH : LOW);
}

bool loadConfig(MachineConfig& config) {
    if (configManager->isConfigLoaded()) {
        config = configManager->getConfig();
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Konfiguacja załadowana");
        #endif
        return true;
    }
    else {
        // Domyślne wartości jeśli konfiguracja nie jest dostępna
        config.xAxis.stepsPerMM = CONFIG::X_STEPS_PER_MM;
        config.xAxis.rapidAcceleration = CONFIG::X_RAPID_ACCELERATION;
        config.xAxis.rapidFeedRate = CONFIG::X_RAPID_FEEDRATE;
        config.xAxis.workFeedRate = CONFIG::X_WORK_FEEDRATE;
        config.xAxis.workAcceleration = CONFIG::X_WORK_ACCELERATION;

        config.yAxis.stepsPerMM = CONFIG::Y_STEPS_PER_MM;
        config.yAxis.rapidFeedRate = CONFIG::Y_RAPID_FEEDRATE;
        config.yAxis.rapidAcceleration = CONFIG::Y_RAPID_ACCELERATION;
        config.yAxis.workFeedRate = CONFIG::Y_WORK_FEEDRATE;
        config.yAxis.workAcceleration = CONFIG::Y_WORK_ACCELERATION;

        config.offsetX = CONFIG::X_OFFSET;
        config.offsetY = CONFIG::Y_OFFSET;

        config.useGCodeFeedRate = CONFIG::USE_GCODE_FEEDRATE;
        config.deactivateESTOP = CONFIG::DEACTIVATE_ESTOP;
        config.deactivateLimitSwitches = CONFIG::DEACTIVATE_LIMIT_SWITCHES;
        config.limitSwitchType = CONFIG::LIMIT_SWITCH_TYPE;
        config.delayAfterStartup = CONFIG::DELAY_AFTER_STARTUP;

        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Blad wczytania konfiguracji - uzyto domyslnej.");
        #endif
        return false;
    }
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

// GCODE

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

    return true; // TEMP

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
                    // TODO: PRZEJRZEĆ CZY PRĘDKOŚĆ DOBRZE SIE USTAWIA
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
    else if (command.startsWith("F")) {
        float feedRate { getParameter(line, 'F') };

        float moveSpeedX { (config.useGCodeFeedRate && !isnan(feedRate)) ? min(feedRate, config.xAxis.workFeedRate) : config.xAxis.workFeedRate };
        float moveSpeedY { (config.useGCodeFeedRate && !isnan(feedRate)) ? min(feedRate, config.yAxis.workFeedRate) : config.yAxis.workFeedRate };

        stepperX.setMaxSpeed(feedRate / 60.0f * config.xAxis.stepsPerMM);
        stepperY.setMaxSpeed(feedRate / 60.0f * config.yAxis.stepsPerMM);
        stepperX.setAcceleration(config.xAxis.workAcceleration * config.xAxis.stepsPerMM);
        stepperY.setAcceleration(config.yAxis.workAcceleration * config.yAxis.stepsPerMM);

    }
    else {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("WARNING: Unrecognized command: %s\n", command.c_str());
        #endif
        return false;
    }

    return true;
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


