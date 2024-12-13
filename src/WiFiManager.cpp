#include "WiFiManager.h"

bool WiFiManager::init() {
    WiFi.mode(WIFI_STA);
    return true;
}

bool WiFiManager::connect(const char* ssid, const char* password, unsigned long timeout) {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    if (waitForConnection(timeout)) {
        Serial.println("Connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println("Connection failed");
        return false;
    }
}

bool WiFiManager::waitForConnection(unsigned long timeout) {
   unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
        Serial.print(".");
        delay(500);
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getLocalIP() {
    return WiFi.localIP().toString();
}
