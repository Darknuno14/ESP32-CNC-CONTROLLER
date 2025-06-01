#pragma once
#include <string>

// ============================================================================
// Enable debugging:
#define DEBUG
#define DEBUG_CONTROL_TASK
//#define DEBUG_CNC_TASK
//#define DEBUG_SERVER_ROUTES
//#define DEBUG_SD
//#define DEBUG_WIFI
//#define DEBUG_CONFIG_MANAGER


namespace PINCONFIG {
    // ============================================================================
    constexpr uint8_t SD_CS_PIN { 5 };    // 
    constexpr uint8_t SD_MOSI_PIN { 23 };   // 
    constexpr uint8_t SD_CLK_PIN { 18 };   // 
    constexpr uint8_t SD_MISO_PIN { 19 };   // 

    constexpr uint8_t WIRE_RELAY_PIN { 27 };   // 
    constexpr uint8_t WIRE_PWM_PIN { 25 };   //
    constexpr uint8_t FAN_RELAY_PIN { 14 };   // 
    constexpr uint8_t FAN_PWM_PIN { 26 };   //

    constexpr uint8_t STEP_X_PIN { 32 };   // 
    constexpr uint8_t DIR_X_PIN { 33 };   // 
    constexpr uint8_t STEP_Y_PIN { 17 };   // 
    constexpr uint8_t DIR_Y_PIN { 16 };   // 

    constexpr uint8_t LIMIT_X_PIN { 34 };   //
    constexpr uint8_t LIMIT_Y_PIN { 35 };   //
    constexpr uint8_t ESTOP_PIN { 39 };   //


    constexpr uint8_t WIRE_PWM_CHANNEL { 0 };    // 
    constexpr uint8_t FAN_PWM_CHANNEL { 1 };    // 
    constexpr uint8_t PWM_RESOLUTION { 8 };    // (0-255)
    constexpr uint32_t PWM_FREQ { 5000 }; // Hz
}
namespace CONFIG {
    // ============================================================================
    // Konfiguracja zadań

    // Rozmiar stosu zadań FreeRTOS
    constexpr uint32_t CONTROLTASK_STACK_SIZE { 8192 }; // [bytes]
    constexpr uint32_t CNCTASK_STACK_SIZE { 6144 }; // [bytes]

    // Priorytety zadań FreeRTOS
    constexpr uint8_t CONTROLTASK_PRIORITY { 2 }; // The priority at which the task should run
    constexpr uint8_t CNCTASK_PRIORITY { 1 };

    // Pomocnicze do przypisania zadań do rdzeni ESP32
    constexpr BaseType_t CORE_0 { 0 };
    constexpr BaseType_t CORE_1 { 1 };

    // ============================================================================
    // Konfiguracja timera dla stepperów
    constexpr uint32_t STEPPER_TIMER_FREQUENCY_US { 100 }; // [µs] Częstotliwość timera dla stepperów (100µs = 10kHz)

    // ============================================================================

    // Maksymalny czas oczekiwania na połączenie z WiFi
    constexpr long MAX_CONNECTION_TIME { 10000 }; // [ms] 

    // Ścieżki plików na karcie SD
    constexpr const char* PROJECTS_DIR { "/Projects/" };
    constexpr const char* CONFIG_DIR { "/Config/" };
    constexpr const char* CONFIG_FILE { "config.json" };
}

namespace DEFAULTS {
    // ============================================================================
    // Konfiguracja maszyny wczytywana przy braku pliku konfiguracyjnego

    // Skalowanie osi X i Y, ilość kroków silnika na milimetr ruchu
    constexpr float X_STEPS_PER_MM { 200.0f }; // [steps]
    constexpr float Y_STEPS_PER_MM { 200.0f };  // [steps]

    // (G0) Prędkości szybkiego ruchu i przyspieszenia
    constexpr float X_RAPID_FEEDRATE { 3000.0f }; // [steps/s]
    constexpr float X_RAPID_ACCELERATION { 3000.0f }; // [steps/s^2]

    constexpr float Y_RAPID_FEEDRATE { 3000.0f }; // [steps/s]
    constexpr float Y_RAPID_ACCELERATION { 3000.0f }; // [steps/s^2]

    // (G1) Prędkości robocze i przyspieszenia
    constexpr float X_WORK_FEEDRATE { 1000.0f }; // [steps/s]
    constexpr float X_WORK_ACCELERATION { 1000.0f }; // [steps/s^2]

    constexpr float Y_WORK_FEEDRATE { 1000.0f }; // [steps/s]
    constexpr float Y_WORK_ACCELERATION { 1000.0f }; // [steps/s^2]

    // Wybór czy prędkość ma być odczytywana z G-code czy z konfiguracji
    constexpr bool USE_GCODE_FEEDRATE { true }; // true = użyj feed rate z G-code, false = użyj feed rate z pliku konfiguracyjnego

    // Czas nagrzewania drutu przed rozpoczęciem pracy/ruchu
    constexpr int DELAY_AFTER_STARTUP { 1000 }; // [ms]

    // Możliwość wyłączenia krańcówki i awaryjnego zatrzymania przy braku ich użycia
    constexpr bool DEACTIVATE_ESTOP { false }; // true = wyłącz awaryjne zatrzymanie, false = włącz awaryjne zatrzymanie
    constexpr bool DEACTIVATE_LIMIT_SWITCHES { false }; // true = wyłącz krańcówki, false = włącz krańcówki

    // Typ styku krańcówki
    // 0 = NO (normal open), 1 = NC (normal close)
    constexpr uint8_t LIMIT_SWITCH_TYPE { 0 };

    // Przesunięcie punktu początkowego na początku cyklu po nagrzewaniu drutu
    // Maszyna robi przejazd od bieżącego punktu o zadany offset
    constexpr float X_OFFSET { 0.0f }; // [mm]
    constexpr float Y_OFFSET { 0.0f }; // [mm]

    // Moc drutu grzejnego i wentylatora
    constexpr float WIRE_POWER { 0.0f }; // [%]
    constexpr float FAN_POWER { 0.0f };  // [%]

}