#include "ConfigManager.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "CONFIGURATION.H"

ConfigManager::ConfigManager(SDCardManager* sdManager): sdManager(sdManager) {
    configMutex = xSemaphoreCreateMutex();
}

ConfigManager::~ConfigManager() {
    if (configMutex != nullptr) {
        vSemaphoreDelete(configMutex);
        configMutex = nullptr;
    }
}

ConfigManagerStatus ConfigManager::init() {
    if (!sdManager || !sdManager->isCardInitialized()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }

    constexpr int MAX_NUM_OF_TRIES {5};
    ConfigManagerStatus status {};
    for(int i{0}; i < MAX_NUM_OF_TRIES; ++i){
        status = loadConfig();
        if (status == ConfigManagerStatus::OK) {
            this->configLoaded = true;
            status = ConfigManagerStatus::OK;
            break;
        }
    }
    return status;

}

ConfigManagerStatus ConfigManager::loadConfig() {
    if (!sdManager->takeSD()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    std::string configFilePath {CONFIG::CONFIG_DIR};
    configFilePath += CONFIG::CONFIG_FILE;

    if (!SD.exists(configFilePath.c_str())) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }
    
    File configFile {SD.open(configFilePath.c_str(), FILE_READ)};
    if (!configFile) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }

    String jsonString = "";
    while (configFile.available()) {
        jsonString += (char)configFile.read();
    }

    configFile.close();
    sdManager->giveSD();
    
    return configFromJson(jsonString);
}

ConfigManagerStatus ConfigManager::saveConfig() {
    if (!sdManager) {
        #ifdef DEBUG_CONFIG_MANAGER
            Serial.println("ERROR: SD Manager is nullptr");
        #endif
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    if (!sdManager->takeSD()) {
        #ifdef DEBUG_CONFIG_MANAGER
            Serial.println("ERROR: Failed to take SD");
        #endif
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    std::string configFilePath {CONFIG::CONFIG_DIR};
    configFilePath += CONFIG::CONFIG_FILE;

    File configFile {SD.open(configFilePath.c_str(), FILE_WRITE)};
    if (!configFile) {
        sdManager->giveSD();
        #ifdef DEBUG_CONFIG_MANAGER
            Serial.println("ERROR: Failed to open config file");
        #endif
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }

    String jsonString {configToJson()};
    
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

MachineConfig ConfigManager::getConfig() {
    MachineConfig configcpy {};
    
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        configcpy = config;
        xSemaphoreGive(configMutex);
    }
    
    return configcpy;
}

ConfigManagerStatus ConfigManager::setConfig(const MachineConfig& newConfig) {
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        config = newConfig;
        xSemaphoreGive(configMutex);
        
        return saveConfig();
    }
    
    return ConfigManagerStatus::UNKNOWN_ERROR;
}

String ConfigManager::configToJson() {
    JsonDocument doc {};
   
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Parametry osi X
        JsonObject xAxis = doc["xAxis"].to<JsonObject>();
        xAxis["stepsPerMM"] = config.xAxis.stepsPerMM;
        xAxis["workFeedRate"] = config.xAxis.workFeedRate;
        xAxis["workAcceleration"] = config.xAxis.workAcceleration;
        xAxis["rapidFeedRate"] = config.xAxis.rapidFeedRate;
        xAxis["rapidAcceleration"] = config.xAxis.rapidAcceleration;
        
        // Parametry osi Y
        JsonObject yAxis = doc["yAxis"].to<JsonObject>();
        yAxis["stepsPerMM"] = config.yAxis.stepsPerMM;
        yAxis["workFeedRate"] = config.yAxis.workFeedRate;
        yAxis["workAcceleration"] = config.yAxis.workAcceleration;
        yAxis["rapidFeedRate"] = config.yAxis.rapidFeedRate;
        yAxis["rapidAcceleration"] = config.yAxis.rapidAcceleration;
        
        // Pozostałe parametry
        doc["useGCodeFeedRate"] = config.useGCodeFeedRate;
        doc["delayAfterStartup"] = config.delayAfterStartup;
        doc["deactivateESTOP"] = config.deactivateESTOP;
        doc["deactivateLimitSwitches"] = config.deactivateLimitSwitches;
        doc["limitSwitchType"] = config.limitSwitchType;
        
        xSemaphoreGive(configMutex);
    }
    
    // Serializuj dokument do stringa
    String result;
    serializeJson(doc, result);
    return result;
}

ConfigManagerStatus ConfigManager::configFromJson(const String& jsonString) {
    JsonDocument doc {};
    
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
        #ifdef DEBUG_CONFIG_MANAGER
            Serial.print("DEBUG CONFIG: JSON parse error: ");
            Serial.println(error.c_str());
        #endif
        return ConfigManagerStatus::JSON_PARSE_ERROR;
    }
    
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Parametry dla osi X
        if (doc["xAxis"].is<JsonObject>()) {
            JsonObject xAxis = doc["xAxis"];
            if (xAxis["stepsPerMM"].is<float>()) config.xAxis.stepsPerMM = xAxis["stepsPerMM"].as<float>();
            if (xAxis["workFeedRate"].is<float>()) config.xAxis.workFeedRate = xAxis["workFeedRate"].as<float>();
            if (xAxis["workAcceleration"].is<float>()) config.xAxis.workAcceleration = xAxis["workAcceleration"].as<float>();
            if (xAxis["rapidFeedRate"].is<float>()) config.xAxis.rapidFeedRate = xAxis["rapidFeedRate"].as<float>();
            if (xAxis["rapidAcceleration"].is<float>()) config.xAxis.rapidAcceleration = xAxis["rapidAcceleration"].as<float>();
        }
        
        // Parametry dla osi Y
        if (doc["yAxis"].is<JsonObject>()) {
            JsonObject yAxis = doc["yAxis"];
            if (yAxis["stepsPerMM"].is<float>()) config.yAxis.stepsPerMM = yAxis["stepsPerMM"].as<float>();
            if (yAxis["workFeedRate"].is<float>()) config.yAxis.workFeedRate = yAxis["workFeedRate"].as<float>();
            if (yAxis["workAcceleration"].is<float>()) config.yAxis.workAcceleration = yAxis["workAcceleration"].as<float>();
            if (yAxis["rapidFeedRate"].is<float>()) config.yAxis.rapidFeedRate = yAxis["rapidFeedRate"].as<float>();
            if (yAxis["rapidAcceleration"].is<float>()) config.yAxis.rapidAcceleration = yAxis["rapidAcceleration"].as<float>();
        }
        
        // Pozostałe parametry
        if (doc["useGCodeFeedRate"].is<bool>()) config.useGCodeFeedRate = doc["useGCodeFeedRate"].as<bool>();
        if (doc["delayAfterStartup"].is<int>()) config.delayAfterStartup = doc["delayAfterStartup"].as<int>();
        if (doc["deactivateESTOP"].is<bool>()) config.deactivateESTOP = doc["deactivateESTOP"].as<bool>();
        if (doc["deactivateLimitSwitches"].is<bool>()) config.deactivateLimitSwitches = doc["deactivateLimitSwitches"].as<bool>();
        if (doc["limitSwitchType"].is<uint8_t>()) config.limitSwitchType = doc["limitSwitchType"].as<uint8_t>();
        
        xSemaphoreGive(configMutex);
        
        #ifdef DEBUG_CONFIG_MANAGER
            Serial.println("DEBUG CONFIG: Configuration successfully loaded from JSON");
        #endif
        
        return ConfigManagerStatus::OK;
    }
    
    return ConfigManagerStatus::UNKNOWN_ERROR;
}

template<typename T>
ConfigManagerStatus ConfigManager::updateParameter(const std::string& paramName, T value) {
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Aktualizacja wybranego parametru
        // Parametry osi X
        if (paramName == "xAxis.stepsPerMM") config.xAxis.stepsPerMM = static_cast<float>(value);
        else if (paramName == "xAxis.workFeedRate") config.xAxis.workFeedRate = static_cast<float>(value);
        else if (paramName == "xAxis.workAcceleration") config.xAxis.workAcceleration = static_cast<float>(value);
        else if (paramName == "xAxis.rapidFeedRate") config.xAxis.rapidFeedRate = static_cast<float>(value);
        else if (paramName == "xAxis.rapidAcceleration") config.xAxis.rapidAcceleration = static_cast<float>(value);
        
        // Parametry osi Y
        else if (paramName == "yAxis.stepsPerMM") config.yAxis.stepsPerMM = static_cast<float>(value);
        else if (paramName == "yAxis.workFeedRate") config.yAxis.workFeedRate = static_cast<float>(value);
        else if (paramName == "yAxis.workAcceleration") config.yAxis.workAcceleration = static_cast<float>(value);
        else if (paramName == "yAxis.rapidFeedRate") config.yAxis.rapidFeedRate = static_cast<float>(value);
        else if (paramName == "yAxis.rapidAcceleration") config.yAxis.rapidAcceleration = static_cast<float>(value);
        
        // Pozostałe parametry
        else if (paramName == "useGCodeFeedRate") config.useGCodeFeedRate = static_cast<bool>(value);
        else if (paramName == "delayAfterStartup") config.delayAfterStartup = static_cast<int>(value);
        else if (paramName == "deactivateESTOP") config.deactivateESTOP = static_cast<bool>(value);
        else if (paramName == "deactivateLimitSwitches") config.deactivateLimitSwitches = static_cast<bool>(value);
        else if (paramName == "limitSwitchType") config.limitSwitchType = static_cast<uint8_t>(value);
        
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