#pragma once

#include <string>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "SDManager.h"

struct MachineConfig {
    // Parametry silników
    struct MotorConfig {
        float stepsPerMM {};        // Kroki na mm [steps]
        float rapidFeedRate {};     // G0 Prędkość [steps/s]
        float rapidAcceleration {}; // G0 Przyspieszenie [steps/s^2]
        float workFeedRate {};      // G1 Prędkość [steps/s]
        float workAcceleration {};  // G1 Przyspieszenie [steps/s^2]
        float offset {};            // Przejazd po nagrzaniu drutu [mm]
    };

    // Osie 
    MotorConfig X {};
    MotorConfig Y {};

    // Pozostałe parametry
    bool useGCodeFeedRate {};      // Czy używać prędkości podanej w G-code
    int delayAfterStartup {};      // Opóźnienie po uruchomieniu (ms)
    bool deactivateESTOP {};       // Wyłączenie zabezpieczenia ESTOP
    bool deactivateLimitSwitches {}; // Wyłączenie wyłączników krańcowych
    uint8_t limitSwitchType {};     // Typ wyłączników krańcowych (0 - NO, 1 - NC)
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

    SDCardManager* sdManager {};

    MachineConfig config {};

    // Mutex do synchronizacji dostępu do konfiguracji
    SemaphoreHandle_t configMutex {};

    // Flaga wskazująca, czy konfiguracja została załadowana
    bool configLoaded { false };

    public:
    // Konstruktor
    ConfigManager(SDCardManager* sdManager);

    // Destruktor
    ~ConfigManager();

    // Inicjalizacja managera konfiguracji
    ConfigManagerStatus init();

    // Pobranie bieżącej konfiguracji
    MachineConfig getConfig();

    // Wczytanie konfiguracji z pliku
    ConfigManagerStatus loadConfig();

    // Zapisanie konfiguracji do pliku
    ConfigManagerStatus saveConfig();

    // Ustawienie całej konfiguracji
    ConfigManagerStatus setConfig(const MachineConfig& newConfig);

    // Aktualizacja pojedynczego parametru
    template<typename T>
    ConfigManagerStatus updateParameter(const std::string& paramName, T value);

    // Serializacja konfiguracji do JSON
    String configToJson();

    // Deserializacja konfiguracji z JSON
    ConfigManagerStatus configFromJson(const String& jsonString);

    // Sprawdzenie, czy konfiguracja została załadowana
    bool isConfigLoaded();
};