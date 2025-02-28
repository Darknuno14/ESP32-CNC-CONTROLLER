#include "ConfigManager.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "CONFIGURATION.H"

ConfigManager::ConfigManager(SDCardManager* sdManager) {
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

    ConfigManagerStatus status = loadConfig();
    if (status == ConfigManagerStatus::OK) {
        configLoaded = true;
        return ConfigManagerStatus::OK;
    }
    else {
        return status;
    }

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
    
    File configFile {SD.open(CONFIG::CONFIG_DIR, FILE_READ)};
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
    if (!sdManager->takeSD()) {
        return ConfigManagerStatus::SD_ACCESS_ERROR;
    }
    
    std::string configFilePath {CONFIG::CONFIG_DIR};
    configFilePath += CONFIG::CONFIG_FILE;

    File configFile {SD.open(configFilePath.c_str(), FILE_WRITE)};
    if (!configFile) {
        sdManager->giveSD();
        return ConfigManagerStatus::FILE_OPEN_FAILED;
    }

    String jsonString {configToJson()};
    
    if (configFile.print(jsonString) == 0) {
        configFile.close();
        sdManager->giveSD();
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
    DynamicJsonDocument doc(CONFIG::JSON_DOC_SIZE);
   
    if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        // Parametry osi X
        JsonObject xAxis = doc.createNestedObject("xAxis");
        xAxis["stepsPerMM"] = config.xAxis.stepsPerMM;
        xAxis["workFeedRate"] = config.xAxis.workFeedRate;
        xAxis["workAcceleration"] = config.xAxis.workAcceleration;
        xAxis["rapidFeedRate"] = config.xAxis.rapidFeedRate;
        xAxis["rapidAcceleration"] = config.xAxis.rapidAcceleration;
        
        // Parametry osi Y
        JsonObject yAxis = doc.createNestedObject("yAxis");
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
    DynamicJsonDocument doc(CONFIG::JSON_DOC_SIZE);
    
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
        if (doc.containsKey("xAxis")) {
            JsonObject xAxis = doc["xAxis"];
            if (xAxis.containsKey("stepsPerMM")) config.xAxis.stepsPerMM = xAxis["stepsPerMM"].as<float>();
            if (xAxis.containsKey("workFeedRate")) config.xAxis.workFeedRate = xAxis["workFeedRate"].as<float>();
            if (xAxis.containsKey("workAcceleration")) config.xAxis.workAcceleration = xAxis["workAcceleration"].as<float>();
            if (xAxis.containsKey("rapidFeedRate")) config.xAxis.rapidFeedRate = xAxis["rapidFeedRate"].as<float>();
            if (xAxis.containsKey("rapidAcceleration")) config.xAxis.rapidAcceleration = xAxis["rapidAcceleration"].as<float>();
        }
        
        // Parametry dla osi Y
        if (doc.containsKey("yAxis")) {
            JsonObject yAxis = doc["yAxis"];
            if (yAxis.containsKey("stepsPerMM")) config.yAxis.stepsPerMM = yAxis["stepsPerMM"].as<float>();
            if (yAxis.containsKey("workFeedRate")) config.yAxis.workFeedRate = yAxis["workFeedRate"].as<float>();
            if (yAxis.containsKey("workAcceleration")) config.yAxis.workAcceleration = yAxis["workAcceleration"].as<float>();
            if (yAxis.containsKey("rapidFeedRate")) config.yAxis.rapidFeedRate = yAxis["rapidFeedRate"].as<float>();
            if (yAxis.containsKey("rapidAcceleration")) config.yAxis.rapidAcceleration = yAxis["rapidAcceleration"].as<float>();
        }
        
        // Pozostałe parametry
        if (doc.containsKey("useGCodeFeedRate")) config.useGCodeFeedRate = doc["useGCodeFeedRate"].as<bool>();
        if (doc.containsKey("delayAfterStartup")) config.delayAfterStartup = doc["delayAfterStartup"].as<int>();
        if (doc.containsKey("deactivateESTOP")) config.deactivateESTOP = doc["deactivateESTOP"].as<bool>();
        if (doc.containsKey("deactivateLimitSwitches")) config.deactivateLimitSwitches = doc["deactivateLimitSwitches"].as<bool>();
        if (doc.containsKey("limitSwitchType")) config.limitSwitchType = doc["limitSwitchType"].as<uint8_t>();
        
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