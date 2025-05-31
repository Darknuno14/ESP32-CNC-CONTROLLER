#include "WiFiManager.h"
#include <WiFi.h>

WiFiManagerStatus WiFiManager::init() {
    // Set the WiFi mode to station (client) mode.
    if (!WiFi.mode(WIFI_STA)) {
        return WiFiManagerStatus::STA_MODE_FAILED;
    }
    return WiFiManagerStatus::OK;
}

WiFiManagerStatus WiFiManager::connect(const char* ssid, const char* password, unsigned long timeout) {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    if (waitForConnection(timeout)) {
        Serial.println("Connected to WiFi, IP address:");
        Serial.println(WiFi.localIP());
        return WiFiManagerStatus::OK;
    }
    else {
        Serial.println("Connection failed");
        return WiFiManagerStatus::OK;
    }
}

bool WiFiManager::waitForConnection(unsigned long timeout) {
    unsigned long startTime = millis();
    unsigned long printTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
        if (millis() - printTime >= 500) {
            Serial.print(".");
            printTime = millis();
        }
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

std::string WiFiManager::getLocalIP() {
    return WiFi.localIP().toString().c_str();
}
