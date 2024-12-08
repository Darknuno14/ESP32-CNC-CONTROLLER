#pragma once
#include <WiFi.h>
class WiFiManager {
public:
    WiFiManager() = default;
    bool init();
    bool connect(const char* ssid, const char* password);
    
    String getLocalIP();
private:
    void waitForConnection();
};