#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <LittleFS.h>
#include "WebServerManager.h"
#include "CONFIGURATION.H"

WebServerManager::WebServerManager(SDCardManager* sdManager, ConfigManager* configManager, QueueHandle_t extCommandQueue) 
    : sdManager(sdManager), configManager(configManager), commandQueue(extCommandQueue) {
}

WebServerManager::~WebServerManager() {
    if (events) {
        eventsInitialized = false;
        delete events;
        events = nullptr;
    }

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

    events->onConnect([](AsyncEventSourceClient *client) {
        if (client->lastId()) {
            Serial.printf("Client reconnected! Last message ID: %u\n", client->lastId());
        }
        
        // Wyślij początkowe info o połączeniu
        client->send("Connected to ESP32 CNC EventSource", NULL, millis(), 1000);
    });

    return WebServerStatus::OK;
}

WebServerStatus WebServerManager::begin() {
    // Check if the server is initialized
    if (server == nullptr) {
        return WebServerStatus::NOT_INITIALIZED;
    }

    // Add the event source handler to the server
    if (events != nullptr) {
        server->addHandler(events);
    }

    // Setup the routes for the web server
    setupRoutes();
    
    // Serve CSS files (z większym bezpieczeństwem)
    if (LittleFS.exists("/css/styles.css")) {
        server->serveStatic("/css/", LittleFS, "/css/")
            .setCacheControl("max-age=86400"); // Cache for 24 hours
    }

    // Serve JavaScript files (z większym bezpieczeństwem)
    if (LittleFS.exists("/js/")) {
        server->serveStatic("/js/", LittleFS, "/js/")
            .setCacheControl("max-age=86400"); // Cache for 24 hours
    }
    
    // Serve HTML and other files from root
    server->serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html");

    
    // Start the web server
    server->begin();
    this->serverStarted = true;

    return WebServerStatus::OK;
}

void WebServerManager::setupRoutes() {
    if (server == nullptr) return; 

    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Serving index.html\n");
        #endif
        request->send(LittleFS, "/index.html", "text/html");
    });

    server->on("/projects.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Serving projects.html");
        #endif
        request->send(LittleFS, "/projects.html", "text/html");
    });

    server->on("/jog.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Serving jog.html");
        #endif
        request->send(LittleFS, "/jog.html", "text/html");
    });

    server->on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Serving config.html");
        #endif
        request->send(LittleFS, "/config.html", "text/html");
    });

    setupIndexRoutes();
    setupConfigRoutes();
    setupJogRoutes();
    setupProjectsRoutes();

    // Brak strony
    server->onNotFound([](AsyncWebServerRequest *request) {
        Serial.printf("404 Not Found: %s (Method: %s)\n", 
                     request->url().c_str(), 
                     request->methodToString());
        request->send(404, "text/plain", "404: Not found");
    });
}

void WebServerManager::setupIndexRoutes() {
    // Przycisk START
    server->on("/api/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Start button pressed");
        #endif
        
        // Zamiast flagi, wysyłamy komendę START przez kolejkę
        this->sendCommand(CommandType::START);
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Przycisk STOP
    server->on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Stop button pressed");
        #endif
        
        this->sendCommand(CommandType::STOP);
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Przycisk PAUSE
    server->on("/api/pause", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Pause button pressed");
        #endif
        
        this->sendCommand(CommandType::PAUSE);
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Przycisk do ręcznego bazowania pozycji
    server->on("/api/zero", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Zero position requested");
        #endif
        
        this->sendCommand(CommandType::ZERO);
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Przycisk bazowania
    server->on("/api/home", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Homing requested");
        #endif
        
        this->sendCommand(CommandType::HOMING);
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Aktualizacja statusu maszyny
    server->on("/api/update-machine-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Machine status requested");
        #endif
        
        static MachineState lastKnownState;
        static unsigned long lastStateUpdate = 0;
        
        // Przygotowanie JSON z danymi
        DynamicJsonDocument doc(CONFIG::JSON_DOC_SIZE);
        
        // Stan maszyny
        doc["state"] = static_cast<int>(lastKnownState.state);
        doc["isPaused"] = lastKnownState.isPaused;
        doc["hasError"] = lastKnownState.hasError;
        doc["errorCode"] = lastKnownState.errorCode;
        
        // Pozycja
        doc["currentX"] = lastKnownState.currentX;
        doc["currentY"] = lastKnownState.currentY;
        doc["relativeMode"] = lastKnownState.relativeMode;
        
        // Stan urządzeń
        doc["hotWireOn"] = lastKnownState.hotWireOn;
        doc["fanOn"] = lastKnownState.fanOn;
        
        // Informacje o zadaniu
        if (lastKnownState.currentProject.length() > 0) {
            doc["currentProject"] = lastKnownState.currentProject;
        } else {
            doc["currentProject"] = "";
        }
        doc["jobProgress"] = lastKnownState.jobProgress;
        doc["currentLine"] = lastKnownState.currentLine;
        doc["jobStartTime"] = lastKnownState.jobStartTime;
        doc["jobRunTime"] = lastKnownState.jobRunTime;
        
        // Konwersja JSON na string
        String jsonString;
        serializeJson(doc, jsonString);
        
        // Wysłanie odpowiedzi
        request->send(200, "application/json", jsonString);
    });
}

void WebServerManager::setupConfigRoutes() {
    // Pobieranie konfiguracji
    server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Configuration requested");
        #endif
        
        if (!this->configManager) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Config Manager not initialized\"}");
            return;
        }
        
        String jsonConfig = this->configManager->configToJson();
        request->send(200, "application/json", jsonConfig);
    });

    // Aktualizacja całej konfiguracji
    server->on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            request->send(200);
        },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String jsonBuffer;
            
            if (index == 0) jsonBuffer = "";
            
            for (size_t i = 0; i < len; i++) {
                jsonBuffer += (char)data[i];
            }
            
            if (index + len == total) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: Processing complete config");
                #endif
                
                if (!this->configManager) {
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"Config Manager not initialized\"}");
                    return;
                }
                
                ConfigManagerStatus status = this->configManager->configFromJson(jsonBuffer);
                
                if (status == ConfigManagerStatus::OK) {
                    status = this->configManager->saveConfig();
                }
                
                String response = "{\"success\":" + String(status == ConfigManagerStatus::OK ? "true" : "false") + "}";
                request->send(status == ConfigManagerStatus::OK ? 200 : 400, "application/json", response);
            }
        }
    );


}

void WebServerManager::setupJogRoutes() {
}

void WebServerManager::setupProjectsRoutes() {

        server->on("/api/sd_content", HTTP_GET, [this](AsyncWebServerRequest *request) {
            // Sprawdź, czy parametr file istnieje
            if (!request->hasParam("file")) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing file parameter\"}");
                return;
            }
            
            // Pobierz nazwę pliku z parametru
            String filename = request->getParam("file")->value();
            
            #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER: Requested SD file content: %s\n", filename.c_str());
            #endif
            
            // Utwórz pełną ścieżkę do pliku na karcie SD
            String filePath = String(CONFIG::PROJECTS_DIR) + filename;
            
            // Sprawdź dostępność karty SD
            if (!this->sdManager->takeSD()) {
                request->send(503, "application/json", "{\"success\":false,\"message\":\"SD card is busy\"}");
                return;
            }
            
            // Sprawdź, czy plik istnieje
            if (!SD.exists(filePath)) {
                Serial.printf("File not found: %s\n", filePath.c_str());
                this->sdManager->giveSD();
                request->send(404, "application/json", "{\"success\":false,\"message\":\"File not found\"}");
                return;
            }
            
            // Wyślij zawartość pliku
            request->send(SD, filePath, "text/plain");
            
            // Zwolnij blokadę karty SD
            this->sdManager->giveSD();
        });

        // Lista plików
        server->on("/api/list-files", HTTP_GET, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: File list requested");
            #endif
            
            if (!this->sdManager) {
                if(sdManager == nullptr) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER ERROR: SD Manager not initialized");
                    #endif
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"SD Manager not initialized\"}");
                    return;
                }
            } else if (sdManager->isCardInitialized() == false) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER ERROR: SD Card not initialized");
                    #endif
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"SD Card not initialized\"}");
                    return;
            }
            
            std::vector<std::string> files;
            SDManagerStatus status = this->sdManager->getProjectFiles(files);
            
            if (status != SDManagerStatus::OK) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to get project files\"}");
                return;
            }
            
            String json = "[";
            for (size_t i{0}; i < files.size(); ++i) {
                if (i > 0) json += ",";
                json += "\"" + String(files[i].c_str()) + "\"";
            }
            json += "]";
    
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Files retrieved successfully\",\"files\":" + json + "}");
        });

        // Wysyłanie pliku
        server->on("/api/upload-file", HTTP_POST,
            [](AsyncWebServerRequest *request) {
                request->send(200, "application/json", "{\"success\":true,\"message\":\"Upload complete\"}");
            },
            [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
                static File uploadFile;
                if (index == 0) {
                    String filePath = String(CONFIG::PROJECTS_DIR) + filename;
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER STATUS: Starting upload of " + String(filename.c_str()));
                    #endif
                    
                    if (!this->sdManager->takeSD()) {
                        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to access SD card\"}");
                        return;
                    }
                    
                    uploadFile = SD.open(filePath.c_str(), FILE_WRITE);
                    if (!uploadFile) {
                        this->sdManager->giveSD();
                        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to open file\"}");
                        return;
                    }
                }
                
                if (uploadFile) {
                    if (len > 0) {
                        uploadFile.write(data, len);
                    }
                    if (final) {
                        uploadFile.close();
                        this->sdManager->giveSD();
                        this->sdManager->updateProjectList();
                    }
                }
            }
        );

        // Odświeżanie listy plików
        server->on("/api/refresh-files", HTTP_POST, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: File list refresh requested");
            #endif
            
            if (!this->sdManager) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"SD Manager not initialized\"}");
                return;
            }
            
            SDManagerStatus status = this->sdManager->updateProjectList();
            bool success = (status == SDManagerStatus::OK);
            
            String response = "{\"success\":" + String(success ? "true" : "false") + "}";
            request->send(success ? 200 : 500, "application/json", response);
        });

        // Wybór pliku
        server->on("/api/select-file", HTTP_POST, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: File selection requested");
            #endif
            
            if (!request->hasParam("file")) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"File parameter missing\"}");
                return;
            }
            
            std::string filename {request->getParam("file")->value().c_str()};
            #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: Selected file: %s\n", filename.c_str());
            #endif
            
            SDManagerStatus status = this->sdManager->setSelectedProject(filename);
            bool success = (status == SDManagerStatus::OK);
            
            String response = "{\"success\":" + String(success ? "true" : "false");
            if (success) {
                response += ",\"file\":\"" + String(filename.c_str()) + "\"";
            } else {
                response += ",\"message\":\"Failed to select file\"";
            }
            response += "}";
            
            request->send(success ? 200 : 400, "application/json", response);
        });

        // Status karty SD
        server->on("/api/sd-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String json = "{\"initialized\":" + String(this->sdManager->isCardInitialized() ? "true" : "false") + "}";
            request->send(200, "application/json", json);
        });

    // Reinicjalizacja karty SD i ConfigManager
    server->on("/api/reinitialize-sd", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: SD card reinitialization requested");
        #endif

        // Najpierw zainicjalizuj kartę SD
        SDManagerStatus sdResult = this->sdManager->init();
        bool sdSuccess = (sdResult == SDManagerStatus::OK);
        
        bool configSuccess = false;
        String message = "";
        
        if (sdSuccess) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: SD card reinitialization successful");
            #endif
            
            // Następnie zainicjalizuj/reinicjalizuj ConfigManager
            if (this->configManager) {
                ConfigManagerStatus configResult = this->configManager->init();
                configSuccess = (configResult == ConfigManagerStatus::OK);
                
                if (configSuccess) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER STATUS: Config manager reinitialized successfully");
                    #endif
                    message = "SD card and configuration reinitialized successfully";
                } else {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER WARNING: Config manager reinitialization failed");
                    #endif
                    message = "SD card reinitialized, but configuration failed";
                }
            } else {
                // Jeśli configManager nie istnieje, stwórz nowy
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER WARNING: Creating new Config manager");
                #endif
                this->configManager = new ConfigManager(this->sdManager);
                ConfigManagerStatus configResult = this->configManager->init();
                configSuccess = (configResult == ConfigManagerStatus::OK);
                
                if (configSuccess) {
                    message = "SD card reinitialized and new configuration manager created";
                } else {
                    message = "SD card reinitialized, but failed to create configuration manager";
                }
            }
            
            // Zaktualizuj listę projektów
            this->sdManager->updateProjectList();
        } else {
            message = "Failed to reinitialize SD card";
        }
    
        // Odpowiedź JSON ze szczegółowymi informacjami
        DynamicJsonDocument doc(256);
        doc["success"] = sdSuccess;
        doc["configSuccess"] = configSuccess;
        doc["message"] = message;
        
        String jsonResponse;
        serializeJson(doc, jsonResponse);
        request->send(200, "application/json", jsonResponse);
    });

        // Usuwanie plików
        server->on("/api/delete-file", HTTP_POST, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: File deletion requested");
            #endif
            
            if (!request->hasParam("file")) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"File parameter missing\"}");
                return;
            }
            
            std::string filename = request->getParam("file")->value().c_str();
            std::string filePath = CONFIG::PROJECTS_DIR + filename;
            
            #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: Deleting file: %s\n", filePath.c_str());
            #endif
            
            if (this->sdManager->takeSD()) {
                bool success = SD.remove(filePath.c_str());
                this->sdManager->giveSD();
                
                if (success) {
                    this->sdManager->updateProjectList();
                    request->send(200, "application/json", "{\"success\":true}");
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Failed to delete file\"}");
                }
            } else {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to access SD card\"}");
            }
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

void WebServerManager::sendCommand(CommandType type, float param1, float param2, float param3) {
    if (commandQueue) {
        WebserverCommand cmd {};
        cmd.type = type;
        cmd.param1 = param1;
        cmd.param2 = param2;
        cmd.param3 = param3;
        
        xQueueSend(commandQueue, &cmd, 0);
        
        #ifdef DEBUG_SERVER_ROUTES
            Serial.printf("DEBUG SERVER: Wysłano komendę typu %d z parametrami: %.2f, %.2f, %.2f\n", 
                         static_cast<int>(type), param1, param2, param3);
        #endif
    }
}

void WebServerManager::sendEvent(const char* event, const char* data) {
    if (events && eventsInitialized) {
        events->send(data, event, millis());
    }
}




