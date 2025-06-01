#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <Ticker.h>

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

// Globalne obiekty dla stepperów i timer
Ticker stepperTicker;
MultiStepper multiStepper;

// Funkcja obsługi przerwania dla stepperów (ISR)
void IRAM_ATTR onStepperTimer() {
    multiStepper.run();
}

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
bool updateMotorSpeed(const char axis, const bool useRapid, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config);
bool updateMotorSpeed(const char axis, const float feedRate, const float accelMultiplier, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config);
bool initializeGCodeProcessing(MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config);
void processGCode(MachineState& cncState, GCodeProcessingState& gCodeState, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config);
bool processGCodeLine(String line, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config);
bool processLinearMove(const String& line, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config, bool isRapid);

void processHoming(MachineState& cncState, HomingState& homingState, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config);

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
    #ifdef DEBUG
    Serial.begin(115200);
    #endif
    // SPI dla karty SD
    SPI.begin(PINCONFIG::SD_CLK_PIN, PINCONFIG::SD_MISO_PIN, PINCONFIG::SD_MOSI_PIN);
    SPI.setFrequency(25000000);

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
    commandQueue = xQueueCreate(5, sizeof(WebserverCommand));

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
        CONFIG::CORE_0                      // Core
    );

    delay(200); // Odczekanie chwili na poprawne uruchomienie pierwszego zadania

    Serial.println("Creating CNC task...");
    xTaskCreatePinnedToCore(taskCNC,        // Task function
        "CNC",                              // Task name
        CONFIG::CNCTASK_STACK_SIZE,         // Stack size
        NULL,                               // Parameters
        CONFIG::CNCTASK_PRIORITY,           // Priority
        NULL,                               // Task handle
        CONFIG::CORE_1                      // Core
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
    HomingState homingState {};

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

    // Dodaj steppery do globalnego MultiStepper
    multiStepper.addStepper(stepperX);
    multiStepper.addStepper(stepperY);

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
            vTaskDelay(pdMS_TO_TICKS(1000)); // Odczekaj chwilę przed ponowną próbą
        }
    } while (configStatus != ConfigManagerStatus::OK);

    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);

    // Uruchom timer dla stepperów (generowanie impulsów w przerwaniach)
    // Przelicz mikrosekundy na sekundy (CONFIG::STEPPER_TIMER_FREQUENCY_US to 100µs = 0.0001s)
    float timerIntervalSeconds = CONFIG::STEPPER_TIMER_FREQUENCY_US / 1000000.0f;
    stepperTicker.attach(timerIntervalSeconds, onStepperTimer);

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: Timer stepperów uruchomiony z częstotliwością %lu µs (%.6f s)\n",
        CONFIG::STEPPER_TIMER_FREQUENCY_US, timerIntervalSeconds);
    #endif

    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };

        // Uwaga: multiStepper.run() jest teraz wywoływane w przerwaniu timera!
        // W stanach STOPPED/ERROR musimy wyczyścić pozycje docelowe stepperów
        if (cncState.state == CNCState::STOPPED || cncState.state == CNCState::ERROR) {
            // Zatrzymaj silniki i wyczyść pozycje docelowe (ustaw cel = obecna pozycja)
            stepperX.stop();
            stepperY.stop();
            stepperX.setCurrentPosition(stepperX.currentPosition());
            stepperY.setCurrentPosition(stepperY.currentPosition());
        }

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

        // GLOBALNA OBSŁUGA KOMENDY STOP - działająca z każdego stanu (dla ESTOP)
        if (commandPending && commandData.type == CommandType::STOP) {
            // Natychmiastowe zatrzymanie wszystkich operacji
            cncState.hotWireOn = false;
            cncState.fanOn = false;

            // Zatrzymaj silniki i wyczyść pozycje docelowe
            stepperX.stop();
            stepperY.stop();
            stepperX.setCurrentPosition(stepperX.currentPosition());
            stepperY.setCurrentPosition(stepperY.currentPosition());

            // Zamknij pliki G-code jeśli otwarte
            if (gCodeState.fileOpen && gCodeState.currentFile) {
                if (sdManager->takeSD()) {
                    gCodeState.currentFile.close();
                    gCodeState.fileOpen = false;
                    sdManager->giveSD();
                }
            }

            // Resetuj flagi G-code
            gCodeState.stopRequested = true;
            gCodeState.pauseRequested = false;
            gCodeState.stage = GCodeProcessingState::ProcessingStage::IDLE;

            // Przejście stanu w zależności od obecnego stanu
            if (cncState.state == CNCState::STOPPED || cncState.state == CNCState::ERROR) {
                // Jeśli już w STOPPED/ERROR, przejdź do IDLE (funkcja RESET)
                cncState.state = CNCState::IDLE;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: RESET z STOPPED/ERROR do IDLE");
                #endif
            }
            else {
                // Z innych stanów przejdź do STOPPED
                cncState.state = CNCState::STOPPED;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: STOP - przejście do STOPPED");
                #endif
            }

            commandPending = false;
        }

        if (commandPending && commandData.type == CommandType::SET_HOTWIRE) {
            if (cncState.state != CNCState::STOPPED && cncState.state != CNCState::ERROR) {
                // Sterowanie drutem grzejnym
                cncState.hotWireOn = (commandData.param1 > 0.5f);
                cncState.hotWirePower = config.hotWirePower;
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: STAN DRUTU %i \n", cncState.hotWireOn);
                #endif
            }
            commandPending = false;
        }

        if (commandPending && commandData.type == CommandType::SET_FAN) {
            // Sterowanie wentylatorem
            cncState.fanOn = (commandData.param1 > 0.5f);
            cncState.fanPower = config.fanPower;
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: STAN WENTYLATORA %i \n", cncState.fanOn);
            #endif
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
                            homingState.stage = HomingState::HomingStage::HOMING_X;
                            homingState.movementInProgress = false;
                            homingState.limitReached = false;
                            homingState.backoffComplete = false;
                            homingState.errorMessage = "";
                            #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG HOME: Rozpoczęcie procedury bazowania");
                            #endif
                            break;

                        case CommandType::JOG:
                            // Rozpoczęcie ruchu ręcznego
                            cncState.state = CNCState::JOG;
                            break;

                        case CommandType::ZERO:
                            // Zerowanie pozycji
                            stepperX.setCurrentPosition(0);
                            stepperY.setCurrentPosition(0);
                            cncState.currentX = 0.0f;
                            cncState.currentY = 0.0f;
                            #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG ZERO: Pozycja wyzerowana");
                            #endif
                            break;

                        default:
                            break;
                    }
                }
                break;

            case CNCState::RUNNING:
                cncState.currentX = stepperX.currentPosition() / config.X.stepsPerMM;
                cncState.currentY = stepperY.currentPosition() / config.Y.stepsPerMM;
                // Przetwarzanie G-code
                if (commandPending) {
                    commandPending = false;
                    switch (commandData.type) {
                        case CommandType::PAUSE:
                            cncState.isPaused = !cncState.isPaused;
                            break;
                            // CommandType::STOP jest obsługiwane globalnie
                        default:
                            break;
                    }
                }

                if (!cncState.isPaused) {
                    processGCode(cncState, gCodeState, multiStepper, stepperX, stepperY, config);

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
                else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                break;

            case CNCState::JOG:
                // Wykonanie jednego kroku JOG
                if (commandPending) {
                    commandPending = false;
                    if (commandData.type == CommandType::JOG) {
                        // Wykonaj ruch JOG
                        float xOffset = commandData.param1;
                        float yOffset = commandData.param2;
                        float speedMode = commandData.param3; // 0.0 = work, 1.0 = rapid

                        #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG JOG: X=%.2f, Y=%.2f, SpeedMode=%.1f\n",
                            xOffset, yOffset, speedMode);
                        #endif

                        // Ustaw prędkość na podstawie speedMode
                        bool useRapid = (speedMode > 0.5f);
                        updateMotorSpeed('X', useRapid, multiStepper, stepperX, stepperY, config);
                        updateMotorSpeed('Y', useRapid, multiStepper, stepperX, stepperY, config);

                        // Oblicz przesunięcie w krokach
                        long stepsX = static_cast<long>(xOffset * config.X.stepsPerMM);
                        long stepsY = static_cast<long>(yOffset * config.Y.stepsPerMM);

                        // Przygotuj pozycje docelowe dla MultiStepper
                        long positions[2];
                        positions[0] = stepperX.currentPosition() + stepsX;
                        positions[1] = stepperY.currentPosition() + stepsY;

                        // Wykonaj ruch synchronizowany
                        multiStepper.moveTo(positions);

                        // Aktualizuj pozycję docelową
                        cncState.currentX += xOffset;
                        cncState.currentY += yOffset;
                    }
                }

                // Sprawdź czy ruch JOG się skończył
                if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                    cncState.state = CNCState::IDLE;
                }
                break;

            case CNCState::HOMING:
                // Obsługa bazowania
                processHoming(cncState, homingState, multiStepper, stepperX, stepperY, config);

                // Sprawdź czy bazowanie zakończone
                if (homingState.stage == HomingState::HomingStage::FINISHED) {
                    cncState.state = CNCState::IDLE;
                    homingState.stage = HomingState::HomingStage::IDLE;
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Bazowanie zakończone pomyślnie");
                    #endif
                }
                else if (homingState.stage == HomingState::HomingStage::ERROR) {
                    cncState.state = CNCState::ERROR;
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("HOME ERROR: %s\n", homingState.errorMessage.c_str());
                    #endif
                }
                break;

            case CNCState::STOPPED:
            case CNCState::ERROR:
                cncState.hotWireOn = false;
                // Zatrzymaj silniki i wyczyść pozycje docelowe
                stepperX.stop();
                stepperY.stop();
                stepperX.setCurrentPosition(stepperX.currentPosition());
                stepperY.setCurrentPosition(stepperY.currentPosition());

                break;
        }

        // Serial.printf("TIME: %lu ms\t",
        //     currentTime);
        // Serial.printf("  Max Speed: %.2f \t",
        //     stepperX.maxSpeed());
        // Serial.printf("  Acceleration: %.2f \t",
        //     stepperX.acceleration());
        // Serial.printf("  Current Position: %ld \t",
        //     stepperX.currentPosition());
        // Serial.printf("  Target Position: %ld \t",
        //     stepperX.targetPosition());
        // Serial.printf("  Distance to go: %ld \t",
        //     stepperX.distanceToGo());
        // Serial.printf("  Is Running: %s\n", stepperX.isRunning() ? "YES" : "NO");

        updateIO(cncState, config);
        vTaskDelay(pdMS_TO_TICKS(1));
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

    MachineState receivedState {};
    TickType_t lastStatusUpdateTime { 0 };
    TickType_t lastDebugTime { 0 };
    TickType_t lastWiFiCheckTime { 0 };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(500) };
    const TickType_t debugUpdateInterval { pdMS_TO_TICKS(1000) };
    const TickType_t wifiCheckInterval { pdMS_TO_TICKS(20000) };

    bool wifiReconnectInProgress { false };

    if (!systemInitialized) {
        ESP.restart(); // Restart systemu, jeśli inicjalizacja nie powiodła się
    }

    while (true) {

        TickType_t currentTime { xTaskGetTickCount() };

        // SPRAWDZANIE I PONOWNE ŁĄCZENIE WiFi
        if ((currentTime - lastWiFiCheckTime) >= wifiCheckInterval) {

            // Sprawdź status WiFi
            if (WiFi.status() != WL_CONNECTED) {
                if (!wifiReconnectInProgress) {
                    #ifdef DEBUG_CONTROL_TASK
                    Serial.println("WiFi CONNECTION: Utracono połączenie, próba ponownego łączenia...");
                    #endif
                    wifiReconnectInProgress = true;
                }

                // Próbuj ponownie połączyć
                WiFiManagerStatus reconnectStatus = wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, 10000); // 10 sekund timeout

                if (reconnectStatus == WiFiManagerStatus::OK) {
                    #ifdef DEBUG_CONTROL_TASK
                    Serial.println("WiFi CONNECTION: Ponowne połączenie udane!");
                    Serial.printf("WiFi CONNECTION: IP: %s\n", WiFi.localIP().toString().c_str());
                    #endif
                }
                else {
                    #ifdef DEBUG_CONTROL_TASK
                    Serial.printf("WiFi CONNECTION ERROR: Nie udało się połączyć ponownie, status: %d\n",
                        static_cast<int>(reconnectStatus));
                    #endif
                }
            }
            else {
                // WiFi połączone - resetuj flagę jeśli była aktywna
                if (wifiReconnectInProgress) {
                    wifiReconnectInProgress = false;
                    #ifdef DEBUG_CONTROL_TASK
                    Serial.println("WiFi CONNECTION: Połączenie stabilne");
                    #endif
                }
            }

            lastWiFiCheckTime = currentTime;
        }

        // AKTUALIZACJA STATUSU MASZYNY (tylko jeśli WiFi działa)
        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {
            if (!webServerManager->isBusy() && WiFi.status() == WL_CONNECTED && !wifiReconnectInProgress) {
                if (xQueuePeek(stateQueue, &receivedState, 0) == pdTRUE)
                    webServerManager->broadcastMachineStatus(receivedState);
                // #ifdef DEBUG_CONTROL_TASK
                // Serial.printf("DEBUG CONTROL: Wysłano status maszyny: X=%.2f, Y=%.2f, State=%d\n",
                //     receivedState.currentX, receivedState.currentY, static_cast<int>(receivedState.state));
                // #endif
            }
            lastStatusUpdateTime = currentTime;
        }

        #ifdef DEBUG_CONTROL_TASK
        // W taskControl, co jakiś czas:
        if ((currentTime - lastDebugTime) >= debugUpdateInterval) {
            Serial.printf("Free heap: %d bytes, Min free: %d bytes\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
            lastDebugTime = currentTime;
        }
        #endif
        // Krótkie opóźnienie dla oszczędzania energii i zapobiegania watchdog timeouts
        vTaskDelay(pdMS_TO_TICKS(wifiReconnectInProgress ? 100 : 20));
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
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji FSManager...\n");
        #endif
        FSManagerStatus fsManagerStatus { fsManager->init() };
        if (fsManagerStatus != FSManagerStatus::OK) {
            #ifdef DEBUG_CONTROL_TASK
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować FSManager: %d\n", static_cast<int>(fsManagerStatus));
            #endif
            return false;
        }
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM ERROR: FSManager jest pustym wskaźnikiem\n");
        #endif
        return false;
    }

    if (sdManager != nullptr) {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji SDCardManager...\n");
        #endif
        SDManagerStatus sdManagerStatus { sdManager->init() };
        if (sdManagerStatus != SDManagerStatus::OK) {
            #ifdef DEBUG_CONTROL_TASK
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować SDCardManager: %d\n", static_cast<int>(sdManagerStatus));
            #endif
            return false;
        }
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM ERROR: SDCardManager jest pustym wskaźnikiem\n");
        #endif
        return false;
    }

    if (configManager != nullptr) {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji ConfigManager...\n");
        #endif
        ConfigManagerStatus configManagerStatus { configManager->init() };
        if (configManagerStatus != ConfigManagerStatus::OK) {
            #ifdef DEBUG_CONTROL_TASK
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować ConfigManager: %d\n", static_cast<int>(configManagerStatus));
            #endif
            return false;
        }
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM ERROR: ConfigManager jest pustym wskaźnikiem\n");
        #endif
        return false;
    }

    if (wifiManager != nullptr) {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji WiFiManager...\n");
        #endif
        WiFiManagerStatus wifiManagerStatus { wifiManager->init() };
        if (wifiManagerStatus != WiFiManagerStatus::OK) {
            #ifdef DEBUG_CONTROL_TASK
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować WiFiManager: %d\n", static_cast<int>(wifiManagerStatus));
            #endif
            return false;
        }
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM ERROR: WiFiManager jest pustym wskaźnikiem\n");
        #endif
        return false;
    }

    if (webServerManager != nullptr) {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM STATUS: Próba inicjalizacji WebServerManager...\n");
        #endif
        WebServerStatus webServerStatus { webServerManager->init() };
        if (webServerStatus != WebServerStatus::OK) {
            #ifdef DEBUG_CONTROL_TASK
            Serial.printf("SYSTEM ERROR: Nie można zainicjalizować WebServerManager: %d\n", static_cast<int>(webServerStatus));
            #endif
            return false;
        }
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.printf("SYSTEM ERROR: WebServerManager jest pustym wskaźnikiem\n");
        #endif
        return false;
    }
    return true;
}

// Nawiązuje połączenie WiFi wykorzystując dane z credentials.h
bool connectToWiFi(WiFiManager* wifiManager) {
    WiFiManagerStatus wifiManagerStatus { wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, CONFIG::MAX_CONNECTION_TIME) };
    if (wifiManagerStatus == WiFiManagerStatus::OK) {
        #ifdef DEBUG_CONTROL_TASK
        Serial.println("STATUS: Connected to WiFi.");
        #endif
        return true;
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.println("ERROR: Failed to connect to WiFi.");
        #endif
        return false;
    }
}

// Uruchamia serwer WWW do sterowania i monitorowania maszyny
bool startWebServer(WebServerManager* webServerManager) {
    WebServerStatus webServerStatus = webServerManager->begin();
    if (webServerStatus == WebServerStatus::OK) {
        #ifdef DEBUG_CONTROL_TASK
        Serial.println("STATUS: Web server started.");
        #endif
        return true;
    }
    else {
        #ifdef DEBUG_CONTROL_TASK
        Serial.println("ERROR: Web server failed to start.");
        #endif
        return false;
    }
}

// IO

// Aktualizuje stan wyjść fizycznych na podstawie stanu maszyny
void updateIO(MachineState& CNCState, const MachineConfig& config) {

    digitalWrite(PINCONFIG::WIRE_RELAY_PIN, CNCState.hotWireOn ? HIGH : LOW);
    digitalWrite(PINCONFIG::FAN_RELAY_PIN, CNCState.fanOn ? HIGH : LOW);
    ledcWrite(PINCONFIG::WIRE_PWM_CHANNEL, CNCState.hotWirePower);
    ledcWrite(PINCONFIG::FAN_PWM_CHANNEL, CNCState.fanPower);

    if (config.deactivateLimitSwitches) {
        CNCState.limitXOn = false;
        CNCState.limitYOn = false;
    }
    else {
        CNCState.limitXOn = digitalRead(PINCONFIG::LIMIT_X_PIN);
        CNCState.limitYOn = digitalRead(PINCONFIG::LIMIT_Y_PIN);
    }

    if (config.deactivateESTOP) {
        CNCState.estopOn = false;
    }
    else {
        CNCState.estopOn = digitalRead(PINCONFIG::ESTOP_PIN);
    }

}


/**
 * Aktualizacja dynamiki silnika krokowego (dane z konfiguracji)
 * @param axis 'X' lub 'Y'
 * @param useRapid true = rapid (G0), false = work (G1)
 */
bool updateMotorSpeed(const char axis, const bool useRapid, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config) {

    float stepsPerMM {};
    float feedRate {};      // mm/s
    float acceleration {};  // mm/s²
    AccelStepper* stepper = nullptr;

    switch (axis) {
        case 'X':
            stepsPerMM = config.X.stepsPerMM;
            feedRate = useRapid ? config.X.rapidFeedRate : config.X.workFeedRate;
            acceleration = useRapid ? config.X.rapidAcceleration : config.X.workAcceleration;
            stepper = &stepperX;
            break;
        case 'Y':
            stepsPerMM = config.Y.stepsPerMM;
            feedRate = useRapid ? config.Y.rapidFeedRate : config.Y.workFeedRate;
            acceleration = useRapid ? config.Y.rapidAcceleration : config.Y.workAcceleration;
            stepper = &stepperY;
            break;
        default:
            return false; // Nieprawidłowa oś
    }

    // Walidacja wartości
    if (stepsPerMM <= 0 || feedRate <= 0 || acceleration <= 0) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG MOTOR ERROR: Nieprawidłowe parametry dla osi %c\n", axis);
        #endif
        return false;
    }

    // feedRate: steps/s
    // acceleration: steps/s²
    float speedStepsPerSec = feedRate;
    float accelStepsPerSecSq = acceleration;

    stepper->setMaxSpeed(speedStepsPerSec);
    stepper->setAcceleration(accelStepsPerSecSq);

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG MOTOR: Oś %c - Prędkość: %.3f mm/s (%.1f steps/s), Akceleracja: %.3f mm/s² (%.1f steps/s²)\n",
        axis, feedRate, speedStepsPerSec, acceleration, accelStepsPerSecSq);
    #endif

    return true;
}

/**
 * Aktualizacja dynamiki silnika krokowego (dane z G-code)
 * @param axis 'X' lub 'Y'
 * @param feedRate Prędkość w mm/s (z G-code)
 * @param accelMultiplier Mnożnik akceleracji (0.5 = połowa prędkości)
 */
bool updateMotorSpeed(const char axis, const float feedRate, const float accelMultiplier, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config) {
    float stepsPerMM {};
    AccelStepper* stepper = nullptr;

    switch (axis) {
        case 'X':
            stepsPerMM = config.X.stepsPerMM;
            stepper = &stepperX;
            break;
        case 'Y':
            stepsPerMM = config.Y.stepsPerMM;
            stepper = &stepperY;
            break;
        default:
            return false; // Nieprawidłowa oś
    }

    // Walidacja wartości
    if (stepsPerMM <= 0 || feedRate <= 0 || accelMultiplier <= 0) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG MOTOR ERROR: Nieprawidłowe parametry dla osi %c (feedRate: %.3f, accelMult: %.2f)\n",
            axis, feedRate, accelMultiplier);
        #endif
        return false;
    }

    // feedRate: mm/s → steps/s
    // acceleration: (mm/s * multiplier) → mm/s² → steps/s²
    float speedStepsPerSec = feedRate * stepsPerMM;
    float accelStepsPerSecSq = (feedRate * accelMultiplier) * stepsPerMM;

    stepper->setMaxSpeed(speedStepsPerSec);
    stepper->setAcceleration(accelStepsPerSecSq);

    return true;
}

float getParameter(const String& line, char param) {
    int index { line.indexOf(param) };
    if (index == -1 || index >= line.length() - 1) return NAN;

    char nextChar { line[index + 1] };
    if (!isdigit(nextChar) && nextChar != '-' && nextChar != '.' && nextChar != '+') {
        return NAN;
    }

    int valueStart { index + 1 };
    int valueEnd { static_cast<int>(line.length()) };

    // Szukaj końca liczby (spacja, tab, koniec linii)
    for (int i { valueStart }; i < line.length(); ++i) {
        char c { line[i] };
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            valueEnd = i;
            break;
        }
    }

    String valueStr { line.substring(valueStart, valueEnd) };
    valueStr.trim();

    if (valueStr.length() == 0) return NAN;

    return valueStr.toFloat();
}

bool initializeGCodeProcessing(MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config) {
    // Pobierz nazwę wybranego projektu z SDManagera
    std::string filename {};
    SDManagerStatus status = sdManager->getSelectedProject(filename);

    if (status != SDManagerStatus::OK || filename.empty()) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC ERROR: Brak wybranego pliku projektu lub błąd SDManager.");
        #endif
        return false;
    }

    // Walidacja nazwy pliku
    if (filename.length() >= sizeof(cncState.currentProject)) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC ERROR: Nazwa pliku za długa: %s\n", filename.c_str());
        #endif
        return false;
    }
    // Zamknij poprzedni plik, jeśli był otwarty
    if (gCodeState.fileOpen && gCodeState.currentFile) {
        if (!sdManager->takeSD()) {
            #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Nie można zablokować SD do zamknięcia pliku");
            #endif
            return false;
        }
        gCodeState.currentFile.close();
        sdManager->giveSD();
        gCodeState.fileOpen = false;
    }

    // Resetuj flagi i stan przetwarzania
    gCodeState.lineNumber = 0;
    gCodeState.totalLines = 0;
    gCodeState.stopRequested = false;
    gCodeState.pauseRequested = false;
    gCodeState.stage = GCodeProcessingState::ProcessingStage::INITIALIZING;
    gCodeState.movementInProgress = false;
    gCodeState.errorMessage = "";
    gCodeState.currentLine = "";
    gCodeState.heatingStartTime = 0;
    gCodeState.heatingDuration = config.delayAfterStartup;

    // Próba otwarcia pliku z retry
    constexpr int MAX_NUM_OF_TRIES = 3;
    for (int i { 0 }; i < MAX_NUM_OF_TRIES; ++i) {
        if (!sdManager->takeSD()) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Odczekaj przed ponowną próbą
            continue;
        }

        std::string filePath = CONFIG::PROJECTS_DIR + filename;
        gCodeState.currentFile = SD.open(filePath.c_str());

        if (gCodeState.currentFile) {

            // Oszaconowano 24 bajtów na linię
            // plik 139 648 bajtów ma 5727 linii -> 139648 / 5727 = 24.4
            // plik 289 824 bajtów ma 11854 linii -> 289824 / 11854 = 24.4
            // Uproszczono ze względu na niepotrzebne komplikowanie obliczeń
            gCodeState.totalLines = gCodeState.currentFile.size() / 24;

            // Przewiń na początek pliku
            gCodeState.currentFile.seek(0);

            gCodeState.fileOpen = true;

            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: Otwarto plik %s, liczba linii: %lu\n",
                filePath.c_str(), gCodeState.totalLines);
            #endif

            sdManager->giveSD();
            break; // Sukces!
        }
        else {
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC ERROR: Próba %d - Nie udało się otworzyć pliku %s\n",
                i + 1, filePath.c_str());
            #endif
            sdManager->giveSD();

            if (i == MAX_NUM_OF_TRIES - 1) {
                return false; // Ostatnia próba nieudana
            }
            vTaskDelay(pdMS_TO_TICKS(200)); // Odczekaj przed kolejną próbą
        }
    }


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

void processGCode(MachineState& cncState, GCodeProcessingState& gCodeState, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {

    // SPRAWDZENIE BEZPIECZEŃSTWA - krańcówki i ESTOP
    if (cncState.estopOn || cncState.limitXOn || cncState.limitYOn) {
        // Natychmiastowe zatrzymanie
        stepperX.stop();
        stepperY.stop();
        stepperX.setCurrentPosition(stepperX.currentPosition());
        stepperY.setCurrentPosition(stepperY.currentPosition());
        cncState.hotWireOn = false;
        cncState.fanOn = false;
        gCodeState.stage = GCodeProcessingState::ProcessingStage::ERROR;
        gCodeState.errorMessage = cncState.estopOn ? "ESTOP" : "Limit";

        // Zamknij plik
        if (gCodeState.fileOpen && gCodeState.currentFile) {
            if (sdManager->takeSD()) {
                gCodeState.currentFile.close();
                gCodeState.fileOpen = false;
                sdManager->giveSD();
            }
        }
        return;
    }

    // MASZYNA STANOWA G-CODE
    switch (gCodeState.stage) {

        case GCodeProcessingState::ProcessingStage::INITIALIZING:
            // Włącz drut i wentylator
            cncState.hotWireOn = true;
            cncState.fanOn = true;
            cncState.hotWirePower = config.hotWirePower;
            cncState.fanPower = config.fanPower;

            gCodeState.heatingStartTime = millis();
            gCodeState.stage = GCodeProcessingState::ProcessingStage::HEATING;

            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG G-CODE: Rozpoczęcie nagrzewania na %lu ms\n", gCodeState.heatingDuration);
            #endif
            break;

        case GCodeProcessingState::ProcessingStage::HEATING:
            // Czekaj na nagrzanie drutu
            if ((millis() - gCodeState.heatingStartTime) >= gCodeState.heatingDuration) {
                gCodeState.stage = GCodeProcessingState::ProcessingStage::MOVING_TO_OFFSET;

                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG G-CODE: Nagrzewanie zakończone, przejazd do offsetu");
                #endif
            }
            break;

        case GCodeProcessingState::ProcessingStage::MOVING_TO_OFFSET: {

                long targetXSteps = config.X.offset * config.X.stepsPerMM;
                long targetYSteps = config.Y.offset * config.Y.stepsPerMM;

                // Jeśli ruch już trwa, czekaj na zakończenie
                if (gCodeState.movementInProgress) {
                    if (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                        return; // Czekaj na zakończenie ruchu
                    }
                    // Ruch zakończony
                    gCodeState.movementInProgress = false;
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;

                    // Ustaw domyślną prędkość roboczą jeśli nie używamy F z G-code
                    if (!config.useGCodeFeedRate) {
                        updateMotorSpeed('X', false, multiStepper, stepperX, stepperY, config); // false = work speed
                        updateMotorSpeed('Y', false, multiStepper, stepperX, stepperY, config);
                    }

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG G-CODE: Dotarcie do offsetu, rozpoczęcie przetwarzania pliku");
                    #endif
                    return;
                }

                // Jeśli już jesteśmy w pozycji offsetu, przejdź od razu dalej
                if (stepperX.currentPosition() == targetXSteps && stepperY.currentPosition() == targetYSteps) {
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;

                    // Ustaw domyślną prędkość roboczą jeśli nie używamy F z G-code
                    if (!config.useGCodeFeedRate) {
                        updateMotorSpeed('X', false, multiStepper, stepperX, stepperY, config); // false = work speed
                        updateMotorSpeed('Y', false, multiStepper, stepperX, stepperY, config);
                        #ifdef DEBUG_CNC_TASK
                        Serial.println("DEBUG G-CODE: Ustawiono domyślną prędkość roboczą");
                        #endif
                    }

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG G-CODE: Już w pozycji offsetu, rozpoczęcie przetwarzania pliku");
                    #endif
                    return;
                }

                // Rozpocznij ruch do offsetu
                updateMotorSpeed('X', true, multiStepper, stepperX, stepperY, config); // rapid dla offsetu
                updateMotorSpeed('Y', true, multiStepper, stepperX, stepperY, config);

                // Przygotuj pozycje docelowe dla MultiStepper
                long positions[2];
                positions[0] = targetXSteps;
                positions[1] = targetYSteps;
                multiStepper.moveTo(positions);
                gCodeState.movementInProgress = true;

                return;
                break;
            }

        case GCodeProcessingState::ProcessingStage::READING_FILE:
            // Sprawdź czy plik jest dostępny
            if (!gCodeState.fileOpen || !gCodeState.currentFile) {
                gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
                return;
            }

            // Sprawdź czy poprzedni ruch się skończył
            if (gCodeState.movementInProgress) {
                if (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                    return; // Czekaj na zakończenie ruchu
                }
                gCodeState.movementInProgress = false;
            }

            // Pobierz SD i czytaj linię
            if (!sdManager->takeSD()) {
                return; // Spróbuj ponownie w następnym cyklu
            }

            if (gCodeState.currentFile.available()) {
                gCodeState.currentLine = gCodeState.currentFile.readStringUntil('\n');
                gCodeState.lineNumber++;
                sdManager->giveSD();
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG G-CODE: Odczytano linię %lu: %s\n", gCodeState.lineNumber, gCodeState.currentLine.c_str());
                #endif

                gCodeState.stage = GCodeProcessingState::ProcessingStage::PROCESSING_LINE;
            }
            else {
                // Koniec pliku
                sdManager->giveSD();
                gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
            }
            break;

        case GCodeProcessingState::ProcessingStage::PROCESSING_LINE:
            // Parsuj i przygotuj ruch
            if (processGCodeLine(gCodeState.currentLine, multiStepper, stepperX, stepperY, cncState, gCodeState, config)) {
                gCodeState.stage = GCodeProcessingState::ProcessingStage::EXECUTING_MOVEMENT;
            }
            else {
                // Przejdź do następnej linii (komentarz, pusta linia, etc.)
                gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
            }
            break;

        case GCodeProcessingState::ProcessingStage::EXECUTING_MOVEMENT:
            // Ruch został już przygotowany w PROCESSING_LINE
            gCodeState.movementInProgress = true;
            gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
            break;

        case GCodeProcessingState::ProcessingStage::FINISHED:
            // Sprawdź czy ruch powrotny się skończył
            if (gCodeState.movementInProgress) {
                if (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                    return; // Czekaj na zakończenie ruchu powrotnego
                }
                gCodeState.movementInProgress = false;

                // Wyłącz drut i wentylator
                cncState.hotWireOn = false;
                cncState.fanOn = false;

                // Zamknij plik
                if (gCodeState.fileOpen && gCodeState.currentFile) {
                    if (sdManager->takeSD()) {
                        gCodeState.currentFile.close();
                        gCodeState.fileOpen = false;
                        sdManager->giveSD();
                    }
                }

                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG G-CODE: Przetwarzanie G-code zakończone");
                #endif
                return;
            }

            // Rozpocznij ruch powrotny do pozycji przed offsetem (0,0)
            updateMotorSpeed('X', true, multiStepper, stepperX, stepperY, config); // true = rapid
            updateMotorSpeed('Y', true, multiStepper, stepperX, stepperY, config);

            // Przygotuj pozycje docelowe dla MultiStepper (powrót do 0,0)
            long positions[2];
            positions[0] = 0;
            positions[1] = 0;
            multiStepper.moveTo(positions);
            gCodeState.movementInProgress = true;

            break;

        case GCodeProcessingState::ProcessingStage::ERROR:
            // Stan błędu - nie rób nic, czekaj na interwencję operatora
            return;

        default:
            gCodeState.stage = GCodeProcessingState::ProcessingStage::ERROR;
            gCodeState.errorMessage = "Unknown processing stage";
            break;

    }
}
bool processGCodeLine(String line, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config) {
    line.trim();

    // Skip puste linie i komentarze
    if (line.length() == 0 || line.startsWith(";") || line.startsWith("(")) {
        return false; // Przejdź do następnej linii
    }

    // Konwertuj na wielkie litery dla łatwiejszego parsowania
    line.toUpperCase();

    // F-CODE - Ustawienie prędkości dla kolejnych ruchów (tylko jeśli useGcodeFeedrate = true)
    if (line.startsWith("F")) {
        if (config.useGCodeFeedRate) {
            float feedRate = getParameter(line, 'F');
            if (!isnan(feedRate)) {
                // Zamień mm/min na mm/s jeśli F jest w mm/min
                feedRate = feedRate / 60.0f;
                // Ustaw prędkość dla obu osi z akceleracją = połowa prędkości
                updateMotorSpeed('X', feedRate, 0.5f, multiStepper, stepperX, stepperY, config);
                updateMotorSpeed('Y', feedRate, 0.5f, multiStepper, stepperX, stepperY, config);
                gCodeState.currentFeedRate = feedRate; // Zapamiętaj aktualną prędkość w mm/s
            }
        }
        return false; // Nie ma ruchu, przejdź do następnej linii
    }

    // G1 - Linear move (ruch roboczy)
    else if (line.startsWith("G1")) {
        return processLinearMove(line, multiStepper, stepperX, stepperY, cncState, gCodeState, config, false);
    }

    // G0 - Rapid move (ruch szybki)
    else if (line.startsWith("G0")) {
        return processLinearMove(line, multiStepper, stepperX, stepperY, cncState, gCodeState, config, true);
    }

    // G90
    else if (line.startsWith("G90")) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG G-CODE: G90 - Tryb bezwzględny");
        #endif
        cncState.relativeMode = false; // Ustaw tryb bezwzględny
        return false; // Nie ma ruchu, ale zmień stan
    }

    // G91
    else if (line.startsWith("G91")) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG G-CODE: G91 - Tryb inkrementalny");
        #endif
        cncState.relativeMode = true; // Ustaw tryb inkrementalny
        return false; // Nie ma ruchu, ale zmień stan
    }

    // M3 - Włącz silnik wrzeciona (jeśli jest)
    else if (line.startsWith("M3")) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG G-CODE: M3 ");
        #endif
        cncState.hotWireOn = true; // Włącz silnik wrzeciona
        cncState.fanOn = true; // Włącz wentylator
        return false; // Nie ma ruchu, ale zmień stan
    }
    // M5 - Wyłącz silnik wrzeciona (jeśli jest)
    else if (line.startsWith("M5")) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG G-CODE: M5 ");
        #endif
        cncState.hotWireOn = false; // Wyłącz silnik wrzeciona
        cncState.fanOn = false; // Wyłącz wentylator
        return false; // Nie ma ruchu, ale zmień stan
    }

    // M30 - Koniec programu
    else if (line.startsWith("M30")) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG G-CODE: M30 - Koniec programu");
        #endif
        gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
        return false; // Nie ma ruchu, ale zmień stan
    }

    // Nieznana komenda - ignoruj
    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG G-CODE: Nieznana komenda: %s\n", line.c_str());
    #endif
    return false;
}

bool processLinearMove(const String& line, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config, bool isRapid) {
    float xPos { getParameter(line, 'X') };
    float yPos { getParameter(line, 'Y') };
    float feedRate { getParameter(line, 'F') };

    bool hasMovement { false };
    bool speedChanged { false };

    // Ustaw prędkość tylko jeśli jest F w linii i useGcodeFeedrate = true
    if (!isnan(feedRate) && config.useGCodeFeedRate) {
        // Zamień mm/min na mm/s jeśli F jest w mm/min
        feedRate = feedRate / 60.0f;
        // Prędkość z G-code, akceleracja = połowa prędkości
        updateMotorSpeed('X', feedRate, 0.5f, multiStepper, stepperX, stepperY, config);
        updateMotorSpeed('Y', feedRate, 0.5f, multiStepper, stepperX, stepperY, config);
        gCodeState.currentFeedRate = feedRate;
        speedChanged = true;
    }

    // Nie ustawiaj prędkości ponownie jeśli nie ma parametru F lub useGcodeFeedrate = false
    // Prędkość została już ustawiona podczas inicjalizacji lub przy poprzednim F-kodzie

    if (speedChanged) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG G-CODE: Zaktualizowano prędkość\n");
        #endif
    }

    // Przygotuj pozycje docelowe dla synchronizacji
    long positions[2];
    positions[0] = stepperX.currentPosition(); // Domyślnie - brak ruchu
    positions[1] = stepperY.currentPosition(); // Domyślnie - brak ruchu

    // Ruch w X
    if (!isnan(xPos)) {
        float targetX;
        if (cncState.relativeMode) {
            // Tryb relatywny - dodaj do aktualnej pozycji
            targetX = cncState.currentX + xPos;
        }
        else {
            // Tryb absolutny - pozycja bezwzględna
            targetX = xPos;
        }

        // Dodaj offset i konwertuj na kroki
        float targetXWithOffset = targetX + config.X.offset;
        positions[0] = targetXWithOffset * config.X.stepsPerMM;
        hasMovement = true;
    }

    // Ruch w Y
    if (!isnan(yPos)) {
        float targetY;
        if (cncState.relativeMode) {
            // Tryb relatywny - dodaj do aktualnej pozycji
            targetY = cncState.currentY + yPos;
        }
        else {
            // Tryb absolutny - pozycja bezwzględna
            targetY = yPos;
        }

        // Dodaj offset i konwertuj na kroki
        float targetYWithOffset = targetY + config.Y.offset;
        positions[1] = targetYWithOffset * config.Y.stepsPerMM;
        hasMovement = true;
    }

    // Wykonaj synchronizowany ruch tylko jeśli jest jakiś ruch
    if (hasMovement) {
        multiStepper.moveTo(positions);
    }

    return hasMovement;
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- HOMING FUNCTIONALITY ---
* ------------------------------------------------------------------------------------------------------------
*/

void processHoming(MachineState& cncState, HomingState& homingState, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {

    // SPRAWDZENIE BEZPIECZEŃSTWA - ESTOP
    if (cncState.estopOn) {
        stepperX.stop();
        stepperY.stop();
        stepperX.setCurrentPosition(stepperX.currentPosition());
        stepperY.setCurrentPosition(stepperY.currentPosition());
        homingState.stage = HomingState::HomingStage::ERROR;
        homingState.errorMessage = "ESTOP active during homing";
        return;
    }

    switch (homingState.stage) {

        case HomingState::HomingStage::HOMING_X: {

                if (!homingState.movementInProgress) {
                    // Rozpocznij ruch w kierunku krańcówki X (kierunek ujemny)
                    // Ustaw prędkość bazowania (niższa niż normalna)
                    float homingSpeedSteps = homingState.homingSpeed * config.X.stepsPerMM;
                    float homingAccelSteps = homingState.homingAcceleration * config.X.stepsPerMM;

                    stepperX.setMaxSpeed(homingSpeedSteps);
                    stepperX.setAcceleration(homingAccelSteps);

                    // Ruch w kierunku ujemnym do krańcówki (daleko - 1000mm)
                    stepperX.move(-1000 * config.X.stepsPerMM);
                    homingState.movementInProgress = true;
                    homingState.limitReached = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Rozpoczęcie bazowania osi X");
                    #endif
                }

                // Sprawdź czy osiągnięto krańcówkę X
                if (cncState.limitXOn && !homingState.limitReached) {
                    // Krańcówka osiągnięta - zatrzymaj ruch
                    stepperX.stop();
                    homingState.limitReached = true;
                    homingState.movementInProgress = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Krańcówka X osiągnięta");
                    #endif

                    // Wycofaj się o małą odległość
                    long backoffSteps = homingState.backoffDistance * config.X.stepsPerMM;
                    stepperX.move(backoffSteps);
                    homingState.backoffComplete = false;
                }

                // Sprawdź czy wycofanie zakończone
                if (homingState.limitReached && stepperX.distanceToGo() == 0 && !homingState.backoffComplete) {
                    // Ustaw pozycję zerową i przejdź do bazowania Y
                    stepperX.setCurrentPosition(0);
                    cncState.currentX = 0.0f;
                    homingState.backoffComplete = true;
                    homingState.stage = HomingState::HomingStage::HOMING_Y;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Oś X zbazowana, rozpoczęcie bazowania osi Y");
                    #endif
                }

                // Sprawdź timeout (zabezpieczenie przed nieskończonym ruchem)
                if (homingState.movementInProgress && stepperX.distanceToGo() == 0 && !cncState.limitXOn) {
                    stepperX.stop();
                    stepperY.stop();
                    stepperX.setCurrentPosition(stepperX.currentPosition());
                    stepperY.setCurrentPosition(stepperY.currentPosition());
                    homingState.stage = HomingState::HomingStage::ERROR;
                    homingState.errorMessage = "X limit switch not reached - check wiring";
                    return;
                }

                break;
            }

        case HomingState::HomingStage::HOMING_Y: {

                if (!homingState.movementInProgress) {
                    // Rozpocznij ruch w kierunku krańcówki Y (kierunek ujemny)
                    float homingSpeedSteps = homingState.homingSpeed * config.Y.stepsPerMM;
                    float homingAccelSteps = homingState.homingAcceleration * config.Y.stepsPerMM;

                    stepperY.setMaxSpeed(homingSpeedSteps);
                    stepperY.setAcceleration(homingAccelSteps);

                    // Ruch w kierunku ujemnym do krańcówki (daleko - 1000mm)
                    stepperY.move(-1000 * config.Y.stepsPerMM);
                    homingState.movementInProgress = true;
                    homingState.limitReached = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Rozpoczęcie bazowania osi Y");
                    #endif
                }

                // Sprawdź czy osiągnięto krańcówkę Y
                if (cncState.limitYOn && !homingState.limitReached) {
                    // Krańcówka osiągnięta - zatrzymaj ruch
                    stepperY.stop();
                    homingState.limitReached = true;
                    homingState.movementInProgress = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Krańcówka Y osiągnięta");
                    #endif

                    // Wycofaj się o małą odległość
                    long backoffSteps = homingState.backoffDistance * config.Y.stepsPerMM;
                    stepperY.move(backoffSteps);
                    homingState.backoffComplete = false;
                }

                // Sprawdź czy wycofanie zakończone
                if (homingState.limitReached && stepperY.distanceToGo() == 0 && !homingState.backoffComplete) {
                    // Ustaw pozycję zerową i zakończ bazowanie
                    stepperY.setCurrentPosition(0);
                    cncState.currentY = 0.0f;
                    homingState.backoffComplete = true;
                    homingState.stage = HomingState::HomingStage::FINISHED;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Oś Y zbazowana, bazowanie zakończone");
                    #endif
                }

                // Sprawdź timeout (zabezpieczenie przed nieskończonym ruchem)
                if (homingState.movementInProgress && stepperY.distanceToGo() == 0 && !cncState.limitYOn) {
                    stepperX.stop();
                    stepperY.stop();
                    stepperX.setCurrentPosition(stepperX.currentPosition());
                    stepperY.setCurrentPosition(stepperY.currentPosition());
                    homingState.stage = HomingState::HomingStage::ERROR;
                    homingState.errorMessage = "Y limit switch not reached - check wiring";
                    return;
                }

                break;
            }

        case HomingState::HomingStage::FINISHED:
        case HomingState::HomingStage::ERROR:
        case HomingState::HomingStage::IDLE:
            // Nic nie rób - te stany są obsługiwane w głównej pętli
            break;
    }
}