#pragma once
#include <WiFi.h>

class WiFiManager {
public:
    static void init(const char* ssid, const char* password);
private:
    static void waitForConnection();
};