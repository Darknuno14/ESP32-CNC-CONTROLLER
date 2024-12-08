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
    // Initialize serial communication
    Serial.begin(115200);
    delay(1000); // Delay to allow time for serial monitor to connect
    
    // Initialize managers using instance methods
    if (!fsManager.init()) {
        Serial.println("ERROR: Failed to mount filesystem");
        return;
    } else {
        Serial.println("STATUS: Filesystem mounted successfully");
    }
    
    if (!wifiManager.connect(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("ERROR: Failed to connect to WiFi");
        return;
    } else {
        Serial.println("STATUS: Successfully connected to WiFi. IP Address: " + wifiManager.getLocalIP());
    }
    
    if (!webServer.init()) {
        Serial.println("ERROR: Failed to initialize web server");
        return;
    } else {
        Serial.println("STATUS: Web server initialized successfully");
    }

    if(!webServer.begin()) {
        Serial.println("ERROR: Failed to start web server");
        return;
    } else {
        Serial.println("STATUS: Web server started successfully");
    } 
}

void loop() {
    delay(10); // Small delay to prevent watchdog issues
}

