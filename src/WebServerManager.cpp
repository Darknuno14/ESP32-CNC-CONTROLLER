#include "WebServerManager.h"
#include "FSManager.h"
#include <LittleFS.h>


AsyncWebServer server(80);
AsyncEventSource* events{nullptr}; 

WebServerManager::WebServerManager(){};

WebServerManager::~WebServerManager() {
    if (events) {
        delete events;
        events = nullptr;
    }
}

bool WebServerManager::init() {
    if (!events) {
        events = new AsyncEventSource("/events");
        return true;
    }
    return false;
}

bool WebServerManager::begin() {
    setupRoutes();
    server.addHandler(events);

    server.serveStatic("/", LittleFS, "/");

    server.begin();
    return true;
}

void WebServerManager::setupRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/api/button", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("STATUS: Button has been pressed");
        request->send(200, "text/plain", "OK");
    });

        // Not found handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404);
    });
}