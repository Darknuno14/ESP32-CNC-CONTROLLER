#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <LittleFS.h>
#include "WebServerManager.h"
#include "CONFIGURATION.H"

WebServerManager::WebServerManager(SDCardManager* sdManager, ConfigManager* configManager) 
    : sdManager(sdManager), configManager(configManager), server(nullptr), events(nullptr) {}

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

        server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Configuration requested");
            #endif
            
            if (!this->configManager) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER ERROR: Config Manager not initialized");
                #endif
                request->send(500, "text/plain", "Config Manager not initialized");
                return;
            }
            
            String jsonConfig = this->configManager->configToJson();
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Sending configuration");
            #endif
            
            request->send(200, "application/json", jsonConfig);
        });
        
        // Endpoint do aktualizacji całej konfiguracji
        server->on("/api/config", HTTP_POST, 
            [](AsyncWebServerRequest *request) {
                // Obsługa zwróconej odpowiedzi - będzie zrealizowana w handleBody
                request->send(200);
            },
            NULL,  // Upload handler - nie używany
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.printf("DEBUG SERVER STATUS: Config update, %u/%u bytes\n", index + len, total);
                #endif
                
                if (!this->configManager) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER ERROR: Config Manager not initialized");
                    #endif
                    request->send(500, "text/plain", "Config Manager not initialized");
                    return;
                }
                
                // Bufor na dane JSON
                static String jsonBuffer;
                
                // Jeśli to pierwszy fragment
                if (index == 0) jsonBuffer = "";
                
                // Dodaj fragment do bufora
                for (size_t i = 0; i < len; i++) {
                    jsonBuffer += (char)data[i];
                }
                
                // Jeśli to ostatni fragment, przetwórz dane
                if (index + len == total) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.println("DEBUG SERVER STATUS: Processing complete config");
                        Serial.println(jsonBuffer);
                    #endif
                    
                    // Przetwórz JSON na konfigurację
                    ConfigManagerStatus status = this->configManager->configFromJson(jsonBuffer);
                    
                    // Zapisz konfigurację
                    if (status == ConfigManagerStatus::OK) {
                        status = this->configManager->saveConfig();
                    }
                    
                    // Wyślij odpowiedź
                    String response = "{\"success\":" + String(status == ConfigManagerStatus::OK ? "true" : "false") + "}";
                    request->send(status == ConfigManagerStatus::OK ? 200 : 400, "application/json", response);
                }
            }
        );
        
        // Endpoint do aktualizacji pojedynczego parametru
        server->on("/api/config/parameter", HTTP_POST, [this](AsyncWebServerRequest *request) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Parameter update requested");
            #endif
            
            if (!this->configManager) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER ERROR: Config Manager not initialized");
                #endif
                request->send(500, "text/plain", "Config Manager not initialized");
                return;
            }
            
            // Sprawdź, czy parametry są obecne
            if (!request->hasParam("name", true) || !request->hasParam("value", true)) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER ERROR: Missing parameters");
                #endif
                request->send(400, "text/plain", "Missing parameters");
                return;
            }
            
            // Pobierz nazwę i wartość parametru
            std::string paramName = request->getParam("name", true)->value().c_str();
            String paramValue = request->getParam("value", true)->value();
            
            ConfigManagerStatus status;
            
            // Określ typ parametru i wywołaj odpowiednią metodę
            if (paramName.find("Steps") != std::string::npos || 
                paramName.find("Feed") != std::string::npos || 
                paramName.find("Acceleration") != std::string::npos ||
                paramName.find("Temperature") != std::string::npos) {
                // Parametry zmiennoprzecinkowe
                status = this->configManager->updateParameter<float>(paramName, paramValue.toFloat());
            } else if (paramName.find("delay") != std::string::npos || 
                      paramName.find("Speed") != std::string::npos) {
                // Parametry całkowite
                status = this->configManager->updateParameter<int>(paramName, paramValue.toInt());
            } else if (paramName.find("enable") != std::string::npos || 
                      paramName.find("use") != std::string::npos) {
                // Parametry logiczne
                status = this->configManager->updateParameter<bool>(paramName, paramValue == "true" || paramValue == "1");
            } else {
                // Domyślnie jako zmiennoprzecinkowe
                status = this->configManager->updateParameter<float>(paramName, paramValue.toFloat());
            }
            
            // Wyślij odpowiedź
            String response = "{\"success\":" + String(status == ConfigManagerStatus::OK ? "true" : "false") + "}";
            request->send(status == ConfigManagerStatus::OK ? 200 : 400, "application/json", response);
        });

            // Endpoint do obsługi plików HTML
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

    // Endpoint do usuwania plików
    server->on("/api/delete-file", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: File deletion requested");
        #endif
        
        if (!request->hasParam("file")) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER ERROR: File parameter missing");
            #endif
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
                this->sdManager->listProjectFiles();
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Failed to delete file\"}");
            }
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to access SD card\"}");
        }
    });

    // Endpoint do podglądu pliku G-code
    server->on("/api/preview", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: G-Code preview requested");
        #endif
        
        if (!request->hasParam("file")) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER ERROR: File parameter missing");
            #endif
            request->send(400, "text/plain", "File parameter missing");
            return;
        }
        
        std::string filename = request->getParam("file")->value().c_str();
        std::string filePath = CONFIG::PROJECTS_DIR + filename;
        
        #ifdef DEBUG_SERVER_ROUTES
            Serial.printf("DEBUG SERVER STATUS: Previewing file: %s\n", filePath.c_str());
        #endif
        
        if (!SD.exists(filePath.c_str())) {
            request->send(404, "text/plain", "File not found");
            return;
        }
        
        // Pozwól WebServowi obsłużyć otwieranie, czytanie i zamykanie pliku
        request->send(SD, filePath.c_str(), "text/plain");
    });

    // Endpoint do wstrzymywania
    server->on("/api/pause", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Pause button pressed");
        #endif
        
        // Ustaw flagę wstrzymania
        this->pauseUserCommand = !pauseUserCommand;
        
        request->send(200, "text/plain", "Processing paused");
    });

    // Endpoint do sterowania ręcznego JOG
    server->on("/api/jog", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            // Ten handler obsługuje sam request (empty)
            request->send(200);
        },
        NULL,  // Upload handler - nie używany
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: JOG command received, %u/%u bytes\n", index + len, total);
            #endif
            
            // Bufor na dane JSON
            static String jsonBuffer;
            
            // Jeśli to pierwszy fragment
            if (index == 0) jsonBuffer = "";
            
            // Dodaj fragment do bufora
            for (size_t i = 0; i < len; i++) {
                jsonBuffer += (char)data[i];
            }
            
            // Jeśli to ostatni fragment, przetwórz dane
            if (index + len == total) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: Processing JOG command");
                    Serial.println(jsonBuffer);
                #endif
                
                // Przetwórz dane JSON (używając ArduinoJson)
                StaticJsonDocument<512> doc;
                DeserializationError error = deserializeJson(doc, jsonBuffer);
                
                if (error) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.printf("DEBUG SERVER ERROR: JSON parsing failed: %s\n", error.c_str());
                    #endif
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                    return;
                }
                
                // Pobierz wartości
                float x = doc["x"];
                float y = doc["y"];
                float speed = doc["speed"];
                
                // Tutaj ustaw flagi JOG dla głównego zadania CNC
                // W tym przykładzie używamy istniejącej flagi commandJog oraz globalnych zmiennych
                // do przechowywania parametrów JOG
                this->jogX = x;
                this->jogY = y;
                this->jogSpeed = speed;
                this->commandJog = true;
                
                // Wyślij odpowiedź
                request->send(200, "application/json", "{\"success\":true}");
            }
        }
    );

    // Endpoint do zerowania pozycji
    server->on("/api/zero", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Zero position requested");
        #endif
        
        // Ustaw flagę zerowania pozycji
        this->commandZero = true;
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Endpoint do bazowania
    server->on("/api/home", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Homing requested");
        #endif
        
        // Ustaw flagę bazowania
        this->commandHoming = true;
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Endpoint do zatrzymania awaryjnego
    server->on("/api/emergency-stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Emergency stop requested");
        #endif
        
        // Ustaw flagę zatrzymania awaryjnego
        this->commandEmergencyStop = true;
        
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Endpoint do sterowania drutem
    server->on("/api/wire", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            // Ten handler obsługuje sam request (empty)
            request->send(200);
        },
        NULL,  // Upload handler - nie używany
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: Wire control command received, %u/%u bytes\n", index + len, total);
            #endif
            
            // Bufor na dane JSON
            static String jsonBuffer;
            
            // Jeśli to pierwszy fragment
            if (index == 0) jsonBuffer = "";
            
            // Dodaj fragment do bufora
            for (size_t i = 0; i < len; i++) {
                jsonBuffer += (char)data[i];
            }
            
            // Jeśli to ostatni fragment, przetwórz dane
            if (index + len == total) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: Processing wire control command");
                    Serial.println(jsonBuffer);
                #endif
                
                // Przetwórz dane JSON
                StaticJsonDocument<128> doc;
                DeserializationError error = deserializeJson(doc, jsonBuffer);
                
                if (error) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.printf("DEBUG SERVER ERROR: JSON parsing failed: %s\n", error.c_str());
                    #endif
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                    return;
                }
                
                // Pobierz wartość stanu
                bool state = doc["state"];
                
                // Ustaw stan drutu (globalnie)
                if (state) {
                    this->wireOn = true;
                } else {
                    this->wireOn = false;
                }
                
                // Wyślij odpowiedź
                request->send(200, "application/json", "{\"success\":true}");
            }
        }
    );

    // Endpoint do sterowania wentylatorem
    server->on("/api/fan", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            // Ten handler obsługuje sam request (empty)
            request->send(200);
        },
        NULL,  // Upload handler - nie używany
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: Fan control command received, %u/%u bytes\n", index + len, total);
            #endif
            
            // Bufor na dane JSON
            static String jsonBuffer;
            
            // Jeśli to pierwszy fragment
            if (index == 0) jsonBuffer = "";
            
            // Dodaj fragment do bufora
            for (size_t i = 0; i < len; i++) {
                jsonBuffer += (char)data[i];
            }
            
            // Jeśli to ostatni fragment, przetwórz dane
            if (index + len == total) {
                #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER STATUS: Processing fan control command");
                    Serial.println(jsonBuffer);
                #endif
                
                // Przetwórz dane JSON
                StaticJsonDocument<128> doc;
                DeserializationError error = deserializeJson(doc, jsonBuffer);
                
                if (error) {
                    #ifdef DEBUG_SERVER_ROUTES
                        Serial.printf("DEBUG SERVER ERROR: JSON parsing failed: %s\n", error.c_str());
                    #endif
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                    return;
                }
                
                // Pobierz wartość stanu
                bool state = doc["state"];
                
                // Ustaw stan wentylatora (globalnie)
                if (state) {
                    this->fanOn = true;
                } else {
                    this->fanOn = false;
                }
                
                // Wyślij odpowiedź
                request->send(200, "application/json", "{\"success\":true}");
            }
        }
    );

    // Endpoint do pobierania statusu maszyny
    server->on("/api/machine-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Machine status requested");
        #endif
        
        // Stwórz JSON z aktualnym statusem maszyny
        String response = "{";
        response += "\"status\":\"" + String(this->machineState) + "\",";
        response += "\"wireOn\":" + String(this->wireOn ? "true" : "false") + ",";
        response += "\"fanOn\":" + String(this->fanOn ? "true" : "false") + ",";
        response += "\"position\":{\"x\":" + String(this->positionX) + ",\"y\":" + String(this->positionY) + "}";
        response += "}";
        
        request->send(200, "application/json", response);
    });

    // Endpoint do pobierania aktualnej pozycji
    server->on("/api/position", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Position requested");
        #endif
        
        // Stwórz JSON z aktualną pozycją
        String response = "{";
        response += "\"x\":" + String(this->positionX) + ",";
        response += "\"y\":" + String(this->positionY);
        response += "}";
        
        request->send(200, "application/json", response);
    });

    // Endpoint do pobierania statusu zadania
    server->on("/api/job-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        #ifdef DEBUG_SERVER_ROUTES
            Serial.println("DEBUG SERVER STATUS: Job status requested");
        #endif
        
        // Stwórz JSON z aktualnym statusem zadania
        String response = "{";
        response += "\"file\":\"" + String(this->sdManager->isProjectSelected() ? this->sdManager->getSelectedProject().c_str() : "") + "\",";
        response += "\"progress\":" + String(this->jobProgress);
        response += "}";
        
        request->send(200, "application/json", response);
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
bool WebServerManager::getPauseCommand() {
    return pauseUserCommand;
}

bool WebServerManager::getJogCommand() {
    bool result = commandJog;
    commandJog = false;  // Automatycznie resetuj flagę po odczycie
    return result;
}

bool WebServerManager::getZeroCommand() {
    bool result = commandZero;
    commandZero = false;  // Automatycznie resetuj flagę po odczycie
    return result;
}

bool WebServerManager::getHomingCommand() {
    bool result = commandHoming;
    commandHoming = false;  // Automatycznie resetuj flagę po odczycie
    return result;
}

bool WebServerManager::getEmergencyStopCommand() {
    bool result = commandEmergencyStop;
    commandEmergencyStop = false;  // Automatycznie resetuj flagę po odczycie
    return result;
}

bool WebServerManager::getWireState() {
    return wireOn;
}

void WebServerManager::setWireState(bool state) {
    wireOn = state;
}

bool WebServerManager::getFanState() {
    return fanOn;
}

void WebServerManager::setFanState(bool state) {
    fanOn = state;
}

void WebServerManager::updateMachineState(const String& state) {
    machineState = state;
}

void WebServerManager::updatePosition(float x, float y) {
    positionX = x;
    positionY = y;
}

void WebServerManager::updateJobProgress(float progress) {
    jobProgress = progress;
}
