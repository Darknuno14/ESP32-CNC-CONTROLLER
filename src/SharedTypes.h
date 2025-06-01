#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>

enum class CommandType {
    START,
    STOP,
    PAUSE,
    HOME,
    RESET,
    JOG,
    ZERO,
    RELOAD_CONFIG,
    SET_HOTWIRE,      // Sterowanie drutem grzejnym
    SET_FAN,          // Sterowanie wentylatorem
};

struct WebserverCommand {
    CommandType type;
    float param1 { 0 };
    float param2 { 0 };
    float param3 { 0 };
    float param4 { 0 };
};

enum class CNCState {
    IDLE,           // Bezczynność, oczekiwanie na polecenia
    RUNNING,        // Wykonywanie programu G-code
    JOG,            // Ruch ręczny
    HOMING,         // Wykonywanie sekwencji bazowania
    STOPPED,        // Działanie maszyny zatrzymane
    ERROR           // Błąd maszyny
};

struct MachineState {
    // Pozycja i ruch
    float currentX { 0.0f };
    float currentY { 0.0f };
    bool relativeMode { false };  // False = absolute positioning, True = relative

    // Stan operacyjny
    CNCState state { CNCState::IDLE };
    bool isPaused { false };
    uint8_t errorID { false };

    // Stan IO
    bool estopOn { false };  // Stan awaryjny
    bool limitXOn { false }; // Stan krańcówki X
    bool limitYOn { false }; // Stan krańcówki Y
    
    bool hotWireOn { false };  // Stan drutu
    bool fanOn { false };      // Stan wentylatora

    uint8_t hotWirePower { 0 };  // Moc drutu (0-255)
    uint8_t fanPower { 0 };      // Moc wentylatora (0-255)

    // Informacje o zadaniu
    char currentProject[20] ;  // Nazwa aktualnego projektu
    uint32_t currentLine { 0 };       // Aktualnie przetwarzana linia G-code
    uint32_t totalLines { 0 };        // Łączna liczba linii G-code
    TickType_t jobStartTime { 0 };  // Czas rozpoczęcia zadania (millis)

    // Statystyki
    TickType_t jobRunTime { 0 };    // Czas pracy maszyny (millis)
    float jobProgress { 0.0f }; // Procent ukończenia zadania (0-100%)
};

struct GCodeProcessingState {
    // Plik i status
    File currentFile {};
    bool fileOpen { false };
    String currentLine { "" };

    // Statystyki 
    uint32_t lineNumber { 0 };
    uint32_t totalLines { 0 };

    // Flagi kontrolne
    volatile bool stopRequested { false };
    volatile bool pauseRequested { false };

    // Stan przetwarzania
    enum class ProcessingStage {
        IDLE,
        INITIALIZING,
        HEATING,
        MOVING_TO_OFFSET,
        READING_FILE,
        PROCESSING_LINE,
        EXECUTING_MOVEMENT,
        FINISHED,
        ERROR
    };

    ProcessingStage stage { ProcessingStage::IDLE };

    // Dane o ruchu
    float targetX { 0.0f };
    float targetY { 0.0f };
    float currentFeedRate { 0.0f };
    bool movementInProgress { false };
    
    // Dane o podgrzewaniu
    unsigned long heatingStartTime { 0 };
    unsigned long heatingDuration { 0 };  // Czas nagrzewania w ms
    
    // Dane o błędzie
    String errorMessage { "" };
};

struct HomingState {
    // Stan bazowania
    enum class HomingStage {
        IDLE,           // Oczekiwanie na rozpoczęcie bazowania
        HOMING_X,       // Bazowanie osi X
        HOMING_Y,       // Bazowanie osi Y
        FINISHED,       // Bazowanie zakończone
        ERROR           // Błąd podczas bazowania
    };
    
    HomingStage stage { HomingStage::IDLE };
    
    // Parametry bazowania
    float homingSpeed { 10.0f };       // Prędkość bazowania w mm/s
    float homingAcceleration { 10.0f }; // Akceleracja bazowania w mm/s²
    float backoffDistance { 2.0f };    // Odległość wycofania po dotknięciu krańcówki w mm
    
    // Stan procesu
    bool movementInProgress { false };
    bool limitReached { false };
    bool backoffComplete { false };
    
    // Informacje o błędzie
    String errorMessage { "" };
};