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
* --- ZMIENNE GLOBALNE ---
* ------------------------------------------------------------------------------------------------------------
*/

// Menadżery systemu współdzielone między zadaniami
SDCardManager* sdManager {};
ConfigManager* configManager {};

// Kolejki FreeRTOS do komunikacji między zadaniami
QueueHandle_t stateQueue {};   // Przekazywanie stanu maszyny z zadania CNC do Control
QueueHandle_t commandQueue {}; // Przekazywanie komend z zadania Control do CNC

bool systemInitialized { false }; // Synchronizacja inicjalizacji między zadaniami

// Obsługa silników krokowych w przerwaniach
Ticker stepperTicker;
MultiStepper multiStepper;

// Procedura obsługi przerwania timera - wykonuje kroki silników
void IRAM_ATTR onStepperTimer() {
    multiStepper.run();
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- PROTOTYPY FUNKCJI ---
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
* --- GŁÓWNY PROGRAM KONTROLERA ---
* ------------------------------------------------------------------------------------------------------------
*/

void setup() {
    // ============================================================================
    // INICJALIZACJA KOMUNIKACJI I PROTOKOŁÓW
    #ifdef DEBUG
    Serial.begin(115200);
    #endif
    
    // Konfiguracja SPI dla komunikacji z kartą SD
    SPI.begin(PINCONFIG::SD_CLK_PIN, PINCONFIG::SD_MISO_PIN, PINCONFIG::SD_MOSI_PIN);
    SPI.setFrequency(25000000);

    // Oczekiwanie na stabilizację połączenia SPI
    delay(200);

    // ============================================================================
    // KONFIGURACJA PINÓW WEJŚĆ/WYJŚĆ
    pinMode(PINCONFIG::LIMIT_X_PIN, INPUT_PULLUP);
    pinMode(PINCONFIG::LIMIT_Y_PIN, INPUT_PULLUP);
    pinMode(PINCONFIG::ESTOP_PIN, INPUT_PULLUP);

    pinMode(PINCONFIG::WIRE_RELAY_PIN, OUTPUT);
    digitalWrite(PINCONFIG::WIRE_RELAY_PIN, LOW);

    pinMode(PINCONFIG::FAN_RELAY_PIN, OUTPUT);
    digitalWrite(PINCONFIG::FAN_RELAY_PIN, LOW);

    // Inicjalizacja kanałów PWM dla sterowania mocą drutu i wentylatora
    ledcSetup(PINCONFIG::WIRE_PWM_CHANNEL, PINCONFIG::PWM_FREQ, PINCONFIG::PWM_RESOLUTION);
    ledcAttachPin(PINCONFIG::WIRE_PWM_PIN, PINCONFIG::WIRE_PWM_CHANNEL);
    ledcWrite(PINCONFIG::WIRE_PWM_CHANNEL, 0);

    ledcSetup(PINCONFIG::FAN_PWM_CHANNEL, PINCONFIG::PWM_FREQ, PINCONFIG::PWM_RESOLUTION);
    ledcAttachPin(PINCONFIG::FAN_PWM_PIN, PINCONFIG::FAN_PWM_CHANNEL);
    ledcWrite(PINCONFIG::FAN_PWM_CHANNEL, 0);

    // ============================================================================
    // TWORZENIE MENADŻERÓW SYSTEMU
    sdManager = new SDCardManager();
    configManager = new ConfigManager(sdManager);

    // Utworzenie kolejek FreeRTOS do komunikacji między zadaniami
    stateQueue = xQueueCreate(1, sizeof(MachineState));
    commandQueue = xQueueCreate(5, sizeof(WebserverCommand));

    if (!stateQueue) {
        Serial.println("SYSTEM ERROR: stateQueue not created!");
    }
    if (!commandQueue) {
        Serial.println("SYSTEM ERROR: commandQueue not created!");
    }

    // Utworzenie zadań FreeRTOS na odpowiednich rdzeniach procesora
    Serial.println("Creating Control task...");
    xTaskCreatePinnedToCore(taskControl,    
        "Control",                          
        CONFIG::CONTROLTASK_STACK_SIZE,     
        NULL,                               
        CONFIG::CONTROLTASK_PRIORITY,       
        NULL,                               
        CONFIG::CORE_0                      
    );

    delay(200); // Oczekiwanie na stabilizację pierwszego zadania

    Serial.println("Creating CNC task...");
    xTaskCreatePinnedToCore(taskCNC,        
        "CNC",                              
        CONFIG::CNCTASK_STACK_SIZE,         
        NULL,                               
        CONFIG::CNCTASK_PRIORITY,           
        NULL,                               
        CONFIG::CORE_1                      
    );

    delay(200); // Oczekiwanie na stabilizację drugiego zadania
}

void loop() {
    // Pętla główna pozostaje pusta - cała logika jest w zadaniach FreeRTOS
}


/*
* ------------------------------------------------------------------------------------------------------------
* --- ZADANIA FREERTOS ---
* ------------------------------------------------------------------------------------------------------------
*/

// Zadanie obsługi fizycznego ruchu CNC i przetwarzania G-code
void taskCNC(void* parameter) {
    #ifdef DEBUG_CNC_TASK
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());
    #endif

    // Struktury danych reprezentujące stan systemu
    MachineState cncState {};
    GCodeProcessingState gCodeState {};
    HomingState homingState {};

    // Bufor na komendy odbierane z interfejsu web
    WebserverCommand commandData {};
    bool commandPending { false };

    // Zarządzanie czasem wykonywania operacji w zadaniu
    TickType_t lastCommandProcessTime { 0 };
    TickType_t lastStatusUpdateTime { 0 };
    const TickType_t commandProcessInterval { pdMS_TO_TICKS(500) };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(100) };

    // Inicjalizacja obiektów stepper motor
    AccelStepper stepperX(AccelStepper::DRIVER, PINCONFIG::STEP_X_PIN, PINCONFIG::DIR_X_PIN);
    AccelStepper stepperY(AccelStepper::DRIVER, PINCONFIG::STEP_Y_PIN, PINCONFIG::DIR_Y_PIN);

    // Rejestracja silników w globalnym koordynatorze ruchu
    multiStepper.addStepper(stepperX);
    multiStepper.addStepper(stepperY);

    // Oczekiwanie na zakończenie inicjalizacji systemu przez zadanie Control
    while (!systemInitialized) {
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Oczekiwanie na inicjalizację systemu");
        #endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Wczytanie parametrów konfiguracyjnych maszyny z karty SD
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
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (configStatus != ConfigManagerStatus::OK);

    // Wyzerowanie pozycji silników przy starcie systemu
    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);

    // Uruchomienie timera generującego impulsy krokowe w przerwaniach
    float timerIntervalSeconds = CONFIG::STEPPER_TIMER_FREQUENCY_US / 1000000.0f;
    stepperTicker.attach(timerIntervalSeconds, onStepperTimer);

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: Timer stepperów uruchomiony z częstotliwością %lu µs (%.6f s)\n",
        CONFIG::STEPPER_TIMER_FREQUENCY_US, timerIntervalSeconds);
    #endif

    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };

        // UWAGA: multiStepper.run() wykonywane jest w przerwaniu timera!
        // Przy zatrzymaniu maszyny należy wyczyścić cele ruchu silników
        if (cncState.state == CNCState::STOPPED || cncState.state == CNCState::ERROR) {
            stepperX.stop();
            stepperY.stop();
            stepperX.setCurrentPosition(stepperX.currentPosition());
            stepperY.setCurrentPosition(stepperY.currentPosition());
        }

        // Odbieranie komend z interfejsu web (z ograniczeniem częstotliwości)
        if ((currentTime - lastCommandProcessTime) >= commandProcessInterval) {
            if (xQueueReceive(commandQueue, &commandData, 0) == pdTRUE) {
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: Otrzymano komendę typu %d\n", static_cast<int>(commandData.type));
                #endif
                commandPending = true;
            }
            lastCommandProcessTime = currentTime;
        }

        // Wysyłanie aktualnego stanu maszyny do interfejsu web
        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {
            if (xQueueOverwrite(stateQueue, &cncState) != pdPASS) {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Nie można wysłać statusu maszyny.");
                #endif
            }
            lastStatusUpdateTime = currentTime;
        }

        // Obsługa komendy przeładowania konfiguracji
        if (commandPending && commandData.type == CommandType::RELOAD_CONFIG) {
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

        // Obsługa awaryjnego zatrzymania - ma priorytet nad wszystkimi innymi operacjami
        if (commandPending && commandData.type == CommandType::STOP) {
            // Natychmiastowe wyłączenie wszystkich urządzeń
            cncState.hotWireOn = false;
            cncState.fanOn = false;

            // Zatrzymanie ruchu i wyczyszczenie celów
            stepperX.stop();
            stepperY.stop();
            stepperX.setCurrentPosition(stepperX.currentPosition());
            stepperY.setCurrentPosition(stepperY.currentPosition());

            // Zamknięcie plików G-code przy awaryjnym zatrzymaniu
            if (gCodeState.fileOpen && gCodeState.currentFile) {
                if (sdManager->takeSD()) {
                    gCodeState.currentFile.close();
                    gCodeState.fileOpen = false;
                    sdManager->giveSD();
                }
            }

            // Resetowanie stanu przetwarzania G-code
            gCodeState.stopRequested = true;
            gCodeState.pauseRequested = false;
            gCodeState.stage = GCodeProcessingState::ProcessingStage::IDLE;

            // Logika przejść stanów przy zatrzymaniu/resecie
            if (cncState.state == CNCState::STOPPED || cncState.state == CNCState::ERROR) {
                // Reset z błędu lub zatrzymania powraca do bezczynności
                cncState.state = CNCState::IDLE;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: RESET z STOPPED/ERROR do IDLE");
                #endif
            }
            else {
                // Zatrzymanie z innych stanów przechodzi do stanu zatrzymania
                cncState.state = CNCState::STOPPED;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: STOP - przejście do STOPPED");
                #endif
            }

            commandPending = false;
        }

        // Obsługa komend sterowania urządzeniami wykonawczymi
        if (commandPending && commandData.type == CommandType::SET_HOTWIRE) {
            if (cncState.state != CNCState::STOPPED && cncState.state != CNCState::ERROR) {
                cncState.hotWireOn = (commandData.param1 > 0.5f);
                cncState.hotWirePower = config.hotWirePower;
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: STAN DRUTU %i \n", cncState.hotWireOn);
                #endif
            }
            commandPending = false;
        }

        if (commandPending && commandData.type == CommandType::SET_FAN) {
            cncState.fanOn = (commandData.param1 > 0.5f);
            cncState.fanPower = config.fanPower;
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: STAN WENTYLATORA %i \n", cncState.fanOn);
            #endif
            commandPending = false;
        }

        // Główna maszyna stanów systemu CNC
        switch (cncState.state) {
            case CNCState::IDLE:
                // Stan bezczynności - oczekiwanie na komendy użytkownika
                if (commandPending) {
                    commandPending = false;
                    switch (commandData.type) {
                        case CommandType::START:
                            // Inicjalizacja i rozpoczęcie wykonania programu G-code
                            if (initializeGCodeProcessing(cncState, gCodeState, config)) {
                                cncState.state = CNCState::RUNNING;
                            }
                            break;

                        case CommandType::HOME:
                            // Rozpoczęcie sekwencji bazowania maszyny
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

                        case CommandType::JOG: {
                            // Bezpośrednie wykonanie ruchu JOG bez zmiany stanu
                            float xOffset = commandData.param1;
                            float yOffset = commandData.param2;
                            float speedMode = commandData.param3; // 0.0 = praca, 1.0 = szybki

                            #ifdef DEBUG_CNC_TASK
                            Serial.printf("DEBUG JOG: X=%.2f, Y=%.2f, SpeedMode=%.1f\n",
                                xOffset, yOffset, speedMode);
                            #endif

                            // Sprawdzenie czy ruch jest możliwy (nie zero)
                            if (abs(xOffset) > 0.001f || abs(yOffset) > 0.001f) {
                                // Przejście do stanu JOG i zaplanowanie ruchu
                                cncState.state = CNCState::JOG;

                                // Wybór profilu prędkości na podstawie trybu
                                bool useRapid = (speedMode > 0.5f);
                                updateMotorSpeed('X', useRapid, multiStepper, stepperX, stepperY, config);
                                updateMotorSpeed('Y', useRapid, multiStepper, stepperX, stepperY, config);

                                // Konwersja przesunięć z mm na kroki
                                long stepsX = static_cast<long>(xOffset * config.X.stepsPerMM);
                                long stepsY = static_cast<long>(yOffset * config.Y.stepsPerMM);

                                // Przygotowanie synchronizowanego ruchu obu osi
                                long positions[2];
                                positions[0] = stepperX.currentPosition() + stepsX;
                                positions[1] = stepperY.currentPosition() + stepsY;

                                multiStepper.moveTo(positions);

                                // Aktualizacja logicznej pozycji w układzie współrzędnych
                                cncState.currentX += xOffset;
                                cncState.currentY += yOffset;

                                #ifdef DEBUG_CNC_TASK
                                Serial.printf("DEBUG JOG: Zaplanowano ruch do pozycji X=%.2f, Y=%.2f\n",
                                    cncState.currentX, cncState.currentY);
                                #endif
                            }
                            break;
                        }

                        case CommandType::ZERO:
                            // Ustawienie aktualnej pozycji jako punkt zerowy
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
                // Aktualizacja pozycji na podstawie rzeczywistego położenia silników
                cncState.currentX = stepperX.currentPosition() / config.X.stepsPerMM;
                cncState.currentY = stepperY.currentPosition() / config.Y.stepsPerMM;
                
                // Obsługa komend podczas wykonywania programu
                if (commandPending) {
                    commandPending = false;
                    switch (commandData.type) {
                        case CommandType::PAUSE:
                            cncState.isPaused = !cncState.isPaused;
                            break;
                            // CommandType::STOP obsługiwane jest globalnie
                        default:
                            break;
                    }
                }

                if (!cncState.isPaused) {
                    processGCode(cncState, gCodeState, multiStepper, stepperX, stepperY, config);

                    // Kalkulacja postępu wykonania zadania
                    cncState.currentLine = gCodeState.lineNumber;
                    cncState.jobProgress = gCodeState.totalLines > 0
                        ? (100.0f * gCodeState.lineNumber / gCodeState.totalLines)
                        : 0.0f;
                    cncState.jobRunTime = millis() - cncState.jobStartTime;

                    // Sprawdzenie zakończenia wykonania programu
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
                    // Przejście do stanu błędu przy wystąpieniu problemów
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
                // Stan ruchu ręcznego - sprawdzanie zakończenia ruchu
                // Aktualizacja pozycji na podstawie rzeczywistego położenia silników
                cncState.currentX = stepperX.currentPosition() / config.X.stepsPerMM;
                cncState.currentY = stepperY.currentPosition() / config.Y.stepsPerMM;

                // Sprawdzenie czy ruch się zakończył
                if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                    cncState.state = CNCState::IDLE;
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG JOG: Ruch zakończony, powrót do IDLE. Pozycja: X=%.2f, Y=%.2f\n",
                        cncState.currentX, cncState.currentY);
                    #endif
                }

                // Obsługa dodatkowych komend JOG podczas ruchu
                if (commandPending && commandData.type == CommandType::JOG) {
                    commandPending = false;
                    
                    float xOffset = commandData.param1;
                    float yOffset = commandData.param2;
                    float speedMode = commandData.param3;

                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG JOG: Dodatkowy ruch podczas JOG: X=%.2f, Y=%.2f\n", xOffset, yOffset);
                    #endif

                    // Sprawdzenie czy nowy ruch jest możliwy
                    if (abs(xOffset) > 0.001f || abs(yOffset) > 0.001f) {
                        // Wybór profilu prędkości
                        bool useRapid = (speedMode > 0.5f);
                        updateMotorSpeed('X', useRapid, multiStepper, stepperX, stepperY, config);
                        updateMotorSpeed('Y', useRapid, multiStepper, stepperX, stepperY, config);

                        // Konwersja przesunięć z mm na kroki
                        long stepsX = static_cast<long>(xOffset * config.X.stepsPerMM);
                        long stepsY = static_cast<long>(yOffset * config.Y.stepsPerMM);

                        // Dodanie do aktualnej pozycji docelowej
                        long positions[2];
                        positions[0] = stepperX.currentPosition() + stepsX;
                        positions[1] = stepperY.currentPosition() + stepsY;

                        multiStepper.moveTo(positions);

                        // Aktualizacja logicznej pozycji
                        cncState.currentX += xOffset;
                        cncState.currentY += yOffset;
                    }
                }
                break;

            case CNCState::HOMING:
                // Wykonanie procedury bazowania maszyny
                processHoming(cncState, homingState, multiStepper, stepperX, stepperY, config);

                // Sprawdzenie zakończenia sekwencji bazowania
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
                // Wyłączenie wszystkich urządzeń i zatrzymanie ruchu w stanach błędu
                cncState.hotWireOn = false;
                stepperX.stop();
                stepperY.stop();
                stepperX.setCurrentPosition(stepperX.currentPosition());
                stepperY.setCurrentPosition(stepperY.currentPosition());

                break;
        }

        // Aktualizacja fizycznych wyjść na podstawie stanu maszyny
        updateIO(cncState, config);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Zadanie obsługi interfejsu webowego i komunikacji sieciowej
void taskControl(void* parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    // Tworzenie instancji menadżerów dla zadania Control
    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager, configManager, commandQueue, stateQueue);

    // Inicjalizacja wszystkich podsystemów
    bool managersInitialized { initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager) };
    bool connectedToWifi { connectToWiFi(wifiManager) };
    bool startedWebServer { startWebServer(webServerManager) };

    // Sygnalizacja zakończenia inicjalizacji do innych zadań
    systemInitialized = managersInitialized && connectedToWifi && startedWebServer;

    // Zmienne robocze zadania Control
    MachineState receivedState {};
    TickType_t lastStatusUpdateTime { 0 };
    TickType_t lastDebugTime { 0 };
    TickType_t lastWiFiCheckTime { 0 };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(500) };
    const TickType_t debugUpdateInterval { pdMS_TO_TICKS(1000) };
    const TickType_t wifiCheckInterval { pdMS_TO_TICKS(20000) };

    bool wifiReconnectInProgress { false };

    if (!systemInitialized) {
        ESP.restart(); // Restart przy niepowodzeniu inicjalizacji
    }

    while (true) {

        TickType_t currentTime { xTaskGetTickCount() };

        // Monitorowanie i automatyczne przywracanie połączenia WiFi
        if ((currentTime - lastWiFiCheckTime) >= wifiCheckInterval) {

            if (WiFi.status() != WL_CONNECTED) {
                if (!wifiReconnectInProgress) {
                    #ifdef DEBUG_CONTROL_TASK
                    Serial.println("WiFi CONNECTION: Utracono połączenie, próba ponownego łączenia...");
                    #endif
                    wifiReconnectInProgress = true;
                }

                // Próba przywrócenia połączenia z timeoutem 10 sekund
                WiFiManagerStatus reconnectStatus = wifiManager->connect(WIFI_SSID, WIFI_PASSWORD, 10000);

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
* --- FUNKCJE POMOCNICZE SYSTEMU ---
* ------------------------------------------------------------------------------------------------------------
*/

// INICJALIZACJA SYSTEMU

// Inicjalizuje wszystkie menadżery systemu (filesystem, karta SD, WiFi, serwer web, konfiguracja)
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

// Nawiązuje połączenie WiFi z parametrami z credentials.h i timeoutem z konfiguracji
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

// OBSŁUGA WEJŚĆ/WYJŚĆ

// Aktualizuje fizyczne wyjścia (przekaźniki, PWM) i odczytuje wejścia (krańcówki, ESTOP)
void updateIO(MachineState& CNCState, const MachineConfig& config) {

    // Sterowanie przekaźnikami urządzeń wykonawczych
    digitalWrite(PINCONFIG::WIRE_RELAY_PIN, CNCState.hotWireOn ? HIGH : LOW);
    digitalWrite(PINCONFIG::FAN_RELAY_PIN, CNCState.fanOn ? HIGH : LOW);
    
    // Regulacja mocy przez PWM
    ledcWrite(PINCONFIG::WIRE_PWM_CHANNEL, CNCState.hotWirePower);
    ledcWrite(PINCONFIG::FAN_PWM_CHANNEL, CNCState.fanPower);

    // Odczyt stanu krańcówek (z możliwością programowego wyłączenia)
    if (config.deactivateLimitSwitches) {
        CNCState.limitXOn = false;
        CNCState.limitYOn = false;
    }
    else {
        // Interpretacja stanu w zależności od typu krańcówki (NO/NC)
        if (config.limitSwitchType == 0) { 
            // NO (Normally Open) - krańcówka aktywna gdy pin HIGH
            CNCState.limitXOn = digitalRead(PINCONFIG::LIMIT_X_PIN) == HIGH;
            CNCState.limitYOn = digitalRead(PINCONFIG::LIMIT_Y_PIN) == HIGH;
        } else { 
            // NC (Normally Closed) - krańcówka aktywna gdy pin LOW
            CNCState.limitXOn = digitalRead(PINCONFIG::LIMIT_X_PIN) == LOW;
            CNCState.limitYOn = digitalRead(PINCONFIG::LIMIT_Y_PIN) == LOW;
        }
    }

    // Odczyt przycisku ESTOP (z możliwością programowego wyłączenia)
    if (config.deactivateESTOP) {
        CNCState.estopOn = false;
    }
    else {
        CNCState.estopOn = digitalRead(PINCONFIG::ESTOP_PIN);
    }

}


// OBSŁUGA SILNIKÓW KROKOWYCH

/**
 * Konfiguruje parametry kinematyczne silnika na podstawie ustawień z konfiguracji
 * @param axis 'X' lub 'Y'
 * @param useRapid true = szybkie pozycjonowanie (G0), false = praca (G1)
 */
bool updateMotorSpeed(const char axis, const bool useRapid, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config) {

    float stepsPerMM {};
    float feedRate {};      // mm/s
    float acceleration {};  // mm/s²
    AccelStepper* stepper = nullptr;

    // Wybór parametrów dla konkretnej osi
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

    // Walidacja parametrów z konfiguracji
    if (stepsPerMM <= 0 || feedRate <= 0 || acceleration <= 0) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG MOTOR ERROR: Nieprawidłowe parametry dla osi %c\n", axis);
        #endif
        return false;
    }

    // Konwersja jednostek do kroków na sekundę
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
 * Konfiguruje parametry kinematyczne silnika na podstawie komend G-code
 * @param axis 'X' lub 'Y'
 * @param feedRate Prędkość w mm/s (z poleceń F w G-code)
 * @param accelMultiplier Mnożnik bazowej akceleracji (0.5 = połowa prędkości pracy)
 */
bool updateMotorSpeed(const char axis, const float feedRate, const float accelMultiplier, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config) {
    float stepsPerMM {};
    AccelStepper* stepper = nullptr;

    // Wybór parametrów dla konkretnej osi
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

    // Walidacja parametrów wejściowych z G-code
    if (stepsPerMM <= 0 || feedRate <= 0 || accelMultiplier <= 0) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG MOTOR ERROR: Nieprawidłowe parametry dla osi %c (feedRate: %.3f, accelMult: %.2f)\n",
            axis, feedRate, accelMultiplier);
        #endif
        return false;
    }

    // Konwersja jednostek: mm/s → steps/s, obliczenie akceleracji
    float speedStepsPerSec = feedRate * stepsPerMM;
    float accelStepsPerSecSq = (feedRate * accelMultiplier) * stepsPerMM;

    stepper->setMaxSpeed(speedStepsPerSec);
    stepper->setAcceleration(accelStepsPerSecSq);

    return true;
}

// PRZETWARZANIE G-CODE

// Ekstraktuje wartość liczbową parametru z linii G-code (np. X10.5, F1200)
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

// Przygotowuje system do wykonania programu G-code (otwiera plik, resetuje stan)
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

    // Sprawdzenie poprawności długości nazwy pliku
    if (filename.length() >= sizeof(cncState.currentProject)) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC ERROR: Nazwa pliku za długa: %s\n", filename.c_str());
        #endif
        return false;
    }
    
    // Bezpieczne zamknięcie poprzedniego pliku jeśli był otwarty
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

    // Resetowanie stanu przetwarzania do wartości początkowych
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

    // Wielokrotne próby otwarcia pliku z karty SD
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
* --- FUNKCJONALNOŚĆ BAZOWANIA MASZYNY ---
* ------------------------------------------------------------------------------------------------------------
*/

// Wykonuje sekwencję bazowania osi X i Y do pozycji zerowej
void processHoming(MachineState& cncState, HomingState& homingState, MultiStepper& multiStepper, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {

    // Sprawdzenie warunków bezpieczeństwa - bazowanie tylko gdy ESTOP nieaktywny
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
                    // Inicjalizacja bazowania osi X - ruch w stronę krańcówki
                    float homingSpeedSteps = homingState.homingSpeed * config.X.stepsPerMM;
                    float homingAccelSteps = homingState.homingAcceleration * config.X.stepsPerMM;

                    stepperX.setMaxSpeed(homingSpeedSteps);
                    stepperX.setAcceleration(homingAccelSteps);

                    // Rozpoczęcie ruchu w kierunku ujemnym (długi dystans dla pewności dotarcia do krańcówki)
                    stepperX.move(-1000 * config.X.stepsPerMM);
                    homingState.movementInProgress = true;
                    homingState.limitReached = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Rozpoczęcie bazowania osi X");
                    #endif
                }

                // Detekcja osiągnięcia krańcówki osi X
                if (cncState.limitXOn && !homingState.limitReached) {
                    stepperX.stop();
                    homingState.limitReached = true;
                    homingState.movementInProgress = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Krańcówka X osiągnięta");
                    #endif

                    // Wycofanie się o bezpieczną odległość od krańcówki
                    long backoffSteps = homingState.backoffDistance * config.X.stepsPerMM;
                    stepperX.move(backoffSteps);
                    homingState.backoffComplete = false;
                }

                // Sprawdzenie zakończenia procedury wycofania dla osi X
                if (homingState.limitReached && stepperX.distanceToGo() == 0 && !homingState.backoffComplete) {
                    // Ustawienie pozycji zerowej i przejście do bazowania osi Y
                    stepperX.setCurrentPosition(0);
                    cncState.currentX = 0.0f;
                    homingState.backoffComplete = true;
                    homingState.stage = HomingState::HomingStage::HOMING_Y;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Oś X zbazowana, rozpoczęcie bazowania osi Y");
                    #endif
                }

                // Obsługa błędu - krańcówka nie została osiągnięta w oczekiwanym czasie
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
                    // Inicjalizacja bazowania osi Y - analogicznie do osi X
                    float homingSpeedSteps = homingState.homingSpeed * config.Y.stepsPerMM;
                    float homingAccelSteps = homingState.homingAcceleration * config.Y.stepsPerMM;

                    stepperY.setMaxSpeed(homingSpeedSteps);
                    stepperY.setAcceleration(homingAccelSteps);

                    // Rozpoczęcie ruchu w kierunku ujemnym do krańcówki Y
                    stepperY.move(-1000 * config.Y.stepsPerMM);
                    homingState.movementInProgress = true;
                    homingState.limitReached = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Rozpoczęcie bazowania osi Y");
                    #endif
                }

                // Detekcja osiągnięcia krańcówki osi Y
                if (cncState.limitYOn && !homingState.limitReached) {
                    stepperY.stop();
                    homingState.limitReached = true;
                    homingState.movementInProgress = false;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Krańcówka Y osiągnięta");
                    #endif

                    // Wycofanie się o bezpieczną odległość od krańcówki Y
                    long backoffSteps = homingState.backoffDistance * config.Y.stepsPerMM;
                    stepperY.move(backoffSteps);
                    homingState.backoffComplete = false;
                }

                // Finalizacja procedury bazowania - obie osie w pozycji zerowej
                if (homingState.limitReached && stepperY.distanceToGo() == 0 && !homingState.backoffComplete) {
                    stepperY.setCurrentPosition(0);
                    cncState.currentY = 0.0f;
                    homingState.backoffComplete = true;
                    homingState.stage = HomingState::HomingStage::FINISHED;

                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG HOME: Oś Y zbazowana, bazowanie zakończone");
                    #endif
                }

                // Obsługa błędu - krańcówka Y nie została osiągnięta
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
            // Stany finalne - obsługiwane przez główną maszynę stanów w taskCNC
            break;
    }
}