#include "WebServerManager.h"

#define DEBUG

WebServerManager::WebServerManager(SDCardManager* sdManager) : sdManager(sdManager), server(nullptr), events(nullptr) {}

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
        #ifdef DEBUG
            Serial.println("SERVER STATUS: Serving index.html\n");
        #endif
        request->send(LittleFS, "/index.html", "text/html");
    });

    // Handle POST requests to /api/button endpoint
    server->on("/api/button", HTTP_POST, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG
            Serial.println("SERVER STATUS: Button press received");
        #endif
        request->send(200, "text/plain", "OK");
    });

    // Add endpoint to get SD card file list
    server->on("/api/sd-files", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG
            Serial.println("SERVER STATUS: File list requested");
            
            if (!this->sdManager) {
                Serial.println("SERVER ERROR: SD Manager not initialized");
                request->send(500, "text/plain", "SD Manager not initialized");
                return;
            }
        #endif
        
        const std::vector<std::string>& files = this->sdManager->getProjectFiles();
        #ifdef DEBUG
            Serial.printf("SERVER STATUS: Found %d files\n", files.size());
        #endif

        String json = "[";
        for (size_t i{0}; i < files.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + String(files[i].c_str()) + "\"";
        }
        json += "]";

        request->send(200, "application/json", json);
    });

    server->on("/api/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        #ifdef DEBUG
            Serial.println("SERVER STATUS: Upload complete");
        #endif
        request->send(200, "text/plain", "Upload complete");
    },
    [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        static File uploadFile;
        if (!index) {
            // Construct full path with leading slash
            String filePath = sdManager->projectsDirectory;
            if (!filePath.startsWith("/")) filePath = "/" + filePath;
            if (!filePath.endsWith("/")) filePath += "/";
            filePath += filename;

            #ifdef DEBUG
                Serial.println("SERVER STATUS: Starting upload of " + filename);
                Serial.println("SERVER STATUS: Saving to " + filePath);
            #endif

            uploadFile = SD.open(filePath, FILE_WRITE);
            if (!uploadFile) {
                #ifdef DEBUG
                    Serial.println("Failed to open file for writing");
                #endif
                request->send(500, "text/plain", "Failed to open file");
                return;
            }
        }
        if (uploadFile) {
            if (len) {
                uploadFile.write(data, len);
                #ifdef DEBUG
                    Serial.printf("SERVER STATUS: Written %d bytes\n", len);
                #endif
            }
            if (final) {
                uploadFile.close();
                #ifdef DEBUG
                    Serial.println("File upload complete");
                #endif
            }
        }
    });

    server->on("/api/refresh-files", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG
            Serial.println("SERVER STATUS: File list refresh requested");
        #endif
    
        if (!this->sdManager) {
            #ifdef DEBUG
                Serial.println("SERVER ERROR: SD Manager not initialized");
            #endif

            request->send(500, "text/plain", "SD Manager not initialized");
            return;
        }
        
        this->sdManager->listProjectFiles();
        #ifdef DEBUG
            Serial.println("SERVER STATUS: File list refreshed");
        #endif
        request->send(200, "text/plain", "Files refreshed");
    });

    // Handle requests to non-existent endpoints with 404 response
    server->onNotFound([](AsyncWebServerRequest *request) {
        #ifdef DEBUG
            Serial.printf("SERVER ERROR: Route not found: %s\n", request->url().c_str());
        #endif
        request->send(404);
    });

}