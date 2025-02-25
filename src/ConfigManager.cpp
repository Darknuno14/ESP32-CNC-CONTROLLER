#include "ConfigManager.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "CONFIGURATION.H"

ConfigManager::ConfigManager(SDCardManager* sdManager, const std::string& configPath) 
    : sdManager(sdManager), configFilePath(configPath), configLoaded(false) {
    
    // Utwórz mutex dla bezpiecznego dostępu do konfiguracji
    configMutex = xSemaphoreCreateMutex();
    
    // Ustaw domyślną konfigurację
    setDefaultConfig();
}

ConfigManager::~ConfigManager() {
    // Zwolnij mutex, jeśli został utworzony
    if (configMutex != nullptr) {
        vSemaphoreDelete(configMutex);
        configMutex = nullptr;
    }
}

ConfigManagerStatus ConfigManager::init() {
    // Sprawdź, czy SD Manager jest zainicjalizowany
    if (!sdManager || !sdManager->isCardInitialized()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    // Spróbuj załadować konfigurację
    ConfigManagerStatus status = loadConfig();
    
    // Jeśli nie udało się załadować, użyj domyślnej konfiguracji
    if (status != ConfigManagerStatus::OK) {
        #ifdef DEBUG_CONFIG_MANAGER
            Serial.println("DEBUG CONFIG: Could not load configuration, using defaults");
        #endif
        
        setDefaultConfig();
        
        // Spróbuj utworzyć katalog konfiguracji, jeśli nie istnieje
        std::string configDir = configFilePath.substr(0, configFilePath.find_last_of('/'));
        if (sdManager->takeSD()) {
            if (!SD.exists(configDir.c_str())) {
                SD.mkdir(configDir.c_str());
            }
            sdManager->giveSD();
        }
        
        // Zapisz domyślną konfigurację
        status = saveConfig();
        if (status != ConfigManagerStatus::OK) {
            #ifdef DEBUG_CONFIG_MANAGER
                Serial.println("DEBUG CONFIG: Could not save default configuration");
            #endif
            return status;
        }
    }
    
    configLoaded = true;
    return ConfigManagerStatus::OK;
}

ConfigManagerStatus ConfigManager::loadConfig() {
    if (!sdManager->takeSD()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    // Sprawdź, czy plik istnieje
    if (!SD.exists(configFilePath.c_str())) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }
    
    // Otwórz plik
    File configFile = SD.open(configFilePath.c_str(), FILE_READ);
    if (!configFile) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }
    
    // Odczytaj zawartość pliku
    String jsonString = "";
    while (configFile.available()) {
        jsonString += (char)configFile.read();
    }
    configFile.close();
    sdManager->giveSD();
    
    // Parsuj JSON
    return configFromJson(jsonString);
}

ConfigManagerStatus ConfigManager::saveConfig() {
    if (!sdManager->takeSD()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    // Otwórz plik do zapisu (nadpisując istniejący)
    File configFile = SD.open(configFilePath.c_str(), FILE_WRITE);
    if (!configFile) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }
    
    // Serializuj konfigurację do JSON
    String jsonString = configToJson();
    
    // Zapisz do pliku
    if (configFile.print(jsonString) == 0) {
        configFile.close();
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_WRITE_FAILED;
    }
    
    configFile.close();
    sdManager->giveSD();
    return ConfigManagerStatus::OK;
}

void ConfigManager::setDefaultConfig() {
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Ustaw domyślne wartości (przykładowe, dostosuj do swoich potrzeb)
        config = MachineConfig();
        xSemaphoreGive(configMutex);
    }
}

MachineConfig ConfigManager::getConfig() {
    MachineConfig result;
    
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        result = config;
        xSemaphoreGive(configMutex);
    }
    
    return result;
}

ConfigManagerStatus ConfigManager::setConfig(const MachineConfig& newConfig) {
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        config = newConfig;
        xSemaphoreGive(configMutex);
        
        // Zapisz nową konfigurację na karcie SD
        return saveConfig();
    }
    
    return ConfigManagerStatus::UNKNOWN_ERROR;
}

String ConfigManager::configToJson() {
    // Utwórz dokument JSON
    DynamicJsonDocument doc(1024); // Rozmiar dostosuj do potrzeb
    
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Silnik X
        JsonObject xAxis = doc.createNestedObject("xAxis");
        xAxis["stepsPerMM"] = config.xAxis.stepsPerMM;
        xAxis["maxFeedRate"] = config.xAxis.maxFeedRate;
        xAxis["maxAcceleration"] = config.xAxis.maxAcceleration;
        xAxis["rapidFeedRate"] = config.xAxis.rapidFeedRate;
        xAxis["rapidAcceleration"] = config.xAxis.rapidAcceleration;
        
        // Silnik Y
        JsonObject yAxis = doc.createNestedObject("yAxis");
        yAxis["stepsPerMM"] = config.yAxis.stepsPerMM;
        yAxis["maxFeedRate"] = config.yAxis.maxFeedRate;
        yAxis["maxAcceleration"] = config.yAxis.maxAcceleration;
        yAxis["rapidFeedRate"] = config.yAxis.rapidFeedRate;
        yAxis["rapidAcceleration"] = config.yAxis.rapidAcceleration;
        
        // Parametry operacyjne
        doc["useGCodeFeedRate"] = config.useGCodeFeedRate;
        doc["delayAfterStartup"] = config.delayAfterStartup;
        
        // Inne parametry
        doc["wireTemperature"] = config.wireTemperature;
        doc["enableFan"] = config.enableFan;
        doc["fanSpeed"] = config.fanSpeed;
        
        xSemaphoreGive(configMutex);
    }
    
    // Serializuj dokument do stringa
    String result;
    serializeJson(doc, result);
    return result;
}

ConfigManagerStatus ConfigManager::configFromJson(const String& jsonString) {
    // Utwórz dokument JSON
    DynamicJsonDocument doc(1024); // Rozmiar dostosuj do potrzeb
    
    // Parsuj JSON
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
        return ConfigManagerStatus::JSON_PARSE_ERROR;
    }
    
    // Odczytaj wartości, z zabezpieczeniem mutexem
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Silnik X
        if (doc.containsKey("xAxis")) {
            JsonObject xAxis = doc["xAxis"];
            config.xAxis.stepsPerMM = xAxis["stepsPerMM"] | config.xAxis.stepsPerMM;
            config.xAxis.maxFeedRate = xAxis["maxFeedRate"] | config.xAxis.maxFeedRate;
            config.xAxis.maxAcceleration = xAxis["maxAcceleration"] | config.xAxis.maxAcceleration;
            config.xAxis.rapidFeedRate = xAxis["rapidFeedRate"] | config.xAxis.rapidFeedRate;
            config.xAxis.rapidAcceleration = xAxis["rapidAcceleration"] | config.xAxis.rapidAcceleration;
        }
        
        // Silnik Y
        if (doc.containsKey("yAxis")) {
            JsonObject yAxis = doc["yAxis"];
            config.yAxis.stepsPerMM = yAxis["stepsPerMM"] | config.yAxis.stepsPerMM;
            config.yAxis.maxFeedRate = yAxis["maxFeedRate"] | config.yAxis.maxFeedRate;
            config.yAxis.maxAcceleration = yAxis["maxAcceleration"] | config.yAxis.maxAcceleration;
            config.yAxis.rapidFeedRate = yAxis["rapidFeedRate"] | config.yAxis.rapidFeedRate;
            config.yAxis.rapidAcceleration = yAxis["rapidAcceleration"] | config.yAxis.rapidAcceleration;
        }
        
        // Parametry operacyjne
        config.useGCodeFeedRate = doc["useGCodeFeedRate"] | config.useGCodeFeedRate;
        config.delayAfterStartup = doc["delayAfterStartup"] | config.delayAfterStartup;
        
        // Inne parametry
        config.wireTemperature = doc["wireTemperature"] | config.wireTemperature;
        config.enableFan = doc["enableFan"] | config.enableFan;
        config.fanSpeed = doc["fanSpeed"] | config.fanSpeed;
        
        xSemaphoreGive(configMutex);
    }
    
    return ConfigManagerStatus::OK;
}

// Szablon metody do aktualizacji pojedynczego parametru
// Uwaga: Poniższa implementacja jest uproszczona i obsługuje tylko kilka podstawowych parametrów
// W praktyce wymagałaby lepszej implementacji z obsługą wszystkich możliwych parametrów
template<typename T>
ConfigManagerStatus ConfigManager::updateParameter(const std::string& paramName, T value) {
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Aktualizacja wybranego parametru
        if (paramName == "xAxis.stepsPerMM") config.xAxis.stepsPerMM = static_cast<float>(value);
        else if (paramName == "xAxis.maxFeedRate") config.xAxis.maxFeedRate = static_cast<float>(value);
        else if (paramName == "yAxis.stepsPerMM") config.yAxis.stepsPerMM = static_cast<float>(value);
        else if (paramName == "yAxis.maxFeedRate") config.yAxis.maxFeedRate = static_cast<float>(value);
        else if (paramName == "useGCodeFeedRate") config.useGCodeFeedRate = static_cast<bool>(value);
        else if (paramName == "delayAfterStartup") config.delayAfterStartup = static_cast<int>(value);
        else if (paramName == "wireTemperature") config.wireTemperature = static_cast<float>(value);
        else if (paramName == "enableFan") config.enableFan = static_cast<bool>(value);
        else if (paramName == "fanSpeed") config.fanSpeed = static_cast<int>(value);
        
        xSemaphoreGive(configMutex);
        
        // Zapisz zaktualizowaną konfigurację
        return saveConfig();
    }
    
    return ConfigManagerStatus::UNKNOWN_ERROR;
}

// Jawne instancje szablonu dla typów, które będą używane
template ConfigManagerStatus ConfigManager::updateParameter<float>(const std::string& paramName, float value);
template ConfigManagerStatus ConfigManager::updateParameter<int>(const std::string& paramName, int value);
template ConfigManagerStatus ConfigManager::updateParameter<bool>(const std::string& paramName, bool value);

bool ConfigManager::isConfigLoaded(){
    return configLoaded;
}