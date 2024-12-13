#include <Arduino.h>

#include "credentials.h"
#include "WiFiManager.h"
#include "FSManager.h"
#include "WebServerManager.h"

// Create manager instances
FSManager fsManager;
WiFiManager wifiManager;
WebServerManager webServer;

void setup() {
    // Start serial communication at 115200 baud rate
    Serial.begin(115200);
    delay(1000); // Wait for serial monitor connection

    // Mount the LittleFS filesystem
    if(fsManager.init()) {
        Serial.println("LittleFS initialized successfully");
    }
    
    // Connect to WiFi using credentials from credentials.h
    if(wifiManager.connect(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("WiFi connected successfully");
    }
    
    // Set up web server routes and handlers
    if (webServer.init()) {
        Serial.println("Web server initialized successfully");
    }

    // Start the web server on configured port
    if (webServer.begin()) {
        Serial.println("Web server started successfully");
    }
}

void loop() {
    delay(10); // Small delay to prevent watchdog issues
}

