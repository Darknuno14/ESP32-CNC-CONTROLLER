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
    ZERO
};

struct WebserverCommand {
    CommandType type;
    float param1 { 0 };
    float param2 { 0 };
    float param3 { 0 };
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

    // Stan urządzeń
    bool hotWireOn { false };  // Stan drutu
    bool fanOn { false };      // Stan wentylatora

    // Informacje o zadaniu
    std::string currentProject { "" };  // Nazwa aktualnego projektu
    float jobProgress { 0.0f };  // 0-100%
    int currentLine { 0 };       // Aktualnie przetwarzana linia G-code

    // Statystyki
    unsigned long jobStartTime { 0 };  // Czas rozpoczęcia zadania (millis)
    unsigned long jobRunTime { 0 };    // Czas pracy (ms)
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
    bool movementInProgress { false };
    
    // Dane o podgrzewaniu
    unsigned long heatingStartTime { 0 };
    unsigned long heatingDuration { 0 };  // Czas nagrzewania w ms
    
    // Dane o błędzie
    String errorMessage { "" };
};