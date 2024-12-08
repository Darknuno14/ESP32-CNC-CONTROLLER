#include <Arduino.h>

#include "credentials.h"
#include "WiFiManager.h"
#include "FSManager.h"
#include "WebServerManager.h"

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    delay(1000); // Delay to allow time for serial monitor to connect
    
    // Initialize the file system
    FSManager::init();
    
    // Initialize WiFi
    WiFiManager::init(WIFI_SSID, WIFI_PASSWORD);
    
    // Initialize the web server
    WebServerManager::init();
    // Start the web server
    WebServerManager::begin(); 
}

void loop() {
    delay(10); // Small delay to prevent watchdog issues
}

