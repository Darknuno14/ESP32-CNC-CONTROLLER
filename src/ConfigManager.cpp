// ================================================================================
//                            MENADŻER KONFIGURACJI CNC
// ================================================================================
// Zarządzanie parametrami maszyny CNC z persystencją na karcie SD
// Obsługuje ładowanie/zapisywanie konfiguracji w formacie JSON
// Thread-safe dzięki mutexom FreeRTOS

#include "ConfigManager.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "CONFIGURATION.H"

// ================================================================================
//                           KONSTRUKTOR I DESTRUKTOR
// ================================================================================

ConfigManager::ConfigManager(SDCardManager* sdManager) : sdManager(sdManager) {
    // Mutex zapewnia bezpieczny dostęp do konfiguracji z wielu wątków
    configMutex = xSemaphoreCreateMutex();
}

ConfigManager::~ConfigManager() {
    // Zwolnienie zasobów FreeRTOS
    if (configMutex != nullptr) {
        vSemaphoreDelete(configMutex);
        configMutex = nullptr;
    }
}

// ================================================================================
//                            INICJALIZACJA SYSTEMU
// ================================================================================

ConfigManagerStatus ConfigManager::init() {
    // Sprawdzenie dostępności karty SD
    if (!sdManager || !sdManager->isCardInitialized()) {
        this->configInitialized = false;
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }

    constexpr int MAX_NUM_OF_TRIES { 5 };
    ConfigManagerStatus status {};

    // Próba wczytania konfiguracji z pliku - maksymalnie 5 prób
    for (int i { 0 }; i < MAX_NUM_OF_TRIES; ++i) {
        status = readConfigFromSD();
        if (status == ConfigManagerStatus::OK) {
            #ifdef DEBUG_CONFIG_MANAGER
            Serial.println("DEBUG CONFIG: Konfiguracja wczytana z pliku");
            #endif
            this->configInitialized = true;
            return ConfigManagerStatus::OK;
        }
    }

    // Fallback - użycie domyślnej konfiguracji jeśli plik niedostępny
    status = loadDefaultConfig();
    if (status == ConfigManagerStatus::OK) {
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("DEBUG CONFIG: Wczytano domyślną konfigurację");
        #endif
        this->configInitialized = true;
        return ConfigManagerStatus::OK;
    }

    #ifdef DEBUG_CONFIG_MANAGER
    Serial.println("ERROR CONFIG: NIE udało się zainicjować konfiguracji");
    #endif
    this->configInitialized = false;
    return status;
}

// ================================================================================
//                            OPERACJE NA PLIKACH SD
// ================================================================================

ConfigManagerStatus ConfigManager::readConfigFromSD() {
    // Blokada dostępu do karty SD (thread-safe)
    if (!sdManager->takeSD()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }

    // Budowa ścieżki do pliku konfiguracji
    std::string configFilePath { CONFIG::CONFIG_DIR };
    configFilePath += CONFIG::CONFIG_FILE;

    if (!SD.exists(configFilePath.c_str())) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }

    File configFile { SD.open(configFilePath.c_str(), FILE_READ) };
    if (!configFile) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }

    // Wczytanie całego pliku do pamięci
    String jsonString = "";
    while (configFile.available()) {
        jsonString += (char)configFile.read();
    }

    configFile.close();
    sdManager->giveSD();

    // Parsowanie JSON i aktualizacja struktury konfiguracji
    return configFromJson(jsonString);
}

ConfigManagerStatus ConfigManager::writeConfigToSD() {
    // Walidacja dostępności SD managera
    if (!sdManager) {
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("ERROR: SD Manager is nullptr");
        #endif
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }

    // Blokada dostępu do karty SD
    if (!sdManager->takeSD()) {
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("ERROR: Failed to take SD");
        #endif
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }

    // Budowa ścieżki do pliku konfiguracji
    std::string configFilePath { CONFIG::CONFIG_DIR };
    configFilePath += CONFIG::CONFIG_FILE;

    File configFile { SD.open(configFilePath.c_str(), FILE_WRITE) };
    if (!configFile) {
        sdManager->giveSD();
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("ERROR: Failed to open config file");
        #endif
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }

    // Konwersja konfiguracji do formatu JSON
    String jsonString { configToJson() };

    // Zapis JSON do pliku z walidacją
    if (configFile.print(jsonString) == 0) {
        configFile.close();
        sdManager->giveSD();
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("ERROR: Failed to write to config file");
        #endif
        return ConfigManagerStatus::FILE_WRITE_FAILED;
    }

    configFile.close();
    sdManager->giveSD();
    return ConfigManagerStatus::OK;
}

// ================================================================================
//                          ZARZĄDZANIE KONFIGURACJĄ
// ================================================================================

ConfigManagerStatus ConfigManager::loadDefaultConfig() {
    // Thread-safe dostęp do struktury konfiguracji
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Inicjalizacja parametrów osi X z wartości domyślnych
        config.X.stepsPerMM = DEFAULTS::X_STEPS_PER_MM;
        config.X.rapidFeedRate = DEFAULTS::X_RAPID_FEEDRATE;
        config.X.rapidAcceleration = DEFAULTS::X_RAPID_ACCELERATION;
        config.X.workFeedRate = DEFAULTS::X_WORK_FEEDRATE;
        config.X.workAcceleration = DEFAULTS::X_WORK_ACCELERATION;
        config.X.offset = DEFAULTS::X_OFFSET;

        // Inicjalizacja parametrów osi Y z wartości domyślnych
        config.Y.stepsPerMM = DEFAULTS::Y_STEPS_PER_MM;
        config.Y.rapidFeedRate = DEFAULTS::Y_RAPID_FEEDRATE;
        config.Y.rapidAcceleration = DEFAULTS::Y_RAPID_ACCELERATION;
        config.Y.workFeedRate = DEFAULTS::Y_WORK_FEEDRATE;
        config.Y.workAcceleration = DEFAULTS::Y_WORK_ACCELERATION;
        config.Y.offset = DEFAULTS::Y_OFFSET;

        // Inicjalizacja parametrów systemowych
        config.useGCodeFeedRate = DEFAULTS::USE_GCODE_FEEDRATE;
        config.delayAfterStartup = DEFAULTS::DELAY_AFTER_STARTUP;
        config.deactivateESTOP = DEFAULTS::DEACTIVATE_ESTOP;
        config.deactivateLimitSwitches = DEFAULTS::DEACTIVATE_LIMIT_SWITCHES;
        config.limitSwitchType = DEFAULTS::LIMIT_SWITCH_TYPE;
        config.hotWirePower = DEFAULTS::WIRE_POWER;
        config.fanPower = DEFAULTS::FAN_POWER;

        xSemaphoreGive(configMutex);
        return ConfigManagerStatus::OK;
    }
    return ConfigManagerStatus::UNKNOWN_ERROR;
}

ConfigManagerStatus ConfigManager::getConfig(MachineConfig& targetConfig) {
    // Sprawdzenie czy konfiguracja została zainicjalizowana
    if (!configInitialized) {
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("DEBUG CONFIG: Configuration not loaded, cannot copy to struct");
        #endif
        return ConfigManagerStatus::MANAGER_NOT_INITIALIZED;
    }

    MachineConfig configcpy {};

    // Thread-safe kopiowanie konfiguracji do struktury docelowej
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        targetConfig = config;
        xSemaphoreGive(configMutex);

        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("DEBUG CONFIG: Configuration copied to target struct");
        #endif
        return ConfigManagerStatus::OK;
    }

    return ConfigManagerStatus::UNKNOWN_ERROR;
}

ConfigManagerStatus ConfigManager::updateConfig(const MachineConfig& newConfig) {
    // Atomowa aktualizacja całej konfiguracji
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        config = newConfig;
        xSemaphoreGive(configMutex);

        // Natychmiastowy zapis do pliku dla persystencji
        return writeConfigToSD();
    }

    return ConfigManagerStatus::UNKNOWN_ERROR;
}

// ================================================================================
//                          KONWERSJA JSON ↔ KONFIGURACJA
// ================================================================================

String ConfigManager::configToJson() {
    JsonDocument doc {};

    // Thread-safe serializacja konfiguracji do JSON
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Budowa obiektu JSON dla osi X
        JsonObject xAxis = doc["xAxis"].to<JsonObject>();
        xAxis["stepsPerMM"] = config.X.stepsPerMM;
        xAxis["workFeedRate"] = config.X.workFeedRate;
        xAxis["workAcceleration"] = config.X.workAcceleration;
        xAxis["rapidFeedRate"] = config.X.rapidFeedRate;
        xAxis["rapidAcceleration"] = config.X.rapidAcceleration;
        xAxis["offset"] = config.X.offset;

        // Budowa obiektu JSON dla osi Y
        JsonObject yAxis = doc["yAxis"].to<JsonObject>();
        yAxis["stepsPerMM"] = config.Y.stepsPerMM;
        yAxis["workFeedRate"] = config.Y.workFeedRate;
        yAxis["workAcceleration"] = config.Y.workAcceleration;
        yAxis["rapidFeedRate"] = config.Y.rapidFeedRate;
        yAxis["rapidAcceleration"] = config.Y.rapidAcceleration;
        yAxis["offset"] = config.Y.offset;

        // Parametry systemowe
        doc["useGCodeFeedRate"] = config.useGCodeFeedRate;
        doc["delayAfterStartup"] = config.delayAfterStartup;
        doc["deactivateESTOP"] = config.deactivateESTOP;
        doc["deactivateLimitSwitches"] = config.deactivateLimitSwitches;
        doc["limitSwitchType"] = config.limitSwitchType;
        doc["hotWirePower"] = config.hotWirePower;
        doc["fanPower"] = config.fanPower;

        xSemaphoreGive(configMutex);
    }

    // Konwersja dokumentu JSON na string
    String result;
    serializeJson(doc, result);
    return result;
}

ConfigManagerStatus ConfigManager::configFromJson(const String& jsonString) {
    JsonDocument doc {};

    // Parsowanie JSON z walidacją błędów
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
        #ifdef DEBUG_CONFIG_MANAGER
        Serial.print("DEBUG CONFIG: JSON parse error: ");
        Serial.println(error.c_str());
        #endif
        return ConfigManagerStatus::JSON_PARSE_ERROR;
    }

    // Thread-safe aktualizacja konfiguracji z JSON
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Bezpieczne parsowanie parametrów osi X z walidacją typów
        if (doc["xAxis"].is<JsonObject>()) {
            JsonObject xAxis = doc["xAxis"];
            if (xAxis["stepsPerMM"].is<float>()) config.X.stepsPerMM = xAxis["stepsPerMM"].as<float>();
            if (xAxis["workFeedRate"].is<float>()) config.X.workFeedRate = xAxis["workFeedRate"].as<float>();
            if (xAxis["workAcceleration"].is<float>()) config.X.workAcceleration = xAxis["workAcceleration"].as<float>();
            if (xAxis["rapidFeedRate"].is<float>()) config.X.rapidFeedRate = xAxis["rapidFeedRate"].as<float>();
            if (xAxis["rapidAcceleration"].is<float>()) config.X.rapidAcceleration = xAxis["rapidAcceleration"].as<float>();
            if (xAxis["offset"].is<float>()) config.X.offset = xAxis["offset"].as<float>();
        }

        // Bezpieczne parsowanie parametrów osi Y z walidacją typów
        if (doc["yAxis"].is<JsonObject>()) {
            JsonObject yAxis = doc["yAxis"];
            if (yAxis["stepsPerMM"].is<float>()) config.Y.stepsPerMM = yAxis["stepsPerMM"].as<float>();
            if (yAxis["workFeedRate"].is<float>()) config.Y.workFeedRate = yAxis["workFeedRate"].as<float>();
            if (yAxis["workAcceleration"].is<float>()) config.Y.workAcceleration = yAxis["workAcceleration"].as<float>();
            if (yAxis["rapidFeedRate"].is<float>()) config.Y.rapidFeedRate = yAxis["rapidFeedRate"].as<float>();
            if (yAxis["rapidAcceleration"].is<float>()) config.Y.rapidAcceleration = yAxis["rapidAcceleration"].as<float>();
            if (yAxis["offset"].is<float>()) config.Y.offset = yAxis["offset"].as<float>();
        }

        // Bezpieczne parsowanie parametrów systemowych z walidacją typów
        if (doc["useGCodeFeedRate"].is<bool>()) config.useGCodeFeedRate = doc["useGCodeFeedRate"].as<bool>();
        if (doc["delayAfterStartup"].is<int>()) config.delayAfterStartup = doc["delayAfterStartup"].as<int>();
        if (doc["deactivateESTOP"].is<bool>()) config.deactivateESTOP = doc["deactivateESTOP"].as<bool>();
        if (doc["deactivateLimitSwitches"].is<bool>()) config.deactivateLimitSwitches = doc["deactivateLimitSwitches"].as<bool>();
        if (doc["limitSwitchType"].is<uint8_t>()) config.limitSwitchType = doc["limitSwitchType"].as<uint8_t>();
        if (doc["hotWirePower"].is<float>()) config.hotWirePower = doc["hotWirePower"].as<float>();
        if (doc["fanPower"].is<float>()) config.fanPower = doc["fanPower"].as<float>();

        xSemaphoreGive(configMutex);

        #ifdef DEBUG_CONFIG_MANAGER
        Serial.println("DEBUG CONFIG: Configuration successfully loaded from JSON");
        #endif

        return ConfigManagerStatus::OK;
    }

    return ConfigManagerStatus::UNKNOWN_ERROR;
}

// ================================================================================
//                         AKTUALIZACJA POJEDYNCZYCH PARAMETRÓW
// ================================================================================

template<typename T>
ConfigManagerStatus ConfigManager::updateParameter(const std::string& paramName, T value) {
    // Auto-inicjalizacja jeśli potrzebna
    if (!configInitialized) {
        init();
    }
    
    // Thread-safe aktualizacja wybranego parametru
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Mapowanie nazw parametrów na pola struktury konfiguracji
        // Parametry kinematyki osi X
        if (paramName == "xAxis.stepsPerMM") config.X.stepsPerMM = static_cast<float>(value);
        else if (paramName == "xAxis.workFeedRate") config.X.workFeedRate = static_cast<float>(value);
        else if (paramName == "xAxis.workAcceleration") config.X.workAcceleration = static_cast<float>(value);
        else if (paramName == "xAxis.rapidFeedRate") config.X.rapidFeedRate = static_cast<float>(value);
        else if (paramName == "xAxis.rapidAcceleration") config.X.rapidAcceleration = static_cast<float>(value);
        else if (paramName == "xAxis.offset") config.X.offset = static_cast<float>(value);

        // Parametry kinematyki osi Y
        else if (paramName == "yAxis.stepsPerMM") config.Y.stepsPerMM = static_cast<float>(value);
        else if (paramName == "yAxis.workFeedRate") config.Y.workFeedRate = static_cast<float>(value);
        else if (paramName == "yAxis.workAcceleration") config.Y.workAcceleration = static_cast<float>(value);
        else if (paramName == "yAxis.rapidFeedRate") config.Y.rapidFeedRate = static_cast<float>(value);
        else if (paramName == "yAxis.rapidAcceleration") config.Y.rapidAcceleration = static_cast<float>(value);
        else if (paramName == "yAxis.offset") config.Y.offset = static_cast<float>(value);


        // Parametry systemowe maszyny
        else if (paramName == "useGCodeFeedRate") config.useGCodeFeedRate = static_cast<bool>(value);
        else if (paramName == "delayAfterStartup") config.delayAfterStartup = static_cast<int>(value);
        else if (paramName == "deactivateESTOP") config.deactivateESTOP = static_cast<bool>(value);
        else if (paramName == "deactivateLimitSwitches") config.deactivateLimitSwitches = static_cast<bool>(value);
        else if (paramName == "limitSwitchType") config.limitSwitchType = static_cast<uint8_t>(value);
        else if (paramName == "hotWirePower") config.hotWirePower = static_cast<float>(value);
        else if (paramName == "fanPower") config.fanPower = static_cast<float>(value);

        xSemaphoreGive(configMutex);

        // Natychmiastowy zapis zmiany do pliku
        return writeConfigToSD();
    }

    return ConfigManagerStatus::UNKNOWN_ERROR;
}

// ================================================================================
//                          JAWNE INSTANCJE SZABLONÓW
// ================================================================================
// Definicje dla kompilera - obsługiwane typy danych w updateParameter
template ConfigManagerStatus ConfigManager::updateParameter<float>(const std::string& paramName, float value);
template ConfigManagerStatus ConfigManager::updateParameter<int>(const std::string& paramName, int value);
template ConfigManagerStatus ConfigManager::updateParameter<bool>(const std::string& paramName, bool value);