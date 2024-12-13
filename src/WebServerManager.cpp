#include "WebServerManager.h"
#include "FSManager.h"
#include <LittleFS.h>

WebServerManager::WebServerManager() : server(nullptr), events{nullptr} {} ;

WebServerManager::~WebServerManager() {
    // Check if events is allocated and delete it
    if (events) {
        delete events;
        events = nullptr; // Set events to nullptr after deletion
    }

    // Check if server is allocated and delete it
    if (server) {
        delete server;
        server = nullptr; // Set server to nullptr after deletion
    }
}

bool WebServerManager::init() {
    // Check if the server is already initialized
    if (server != nullptr) {
        Serial.println("ERROR: Web server already initialized!");
        return false;
    }

    // Allocate memory for the web server
    server = new AsyncWebServer(80);
    if (server == nullptr) {
        Serial.println("ERROR: Failed to allocate memory for web server!");
        return false;
    }

    // Allocate memory for the event source if not already done
    if (!events) {
        events = new AsyncEventSource("/events");
        if (events == nullptr) { 
            Serial.println("ERROR: Failed to allocate memory for event source!");
            // Clean up allocated server memory
            delete server;
            server = nullptr;
            return false;
        }
    }

    return true;
}

bool WebServerManager::begin() {
    // Check if the server is initialized
    if (server == nullptr) {
        Serial.println("ERROR: Web server not initialized!");
        return false;
    }

    // Setup the routes for the web server
    setupRoutes();

    // Add the event source handler to the server
    server->addHandler(events);

    // Serve static files from the LittleFS filesystem
    server->serveStatic("/", LittleFS, "/");

    // Start the web server
    server->begin();
    
    return true;
}

void WebServerManager::setupRoutes() {
    // Return if server isn't initialized
    if (server == nullptr) return; 

    // Serve the main index.html page on root URL
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    // Handle POST requests to /api/button endpoint
    server->on("/api/button", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("STATUS: Button has been pressed");
        request->send(200, "text/plain", "OK");
    });

    // Handle requests to non-existent endpoints with 404 response
    server->onNotFound([](AsyncWebServerRequest *request) {
        request->send(404);
    });
}