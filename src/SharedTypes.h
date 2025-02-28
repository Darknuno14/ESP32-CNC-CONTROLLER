#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>

enum class CommandType {
    START,
    STOP,
    PAUSE,
    JOG,
    HOMING,
    RESET,
    ZERO
};

struct WebserverCommand {
    CommandType type;
    float param1 {0}; 
    float param2 {0}; 
    float param3 {0};
};

enum class CNCState {
    IDLE,           // Bezczynność, oczekiwanie na polecenia
    RUNNING,        // Wykonywanie programu G-code
    JOG,            // Tryb ręcznego sterowania
    HOMING,         // Wykonywanie sekwencji bazowania
    STOPPED,        // Działanie maszyny zatrzymane
    ERROR           // Błąd maszyny
};

struct MachineState {
    // Pozycja i ruch
    float currentX {0.0f};
    float currentY {0.0f};
    bool relativeMode {false};  // False = absolute positioning, True = relative
    
    // Stan operacyjny
    CNCState state {CNCState::IDLE};
    bool isPaused {false};
    
    // Stan urządzeń
    bool hotWireOn {false};  // Stan drutu
    bool fanOn {false};      // Stan wentylatora
    
    // Informacje o zadaniu
    std::string currentProject {};
    float jobProgress {0.0f};  // 0-100%
    int currentLine {0};       // Aktualnie przetwarzana linia G-code
    
    // Statystyki
    unsigned long jobStartTime {0};  // Czas rozpoczęcia zadania (millis)
    unsigned long jobRunTime {0};    // Czas pracy (ms)
    
    // Flagi błędów
    bool hasError {false};
    uint8_t errorCode {0};
};