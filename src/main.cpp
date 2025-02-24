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

#include "FSManager.h"
#include "SDManager.h"
#include "WiFiManager.h"
#include "WebServerManager.h"


/*-- GLOBAL SCOPE --*/

// Globalna instancja, aby był dostęp dla wszystkich zadań
SDCardManager* sdManager{new SDCardManager()};

// Zmienne globalne do komunikacji między zadaniami.
// Task1 ustawia tę flagę na true, na podstawie aktywności serwera web,
// a następnie Task2 odczytuje tę flagę i ją przetwarza
volatile bool commandStart{false}; 
volatile bool commandStop{false};
volatile bool commandJog{false};
volatile bool commandHoming{false}; 
volatile bool commandReset{false}; 

// Flaga aktywnego cięcia
volatile bool cuttingActive{false};
struct stepperParams {
    float stepsPerMM;

    float maxFeedRate;
    float maxAcceleration;

    float rapidFeedRate;
    float rapidAcceleration;

    float currentFeedRate;
    float currentAcceleration;
};

struct MachineState {
    float currentX {0.0f};
    float currentY {0.0f};

    bool relativeMode {false};       // False = absolute positioning, True = relative
    
    bool spindleOn {false};          
    bool isPaused {false};
    
    stepperParams X;
    stepperParams Y;
    
    QueueHandle_t progressQueue{nullptr};
};

// Struktura do raportowania postępu do interfejsu webowego
struct ProgressData {
    float x;
    float y;
    uint8_t status; // 0 = idle, 1 = running, 2 = paused, 3 = error 
};

/* --FUNCTION PROTOTYPES-- */

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager);
uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, const bool& pauseCondition);
void connectToWiFi(WiFiManager* wifiManager);
void startWebServer(WebServerManager* webServerManager);
float getParameter(const String& line, char param);
bool processGCodeLine(String line, MachineState& state, AccelStepper& stepperX, AccelStepper& stepperY);
uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, 
    volatile const bool& pauseCondition, AccelStepper& stepperX, AccelStepper& stepperY);

/*-- Tasks --*/

void taskControl(void * parameter) {
    Serial.printf("STATUS: Task1 started on core %d\n", xPortGetCoreID());

    FSManager* fsManager = new FSManager();
    WiFiManager* wifiManager = new WiFiManager();
    WebServerManager* webServerManager = new WebServerManager(sdManager); 

    initializeManagers(fsManager, sdManager, wifiManager, webServerManager);
    connectToWiFi(wifiManager);
    startWebServer(webServerManager);
    
    while (true) {
        commandStart = webServerManager->getStartCommand();
        commandStop = webServerManager->getStopCommand();
        vTaskDelay(pdMS_TO_TICKS(10)); // delay to prevent watchdog timeouts
    }
}
 
void taskCNC(void * parameter) {
    Serial.printf("STATUS: Task2 started on core %d\n", xPortGetCoreID());

    // Definicja stanów maszyny
    enum class CNCState {
        IDLE,           // Bezczynność, oczekiwanie na polecenia
        RUNNING,        // Wykonywanie programu G-code
        JOG,            // Tryb ręcznego sterowania
        HOMING,         // Wykonywanie sekwencji bazowania
        PAUSED,         // Wykonywanie programu wstrzymane
        STOPPED,        // Wykonywanie programu zatrzymane
        ERROR           // Wystąpił błąd
    };

    CNCState currentState {CNCState::IDLE};

    bool projectReady {false}; // Czy projekt jest wybrany
    std::string projectName {""}; // Nazwa wybranego projektu

    unsigned long lastStatusUpdateTime {0}; // Ostatnia aktualizacja statusu
    constexpr unsigned long STATUS_UPDATE_INTERVAL {500}; // Interwał aktualizacji statusu (500ms)

    volatile bool eStopTriggered {false};

    MachineState state {};
    state.relativeMode = false,
    state.X = {
        .maxFeedRate = 0.0,
        .maxAcceleration = 0.0,
        .rapidFeedRate = 0.0,
        .rapidAcceleration = 0.0
    };
    state.Y = {
        .maxFeedRate = 0.0,
        .maxAcceleration = 0.0,
        .rapidFeedRate = 0.0,
        .rapidAcceleration = 0.0
    };
    state.progressQueue = xQueueCreate(5, sizeof(ProgressData));

    while (true) {
        // Sprawdź, czy projekt jest wybrany
        projectReady = sdManager->isProjectSelected();
        if (projectReady) {
            projectName = sdManager->getSelectedProject();
        }

        // Sprawdź zatrzymanie awaryjne w dowolnym stanie
        if (eStopTriggered) {
            // Procedura zatrzymania awaryjnego
            
            // ZAIMPLEMENTOWAĆ 

            eStopTriggered = false; // TEMP
            Serial.println("ALERT: Uruchomiono zatrzymanie awaryjne!");
        }

        switch (currentState) {
            case CNCState::IDLE:
                // Stan bezczynności - maszyna jest gotowa na polecenia
                if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                    // Wysyłaj aktualizacje statusu okresowo
                    ProgressData data {state.currentX, state.currentX, 0}; // 0 = bezczynność
                    xQueueSend(state.progressQueue, &data, 0);
                    lastStatusUpdateTime = millis();
                }
                
                if (commandStart && projectReady) {
                    // Rozpocznij wykonywanie programu
                    #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC: Starting program: %s\n", projectName.c_str());
                    #endif
                    currentState = CNCState::RUNNING;
                } else if (commandJog) {
                    // Wejdź w tryb ręcznego sterowania
                    #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC: Entering JOG mode\n");
                    #endif
                    currentState = CNCState::JOG;
                } else if (commandHoming) {
                    // Rozpocznij sekwencję bazowania
                    #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC: Starting homing sequence\n");
                    #endif
                    currentState = CNCState::HOMING;
                }
                break;

            case CNCState::RUNNING:
                // Wykonywanie programu G-code
                Serial.printf("INFO: Uruchomiono program: %s\n", projectName.c_str());
                
                // Raportuj status wykonywania
                {
                    ProgressData data = {state.currentX, state.currentY, 1}; // 1 = wykonywanie
                    xQueueSend(state.progressQueue, &data, 0);
                }
                
                // Przetwarzaj plik G-code
                processResult = processGCodeFile(
                    projectName, 
                    commandStop, 
                    commandPause, 
                    sdManager
                    
                );
                
                // Obsłuż wynik
                switch (processResult) {
                    case 0: // Sukces
                        Serial.println("INFO: Program zakończony pomyślnie");
                        currentState = CNCState::IDLE;
                        break;
                    case 1: // Zatrzymany przez użytkownika
                        Serial.println("INFO: Program zatrzymany przez użytkownika");
                        currentState = CNCState::STOPPED;
                        break;
                    case 2: // Błąd podczas przetwarzania
                        Serial.println("ERROR: Wykonanie programu nie powiodło się");
                        currentState = CNCState::ERROR;
                        break;
                    default:
                        Serial.println("ERROR: Nieznany wynik przetwarzania");
                        currentState = CNCState::ERROR;
                        break;
                }
                
                // Upewnij się, że drut tnący jest wyłączony po zakończeniu programu
                digitalWrite(CONFIG::SPINDLE_PIN, LOW);
                cuttingWireActive = false;
                break;

            case CNCState::PAUSED:
                // Wykonywanie programu wstrzymane
                // Wyślij status wstrzymania
                if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                    ProgressData data = {currentX, currentY, 2}; // 2 = wstrzymano
                    xQueueSend(progressQueue, &data, 0);
                    lastStatusUpdateTime = millis();
                }
                
                // Sprawdź polecenie wznowienia lub zatrzymania
                if (commandStart) { // Wznów
                    currentState = CNCState::RUNNING;
                } else if (commandStop) { // Zatrzymaj
                    currentState = CNCState::STOPPED;
                }
                break;

            case CNCState::JOG:
                // Tryb ręcznego sterowania
                handleJogMode(stepperX, stepperY, currentX, currentY);
                
                // Wyślij status trybu JOG
                if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                    ProgressData data = {currentX, currentY, 5}; // 5 = tryb jog
                    xQueueSend(progressQueue, &data, 0);
                    lastStatusUpdateTime = millis();
                }
                
                // Sprawdź polecenie wyjścia
                if (commandStop) {
                    currentState = CNCState::IDLE;
                }
                break;

            case CNCState::HOMING:
                // Sekwencja bazowania
                Serial.println("INFO: Uruchomiono sekwencję bazowania");
                
                // Wyślij status bazowania
                ProgressData homingData = {currentX, currentY, 6}; // 6 = bazowanie
                xQueueSend(progressQueue, &homingData, 0);
                
                // Uruchom sekwencję bazowania
                if (runHomingSequence(stepperX, stepperY)) {
                    // Bazowanie zakończone powodzeniem
                    currentX = 0.0f;
                    currentY = 0.0f;
                    currentState = CNCState::IDLE;
                    
                    Serial.println("INFO: Bazowanie zakończone pomyślnie");
                } else {
                    // Bazowanie nie powiodło się
                    currentState = CNCState::ERROR;
                    Serial.println("ERROR: Sekwencja bazowania nie powiodła się");
                }
                break;

            case CNCState::STOPPED:
                // Program został zatrzymany
                // Wyślij status zatrzymania
                if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                    ProgressData data = {currentX, currentY, 3}; // 3 = zatrzymano
                    xQueueSend(progressQueue, &data, 0);
                    lastStatusUpdateTime = millis();
                }
                
                // Upewnij się, że drut tnący jest wyłączony
                if (cuttingWireActive) {
                    digitalWrite(CONFIG::SPINDLE_PIN, LOW);
                    cuttingWireActive = false;
                }
                
                // Czekaj na polecenie resetu
                if (commandReset) {
                    currentState = CNCState::IDLE;
                }
                break;

            case CNCState::ERROR:
                // Stan błędu
                // Wyślij status błędu
                if (millis() - lastStatusUpdateTime > STATUS_UPDATE_INTERVAL) {
                    ProgressData data = {currentX, currentY, 7}; // 7 = błąd
                    xQueueSend(progressQueue, &data, 0);
                    lastStatusUpdateTime = millis();
                }
                
                // Upewnij się, że drut tnący jest wyłączony
                if (cuttingWireActive) {
                    digitalWrite(CONFIG::SPINDLE_PIN, LOW);
                    cuttingWireActive = false;
                }
                
                // Czekaj na polecenie resetu
                if (commandReset) {
                    currentState = CNCState::IDLE;
                }
                break;
        }

        // Krótkie opóźnienie, aby zapobiec przekroczeniu czasu watchdoga i umożliwić działanie innych zadań
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*-- Main Program --*/

void setup() {
    Serial.begin(115200);
    delay(1000);

    SPI.begin(CONFIG::SD_CLK_PIN, CONFIG::SD_MISO_PIN, CONFIG::SD_MOSI_PIN);

    pinMode(CONFIG::WIRE_RELAY_PIN, OUTPUT); digitalWrite(CONFIG::WIRE_RELAY_PIN, LOW);
    pinMode(CONFIG::FAN_RELAY_PIN, OUTPUT); digitalWrite(CONFIG::FAN_RELAY_PIN, LOW);

    xTaskCreatePinnedToCore(taskControl,  // Task function
                            "Control",  // Task name
                            CONFIG::CONTROLTASK_STACK_SIZE,  // Stack size
                            NULL,   // Parameters
                            CONFIG::CONTROLTASK_PRIORITY,      // Priority
                            NULL,   // Task handle
                            CONFIG::CORE_0 // Core
                            );

    xTaskCreatePinnedToCore(taskCNC, 
                           "CNC", 
                           CONFIG::CNCTASK_STACK_SIZE, 
                           NULL,
                           CONFIG::CNCTASK_PRIORITY, 
                           NULL, 
                           CONFIG::CORE_1);
}

void loop() {
    digitalWrite(CONFIG::WIRE_RELAY_PIN, cuttingActive ? HIGH : LOW);
    digitalWrite(CONFIG::FAN_RELAY_PIN, cuttingActive ? HIGH : LOW);
}

/*-- MISCELLANEOUS FUNCTIONS --*/

void initializeManagers(FSManager* fsManager, SDCardManager* sdManager, WiFiManager* wifiManager, WebServerManager* webServerManager) {
    
    FSManagerStatus fsManagerStatus = fsManager->init();
    SDMenagerStatus sdManagerStatus = sdManager->init();
    WiFiManagerStatus wifiManagerStatus = wifiManager->init();
    WebServerStatus webServerManagerStatus = webServerManager->init();

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
        case SDMenagerStatus::OK: 
            Serial.println("STATUS: SD Manager initialized successfully."); break;
        case SDMenagerStatus::INIT_FAILED:
            Serial.println("ERROR: SD Manager initialization failed."); break;
        case SDMenagerStatus::DIRECTORY_CREATE_FAILED:
            Serial.println("ERROR: SD Manager directory creation failed."); break;
        case SDMenagerStatus::DIRECTORY_OPEN_FAILED:
            Serial.println("ERROR: SD Manager directory open failed."); break;
        case SDMenagerStatus::MUTEX_CREATE_FAILED:
            Serial.println("ERROR: SD Manager mutex creation failed."); break;
        case SDMenagerStatus::FILE_OPEN_FAILED:
            Serial.println("ERROR: SD Manager file open failed."); break;
        case SDMenagerStatus::CARD_NOT_INITIALIZED:
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
    int index {line.indexOf(param)};
    if (index == -1) return NAN;
    
    // Wyznaczanie początku tesktu z parametrem
    int valueStart {index + 1};
    int valueEnd {line.length()};
    
    // Wyznaczanie końca tekstu z parametrem (następna spacja lub koniec linii)
    for (int i{valueStart}; i < line.length(); ++i) {
        if (line[i] == ' ' || line[i] == '\t') {
            valueEnd = i;
            break;
        }
    }

    return line.substring(valueStart, valueEnd).toFloat();
}

bool processGCodeLine(String line, MachineState& state, AccelStepper& stepperX, AccelStepper& stepperY) {
    #ifdef DEBUG_CNC_TASK
        Serial.printf("DEBUG CNC GCODE: Processing: %s\n", line.c_str());
    #endif
    
    // Usuwanie białcyh znaków i pustych linii/komentarzy
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
                    
                    // Zaktualizuj prędkość posuwu jeśli określona
                    if (!isnan(feedRate)) {
                        state.currentFeedRate = constrain(feedRate, 0.1, state.maxFeedRate);
                    }
                    
                    // Ustaw prędkość ruchu w zależności od typu komendy
                    float moveSpeed {(gCode == 0) ? state.rapidFeedRate : state.currentFeedRate};
                    
                    // Oblicz ruch w krokach
                    long targetStepsX {newX * state.stepsPerMM};
                    long targetStepsY {newY * state.stepsPerMM};
                    
                    #ifdef DEBUG_CNC_TASK
                        Serial.printf("DEBUG CNC MOVE: X%.3f Y%.3f F%.1f\n", newX, newY, moveSpeed);
                    #endif
                    
                    // prędkości silników
                    float stepsPerSec {moveSpeed / 60.0f * state.stepsPerMM};
                    stepperX.setMaxSpeed(stepsPerSec);
                    stepperY.setMaxSpeed(stepsPerSec);
                    
                    // przyspieszenie
                    float acceleration {stepsPerSec * 2}; // TYMCZASOWO USTAWIONE 2 JAKO MULTIPLIKATOR -> DO POPRAWY
                    stepperX.setAcceleration(acceleration);
                    stepperY.setAcceleration(acceleration);
                    
                    // Ustaw pozycje docelowe
                    stepperX.moveTo(targetStepsX);
                    stepperY.moveTo(targetStepsY);
                    
                    // Wykonanie ruchu bez blokowania
                    bool moving {true};
                    unsigned long lastProgressUpdate {millis()};
                    
                    while (moving) {
                        // Uruchom silniki
                        stepperX.run();
                        stepperY.run();
                        
                        // Sprawdź czy ruch jest zakończony
                        if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
                            moving = false;
                        }
                        
                        // Raportuj postęp co 100ms
                        if (millis() - lastProgressUpdate > 100) {
                            float currentPosX {stepperX.currentPosition() / state.stepsPerMM};
                            float currentPosY {stepperY.currentPosition() / state.stepsPerMM};
                            
                            // Wyślij aktualizację postępu jeśli kolejka jest dostępna
                            if (state.progressQueue != nullptr) {
                                ProgressData data {currentPosX, currentPosY, 1};  // 1 = uruchomiony
                                xQueueSend(state.progressQueue, &data, 0);  // Nie blokuj jeśli kolejka jest pełna
                            }
                            
                            lastProgressUpdate = millis();
                        }
                        
                        vTaskDelay(1);
                    }
                    
                    // Zaktualizuj obecną pozycję
                    state.currentX = newX;
                    state.currentY = newY;
                    
                    // Końcowa aktualizacja pozycji
                    if (state.progressQueue != nullptr) {
                        ProgressData data = {state.currentX, state.currentY, 0};  // 0 = bezczynny
                        xQueueSend(state.progressQueue, &data, 0);
                    }
                }
                break;
                
            case 2:  // G2: Łuk zgodnie z ruchem wskazówek zegara
            case 3:  // G3: Łuk przeciwnie do ruchu wskazówek zegara
                // Jeszcze nie zaimplementowane - wymagałoby interpolacji łuku
                Serial.println("WARNING: Arc commands (G2/G3) not implemented yet");
                break;
                
            case 4:  // G4: Zatrzymanie czasowe
                {
                    float seconds {getParameter(line, 'P') / 1000.0f};  // P jest w milisekundach
                    if (isnan(seconds)) {
                        seconds = getParameter(line, 'S');  // S jest w sekundach
                    }
                    
                    if (!isnan(seconds)) {
                        #ifdef DEBUG_CNC_TASK
                            Serial.printf("DEBUG CNC DWELL: Pausing for %.2f seconds\n", seconds);
                        #endif
                        vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
                    }
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
                
                if (state.progressQueue != nullptr) {
                    ProgressData data = {0, 0, 0};  // 0 = bezczynny
                    xQueueSend(state.progressQueue, &data, 0);
                }
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
                Serial.printf("WARNING: Unsupported G-code: G%d\n", gCode);
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
                
                if (state.progressQueue != nullptr) {
                    ProgressData data = {state.currentX, state.currentY, 2};  // 2 = wstrzymany
                    xQueueSend(state.progressQueue, &data, 0);
                }
                
                // DODAĆ WARUNEK WZNOWIENIA
                break;
                
            case 3:  // M3: Włączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC WIRE: Wire ON");
                #endif
                cuttingActive = true;
                state.spindleOn = true;
                
                // Czekaj aż drut się nagrzeje
                vTaskDelay(pdMS_TO_TICKS(TEMP::delayAfterStartup));
                break;
                
            case 5:  // M5: Wyłączenie wrzeciona/drutu
                #ifdef DEBUG_CNC_TASK
                    Serial.println("DEBUG CNC WIRE: Wire OFF");
                #endif
                cuttingActive = false;
                state.spindleOn = false;
                break;
                
            default:
                Serial.printf("WARNING: Unsupported M-code: M%d\n", mCode);
                break;
        }
    }
    else {
        Serial.printf("WARNING: Unrecognized command: %s\n", command.c_str());
        return false;
    }
    
    return true;
}

uint8_t processGCodeFile(const std::string& filename, volatile const bool& stopCondition, 
    volatile const bool& pauseCondition, AccelStepper& stepperX, AccelStepper& stepperY) {
    #ifdef DEBUG_CNC_TASK
        Serial.println("DEBUG CNC STATUS: Starting to process file");
    #endif

    // Konfiguracja silników
    stepperX.setMaxSpeed(state.maxFeedRate / 60.0f * state.stepsPerMM);
    stepperY.setMaxSpeed(state.maxFeedRate / 60.0f * state.stepsPerMM);
    stepperX.setAcceleration(state.maxFeedRate / 30.0f * state.stepsPerMM);
    stepperY.setAcceleration(state.maxFeedRate / 30.0f * state.stepsPerMM);
    
    // Przejęcie dostępu do karty SD
    if (!sdManager->takeSD()) {
        #ifdef DEBUG_CNC_TASK
            Serial.println("DEBUG CNC ERROR: Nie udało się zablokować karty SD");
        #endif
        return 2; // Błąd: nie można zablokować karty SD
    }
    
    // Otwarcie pliku
    std::string filePath = CONFIG::PROJECTS_DIR + filename;
    File file = SD.open(filePath.c_str());
    
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
    
    // Wysłanie początkowej aktualizacji postępu
    if (state.progressQueue != nullptr) {
        ProgressData data = {state.currentX, state.currentY, 1}; // 1 = uruchomiony
        xQueueSend(state.progressQueue, &data, 0);
    }
    
    // Przetwarzanie pliku linia po linii
    uint32_t lineNumber = 0;
    bool processingError = false;
    
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
        if (!processGCodeLine(line, state, stepperX, stepperY)) {
            #ifdef DEBUG_CNC_TASK
                Serial.printf("DEBUG CNC ERROR: Błąd przetwarzania linii %d\n", lineNumber);
            #endif
            processingError = true;
            break;
        }
        
        // Oddaj czas innym zadaniom
        vTaskDelay(1);
    }
    
    // Wyłącz wrzeciono/drut jeśli jest włączony
    if (state.spindleOn || cuttingActive) {
        state.spindleOn = false;
        cuttingActive = false;
    }
    
    // Zamknij plik i zwolnij kartę SD
    file.close();
    sdManager->giveSD();
    
    // Wyślij końcową aktualizację statusu
    if (state.progressQueue != nullptr) {
        uint8_t finalStatus = stopCondition ? 3 : (processingError ? 3 : 0); // 0 = bezczynny, 3 = błąd
        ProgressData data = {state.currentX, state.currentY, finalStatus};
        xQueueSend(state.progressQueue, &data, 0);
        
        // Wyczyść kolejkę
        vQueueDelete(state.progressQueue);
        state.progressQueue = nullptr;
    }
    
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