#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AccelStepper.h>
#include <esp_task_wdt.h>

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

// Struktura przechowująca stan przetwarzania G-code
struct GCodeProcessingState {
    // Plik i status
    File currentFile;
    bool fileOpen = false;
    String currentLine = "";
    
    // Statystyki 
    uint32_t lineNumber = 0;
    uint32_t totalLines = 0;
    
    // Flagi kontrolne
    volatile bool stopRequested = false;
    volatile bool pauseRequested = false;
    
    // Stan przetwarzania
    enum class ProcessingStage {
        IDLE,
        MOVING_TO_OFFSET,
        READING_FILE,
        PROCESSING_LINE,
        EXECUTING_MOVEMENT,
        FINISHED,
        ERROR
    };
    
    ProcessingStage stage = ProcessingStage::IDLE;
    
    // Dane o ruchu
    float targetX = 0.0f;
    float targetY = 0.0f;
    bool movementInProgress = false;
};

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
void updateOutputs(const MachineState& state);

// Wykonuje ruch manualny (jog) na podstawie poleceń z interfejsu webowego
bool executeJogMovement(AccelStepper& stepperX, AccelStepper& stepperY, float xOffset, float yOffset, float speed,
    const MachineConfig& config, MachineState& state);

// Wykonuje sekwencję bazowania osi (homing)
bool executeHomingSequence(AccelStepper& stepperX, AccelStepper& stepperY, const MachineConfig& config,
    MachineState& state);

// Ładuje konfigurację z pliku lub używa domyślnych wartości
bool loadConfig(MachineConfig& config);

// Przetwarza komendę od interfejsu webowego
void processCommand(WebserverCommand& cmd, MachineState& state, 
                   GCodeProcessingState& gCodeState,
                   AccelStepper& stepperX, AccelStepper& stepperY,
                   MachineConfig& config);

// Inicjalizuje przetwarzanie pliku G-code
bool initializeGCodeProcessing(const std::string& filename, MachineState& state,
                            GCodeProcessingState& gCodeState, MachineConfig& config);

// Nieblokująca maszyna stanowa do przetwarzania G-code
void processGCodeStateMachine(MachineState& state, GCodeProcessingState& gCodeState,
                            AccelStepper& stepperX, AccelStepper& stepperY,
                            MachineConfig& config);

// Przetwarza pojedynczą linię G-code bez blokowania
bool processGCodeLineNonBlocking(String line, AccelStepper& stepperX, AccelStepper& stepperY,
                                MachineState& state, GCodeProcessingState& gCodeState,
                                MachineConfig& config);

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

    // Krótsze opóźnienie początkowe aby nie blokować
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Komunikacja między zadaniami
    TickType_t lastCommandProcessTime = 0;
    TickType_t lastStatusUpdateTime = 0;
    const TickType_t commandProcessInterval = pdMS_TO_TICKS(50);  // Częstsze sprawdzanie komend
    const TickType_t statusUpdateInterval = pdMS_TO_TICKS(200);   // Częstsze aktualizacje statusu

    // Stan maszyny
    MachineState state {};
    GCodeProcessingState gCodeState;

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

    while (true) {
        TickType_t currentTime = xTaskGetTickCount();

        // 1. Odbieranie komend z kolejki
        if ((currentTime - lastCommandProcessTime) >= commandProcessInterval) {
            if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: Otrzymano komendę typu %d z parametrami: %.2f, %.2f, %.2f\n", 
                    static_cast<int>(cmd.type), cmd.param1, cmd.param2, cmd.param3);
                #endif
                commandPending = true;
            }
            lastCommandProcessTime = currentTime;
        }

        // 2. Przetwarzanie komend
        if (commandPending) {
            processCommand(cmd, state, gCodeState, stepperX, stepperY, config);
            commandPending = false;
        }

        // 3. Wysyłanie statusu maszyny
        if ((currentTime - lastStatusUpdateTime) >= statusUpdateInterval) {
            // Tymczasowo używamy tylko logowania - docelowo wyślemy przez kolejkę
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: Stan maszyny: %d, Pozycja: X=%.3f, Y=%.3f, Postęp: %.1f%%\n",
                static_cast<int>(state.state), state.currentX, state.currentY, state.jobProgress);
            #endif
            
            // W przyszłości można dodać pełne wysyłanie do kolejki stateQueue
            // Ale trzeba uważać na string w MachineState (currentProject)
            
            lastStatusUpdateTime = currentTime;
        }

        // 4. Wykonanie odpowiednich działań w zależności od stanu maszyny
        switch (state.state) {
            case CNCState::IDLE:
                // W stanie IDLE nic nie robimy, czekamy na komendy
                break;

            case CNCState::RUNNING:
                // W stanie RUNNING przetwarzamy plik G-code nieblokująco
                if (!gCodeState.pauseRequested) {
                    // Wykonaj tylko jeden krok maszyny stanowej
                    processGCodeStateMachine(state, gCodeState, stepperX, stepperY, config);
                }
                break;

            case CNCState::JOG:
                // Wykonanie jednego kroku JOG
                if (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                    stepperX.run();
                    stepperY.run();
                    
                    // Aktualizacja pozycji
                    state.currentX = stepperX.currentPosition() / config.xAxis.stepsPerMM;
                    state.currentY = stepperY.currentPosition() / config.yAxis.stepsPerMM;
                }
                else {
                    // Zakończono ruch JOG
                    state.state = CNCState::IDLE;
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC: Ruch JOG zakończony. Pozycja: X=%.3f, Y=%.3f\n", 
                        state.currentX, state.currentY);
                    #endif
                }
                break;

            case CNCState::HOMING:
                // W stanie HOMING sprawdzamy, czy bazowanie zostało zakończone
                // Aktualnie funkcja executeHomingSequence jest blokująca i wykonujemy ją
                // bezpośrednio w obsłudze komendy
                state.state = CNCState::IDLE;
                break;

            case CNCState::STOPPED:
            case CNCState::ERROR:
                // Zatrzymany/błąd - nic nie robimy, czekamy na RESET
                break;
        }

        // 5. Aktualizacja stanu wyjść
        updateOutputs(state);

        // 6. Krótka przerwa, zależna od stanu maszyny
        if (state.state == CNCState::RUNNING && 
            gCodeState.stage == GCodeProcessingState::ProcessingStage::EXECUTING_MOVEMENT) {
            // Podczas ruchu używamy krótszej przerwy, aby nie spowalniać
            vTaskDelay(1);  // Minimalne opóźnienie, aby dać szansę inne zadania
        } else {
            // W innych stanach dłuższa przerwa dla oszczędzania energii
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

    // Bufor na przychodzący stan maszyny
    MachineState currentState {};

    // Przechowujemy ostatni stan maszyny
    MachineState lastState {};
    memset(&lastState, 0, sizeof(MachineState));

    while (true) {
        // TODO: Odbiór statusu maszyny z kolejki i przekazanie do webserwera

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
    
    // Zwiększ timeout watchdoga
    esp_task_wdt_init(10, true); // 10 sekund zamiast domyślnych 5

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

void processCommand(WebserverCommand& cmd, MachineState& state, 
                   GCodeProcessingState& gCodeState,
                   AccelStepper& stepperX, AccelStepper& stepperY,
                   MachineConfig& config) {
    
    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: Przetwarzanie komendy typu %d z parametrami: %.2f, %.2f, %.2f\n", 
        static_cast<int>(cmd.type), cmd.param1, cmd.param2, cmd.param3);
    #endif
    
    switch (state.state) {
        case CNCState::IDLE:
            switch (cmd.type) {
                case CommandType::START:
                    if (sdManager->isProjectSelected()) {
                        std::string projectName;
                        if (sdManager->getSelectedProject(projectName) == SDManagerStatus::OK) {
                            #ifdef DEBUG_CNC_TASK
                            Serial.printf("DEBUG CNC: Rozpoczynam przetwarzanie projektu: %s\n", projectName.c_str());
                            #endif
                            
                            if (initializeGCodeProcessing(projectName, state, gCodeState, config)) {
                                state.state = CNCState::RUNNING;
                                state.isPaused = false;
                                gCodeState.stopRequested = false;
                                gCodeState.pauseRequested = false;
                                gCodeState.stage = GCodeProcessingState::ProcessingStage::MOVING_TO_OFFSET;
                            } else {
                                #ifdef DEBUG_CNC_TASK
                                Serial.println("DEBUG CNC ERROR: Nie udało się uruchomić przetwarzania projektu");
                                #endif
                            }
                        } else {
                            #ifdef DEBUG_CNC_TASK
                            Serial.println("DEBUG CNC ERROR: Nie udało się pobrać nazwy projektu");
                            #endif
                        }
                    } else {
                        #ifdef DEBUG_CNC_TASK
                        Serial.println("DEBUG CNC ERROR: Nie wybrano projektu");
                        #endif
                    }
                    break;
                    
                case CommandType::JOG:
                    if (cmd.param1 == -1 && cmd.param2 == -1) {
                        // Obsługa drut grzejny
                        state.hotWireOn = (cmd.param3 > 0.5f);
                        updateOutputs(state);
                        
                        #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC: Drut grzejny %s\n", state.hotWireOn ? "WŁĄCZONY" : "WYŁĄCZONY");
                        #endif
                    } else if (cmd.param1 == -2 && cmd.param2 == -2) {
                        // Obsługa wentylator
                        state.fanOn = (cmd.param3 > 0.5f);
                        updateOutputs(state);
                        
                        #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC: Wentylator %s\n", state.fanOn ? "WŁĄCZONY" : "WYŁĄCZONY");
                        #endif
                    } else {
                        // Normalny ruch JOG
                        state.state = CNCState::JOG;
                        executeJogMovement(stepperX, stepperY, cmd.param1, cmd.param2, 
                                          cmd.param3 > 0 ? cmd.param3 : config.xAxis.workFeedRate, 
                                          config, state);
                    }
                    break;
                    
                case CommandType::HOMING:
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Rozpoczynam bazowanie");
                    #endif
                    
                    state.state = CNCState::HOMING;
                    executeHomingSequence(stepperX, stepperY, config, state);
                    state.state = CNCState::IDLE;
                    break;
                    
                case CommandType::ZERO:
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Zerowanie pozycji");
                    #endif
                    
                    // Resetuj pozycję
                    stepperX.setCurrentPosition(0);
                    stepperY.setCurrentPosition(0);
                    state.currentX = 0;
                    state.currentY = 0;
                    break;
                    
                case CommandType::RESET:
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Reset systemu");
                    #endif
                    
                    // Reset do stanu początkowego
                    stepperX.setCurrentPosition(0);
                    stepperY.setCurrentPosition(0);
                    state.currentX = 0;
                    state.currentY = 0;
                    state.relativeMode = false;
                    state.hotWireOn = false;
                    state.fanOn = false;
                    state.isPaused = false;
                    state.hasError = false;
                    updateOutputs(state);
                    break;
                    
                default:
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC WARNING: Nieznana komenda typu %d w stanie IDLE\n", 
                        static_cast<int>(cmd.type));
                    #endif
                    break;
            }
            break;
            
        case CNCState::RUNNING:
            switch (cmd.type) {
                case CommandType::STOP:
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Zatrzymanie przetwarzania");
                    #endif
                    
                    gCodeState.stopRequested = true;
                    break;
                    
                case CommandType::PAUSE:
                    gCodeState.pauseRequested = !gCodeState.pauseRequested;
                    state.isPaused = gCodeState.pauseRequested;
                    
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC: %s przetwarzania\n", 
                        state.isPaused ? "Wstrzymanie" : "Wznowienie");
                    #endif
                    break;
                    
                case CommandType::RESET:
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Reset systemu podczas przetwarzania");
                    #endif
                    
                    gCodeState.stopRequested = true;
                    state.state = CNCState::IDLE;
                    state.hotWireOn = false;
                    state.fanOn = false;
                    state.isPaused = false;
                    state.hasError = false;
                    updateOutputs(state);
                    break;
                    
                default:
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC WARNING: Nieznana komenda typu %d w stanie RUNNING\n", 
                        static_cast<int>(cmd.type));
                    #endif
                    break;
            }
            break;
            
        case CNCState::JOG:
            if (cmd.type == CommandType::STOP || cmd.type == CommandType::RESET) {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Przerwanie ruchu JOG");
                #endif
                
                state.state = CNCState::IDLE;
                stepperX.stop();
                stepperY.stop();
            }
            break;
            
        case CNCState::HOMING:
            if (cmd.type == CommandType::STOP || cmd.type == CommandType::RESET) {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Przerwanie procesu bazowania");
                #endif
                
                state.state = CNCState::IDLE;
                stepperX.stop();
                stepperY.stop();
            }
            break;
            
        case CNCState::STOPPED:
        case CNCState::ERROR:
            if (cmd.type == CommandType::RESET) {
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Reset systemu ze stanu błędu/zatrzymania");
                #endif
                
                state.state = CNCState::IDLE;
                state.hotWireOn = false;
                state.fanOn = false;
                state.isPaused = false;
                state.hasError = false;
                updateOutputs(state);
            }
            break;
            
        default:
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC WARNING: Nieznany stan maszyny: %d\n", 
                static_cast<int>(state.state));
            #endif
            break;
    }
}

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
    
    // Nie przeliczaj dokładnej liczby linii - to zajmuje za dużo czasu
    // Zamiast tego oszacuj liczbę linii na podstawie rozmiaru pliku
    // zakładając średnio 20 bajtów na linię
    gCodeState.totalLines = gCodeState.currentFile.size() / 20;
    
    gCodeState.fileOpen = true;
    
    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC: Otwarto plik %s, liczba linii: %d\n", 
        filePath.c_str(), gCodeState.totalLines);
    #endif
    
    sdManager->giveSD();
    
    // Inicjalizacja stanu maszyny
    state.currentProject = filename;
    state.jobStartTime = millis();
    state.jobProgress = 0.0f;
    state.currentLine = 0;
    state.hotWireOn = false;
    state.fanOn = false;
    updateOutputs(state);
    
    return true;
}

void processGCodeStateMachine(MachineState& state, GCodeProcessingState& gCodeState,
                            AccelStepper& stepperX, AccelStepper& stepperY,
                            MachineConfig& config) {
    // Sprawdź warunek zatrzymania
    if (gCodeState.stopRequested) {
        if (gCodeState.fileOpen) {
            // Zamknij plik
            sdManager->takeSD();
            gCodeState.currentFile.close();
            gCodeState.fileOpen = false;
            sdManager->giveSD();
        }
        
        // Zatrzymaj silniki
        stepperX.stop();
        stepperY.stop();
        
        // Aktualizuj stan
        state.state = CNCState::STOPPED;
        state.hotWireOn = false;
        updateOutputs(state);
        gCodeState.stage = GCodeProcessingState::ProcessingStage::IDLE;
        
        #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC: Przetwarzanie zatrzymane");
        #endif
        return;
    }
    
    // Sprawdź pauzę
    if (gCodeState.pauseRequested) {
        // W trybie pauzy nie rób nic, czekaj na wznowienie
        return;
    }
    
    // Główna maszyna stanowa dla przetwarzania G-code - wykonaj tylko JEDEN krok na iterację pętli
    switch (gCodeState.stage) {
        case GCodeProcessingState::ProcessingStage::IDLE:
            // Jeśli jesteśmy w stanie IDLE, przejdź do przesunięcia offsetu
            gCodeState.stage = GCodeProcessingState::ProcessingStage::MOVING_TO_OFFSET;
            #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC: Rozpoczynam przetwarzanie G-code");
            #endif
            break;
            
        case GCodeProcessingState::ProcessingStage::MOVING_TO_OFFSET:
            // Wykonaj ruch do pozycji offsetu, jeśli potrzebny
            if (config.offsetX != 0.0f || config.offsetY != 0.0f) {
                if (!gCodeState.movementInProgress) {
                    // Inicjalizuj ruch offsetowy
                    long targetStepsX = static_cast<long>(config.offsetX * config.xAxis.stepsPerMM);
                    long targetStepsY = static_cast<long>(config.offsetY * config.yAxis.stepsPerMM);
                    
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC: Przesunięcie do pozycji początkowej: X=%.2f, Y=%.2f\n",
                        config.offsetX, config.offsetY);
                    #endif
                    
                    // Ustaw parametry ruchu
                    stepperX.setMaxSpeed(config.xAxis.rapidFeedRate / 60.0f * config.xAxis.stepsPerMM);
                    stepperY.setMaxSpeed(config.yAxis.rapidFeedRate / 60.0f * config.yAxis.stepsPerMM);
                    stepperX.setAcceleration(config.xAxis.rapidAcceleration * config.xAxis.stepsPerMM);
                    stepperY.setAcceleration(config.yAxis.rapidAcceleration * config.yAxis.stepsPerMM);
                    
                    // Ustaw pozycje docelowe
                    stepperX.moveTo(targetStepsX);
                    stepperY.moveTo(targetStepsY);
                    gCodeState.movementInProgress = true;
                }
                
                // Wykonaj TYLKO JEDEN krok ruchu
                stepperX.run();
                stepperY.run();
                
                // Aktualizuj pozycję
                state.currentX = stepperX.currentPosition() / config.xAxis.stepsPerMM;
                state.currentY = stepperY.currentPosition() / config.yAxis.stepsPerMM;
                
                // Sprawdź, czy ruch został zakończony
                if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                    gCodeState.movementInProgress = false;
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
                    
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Osiągnięto pozycję początkową");
                    #endif
                }
            } else {
                // Brak offsetu, przejdź od razu do czytania pliku
                gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
                
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Brak offsetu, przechodzę do czytania pliku");
                #endif
            }
            break;
            
        case GCodeProcessingState::ProcessingStage::READING_FILE:
            // Odczytaj linię z pliku
            if (!sdManager->takeSD()) {
                // Nie udało się zablokować karty SD, spróbuj później
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC ERROR: Nie udało się zablokować karty SD podczas czytania");
                #endif
                break;
            }
            
            if (gCodeState.currentFile.available()) {
                gCodeState.currentLine = gCodeState.currentFile.readStringUntil('\n');
                gCodeState.lineNumber++;
                state.currentLine = gCodeState.lineNumber;
                
                // Oblicz postęp
                float filePosition = gCodeState.currentFile.position();
                float fileSize = gCodeState.currentFile.size();
                state.jobProgress = (filePosition / fileSize) * 100.0f;
                
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC: Czytam linię %d: %s (postęp: %.1f%%)\n", 
                    gCodeState.lineNumber, gCodeState.currentLine.c_str(), state.jobProgress);
                #endif
                
                sdManager->giveSD();
                gCodeState.stage = GCodeProcessingState::ProcessingStage::PROCESSING_LINE;
            } else {
                // Koniec pliku
                gCodeState.currentFile.close();
                gCodeState.fileOpen = false;
                sdManager->giveSD();
                
                state.state = CNCState::IDLE;
                state.hotWireOn = false;
                updateOutputs(state);
                gCodeState.stage = GCodeProcessingState::ProcessingStage::FINISHED;
                
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Przetwarzanie pliku zakończone");
                #endif
            }
            break;
            
        case GCodeProcessingState::ProcessingStage::PROCESSING_LINE:
            // Przetwórz linię G-code bez blokowania
            if (processGCodeLineNonBlocking(gCodeState.currentLine, stepperX, stepperY,
                                          state, gCodeState, config)) {
                // Jeśli linia zawiera ruch, przejdź do fazy wykonywania ruchu
                if (gCodeState.movementInProgress) {
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::EXECUTING_MOVEMENT;
                    
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Rozpoczęcie wykonywania ruchu");
                    #endif
                } else {
                    // Jeśli nie ma ruchu, przejdź od razu do następnej linii
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
                    
                    #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC: Brak ruchu, przechodzę do następnej linii");
                    #endif
                }
            } else {
                // Błąd przetwarzania linii
                gCodeState.stage = GCodeProcessingState::ProcessingStage::ERROR;
                state.state = CNCState::ERROR;
                state.hasError = true;
                
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC ERROR: Błąd przetwarzania linii %d\n", gCodeState.lineNumber);
                #endif
            }
            break;
            
        case GCodeProcessingState::ProcessingStage::EXECUTING_MOVEMENT:
            // Wykonaj JEDEN krok ruchu z linii G-code
            if (gCodeState.movementInProgress) {
                // Wykonaj tylko jeden krok, aby nie blokować watchdoga
                bool xMoved = stepperX.run();
                bool yMoved = stepperY.run();
                
                // Aktualizuj pozycję
                state.currentX = stepperX.currentPosition() / config.xAxis.stepsPerMM;
                state.currentY = stepperY.currentPosition() / config.yAxis.stepsPerMM;
                
                // Sprawdź, czy ruch został zakończony
                if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                    gCodeState.movementInProgress = false;
                    gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
                    
                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC: Ruch zakończony. Pozycja: X=%.3f, Y=%.3f\n", 
                        state.currentX, state.currentY);
                    #endif
                }
            } else {
                // Brak ruchu do wykonania, przejdź do kolejnej linii
                gCodeState.stage = GCodeProcessingState::ProcessingStage::READING_FILE;
                
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC: Brak aktywnego ruchu, przechodzę do następnej linii");
                #endif
            }
            break;
            
        case GCodeProcessingState::ProcessingStage::FINISHED:
            // Zakończono przetwarzanie pliku G-code
            state.state = CNCState::IDLE;
            state.jobProgress = 100.0f;
            state.jobRunTime = millis() - state.jobStartTime;
            
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC: Przetwarzanie zakończone. Czas: %lu ms\n", state.jobRunTime);
            #endif
            break;
            
        case GCodeProcessingState::ProcessingStage::ERROR:
            // Błąd przetwarzania
            state.state = CNCState::ERROR;
            state.hasError = true;
            
            #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Błąd przetwarzania G-code");
            #endif
            break;
            
        default:
            // Stan nieznany
            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC WARNING: Nieznany stan przetwarzania: %d\n", 
                static_cast<int>(gCodeState.stage));
            #endif
            break;
    }
    
    // Aktualizuj czas pracy
    if (state.state == CNCState::RUNNING && !state.isPaused) {
        state.jobRunTime = millis() - state.jobStartTime;
    }
}

bool processGCodeLineNonBlocking(String line, AccelStepper& stepperX, AccelStepper& stepperY,
                                MachineState& state, GCodeProcessingState& gCodeState,
                                MachineConfig& config) {
    // Usuń komentarze i białe znaki
    line.trim();
    if (line.length() == 0)
        return true;

    int commentIndex = line.indexOf(';');
    if (commentIndex != -1) {
        line = line.substring(0, commentIndex);
        line.trim();
        if (line.length() == 0)
            return true;
    }

    // Parsuj komendę
    int spaceIndex = line.indexOf(' ');
    String command = (spaceIndex != -1) ? line.substring(0, spaceIndex) : line;
    command.toUpperCase();

    #ifdef DEBUG_CNC_TASK
    Serial.printf("DEBUG CNC GCODE: Przetwarzam linię: %s\n", line.c_str());
    #endif

    // Przetwarzanie komend G-code bez blokowania
    if (command.startsWith("G")) {
        int gCode = command.substring(1).toInt();

        switch (gCode) {
            case 0: // G0: Szybki ruch
            case 1: // G1: Ruch liniowy
                {
                    float targetX = getParameter(line, 'X');
                    float targetY = getParameter(line, 'Y');
                    float feedRate = getParameter(line, 'F');

                    // Oblicz nowe pozycje
                    float newX = isnan(targetX) ? state.currentX : (state.relativeMode ? state.currentX + targetX : targetX);
                    float newY = isnan(targetY) ? state.currentY : (state.relativeMode ? state.currentY + targetY : targetY);

                    // Ustaw prędkości w zależności od rodzaju ruchu
                    float moveSpeedX = (gCode == 0) ? config.xAxis.rapidFeedRate :
                        (config.useGCodeFeedRate && !isnan(feedRate)) ?
                        min(feedRate, config.xAxis.workFeedRate) : config.xAxis.workFeedRate;

                    float moveSpeedY = (gCode == 0) ? config.yAxis.rapidFeedRate :
                        (config.useGCodeFeedRate && !isnan(feedRate)) ?
                        min(feedRate, config.yAxis.workFeedRate) : config.yAxis.workFeedRate;

                    // Przelicz na kroki
                    long targetStepsX = static_cast<long>(newX * config.xAxis.stepsPerMM);
                    long targetStepsY = static_cast<long>(newY * config.yAxis.stepsPerMM);

                    // Prędkości silników
                    float stepsXPerSec = moveSpeedX / 60.0f * config.xAxis.stepsPerMM;
                    float stepsYPerSec = moveSpeedY / 60.0f * config.yAxis.stepsPerMM;

                    #ifdef DEBUG_CNC_TASK
                    Serial.printf("DEBUG CNC MOVE: X=%.3f Y=%.3f VX=%.3f VY=%.3f\n", 
                        newX, newY, moveSpeedX, moveSpeedY);
                    #endif

                    // Ustaw parametry silników
                    stepperX.setMaxSpeed(stepsXPerSec);
                    stepperY.setMaxSpeed(stepsYPerSec);
                    stepperX.setAcceleration(config.xAxis.rapidAcceleration * config.xAxis.stepsPerMM);
                    stepperY.setAcceleration(config.yAxis.rapidAcceleration * config.yAxis.stepsPerMM);

                    // Ustaw ruch
                    stepperX.moveTo(targetStepsX);
                    stepperY.moveTo(targetStepsY);

                    // Zapamiętaj cel ruchu
                    gCodeState.targetX = newX;
                    gCodeState.targetY = newY;
                    gCodeState.movementInProgress = true;
                }
                break;

            case 28: // G28: Bazowanie
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC HOME: Bazowanie osi X i Y");
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
                Serial.println("DEBUG CNC MODE: Pozycjonowanie absolutne");
                #endif
                break;

            case 91: // G91: Pozycjonowanie względne
                state.relativeMode = true;
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
        int mCode = command.substring(1).toInt();

        switch (mCode) {
            case 0: // M0: Pauza programu
            case 1: // M1: Opcjonalna pauza programu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC PAUSE: Program wstrzymany, oczekiwanie na wznowienie");
                #endif
                
                state.isPaused = true;
                gCodeState.pauseRequested = true;
                break;

            case 3: // M3: Włączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC WIRE: Drut włączony");
                #endif
                
                state.hotWireOn = true;
                updateOutputs(state);
                break;

            case 5: // M5: Wyłączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC WIRE: Drut wyłączony");
                #endif
                
                state.hotWireOn = false;
                updateOutputs(state);
                break;
                
            case 106: // M106: Włączenie wentylatora
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC FAN: Wentylator włączony");
                #endif
                
                state.fanOn = true;
                updateOutputs(state);
                break;
                
            case 107: // M107: Wyłączenie wentylatora
                #ifdef DEBUG_CNC_TASK
                Serial.println("DEBUG CNC FAN: Wentylator wyłączony");
                #endif
                
                state.fanOn = false;
                updateOutputs(state);
                break;

            default:
                #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC WARNING: Nieobsługiwany kod M: M%d\n", mCode);
                #endif
                break;
        }
    }
    else if (command.startsWith("F")) {
        float feedRate = getParameter(line, 'F');

        if (!isnan(feedRate) && config.useGCodeFeedRate) {
            float moveSpeedX = min(feedRate, config.xAxis.workFeedRate);
            float moveSpeedY = min(feedRate, config.yAxis.workFeedRate);

            #ifdef DEBUG_CNC_TASK
            Serial.printf("DEBUG CNC FEEDRATE: F=%.3f (X=%.3f, Y=%.3f)\n", 
                feedRate, moveSpeedX, moveSpeedY);
            #endif

            stepperX.setMaxSpeed(moveSpeedX / 60.0f * config.xAxis.stepsPerMM);
            stepperY.setMaxSpeed(moveSpeedY / 60.0f * config.yAxis.stepsPerMM);
            stepperX.setAcceleration(config.xAxis.workAcceleration * config.xAxis.stepsPerMM);
            stepperY.setAcceleration(config.yAxis.workAcceleration * config.yAxis.stepsPerMM);
        }
    }
    else {
        #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC WARNING: Nierozpoznana komenda: %s\n", command.c_str());
        #endif
        
        // Ignorujemy nieznane komendy, ale nie traktujemy ich jako błąd
        return true;
    }

    return true;
}

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

void updateOutputs(const MachineState& state) {
    digitalWrite(CONFIG::WIRE_RELAY_PIN, state.hotWireOn ? HIGH : LOW);
    digitalWrite(CONFIG::FAN_RELAY_PIN, state.fanOn ? HIGH : LOW);
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
    state.currentX = 0;
    state.currentY = 0;

    return true;
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