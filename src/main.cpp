#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "credentials.h" // WiFi credentials

// WiFi Credentials
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Web Server on port 80
AsyncWebServer server(80);

// Create an Event Source
// It has to be a pointer because the application was crashing when it was a global variable
AsyncEventSource* events = nullptr;


void setupWiFi() 
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.println("STATUS: Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.println('.');
        delay(500);
    }
    Serial.println("STATUS: Successfully connected to WiFi. IP Address: " + WiFi.localIP().toString());
}

void setupWebServer() {
    events = new AsyncEventSource("/events");

    // Serve index.html
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        events->send("ping", "event", millis());
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.serveStatic("/", LittleFS, "/");

    // 404 Handler
    server.onNotFound([](AsyncWebServerRequest *request)
    {
        request->send(404, "text/plain", "Not Found");
    });

    events->onConnect([](AsyncEventSource *source, AsyncEventSourceClient *client){
        Serial.println("Client connected");
    });

    events->onDisconnect([](AsyncEventSource *source, AsyncEventSourceClient *client){
        Serial.println("Client disconnected");
    });

    server.addHandler(events);
    server.begin();
    Serial.println("STATUS: Successfully started HTTP Server");
}

void setupFS()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("ERROR: LittleFS Mount Failed");
    }
    else
    {
        Serial.println("STATUS: Successfully mounted LittleFS");
    }

}
void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    // Mount LittleFS - true 
    setupFS();

    // Connect to WiFi
    setupWiFi();
    
    // Setup Web Server
    setupWebServer();
}

void loop() {
    // Periodic WiFi connection check
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 60000)
    {
        lastCheck = millis();

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("WiFi Disconnected. Reconnecting...");
            WiFi.reconnect();
        }
    }
}