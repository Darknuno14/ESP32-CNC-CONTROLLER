#include "WebServerManager.h"


WebServerManager::WebServerManager(SDCardManager* sdManager) : sdManager(sdManager), server(nullptr), events(nullptr) {}

WebServerManager::~WebServerManager() {
    // Check if events is allocated and delete it
    if (events) {
        eventsInitialized = false;
        delete events;
        events = nullptr; // Set events to nullptr after deletion
    }

    // Check if server is allocated and delete it
    if (server) {
        serverInitialized = false;
        serverStarted = false;
        delete server;
        server = nullptr; // Set server to nullptr after deletion
    }
}

WebServerError WebServerManager::init() {
    // Check if the server is already initialized
    if (server != nullptr) {
        return WebServerError::ALREADY_INITIALIZED;
    }

    // Allocate memory for the web server
    server = new AsyncWebServer(80);
    if (server == nullptr) {
        return WebServerError::SERVER_ALLOCATION_FAILED;
    } else {
        serverInitialized = true;
    }

    // Allocate memory for the event source if not already done
    if (!events) {
        events = new AsyncEventSource("/events");
        if (events == nullptr) { 
            // Clean up allocated server memory
            delete server;
            server = nullptr;
            return WebServerError::EVENT_SOURCE_FAILED;
        } else {
            eventsInitialized = true;
        }
    }

    return WebServerError::OK;
}

WebServerError WebServerManager::begin() {
    // Check if the server is initialized
    if (server == nullptr) {
        return WebServerError::ALREADY_INITIALIZED;
    }

    // Setup the routes for the web server
    setupRoutes();

    // Add the event source handler to the server
    server->addHandler(events);

    // Serve static files from the LittleFS filesystem
    server->serveStatic("/", LittleFS, "/");

    // Start the web server
    server->begin();
    serverStarted = true;

    return WebServerError::OK;
}

void WebServerManager::setupRoutes() {
    // Return if server isn't initialized
    if (server == nullptr) return; 

    // Serve the main index.html page on root URL
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Serving index.html\n");
        #endif
        request->send(LittleFS, "/index.html", "text/html");
    });

    // Handle POST requests to /api/button endpoint
    server->on("/api/button", HTTP_POST, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Button press received");
        #endif
        request->send(200, "text/plain", "OK");
    });

    // Add endpoint to get SD card file list
    server->on("/api/sd-files", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: File list requested");
            
            if (!this->sdManager) {
                Serial.println("DEBUG SERVER ERROR: SD Manager not initialized");
                request->send(500, "text/plain", "SD Manager not initialized");
                return;
            }
        #endif
        
        const std::vector<std::string>& files = this->sdManager->getProjectFiles();
        #ifdef DEBUG_SERVER_ROUTES
            Serial.printf("DEBUG SERVER STATUS: Found %d files\n", files.size());
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
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Upload complete");
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

            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Starting upload of " + filename);
                Serial.println("DEBUG SERVER STATUS: Saving to " + filePath);
            #endif

            uploadFile = SD.open(filePath, FILE_WRITE);
            if (!uploadFile) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG Failed to open file for writing");
                #endif
                request->send(500, "text/plain", "Failed to open file");
                return;
            }
        }
        if (uploadFile) {
            if (len) {
                uploadFile.write(data, len);
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.printf("DEBUG SERVER STATUS: Written %d bytes\n", len);
                #endif
            }
            if (final) {
                uploadFile.close();
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG File upload complete");
                #endif
            }
        }
    });

    server->on("/api/refresh-files", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: File list refresh requested");
        #endif
    
        if (!this->sdManager) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER ERROR: SD Manager not initialized");
            #endif

            request->send(500, "text/plain", "SD Manager not initialized");
            return;
        }
        
        this->sdManager->listProjectFiles();
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: File list refreshed");
        #endif
        request->send(200, "text/plain", "Files refreshed");
    });

    // Handle requests to non-existent endpoints with 404 response
    server->onNotFound([](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.printf("DEBUG SERVER ERROR: Route not found: %s\n", request->url().c_str());
        #endif
        request->send(404);
    });

}

bool WebServerManager::isServerStarted() {
    return serverStarted;
}

bool WebServerManager::isEventsInitialized() {
    return eventsInitialized;
}

bool WebServerManager::isServerInitialized() {
    return serverInitialized;
}