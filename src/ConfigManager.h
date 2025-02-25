#pragma once

#include <string>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "SDManager.h"

// Struktura przechowująca wszystkie parametry konfigurowalne
struct MachineConfig {
    // Parametry silników
    struct MotorConfig {
        float stepsPerMM;        // Ilość kroków na milimetr
        float maxFeedRate;       // Maksymalna prędkość ruchu (mm/min)
        float maxAcceleration;   // Maksymalne przyspieszenie (mm/s²)
        float rapidFeedRate;     // Prędkość ruchu szybkiego (mm/min)
        float rapidAcceleration; // Przyspieszenie ruchu szybkiego (mm/s²)
    };

    MotorConfig xAxis;
    MotorConfig yAxis;

    // Parametry operacyjne
    bool useGCodeFeedRate;      // Czy używać prędkości podanej w G-code
    int delayAfterStartup;      // Opóźnienie po uruchomieniu (ms)
    
    // Inne parametry
    float wireTemperature;      // Temperatura drutu (°C, jeśli stosowane)
    bool enableFan;             // Czy włączyć wentylator podczas pracy
    int fanSpeed;               // Prędkość wentylatora (0-255)

    // Konstruktor z wartościami domyślnymi
    MachineConfig() : 
        xAxis({200.0f, 3000.0f, 500.0f, 5000.0f, 1000.0f}),
        yAxis({200.0f, 3000.0f, 500.0f, 5000.0f, 1000.0f}),
        useGCodeFeedRate(true),
        delayAfterStartup(1000),
        wireTemperature(300.0f),
        enableFan(true),
        fanSpeed(255) {}
};

enum class ConfigManagerStatus {
    OK,
    FILE_OPEN_FAILED,
    FILE_WRITE_FAILED,
    JSON_PARSE_ERROR,
    JSON_SERIALIZE_ERROR,
    SD_ACCESS_ERROR,
    UNKNOWN_ERROR
};

class ConfigManager {
private:
    // Ścieżka do pliku konfiguracyjnego
    std::string configFilePath;
    
    // Wskaźnik do managera karty SD
    SDCardManager* sdManager;
    
    // Bieżąca konfiguracja
    MachineConfig config;
    
    // Mutex do synchronizacji dostępu do konfiguracji
    SemaphoreHandle_t configMutex;
    
    // Flaga wskazująca, czy konfiguracja została załadowana
    bool configLoaded;

public:
    // Konstruktor
    ConfigManager(SDCardManager* sdManager, const std::string& configPath = "/config/machine_config.json");
    
    // Destruktor
    ~ConfigManager();
    
    // Inicjalizacja managera konfiguracji
    ConfigManagerStatus init();
    
    // Wczytanie konfiguracji z pliku
    ConfigManagerStatus loadConfig();
    
    // Zapisanie konfiguracji do pliku
    ConfigManagerStatus saveConfig();
    
    // Ustawienie domyślnej konfiguracji
    void setDefaultConfig();
    
    // Pobranie bieżącej konfiguracji (bezpieczne, z mutexem)
    MachineConfig getConfig();
    
    // Ustawienie całej konfiguracji (bezpieczne, z mutexem)
    ConfigManagerStatus setConfig(const MachineConfig& newConfig);
    
    // Aktualizacja pojedynczego parametru (jako przykład, można rozszerzyć)
    template<typename T>
    ConfigManagerStatus updateParameter(const std::string& paramName, T value);
    
    // Serializacja konfiguracji do JSON
    String configToJson();
    
    // Deserializacja konfiguracji z JSON
    ConfigManagerStatus configFromJson(const String& jsonString);

    // Sprawdzenie, czy konfiguracja została załadowana
    bool isConfigLoaded();
};