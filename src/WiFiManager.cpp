#include "WiFiManager.h"

bool WiFiManager::init() {
    WiFi.mode(WIFI_STA);
    return true;
}

bool WiFiManager::connect(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    waitForConnection();
    return true;
}

void WiFiManager::waitForConnection() {
    while (WiFi.status() != WL_CONNECTED) {
        Serial.println('.');
        delay(500);
    }
} 

String WiFiManager::getLocalIP() {
    return WiFi.localIP().toString();
}
