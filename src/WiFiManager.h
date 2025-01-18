#pragma once
#include <WiFi.h>
class WiFiManager {
    private:
        bool waitForConnection(unsigned long timeout);
    public:
        WiFiManager() = default;
        bool init();
        bool connect(const char* ssid, const char* password, unsigned long timeout = 10000);    
        String getLocalIP();
};