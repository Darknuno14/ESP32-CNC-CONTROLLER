#pragma once
#include <string>

enum class WiFiManagerStatus {
    OK,
    STA_MODE_FAILED,
    WIFI_NO_CONNECTION,
};

class WiFiManager {
    private:

    // Waits for a WiFi connection within the specified timeout
    bool waitForConnection(unsigned long timeout);

    public:
    // Default constructor for WiFiManager
    WiFiManager() = default;

    // Connects to the specified WiFi network with a timeout
    WiFiManagerStatus init();

    // Connects to the specified WiFi network using the provided SSID and password, with an optional timeout
    WiFiManagerStatus connect(const char* ssid, const char* password, unsigned long timeout = 10000);

    // Gets the local IP address of the ESP32 as a string
    std::string getLocalIP();
};