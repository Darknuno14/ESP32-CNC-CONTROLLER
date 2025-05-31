#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AccelStepper.h>

#include <vector>
#include <string>
#include <algorithm>

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

// Priority-based command queue
QueueHandle_t priorityCommandQueue {}; // Priority-based command queue

// Performance monitoring
PerformanceMetrics performanceMetrics {};
MachineState lastBroadcastState {};     // For delta updates
EventSourceConfig eventSourceConfig {};
TimeoutConfig timeoutConfig {};

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

// G-code processing functions
float getParameter(const String& line, char param);
bool updateMotorSpeed(const char axis, const bool useRapid, AccelStepper& stepper, const MachineConfig& config);
bool updateMotorSpeed(const char axis, const float feedRate, const float accelMultiplier, AccelStepper& stepper, const MachineConfig& config);
bool initializeGCodeProcessing(MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config);
void processGCode(MachineState& cncState, GCodeProcessingState& gCodeState, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config);
bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config);
bool processLinearMove(const String& line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config, bool isRapid);

// JOG and HOMING functions
void processJogCommand(const WebserverCommand& command, JogState& jogState, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config);
void processJog(JogState& jogState, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, const MachineConfig& config);
void processHomingCommand(HomingState& homingState, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config);
void processHoming(HomingState& homingState, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, const MachineConfig& config);

// Performance and optimization functions
bool runSteppersWithTimeout(AccelStepper& stepperX, AccelStepper& stepperY, uint32_t maxTimeMs = 0);
bool processPriorityCommand(PriorityCommand& priorityCmd, WebserverCommand& normalCmd);
void updatePerformanceMetrics();
bool processGCodeWithTimeSlicing(MachineState& cncState, GCodeProcessingState& gCodeState, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config);
bool takeSDWithTimeout(uint32_t timeoutMs = 0);
void giveSDSafe();

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

    // ============================================================================    sdManager = new SDCardManager();
    configManager = new ConfigManager(sdManager);

    // Initialize performance monitoring and timeout configurations
    memset(&performanceMetrics, 0, sizeof(PerformanceMetrics));
    memset(&lastBroadcastState, 0, sizeof(MachineState));
    
    // Initialize timeout configurations with default values
    timeoutConfig.sdOperationTimeout = 5000;        // 5 seconds for SD operations
    timeoutConfig.stepperOperationTimeout = 1000;   // 1 second for stepper operations  
    timeoutConfig.queueOperationTimeout = 100;      // 100ms for queue operations
    timeoutConfig.gCodeProcessingTimeout = 10;      // 10ms max per G-code line
      // Initialize EventSource configuration
    eventSourceConfig.idleInterval = 500;           // IDLE state - 500ms
    eventSourceConfig.runningInterval = 100;        // RUNNING state - 100ms
    eventSourceConfig.jogInterval = 50;             // JOG state - 50ms
    eventSourceConfig.homingInterval = 100;         // HOMING state - 100ms
    eventSourceConfig.errorInterval = 200;          // ERROR state - 200ms

    // Inicjalizacja kolejek do komunikacji między zadaniami - zoptymalizowane rozmiary
    stateQueue = xQueueCreate(CONFIG::STATE_QUEUE_SIZE, sizeof(MachineState));
    commandQueue = xQueueCreate(CONFIG::COMMAND_QUEUE_SIZE, sizeof(WebserverCommand));
    priorityCommandQueue = xQueueCreate(CONFIG::COMMAND_QUEUE_SIZE * 2, sizeof(PriorityCommand));

    if (!stateQueue) {
        Serial.println("SYSTEM ERROR: stateQueue not created!");
    }
    if (!commandQueue) {
        Serial.println("SYSTEM ERROR: commandQueue not created!");
    }
    if (!priorityCommandQueue) {
        Serial.println("SYSTEM ERROR: priorityCommandQueue not created!");
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
    #ifdef DEBUG_SYSTEM_CRITICAL
    Serial.printf("STATUS: CNC Task started on core %d\n", xPortGetCoreID());
    #endif    // Stany maszyny
    MachineState cncState {};
    GCodeProcessingState gCodeState {};
    JogState jogState {};
    HomingState homingState {};

    // Komendy odbierane z interfejsu webowego
    WebserverCommand commandData {};
    bool commandPending { false };

    // Komunikacja między zadaniami - zoptymalizowane interwały
    TickType_t lastCommandProcessTime { 0 };
    TickType_t lastStatusUpdateTime { 0 };
    TickType_t lastIOUpdateTime { 0 };
    const TickType_t commandProcessInterval { pdMS_TO_TICKS(CONFIG::COMMAND_PROCESS_INTERVAL) };
    const TickType_t statusUpdateInterval { pdMS_TO_TICKS(CONFIG::STATUS_UPDATE_INTERVAL) };
    const TickType_t ioUpdateInterval { pdMS_TO_TICKS(CONFIG::IO_UPDATE_INTERVAL) };

    // Silniki krokowe
    AccelStepper stepperX(AccelStepper::DRIVER, PINCONFIG::STEP_X_PIN, PINCONFIG::DIR_X_PIN);
    AccelStepper stepperY(AccelStepper::DRIVER, PINCONFIG::STEP_Y_PIN, PINCONFIG::DIR_Y_PIN);

    // Oczekiwanie na inicjalizację systemu - zoptymalizowane
    while (!systemInitialized) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Zmniejszone z 1000ms na 100ms
    }    // Parametry (konfiguracja) maszyny - zoptymalizowane ładowanie
    MachineConfig config {};
    ConfigManagerStatus configStatus {};
    int configRetryCount = 0;
    const int maxConfigRetries = 10;
    
    do {
        if (configManager != nullptr) {
            configStatus = configManager->getConfig(config);
        } else {
            #ifdef DEBUG_SYSTEM_CRITICAL
            Serial.println("ERROR CNC: Config manager not initialized.");
            #endif
            configStatus = ConfigManagerStatus::MANAGER_NOT_INITIALIZED;
        }
        
        if (configStatus != ConfigManagerStatus::OK) {
            configRetryCount++;
            if (configRetryCount >= maxConfigRetries) {
                #ifdef DEBUG_SYSTEM_CRITICAL
                Serial.println("CRITICAL ERROR: Failed to load config after max retries. Restarting...");
                #endif
                ESP.restart();
            }
            vTaskDelay(pdMS_TO_TICKS(500)); // Zmniejszone opóźnienie retry
        }
    } while (configStatus != ConfigManagerStatus::OK);

    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };
        uint32_t taskStartTime = micros();
        
        // KRYTYCZNE: Zawsze uruchamiaj silniki krokowe z timeout watchdog
        runSteppersWithTimeout(stepperX, stepperY);

        // Odbieranie komend z kolejki - priority-based system
        if ((currentTime - lastCommandProcessTime) >= commandProcessInterval) {
            PriorityCommand priorityCmd {};
            if (processPriorityCommand(priorityCmd, commandData)) {
                commandPending = true;
            }
            lastCommandProcessTime = currentTime;
        }

        // Wysyłanie statusu maszyny - zoptymalizowane częstotliwość
        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {
            // Aktualizuj pozycje przed wysłaniem statusu
            cncState.currentX = stepperX.currentPosition() / config.X.stepsPerMM;
            cncState.currentY = stepperY.currentPosition() / config.Y.stepsPerMM;
            
            if (xQueueOverwrite(stateQueue, &cncState) != pdPASS) {
                performanceMetrics.stateQueueDrops++;
                // Tylko krytyczne błędy są logowane
                #ifdef DEBUG_SYSTEM_CRITICAL
                Serial.println("CRITICAL: Failed to send machine status.");
                #endif
            }
            lastStatusUpdateTime = currentTime;
        }

        // Aktualizacja I/O - zoptymalizowana częstotliwość
        if ((currentTime - lastIOUpdateTime) >= ioUpdateInterval) {
            updateIO(cncState, config);
            lastIOUpdateTime = currentTime;
        }
        
        // Performance monitoring
        uint32_t taskEndTime = micros();
        uint32_t taskDuration = taskEndTime - taskStartTime;
        performanceMetrics.maxCncTaskTime = std::max(performanceMetrics.maxCncTaskTime, taskDuration);
        updatePerformanceMetrics();// Przetwarzanie odebranych komend
        if (commandPending) {
            switch (commandData.type) {
                case CommandType::RELOAD_CONFIG: {
                    ConfigManagerStatus reloadStatus = configManager->getConfig(config);
                    if (reloadStatus != ConfigManagerStatus::OK) {
                        #ifdef DEBUG_SYSTEM_CRITICAL
                        Serial.println("CRITICAL: Config reload failed.");
                        #endif
                    }
                    commandPending = false;
                    break;
                }
                
                case CommandType::SET_WIRE_POWER:
                    cncState.hotWirePower = static_cast<uint8_t>(commandData.param3 * 2.55f); // 0-100% -> 0-255
                    commandPending = false;
                    break;
                    
                case CommandType::SET_FAN_POWER:
                    cncState.fanPower = static_cast<uint8_t>(commandData.param3 * 2.55f); // 0-100% -> 0-255
                    commandPending = false;
                    break;
                    
                case CommandType::EMERGENCY_STOP:
                    // Natychmiastowe zatrzymanie wszystkich operacji
                    stepperX.stop();
                    stepperY.stop();
                    cncState.hotWireOn = false;
                    cncState.fanOn = false;
                    cncState.state = CNCState::STOPPED;
                    jogState.isActive = false;
                    homingState.isActive = false;
                    gCodeState.stopRequested = true;
                    commandPending = false;
                    break;
                    
                default:
                    // Komendy specyficzne dla stanu będą obsłużone w switch(state)
                    break;
            }
        }

        switch (cncState.state) {            case CNCState::IDLE:
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

                        case CommandType::JOG_X_PLUS:
                        case CommandType::JOG_X_MINUS:
                        case CommandType::JOG_Y_PLUS:
                        case CommandType::JOG_Y_MINUS:
                            // Rozpoczęcie ruchu ręcznego
                            processJogCommand(commandData, jogState, stepperX, stepperY, config);
                            cncState.state = CNCState::JOG;
                            break;

                        default:
                            break;
                    }
                }
                break;case CNCState::RUNNING:
                // Przetwarzanie komend w trybie RUNNING
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

                // Obsługa STOP request
                if (gCodeState.stopRequested) {
                    if (gCodeState.fileOpen && gCodeState.currentFile) {
                        gCodeState.currentFile.close();
                        gCodeState.fileOpen = false;
                    }
                    cncState.state = CNCState::STOPPED;
                    break;
                }

                // Obsługa PAUSE/RESUME
                if (gCodeState.pauseRequested) {
                    cncState.isPaused = true;
                    break;
                } else {
                    cncState.isPaused = false;
                }                // Kontynuuj przetwarzanie G-code jeśli nie jest wstrzymane
                if (!cncState.isPaused) {
                    // Use time-sliced G-code processing for better responsiveness
                    processGCodeWithTimeSlicing(cncState, gCodeState, stepperX, stepperY, config);

                    // Aktualizacja postępu - optymalizacja: tylko jeśli są nowe dane
                    if (gCodeState.lineNumber != cncState.currentLine) {
                        cncState.currentLine = gCodeState.lineNumber;
                        cncState.jobProgress = gCodeState.totalLines > 0
                            ? (100.0f * gCodeState.lineNumber / gCodeState.totalLines)
                            : 0.0f;
                        cncState.jobRunTime = millis() - cncState.jobStartTime;
                    }

                    // Sprawdzenie zakończenia lub błędu
                    if (gCodeState.stage == GCodeProcessingState::ProcessingStage::FINISHED) {
                        if (gCodeState.fileOpen && gCodeState.currentFile) {
                            gCodeState.currentFile.close();
                            gCodeState.fileOpen = false;
                        }
                        cncState.state = CNCState::IDLE;
                    } else if (gCodeState.stage == GCodeProcessingState::ProcessingStage::ERROR) {
                        cncState.state = CNCState::ERROR;
                        #ifdef DEBUG_SYSTEM_CRITICAL
                        Serial.printf("CNC ERROR: %s\n", gCodeState.errorMessage.c_str());
                        #endif
                    }
                }
                break;            case CNCState::JOG:
                // Obsługa ruchu ręcznego (JOG)
                if (commandPending) {
                    switch (commandData.type) {
                        case CommandType::JOG_X_PLUS:
                        case CommandType::JOG_X_MINUS:
                        case CommandType::JOG_Y_PLUS:
                        case CommandType::JOG_Y_MINUS:
                            processJogCommand(commandData, jogState, stepperX, stepperY, config);
                            break;
                            
                        case CommandType::STOP:
                        case CommandType::EMERGENCY_STOP:
                            // Zatrzymaj JOG i przejdź do IDLE
                            stepperX.stop();
                            stepperY.stop();
                            jogState.isActive = false;
                            jogState.movementInProgress = false;
                            cncState.state = CNCState::IDLE;
                            break;
                            
                        default:
                            break;
                    }
                    commandPending = false;
                }
                
                // Kontynuuj JOG jeśli jest aktywny
                if (jogState.isActive) {
                    processJog(jogState, stepperX, stepperY, cncState, config);
                } else {
                    // JOG zakończony - wróć do IDLE
                    cncState.state = CNCState::IDLE;
                }
                break;

            case CNCState::HOMING:
                // Obsługa sekwencji bazowania
                if (commandPending) {
                    switch (commandData.type) {
                        case CommandType::HOME:
                            if (!homingState.isActive) {
                                processHomingCommand(homingState, stepperX, stepperY, config);
                            }
                            break;
                            
                        case CommandType::STOP:
                        case CommandType::EMERGENCY_STOP:
                            // Zatrzymaj HOMING i przejdź do IDLE
                            stepperX.stop();
                            stepperY.stop();
                            homingState.isActive = false;
                            homingState.movementInProgress = false;
                            homingState.stage = HomingState::HomingStage::IDLE;
                            cncState.state = CNCState::IDLE;
                            cncState.isHomed = false;
                            break;
                            
                        default:
                            break;
                    }
                    commandPending = false;
                }
                
                // Kontynuuj HOMING jeśli jest aktywny
                if (homingState.isActive) {
                    processHoming(homingState, stepperX, stepperY, cncState, config);
                } else {
                    // HOMING zakończony lub błąd - wróć do IDLE
                    if (homingState.stage == HomingState::HomingStage::FINISHED) {
                        #ifdef DEBUG_SYSTEM_CRITICAL
                        Serial.println("HOMING: Completed successfully, returning to IDLE");
                        #endif
                    } else if (homingState.stage == HomingState::HomingStage::ERROR) {
                        cncState.state = CNCState::ERROR;
                        #ifdef DEBUG_SYSTEM_CRITICAL
                        Serial.printf("HOMING: Error occurred: %s\n", homingState.errorMessage.c_str());
                        #endif
                        break;
                    }
                    cncState.state = CNCState::IDLE;
                }
                break;

            case CNCState::STOPPED:
            case CNCState::ERROR:
                // Reset do IDLE po otrzymaniu odpowiedniej komendy
                if (commandPending) {
                    commandPending = false;
                    if (commandData.type == CommandType::RESET) {
                        cncState.state = CNCState::IDLE;
                        gCodeState.stopRequested = false;
                        gCodeState.pauseRequested = false;
                    }
                }
                break;
        }

        // Zoptymalizowane opóźnienie na końcu pętli
        vTaskDelay(pdMS_TO_TICKS(CONFIG::CNC_TASK_DELAY));
    }
}

// Zadanie kontroli i sterowania maszyną - obsługa interfejsu webowego
void taskControl(void* parameter) {
    #ifdef DEBUG_SYSTEM_CRITICAL
    Serial.printf("STATUS: Control Task started on core %d\n", xPortGetCoreID());
    #endif

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager, configManager, commandQueue, stateQueue, priorityCommandQueue);

    bool managersInitialized { initializeManagers(fsManager, sdManager, wifiManager, webServerManager, configManager) };
    bool connectedToWifi { connectToWiFi(wifiManager) };
    bool startedWebServer { startWebServer(webServerManager) };

    systemInitialized = managersInitialized && connectedToWifi && startedWebServer;

    if (!systemInitialized) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("CRITICAL: System initialization failed. Restarting...");
        #endif
        ESP.restart();
    }    MachineState receivedState {};
    MachineState previousState {};
    
    TickType_t lastStatusUpdateTime { 0 };
    TickType_t lastSystemMonitorTime { 0 };
    const TickType_t systemMonitorInterval { pdMS_TO_TICKS(CONFIG::SYSTEM_MONITOR_INTERVAL) };

    while (true) {
        TickType_t currentTime { xTaskGetTickCount() };

        // Adaptive EventSource broadcasting based on machine state
        if (xQueuePeek(stateQueue, &receivedState, 0) == pdTRUE) {
            // Get adaptive interval based on current state
            uint32_t adaptiveInterval = eventSourceConfig.getCurrentInterval(receivedState.state);
            TickType_t statusUpdateInterval = pdMS_TO_TICKS(adaptiveInterval);
            
            if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {
                if (!webServerManager->isBusy()) {
                    // Use adaptive broadcasting with delta updates
                    webServerManager->broadcastMachineStatusAdaptive(receivedState, previousState, eventSourceConfig);
                    previousState = receivedState; // Update previous state for next delta calculation
                    performanceMetrics.controlTaskCycles++;
                }
                lastStatusUpdateTime = currentTime;
            }
        }        // System monitoring - rzadziej wykonywany
        if ((currentTime - lastSystemMonitorTime) >= systemMonitorInterval) {
            updatePerformanceMetrics();
            
            // Update stack metrics for Control task specifically
            UBaseType_t controlStackFree = uxTaskGetStackHighWaterMark(NULL);
            performanceMetrics.stackHighWaterMarkControl = controlStackFree;
            performanceMetrics.minStackFree = std::min(performanceMetrics.minStackFree, controlStackFree);
            
            #ifdef DEBUG_SYSTEM_CRITICAL
            Serial.printf("Performance: Heap=%d, CNC cycles=%d, Delta updates=%d, Stack free=%d\n",
                performanceMetrics.freeHeap, performanceMetrics.cncTaskCycles, 
                performanceMetrics.deltaUpdates, controlStackFree);
            #endif
            lastSystemMonitorTime = currentTime;
        }
        
        // Zoptymalizowane opóźnienie
        vTaskDelay(pdMS_TO_TICKS(CONFIG::CONTROL_TASK_DELAY));
    }
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- MISCELLANEOUS FUNCTIONS ---
* ------------------------------------------------------------------------------------------------------------
*/

// SYSYTEM

// Inicjalizuje wszystkie obiekty zarządzające systemem - zoptymalizowane logowanie
bool initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager, ConfigManager* configManager) {
    
    // FSManager
    if (fsManager == nullptr) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("CRITICAL: FSManager is null pointer");
        #endif
        return false;
    }
    
    FSManagerStatus fsManagerStatus { fsManager->init() };
    if (fsManagerStatus != FSManagerStatus::OK) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("CRITICAL: FSManager init failed: %d\n", static_cast<int>(fsManagerStatus));
        #endif
        return false;
    }

    // SDCardManager
    if (sdManager == nullptr) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("CRITICAL: SDCardManager is null pointer");
        #endif
        return false;
    }
    
    SDManagerStatus sdManagerStatus { sdManager->init() };
    if (sdManagerStatus != SDManagerStatus::OK) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("CRITICAL: SDCardManager init failed: %d\n", static_cast<int>(sdManagerStatus));
        #endif
        return false;
    }

    // ConfigManager
    if (configManager == nullptr) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("CRITICAL: ConfigManager is null pointer");
        #endif
        return false;
    }
    
    ConfigManagerStatus configManagerStatus { configManager->init() };
    if (configManagerStatus != ConfigManagerStatus::OK) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("CRITICAL: ConfigManager init failed: %d\n", static_cast<int>(configManagerStatus));
        #endif
        return false;
    }

    // WiFiManager
    if (wifiManager == nullptr) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("CRITICAL: WiFiManager is null pointer");
        #endif
        return false;
    }
    
    WiFiManagerStatus wifiManagerStatus { wifiManager->init() };
    if (wifiManagerStatus != WiFiManagerStatus::OK) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("CRITICAL: WiFiManager init failed: %d\n", static_cast<int>(wifiManagerStatus));
        #endif
        return false;
    }

    // WebServerManager
    if (webServerManager == nullptr) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("CRITICAL: WebServerManager is null pointer");
        #endif
        return false;
    }
      WebServerStatus webServerStatus { webServerManager->init() };
    if (webServerStatus != WebServerStatus::OK) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("CRITICAL: WebServerManager init failed: %d\n", static_cast<int>(webServerStatus));
        #endif
        return false;
    }

    #ifdef DEBUG_SYSTEM_CRITICAL
    Serial.println("SUCCESS: All managers initialized successfully");
    #endif
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
bool updateMotorSpeed(const char axis, const bool useRapid, AccelStepper& stepper, const MachineConfig& config) {

    float stepsPerMM {};
    float feedRate {};      // mm/s
    float acceleration {};  // mm/s²

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

    // Walidacja wartości
    if (stepsPerMM <= 0 || feedRate <= 0 || acceleration <= 0) {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG MOTOR ERROR: Nieprawidłowe parametry dla osi %c\n", axis);
        #endif
        return false;
    }

    // feedRate: mm/s → steps/s
    // acceleration: mm/s² → steps/s²
    float speedStepsPerSec = feedRate * stepsPerMM;
    float accelStepsPerSecSq = acceleration * stepsPerMM;

    stepper.setMaxSpeed(speedStepsPerSec);
    stepper.setAcceleration(accelStepsPerSecSq);

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

    stepper.setMaxSpeed(speedStepsPerSec);
    stepper.setAcceleration(accelStepsPerSecSq);

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
    gCodeState.currentLine = "";    gCodeState.heatingStartTime = 0;
    gCodeState.heatingDuration = config.delayAfterStartup;

    // Próba otwarcia pliku z retry i timeout
    constexpr int MAX_NUM_OF_TRIES = 3;
    for (int i { 0 }; i < MAX_NUM_OF_TRIES; ++i) {
        if (!takeSDWithTimeout()) {
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
            gCodeState.currentFile.seek(0);            gCodeState.fileOpen = true;
            performanceMetrics.sdOperations++;

            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: Otwarto plik %s, liczba linii: %lu\n",
                filePath.c_str(), gCodeState.totalLines);
            #endif

            giveSDSafe();
            break; // Sukces!
        }
        else {
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC ERROR: Próba %d - Nie udało się otworzyć pliku %s\n",
                i + 1, filePath.c_str());
            #endif
            giveSDSafe();

            if (i == MAX_NUM_OF_TRIES - 1) {
                return false; // Ostatnia próba nieudana
            }
            vTaskDelay(pdMS_TO_TICKS(200)); // Odczekaj przed kolejną próbą
        }    }

    // Inicjalizacja stanu maszyny
    strncpy(cncState.currentProject, filename.c_str(), sizeof(cncState.currentProject) - 1);
    cncState.currentProject[sizeof(cncState.currentProject) - 1] = '\0';
    cncState.jobStartTime = millis();
    cncState.jobProgress = 0.0f;
    cncState.currentLine = 0;
    cncState.totalLines = gCodeState.totalLines;

    return true;
}

void processGCode(MachineState& cncState, GCodeProcessingState& gCodeState, AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {

    // SPRAWDZENIE BEZPIECZEŃSTWA - krańcówki i ESTOP
    if (cncState.estopOn || cncState.limitXOn || cncState.limitYOn) {
        // Natychmiastowe zatrzymanie
        stepperX.stop();
        stepperY.stop();
        cncState.hotWireOn = false;
        cncState.fanOn = false;
        gCodeState.stage = GCodeProcessingState::ProcessingStage::ERROR;
        gCodeState.errorMessage = cncState.estopOn ? "ESTOP" : "Limit";        // Zamknij plik w przypadku błędu z timeout protection
        if (gCodeState.fileOpen && gCodeState.currentFile) {
            if (takeSDWithTimeout()) {
                gCodeState.currentFile.close();
                gCodeState.fileOpen = false;
                giveSDSafe();
                performanceMetrics.sdOperations++;
            }
            else {
                performanceMetrics.sdTimeouts++;
                #ifdef DEBUG_SYSTEM_CRITICAL
                Serial.println("WARNING: Failed to close G-code file in error handler due to SD timeout");
                #endif
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
                    if (stepperX.isRunning() || stepperY.isRunning()) {
                        return; // Czekaj na zakończenie ruchu
                    }
                    // Ruch zakończony
                    gCodeState.movementInProgress = false;
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;

                    // Ustaw domyślną prędkość roboczą jeśli nie używamy F z G-code
                    if (!config.useGCodeFeedRate) {
                        updateMotorSpeed('X', false, stepperX, config); // false = work speed
                        updateMotorSpeed('Y', false, stepperY, config);
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
                        updateMotorSpeed('X', false, stepperX, config); // false = work speed
                        updateMotorSpeed('Y', false, stepperY, config);
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
                updateMotorSpeed('X', true, stepperX, config); // rapid dla offsetu
                updateMotorSpeed('Y', true, stepperY, config);
                stepperX.moveTo(targetXSteps);
                stepperY.moveTo(targetYSteps);
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
                if (stepperX.isRunning() || stepperY.isRunning()) {
                    return; // Czekaj na zakończenie ruchu
                }
                gCodeState.movementInProgress = false;
            }            // Pobierz SD i czytaj linię z timeout
            if (!takeSDWithTimeout()) {
                performanceMetrics.sdTimeouts++;
                return; // Spróbuj ponownie w następnym cyklu
            }

            if (gCodeState.currentFile.available()) {
                gCodeState.currentLine = gCodeState.currentFile.readStringUntil('\n');
                gCodeState.lineNumber++;
                giveSDSafe();
                performanceMetrics.sdOperations++;
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG G-CODE: Odczytano linię %lu: %s\n", gCodeState.lineNumber, gCodeState.currentLine.c_str());
                #endif

                gCodeState.stage = GCodeProcessingState::ProcessingStage::PROCESSING_LINE;
            }
            else {
                // Koniec pliku
                giveSDSafe();
                gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
            }
            break;

        case GCodeProcessingState::ProcessingStage::PROCESSING_LINE:
            // Parsuj i przygotuj ruch
            if (processGCodeLine(gCodeState.currentLine, stepperX, stepperY, cncState, gCodeState, config)) {
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
                if (stepperX.isRunning() || stepperY.isRunning()) {
                    return; // Czekaj na zakończenie ruchu powrotnego
                }
                gCodeState.movementInProgress = false;                // Wyłącz drut i wentylator
                cncState.hotWireOn = false;
                cncState.fanOn = false;

                // Zamknij plik z timeout protection
                if (gCodeState.fileOpen && gCodeState.currentFile) {
                    if (takeSDWithTimeout()) {
                        gCodeState.currentFile.close();
                        gCodeState.fileOpen = false;
                        giveSDSafe();
                        performanceMetrics.sdOperations++;
                    }
                    else {
                        performanceMetrics.sdTimeouts++;
                        #ifdef DEBUG_SYSTEM_CRITICAL
                        Serial.println("WARNING: Failed to close G-code file due to SD timeout");
                        #endif
                    }
                }

                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG G-CODE: Przetwarzanie G-code zakończone");
                #endif
                return;
            }

            // Rozpocznij ruch powrotny do pozycji przed offsetem (0,0)
            updateMotorSpeed('X', true, stepperX, config); // true = rapid
            updateMotorSpeed('Y', true, stepperY, config);

            stepperX.moveTo(0);
            stepperY.moveTo(0);
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
bool processGCodeLine(String line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config) {
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
                updateMotorSpeed('X', feedRate, 0.5f, stepperX, config);
                updateMotorSpeed('Y', feedRate, 0.5f, stepperY, config);
                gCodeState.currentFeedRate = feedRate; // Zapamiętaj aktualną prędkość w mm/s
            }
        }
        return false; // Nie ma ruchu, przejdź do następnej linii
    }

    // G0 - Rapid move (ruch szybki)
    else if (line.startsWith("G0")) {
        return processLinearMove(line, stepperX, stepperY, cncState, gCodeState, config, true);
    }

    // G1 - Linear move (ruch roboczy)
    else if (line.startsWith("G1")) {
        return processLinearMove(line, stepperX, stepperY, cncState, gCodeState, config, false);
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

bool processLinearMove(const String& line, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, GCodeProcessingState& gCodeState, MachineConfig& config, bool isRapid) {
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
        updateMotorSpeed('X', feedRate, 0.5f, stepperX, config);
        updateMotorSpeed('Y', feedRate, 0.5f, stepperY, config);
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
        stepperX.moveTo(targetXWithOffset * config.X.stepsPerMM);
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
        stepperY.moveTo(targetYWithOffset * config.Y.stepsPerMM);
        hasMovement = true;

    }

    return hasMovement;
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- Performance Monitoring and Optimization Functions ---
* ------------------------------------------------------------------------------------------------------------
*/

// Safe SD operations with timeout
bool takeSDWithTimeout(uint32_t timeoutMs) {
    uint32_t timeout = (timeoutMs == 0) ? timeoutConfig.sdOperationTimeout : timeoutMs;
    uint32_t startTime = millis();
    
    while ((millis() - startTime) < timeout) {
        if (sdManager->takeSD()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent busy waiting
    }
      performanceMetrics.sdTimeouts++;
    performanceMetrics.maxSdWaitTime = std::max(performanceMetrics.maxSdWaitTime, (uint32_t)(millis() - startTime));
    
    #ifdef DEBUG_SYSTEM_CRITICAL
    Serial.printf("WARNING: SD operation timeout after %dms\n", millis() - startTime);
    #endif
    
    return false;
}

void giveSDSafe() {
    if (sdManager) {
        sdManager->giveSD();
    }
}

// Stepper operations with watchdog timeout
bool runSteppersWithTimeout(AccelStepper& stepperX, AccelStepper& stepperY, uint32_t maxTimeMs) {
    uint32_t timeout = (maxTimeMs == 0) ? timeoutConfig.stepperOperationTimeout : maxTimeMs;
    uint32_t startTime = millis();
    
    bool xRunning = stepperX.run();
    bool yRunning = stepperY.run();
      uint32_t elapsedTime = millis() - startTime;
    performanceMetrics.maxStepperTime = std::max(performanceMetrics.maxStepperTime, elapsedTime);
    
    if (elapsedTime > timeout) {
        performanceMetrics.stepperTimeouts++;
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("WARNING: Stepper operation took %dms (timeout: %dms)\n", elapsedTime, timeout);
        #endif
    }
    
    return xRunning || yRunning;
}

// Calculate delta updates for EventSource broadcasting
MachineStateDelta calculateStateDelta(const MachineState& current, const MachineState& previous) {
    MachineStateDelta delta {};
    
    // Position changes
    if (abs(current.currentX - previous.currentX) > 0.01f || abs(current.currentY - previous.currentY) > 0.01f) {
        delta.hasPositionUpdate = true;
        delta.deltaX = current.currentX - previous.currentX;
        delta.deltaY = current.currentY - previous.currentY;
    }
    
    // State changes
    if (current.state != previous.state || current.isPaused != previous.isPaused || current.isHomed != previous.isHomed) {
        delta.hasStateUpdate = true;
        delta.newState = current.state;
        delta.newPauseState = current.isPaused;
        delta.newHomedState = current.isHomed;
    }
    
    // I/O changes
    if (current.estopOn != previous.estopOn || current.limitXOn != previous.limitXOn || 
        current.limitYOn != previous.limitYOn || current.hotWireOn != previous.hotWireOn ||
        current.fanOn != previous.fanOn || current.hotWirePower != previous.hotWirePower ||
        current.fanPower != previous.fanPower) {
        delta.hasIOUpdate = true;
        delta.newEstopState = current.estopOn;
        delta.newLimitXState = current.limitXOn;
        delta.newLimitYState = current.limitYOn;
        delta.newHotWireState = current.hotWireOn;
        delta.newFanState = current.fanOn;
        delta.newHotWirePower = current.hotWirePower;
        delta.newFanPower = current.fanPower;
    }
    
    // Progress changes
    if (current.currentLine != previous.currentLine || abs(current.jobProgress - previous.jobProgress) > 0.1f) {
        delta.hasProgressUpdate = true;
        delta.newCurrentLine = current.currentLine;
        delta.newProgress = current.jobProgress;
    }
    
    // Error changes
    if (current.errorID != previous.errorID) {
        delta.hasErrorUpdate = true;
        delta.newErrorID = current.errorID;
    }
    
    return delta;
}

// Process priority commands
bool processPriorityCommand(PriorityCommand& priorityCmd, WebserverCommand& normalCmd) {
    // Check for priority commands first
    if (xQueueReceive(priorityCommandQueue, &priorityCmd, 0) == pdTRUE) {
        // Check if command is still valid (not timed out)
        if ((xTaskGetTickCount() - priorityCmd.timestamp) < pdMS_TO_TICKS(5000)) { // 5 second timeout
            normalCmd = priorityCmd.command;
            return true;
        } else {
            performanceMetrics.commandQueueDrops++;
            #ifdef DEBUG_SYSTEM_CRITICAL
            Serial.println("WARNING: Priority command timed out");
            #endif
        }
    }
    
    // Check normal command queue
    if (xQueueReceive(commandQueue, &normalCmd, 0) == pdTRUE) {
        return true;
    }
    
    return false;
}

// Add priority command to queue
bool addPriorityCommand(CommandType type, CommandPriority priority, float param1 = 0.0f, float param2 = 0.0f, float param3 = 0.0f) {
    PriorityCommand priorityCmd {};
    priorityCmd.command.type = type;
    priorityCmd.command.param1 = param1;
    priorityCmd.command.param2 = param2;
    priorityCmd.command.param3 = param3;
    priorityCmd.priority = priority;
    priorityCmd.timestamp = xTaskGetTickCount();
    
    if (xQueueSend(priorityCommandQueue, &priorityCmd, pdMS_TO_TICKS(timeoutConfig.queueOperationTimeout)) == pdTRUE) {
        return true;
    } else {
        performanceMetrics.commandQueueDrops++;
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.println("WARNING: Failed to add priority command to queue");
        #endif
        return false;
    }
}

// Update performance metrics
void updatePerformanceMetrics() {
    // Update memory metrics using built-in method
    performanceMetrics.updateMemoryMetrics();
    
    // Update stack metrics using built-in method
    performanceMetrics.updateStackMetrics();
    
    // Log critical alerts
    if (performanceMetrics.memoryAlertTriggered) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("MEMORY ALERT: Free heap below 20KB: %d bytes\n", performanceMetrics.freeHeap);
        #endif
    }
    
    if (performanceMetrics.stackAlertTriggered) {
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("STACK ALERT: Stack overflow detected, free stack: %d bytes\n", performanceMetrics.minStackFree);
        #endif
    }
}

// G-code processing with time slicing
bool processGCodeWithTimeSlicing(MachineState& cncState, GCodeProcessingState& gCodeState, 
                                AccelStepper& stepperX, AccelStepper& stepperY, MachineConfig& config) {
    uint32_t startTime = millis();
    
    // Original G-code processing logic here
    processGCode(cncState, gCodeState, stepperX, stepperY, config);
    
    uint32_t elapsedTime = millis() - startTime;
    
    // If processing took too long, yield control
    if (elapsedTime > timeoutConfig.gCodeProcessingTimeout) {
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to other tasks
        #ifdef DEBUG_SYSTEM_CRITICAL
        Serial.printf("INFO: G-code processing yielded after %dms\n", elapsedTime);
        #endif
    }
    
    return true;
}

/*
* ------------------------------------------------------------------------------------------------------------
* --- JOG and HOMING Function Implementations ---
* ------------------------------------------------------------------------------------------------------------
*/

// Process JOG command and setup movement parameters
void processJogCommand(const WebserverCommand& command, JogState& jogState, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config) {
    // Reset previous JOG state
    jogState.jogXPlus = false;
    jogState.jogXMinus = false;
    jogState.jogYPlus = false;
    jogState.jogYMinus = false;
    jogState.movementInProgress = false;
    
    // Set JOG parameters from command
    jogState.jogDistance = command.param1 != 0.0f ? command.param1 : 10.0f; // Default 10mm if not specified
    jogState.useRapidSpeed = command.param2 != 0.0f; // param2 = speed flag
    
    // Set movement direction based on command type
    switch (command.type) {
        case CommandType::JOG_X_PLUS:
            jogState.jogXPlus = true;
            break;
        case CommandType::JOG_X_MINUS:
            jogState.jogXMinus = true;
            break;
        case CommandType::JOG_Y_PLUS:
            jogState.jogYPlus = true;
            break;
        case CommandType::JOG_Y_MINUS:
            jogState.jogYMinus = true;
            break;
        case CommandType::JOG:
            // Generic JOG command with X/Y parameters
            if (command.param1 > 0) jogState.jogXPlus = true;
            else if (command.param1 < 0) jogState.jogXMinus = true;
            
            if (command.param2 > 0) jogState.jogYPlus = true;
            else if (command.param2 < 0) jogState.jogYMinus = true;
            
            jogState.jogDistance = std::abs(command.param1) + std::abs(command.param2);
            break;
        default:
            return; // Invalid command
    }
    
    jogState.isActive = true;
    
    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG JOG: Starting JOG movement - Distance: %.2f mm, Rapid: %s\n", 
                  jogState.jogDistance, jogState.useRapidSpeed ? "YES" : "NO");
    #endif
}

// Process ongoing JOG movement
void processJog(JogState& jogState, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, const MachineConfig& config) {
    if (!jogState.isActive) return;
    
    // If movement is in progress, check if it's finished
    if (jogState.movementInProgress) {
        if (!stepperX.isRunning() && !stepperY.isRunning()) {
            // Movement finished
            jogState.movementInProgress = false;
            jogState.isActive = false;
            
            #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG JOG: Movement completed");
            #endif
        }
        return;
    }
    
    // Start new movement if not in progress
    float moveDistance = jogState.jogDistance;
    long stepsX = 0, stepsY = 0;
    
    // Calculate step movements
    if (jogState.jogXPlus) stepsX = static_cast<long>(moveDistance * config.X.stepsPerMM);
    else if (jogState.jogXMinus) stepsX = static_cast<long>(-moveDistance * config.X.stepsPerMM);
    
    if (jogState.jogYPlus) stepsY = static_cast<long>(moveDistance * config.Y.stepsPerMM);
    else if (jogState.jogYMinus) stepsY = static_cast<long>(-moveDistance * config.Y.stepsPerMM);
    
    // Set motor speeds
    if (jogState.useRapidSpeed) {
        updateMotorSpeed('X', true, stepperX, config);
        updateMotorSpeed('Y', true, stepperY, config);
    } else {
        updateMotorSpeed('X', false, stepperX, config);
        updateMotorSpeed('Y', false, stepperY, config);
    }
    
    // Set target positions (relative movement)
    stepperX.move(stepsX);
    stepperY.move(stepsY);
    
    jogState.movementInProgress = true;
    
    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG JOG: Started movement - X steps: %ld, Y steps: %ld\n", stepsX, stepsY);
    #endif
}

// Process HOMING command and setup homing sequence
void processHomingCommand(HomingState& homingState, AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config) {
    // Initialize homing sequence
    homingState.isActive = true;
    homingState.movementInProgress = false;
    homingState.stage = HomingState::HomingStage::MOVING_X_TO_LIMIT;
    homingState.errorMessage = "";
    
    // Set homing speeds
    float homingSpeedX = homingState.homingSpeed / 60.0f * config.X.stepsPerMM; // Convert mm/min to steps/s
    float homingSpeedY = homingState.homingSpeed / 60.0f * config.Y.stepsPerMM;
    
    stepperX.setMaxSpeed(homingSpeedX);
    stepperY.setMaxSpeed(homingSpeedY);
    stepperX.setAcceleration(homingSpeedX * 2); // Quick acceleration for homing
    stepperY.setAcceleration(homingSpeedY * 2);
    
    #ifdef DEBUG_CNC_TASK
    Serial.println("DEBUG HOMING: Starting homing sequence");
    #endif
}

// Process ongoing HOMING sequence
void processHoming(HomingState& homingState, AccelStepper& stepperX, AccelStepper& stepperY, MachineState& cncState, const MachineConfig& config) {
    if (!homingState.isActive) return;
    
    switch (homingState.stage) {
        case HomingState::HomingStage::MOVING_X_TO_LIMIT:
            // TODO: Implement actual limit switch detection
            // For now, just simulate homing by moving to negative direction and then setting position to 0
            if (!homingState.movementInProgress) {
                stepperX.move(-10000); // Move large distance in negative direction
                homingState.movementInProgress = true;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG HOMING: Moving X to limit");
                #endif
            } else if (!stepperX.isRunning()) {
                // Simulated limit reached
                stepperX.setCurrentPosition(0);
                homingState.movementInProgress = false;
                homingState.stage = HomingState::HomingStage::BACKING_OFF_X;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG HOMING: X limit reached, backing off");
                #endif
            }
            break;
            
        case HomingState::HomingStage::BACKING_OFF_X:
            if (!homingState.movementInProgress) {
                long backoffSteps = static_cast<long>(homingState.backoffDistance * config.X.stepsPerMM);
                stepperX.move(backoffSteps);
                homingState.movementInProgress = true;
            } else if (!stepperX.isRunning()) {
                homingState.movementInProgress = false;
                homingState.stage = HomingState::HomingStage::MOVING_Y_TO_LIMIT;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG HOMING: Starting Y homing");
                #endif
            }
            break;
            
        case HomingState::HomingStage::MOVING_Y_TO_LIMIT:
            if (!homingState.movementInProgress) {
                stepperY.move(-10000); // Move large distance in negative direction
                homingState.movementInProgress = true;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG HOMING: Moving Y to limit");
                #endif
            } else if (!stepperY.isRunning()) {
                stepperY.setCurrentPosition(0);
                homingState.movementInProgress = false;
                homingState.stage = HomingState::HomingStage::BACKING_OFF_Y;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG HOMING: Y limit reached, backing off");
                #endif
            }
            break;
            
        case HomingState::HomingStage::BACKING_OFF_Y:
            if (!homingState.movementInProgress) {
                long backoffSteps = static_cast<long>(homingState.backoffDistance * config.Y.stepsPerMM);
                stepperY.move(backoffSteps);
                homingState.movementInProgress = true;
            } else if (!stepperY.isRunning()) {
                homingState.movementInProgress = false;
                homingState.stage = HomingState::HomingStage::FINISHED;
                cncState.isHomed = true;
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG HOMING: Homing sequence completed");
                #endif
            }
            break;
            
        case HomingState::HomingStage::FINISHED:
            homingState.isActive = false;
            break;
            
        case HomingState::HomingStage::ERROR:
            homingState.isActive = false;
            cncState.isHomed = false;
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG HOMING: Error - %s\n", homingState.errorMessage.c_str());
            #endif
            break;
            
        default:
            homingState.stage = HomingState::HomingStage::ERROR;
            homingState.errorMessage = "Unknown homing stage";
            break;
    }
}