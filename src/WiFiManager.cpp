// ================================================================================
//                            MENADŻER POŁĄCZENIA WiFi
// ================================================================================
// Zarządzanie połączeniem WiFi ESP32 w trybie stacji (klient)
// Obsługuje inicjalizację, łączenie z siecią oraz monitorowanie stanu

#include "WiFiManager.h"
#include <WiFi.h>

// ================================================================================
//                            INICJALIZACJA SYSTEMU
// ================================================================================

WiFiManagerStatus WiFiManager::init() {
    // Ustawienie trybu WiFi na stację (klient) - wymagane przed łączeniem
    if (!WiFi.mode(WIFI_STA)) {
        return WiFiManagerStatus::STA_MODE_FAILED;
    }
    return WiFiManagerStatus::OK;
}

// ================================================================================
//                          ZARZĄDZANIE POŁĄCZENIEM
// ================================================================================

WiFiManagerStatus WiFiManager::connect(const char* ssid, const char* password, unsigned long timeout) {
    // Inicjalizacja połączenia z podanymi parametrami sieci
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    
    // Oczekiwanie na nawiązanie połączenia z timeout
    if (waitForConnection(timeout)) {
        Serial.println("Connected to WiFi, IP address:");
        Serial.println(WiFi.localIP());
        return WiFiManagerStatus::OK;
    }
    else {
        Serial.println("Connection failed");
        return WiFiManagerStatus::WIFI_NO_CONNECTION;
    }
}

bool WiFiManager::waitForConnection(unsigned long timeout) {
    unsigned long startTime = millis();
    unsigned long printTime = millis();
    
    // Pętla oczekiwania z wizualną informacją o postępie
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
        if (millis() - printTime >= 500) {
            Serial.print(".");
            printTime = millis();
        }
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

// ================================================================================
//                          INFORMACJE O POŁĄCZENIU
// ================================================================================

std::string WiFiManager::getLocalIP() {
    // Zwrócenie aktualnego adresu IP urządzenia w sieci
    return WiFi.localIP().toString().c_str();
}
