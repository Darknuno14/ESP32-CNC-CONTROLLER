#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <LittleFS.h>
#include "WebServerManager.h"
#include "CONFIGURATION.H"

WebServerManager::WebServerManager(SDCardManager* sdManager) : sdManager(sdManager), server(nullptr), events(nullptr) {}

WebServerManager::~WebServerManager() {
    // Delete event source if allocated
    if (events) {
        eventsInitialized = false;
        delete events;
        events = nullptr;
    }

    // Delete server if allocated
    if (server) {
        serverInitialized = false;
        serverStarted = false;
        delete server;
        server = nullptr; 
    }
}

WebServerStatus WebServerManager::init() {
    // If already initialized, return already initialized status
    if (server != nullptr) {
        return WebServerStatus::ALREADY_INITIALIZED;
    }

    // Allocate memory for the web server
    server = new AsyncWebServer(80);
    if (server == nullptr) {
        return WebServerStatus::SERVER_ALLOCATION_FAILED;
    }
    this->serverInitialized = true;

    // Allocate memory for the event source if not already done
    if (!events) {
        events = new AsyncEventSource("/events");
        if (events == nullptr) { 
            delete server;
            server = nullptr;
            return WebServerStatus::EVENT_SOURCE_FAILED;
        } 
        this->eventsInitialized = true;
    }

    return WebServerStatus::OK;
}

WebServerStatus WebServerManager::begin() {
    // Check if the server is initialized
    if (server == nullptr) {
        return WebServerStatus::ALREADY_INITIALIZED;
    }

    // Setup the routes for the web server
    setupRoutes();

    // Add the event source handler to the server
    server->addHandler(events);

    // Serve static files from the LittleFS filesystem
    server->serveStatic("/", LittleFS, "/");

    // Start the web server
    server->begin();
    this->serverStarted = true;

    return WebServerStatus::OK;
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

    // End
    server->on("/api/sd-files", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: File list requested");
            
            if (!this->sdManager) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER ERROR: SD Manager not initialized");
                #endif
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

    // File upload endpoint
    server->on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Upload complete");
            #endif
            request->send(200, "text/plain", "Upload complete");
        },
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            static File uploadFile;
            if (index == 0) {
                // Build full file path (ensure leading slash if required)
                std::string filePath = CONFIG::PROJECTS_DIR + std::string(filename.c_str());
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: Starting upload of " + String(filename.c_str()));
                    Serial.println("DEBUG SERVER STATUS: Saving to " + String(filePath.c_str()));
                #endif
                uploadFile = SD.open(filePath.c_str(), FILE_WRITE);
                if (!uploadFile) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER ERROR: Failed to open file for writing");
                    #endif
                    request->send(500, "text/plain", "Failed to open file");
                    return;
                }
            }
            if (uploadFile) {
                if (len > 0) {
                    uploadFile.write(data, len);
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.printf("DEBUG SERVER STATUS: Written %d bytes\n", len);
                    #endif
                }
                if (final) {
                    uploadFile.close();
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER STATUS: File upload complete");
                    #endif
                }
            }
        });

    // Endpoint to refresh file list
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

    // Endpoint to select a file
    // Endpoint for selecting a file
    server->on("/api/select-file", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: File selection requested");
        #endif
        if (!request->hasParam("file")) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER ERROR: File parameter missing");
            #endif
            request->send(400, "text/plain", "File parameter missing");
            return;
        }
        std::string filename {request->getParam("file")->value().c_str()};
        #ifdef DEBUG_SERVER_ROUTES
            Serial.printf("DEBUG SERVER STATUS: Selected file: %s\n", filename.c_str());
        #endif
        sdManager->setSelectedProject(filename);
        request->send(200, "text/plain", "File selected: " + String(filename.c_str()));
    });

    // Endpoint to update the START command
    server->on("/api/start", HTTP_POST, [this](AsyncWebServerRequest *request) {

        this->startUserCommand = !startUserCommand;
        this->stopUserCommand = false;
 
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Start button pressed, commandStart= " + String(startUserCommand));
        #endif

        request->send(200, "text/plain", "Processing started");
    });

    // Update STOP endpoint
    server->on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {

        this->startUserCommand = false;
        this->stopUserCommand = !stopUserCommand;

        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Stop button pressed, commandStop = " + String(stopUserCommand));
        #endif
 
        request->send(200, "text/plain", "Processing started");
    });

    // Add endpoint for reading file content
    server->on("/api/sd-files/^.*", HTTP_GET, [this](AsyncWebServerRequest *request) {
        std::string filename {request->url().substring(14).c_str()}; // Correct offset: remove "/api/sd-files/"
        std::string filePath {CONFIG::PROJECTS_DIR + filename};
    
        if (!SD.exists(filePath.c_str())) {
            request->send(404, "text/plain", "File not found");
            return;
        }
    
        // Let the AsyncWebServer handle opening, reading, and closing the file.
        request->send(SD, filePath.c_str(), "text/plain");
    });

    server->on("/api/sd-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String json = "{\"initialized\":" + String(this->sdManager->isCardInitialized() ? "true" : "false") + "}";
        request->send(200, "application/json", json);
    });
        
        // Endpoint to reinitialize the SD card
        server->on("/api/reinitialize-sd", HTTP_POST, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: SD card reinitialization requested");
            #endif
    
            SDMenagerStatus result = this->sdManager->init();
            bool success = (result == SDMenagerStatus::OK);
            
            if (success) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: SD card reinitialization successful");
                #endif
                // Only refresh file list if initialization was successful
                this->sdManager->listProjectFiles();
            } else {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: SD card reinitialization failed");
                #endif
            }
    
            String json = "{\"success\":" + String(success ? "true" : "false") + "}";
            request->send(200, "application/json", json);
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
bool WebServerManager::getStartCommand() {
    return startUserCommand;
}   
bool WebServerManager::getStopCommand() {
    return stopUserCommand;
}
