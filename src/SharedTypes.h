#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>
#include <Arduino.h>
#include <FS.h>
#include <SD.h>

enum class CommandType {
    START,
    STOP,
    PAUSE,
    HOME,
    RESET,
    JOG,
    ZERO,
    RELOAD_CONFIG,
    // Nowe komendy dla lepszej kontroli
    JOG_X_PLUS,     // Ruch +X
    JOG_X_MINUS,    // Ruch -X
    JOG_Y_PLUS,     // Ruch +Y
    JOG_Y_MINUS,    // Ruch -Y
    SET_WIRE_POWER, // Ustawienie mocy drutu
    SET_FAN_POWER,  // Ustawienie mocy wentylatora
    EMERGENCY_STOP  // Awaryjne zatrzymanie
};

struct WebserverCommand {
    CommandType type;
    float param1 { 0 };  // Dla JOG: odległość w mm
    float param2 { 0 };  // Dla JOG: prędkość (true=rapid, false=work)
    float param3 { 0 };  // Dla SET_POWER: wartość mocy 0-100%
    float param4 { 0 };  // Rezerwowe
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
    bool relativeMode { false };  // False = absolute positioning, True = relative    // Stan operacyjny
    CNCState state { CNCState::IDLE };
    bool isPaused { false };
    bool isHomed { false };  // Stan bazowania
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
    TickType_t jobStartTime { 0 };  // Czas rozpoczęcia zadania (millis)    // Statystyki
    TickType_t jobRunTime { 0 };    // Czas pracy maszyny (millis)
    float jobProgress { 0.0f }; // Procent ukończenia zadania (0-100%)
};

// Struktura dla obsługi ruchu ręcznego (JOG)
struct JogState {
    bool isActive { false };
    bool movementInProgress { false };
    float jogDistance { 10.0f };  // Domyślna odległość JOG w mm
    bool useRapidSpeed { true };   // true = rapid, false = work speed
    
    // Kierunki ruchu
    bool jogXPlus { false };
    bool jogXMinus { false };
    bool jogYPlus { false };
    bool jogYMinus { false };
};

// Struktura dla obsługi bazowania (HOMING)
struct HomingState {
    bool isActive { false };
    bool movementInProgress { false };
    
    enum class HomingStage {
        IDLE,
        MOVING_X_TO_LIMIT,
        BACKING_OFF_X,
        MOVING_Y_TO_LIMIT,
        BACKING_OFF_Y,
        FINISHED,
        ERROR
    };
    
    HomingStage stage { HomingStage::IDLE };
    String errorMessage { "" };
    
    // Parametry bazowania
    float homingSpeed { 500.0f };     // Prędkość bazowania w steps/s
    float backoffDistance { 5.0f };   // Odległość wycofania w mm
    float slowHomingSpeed { 100.0f }; // Wolna prędkość dla precyzyjnego bazowania
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

// Performance monitoring structures
struct PerformanceMetrics {
    // Task performance
    uint32_t cncTaskCycles { 0 };
    uint32_t controlTaskCycles { 0 };
    uint32_t maxCncTaskTime { 0 };      // Max execution time in microseconds
    uint32_t maxControlTaskTime { 0 };   // Max execution time in microseconds
    
    // Queue performance
    uint32_t stateQueueDrops { 0 };     // Dropped state updates
    uint32_t commandQueueDrops { 0 };   // Dropped commands
    uint32_t maxStateQueueWait { 0 };   // Max wait time for state queue
    uint32_t maxCommandQueueWait { 0 }; // Max wait time for command queue
    
    // SD Card performance
    uint32_t sdOperations { 0 };
    uint32_t sdTimeouts { 0 };
    uint32_t maxSdWaitTime { 0 };       // Max SD operation time
    
    // EventSource performance
    uint32_t broadcastCount { 0 };
    uint32_t deltaUpdates { 0 };        // Number of delta updates sent
    uint32_t fullUpdates { 0 };         // Number of full updates sent
    uint32_t maxBroadcastTime { 0 };    // Max broadcast time in microseconds
    
    // Stepper performance
    uint32_t stepperCycles { 0 };
    uint32_t stepperTimeouts { 0 };
    uint32_t maxStepperTime { 0 };      // Max stepper operation time
    
    // Memory usage monitoring
    uint32_t freeHeap { 0 };
    uint32_t minFreeHeap { UINT32_MAX };
    uint32_t maxHeapUsed { 0 };
    uint32_t totalHeapSize { 0 };
    
    // Stack monitoring
    uint32_t stackHighWaterMarkCNC { 0 };
    uint32_t stackHighWaterMarkControl { 0 };
    uint32_t minStackFree { UINT32_MAX };
    bool stackOverflowDetected { false };
    
    // System alerts
    bool memoryAlertTriggered { false };
    bool stackAlertTriggered { false };
    uint32_t lastMemoryAlert { 0 };    uint32_t lastStackAlert { 0 };
    
    // Method declarations for performance metrics
    void updateMemoryMetrics();
    void updateStackMetrics();
};

// Delta update structure for efficient EventSource broadcasting
struct MachineStateDelta {
    bool hasPositionUpdate { false };
    bool hasStateUpdate { false };
    bool hasIOUpdate { false };
    bool hasProgressUpdate { false };
    bool hasErrorUpdate { false };
    
    // Position changes
    float deltaX { 0.0f };
    float deltaY { 0.0f };
    
    // State changes
    CNCState newState { CNCState::IDLE };
    bool newPauseState { false };
    bool newHomedState { false };
    
    // IO changes
    bool newEstopState { false };
    bool newLimitXState { false };
    bool newLimitYState { false };
    bool newHotWireState { false };
    bool newFanState { false };
    uint8_t newHotWirePower { 0 };
    uint8_t newFanPower { 0 };
    
    // Progress changes
    uint32_t newCurrentLine { 0 };
    float newProgress { 0.0f };
    
    // Error changes
    uint8_t newErrorID { 0 };
};

// Priority-based command structure
enum class CommandPriority {
    EMERGENCY = 0,  // Highest priority - ESTOP, RESET
    HIGH_PRIORITY = 1,       // High priority - STOP, PAUSE
    NORMAL = 2,     // Normal priority - START, HOME, JOG
    LOW_PRIORITY = 3         // Low priority - CONFIG changes
};

struct PriorityCommand {
    WebserverCommand command;
    CommandPriority priority;
    TickType_t timestamp;   // For timeout handling
};

// Adaptive EventSource frequencies based on machine state
struct EventSourceConfig {
    uint32_t idleInterval { 500 };      // IDLE state - 500ms
    uint32_t runningInterval { 100 };   // RUNNING state - 100ms
    uint32_t jogInterval { 50 };        // JOG state - 50ms
    uint32_t homingInterval { 100 };    // HOMING state - 100ms
    uint32_t errorInterval { 200 };     // ERROR state - 200ms
    
    uint32_t getCurrentInterval(CNCState state) const {
        switch (state) {
            case CNCState::IDLE: return idleInterval;
            case CNCState::RUNNING: return runningInterval;
            case CNCState::JOG: return jogInterval;
            case CNCState::HOMING: return homingInterval;
            case CNCState::ERROR: return errorInterval;
            default: return runningInterval;
        }
    }
};

// Timeout configuration for various operations
struct TimeoutConfig {
    uint32_t sdOperationTimeout { 5000 };      // 5 seconds for SD operations
    uint32_t stepperOperationTimeout { 1000 }; // 1 second for stepper operations
    uint32_t queueOperationTimeout { 100 };    // 100ms for queue operations
    uint32_t gCodeProcessingTimeout { 10 };    // 10ms max per G-code line
};