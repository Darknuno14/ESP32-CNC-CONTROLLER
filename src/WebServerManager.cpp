#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <LittleFS.h>
#include "WebServerManager.h"
#include "CONFIGURATION.H"

WebServerManager::WebServerManager(SDCardManager* sdManager, ConfigManager* configManager, QueueHandle_t extCommandQueue, QueueHandle_t extStateQueue)
    : sdManager(sdManager), configManager(configManager), commandQueue(extCommandQueue), stateQueue(extStateQueue) {
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

    events->onConnect([](AsyncEventSourceClient* client) {
        if (client->lastId()) {
            Serial.printf("Client reconnected! Last message ID: %u\n", client->lastId());
        }

        // Wyślij początkowe info o połączeniu
        client->send("Connected to ESP32 CNC EventSource", NULL, millis(), 1000);
        });

    return WebServerStatus::OK;
}

WebServerStatus WebServerManager::begin() {

    if (server == nullptr) {
        return WebServerStatus::NOT_INITIALIZED;
    }

    server->addHandler(events);
    setupRoutes();



    server->begin();
    this->serverStarted = true;

    return WebServerStatus::OK;
}

void WebServerManager::setupRoutes() {
    if (server == nullptr) return;

    setupCommonRoutes();
    setupIndexRoutes();
    setupConfigRoutes();
    setupJogRoutes();
    setupProjectsRoutes();

    if (LittleFS.exists("/css/styles.css")) {
        server->serveStatic("/css/", LittleFS, "/css/")
            .setCacheControl("max-age=86400")
            .setTryGzipFirst(true);
    }

    // Serve JavaScript files (z większym bezpieczeństwem)
    if (LittleFS.exists("/js/")) {
        server->serveStatic("/js/", LittleFS, "/js/")
            .setCacheControl("max-age=86400")
            .setTryGzipFirst(true);
    }

    // Serve HTML and other files from root
    server->serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setTryGzipFirst(true);

    // Brak strony
    server->onNotFound([](AsyncWebServerRequest* request) {
        Serial.printf("404 Not Found: %s (Method: %s)\n",
            request->url().c_str(),
            request->methodToString());
        request->send(404, "text/plain", "404: Not found");
        });
}

void WebServerManager::setupCommonRoutes() {

    // Status karty SD
    server->on("/api/sd-status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String json = "{\"initialized\":" + String(this->sdManager->isCardInitialized() ? "true" : "false") + "}";
        request->send(200, "application/json", json);
        });


    // Reinicjalizacja karty SD i ConfigManager
    server->on("/api/reinitialize-sd", HTTP_POST, [this](AsyncWebServerRequest* request) {
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
                }
                else {
                    #ifdef DEBUG_SERVER_ROUTES
                    Serial.println("DEBUG SERVER WARNING: Config manager reinitialization failed");
                    #endif
                    message = "SD card reinitialized, but configuration failed";
                }
            }
            else {
                // Jeśli configManager nie istnieje, stwórz nowy
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER WARNING: Creating new Config manager");
                #endif
                this->configManager = new ConfigManager(this->sdManager);
                ConfigManagerStatus configResult = this->configManager->init();
                configSuccess = (configResult == ConfigManagerStatus::OK);

                if (configSuccess) {
                    message = "SD card reinitialized and new configuration manager created";
                }
                else {
                    message = "SD card reinitialized, but failed to create configuration manager";
                }
            }

            // Zaktualizuj listę projektów
            this->sdManager->updateProjectList();
        }
        else {
            message = "Failed to reinitialize SD card";
        }

        // Odpowiedź JSON ze szczegółowymi informacjami
        JsonDocument doc {};
        doc["success"] = sdSuccess;
        doc["configSuccess"] = configSuccess;
        doc["message"] = message;

        String jsonResponse;
        serializeJson(doc, jsonResponse);
        request->send(200, "application/json", jsonResponse);
        });

}

void WebServerManager::setupIndexRoutes() {
    // Przycisk START
    server->on("/api/start", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Komenda START");
        #endif

        // Zamiast flagi, wysyłamy komendę START przez kolejkę
        this->sendCommand(CommandType::START);

        request->send(200, "application/json", "{\"success\":true}");
        });

    // Przycisk PAUSE
    server->on("/api/pause", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Komenda PAUSE");
        #endif

        this->sendCommand(CommandType::PAUSE);

        request->send(200, "application/json", "{\"success\":true}");
        });

    // Przycisk STOP
    server->on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Komenda STOP");
        #endif

        this->sendCommand(CommandType::STOP);

        request->send(200, "application/json", "{\"success\":true}");
        });

    // Przycisk RESET (powrót do IDLE z stanów STOPPED/ERROR)
    server->on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Komenda RESET");
        #endif

        // RESET używa tej samej komendy co STOP - w stanie STOPPED/ERROR komenda STOP resetuje do IDLE
        this->sendCommand(CommandType::STOP);

        request->send(200, "application/json", "{\"success\":true}");
        });
}

void WebServerManager::setupConfigRoutes() {
    // Pobieranie konfiguracji
    server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Configuration requested");
        #endif

        if (!this->configManager) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Config Manager not initialized\"}");
            return;
        }

        // Force reload from SD if available
        if (this->sdManager->isCardInitialized()) {
            ConfigManagerStatus status = this->configManager->readConfigFromSD();
            if (status != ConfigManagerStatus::OK) {
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER WARNING: Failed to read config from SD, using current config");
                #endif
            }
        }

        String jsonConfig = this->configManager->configToJson();

        #ifdef DEBUG_SERVER_ROUTES        
        Serial.println("DEBUG: Generated JSON config:");
        Serial.println(jsonConfig);
        #endif

        // Add proper headers
        request->send(200, "application/json", jsonConfig);
        });

    // Aktualizacja całej konfiguracji
    server->on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "text/plain", "Processing POST request...");
        },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            processJsonRequest(request, data, len, index, total, 2048, [this, request](const String& jsonStr) {
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Processing complete config");
                Serial.println("DEBUG SERVER: Received JSON: " + jsonStr);
                #endif

                if (!this->configManager) {
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"Config Manager not initialized\"}");
                    return;
                }

                ConfigManagerStatus status = this->configManager->configFromJson(jsonStr);
                String message;

                if (status == ConfigManagerStatus::OK) {
                    status = this->configManager->writeConfigToSD();
                }

                switch (status) {
                    case ConfigManagerStatus::OK:
                        message = "Configuration saved successfully";
                        break;
                    case ConfigManagerStatus::SD_ACCESS_ERROR:
                        message = "SD access error";
                        break;
                    case ConfigManagerStatus::FILE_OPEN_FAILED:
                        message = "Failed to open config file";
                        break;
                    case ConfigManagerStatus::FILE_WRITE_FAILED:
                        message = "Failed to write to config file";
                        break;
                    case ConfigManagerStatus::JSON_PARSE_ERROR:
                        message = "Invalid JSON format";
                        break;
                    default:
                        message = "Unknown error";
                        break;
                }

                String response = "{\"success\":" + String(status == ConfigManagerStatus::OK ? "true" : "false") +
                    ",\"message\":\"" + message + "\"}";
                request->send(status == ConfigManagerStatus::OK ? 200 : 400, "application/json", response);
                });
        });
}

void WebServerManager::setupJogRoutes() {
    // Endpoint do sterowania ruchem JOG
    server->on("/api/jog", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "text/plain", "Processing JOG request...");
        },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            processJsonRequest(request, data, len, index, total, 512, [this, request](const String& jsonStr) {
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Processing JOG command");
                Serial.println("DEBUG SERVER: Received JSON: " + jsonStr);
                #endif

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, jsonStr);

                if (error) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON format\"}");
                    return;
                }

                // Sprawdź, czy wymagane parametry istnieją
                if (!doc["x"].is<float>() || !doc["y"].is<float>() || !doc["speedMode"].is<const char*>()) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
                    return;
                }

                float x = doc["x"].as<float>();
                float y = doc["y"].as<float>();
                String speedMode = doc["speedMode"].as<String>();

                // Konwertuj tryb prędkości na wartość numeryczną
                // 0.0 = WORK mode, 1.0 = RAPID mode
                float speedValue = (speedMode == "rapid") ? 1.0f : 0.0f;

                #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: JOG command: X=%.2f, Y=%.2f, SpeedMode=%s (%.1f)\n",
                    x, y, speedMode.c_str(), speedValue);
                #endif

                // Wyślij komendę JOG przez kolejkę
                this->sendCommand(CommandType::JOG, x, y, speedValue);

                request->send(200, "application/json", "{\"success\":true}");
                });
        }
    );

    // Endpoint do sterowania drutem grzejnym
    server->on("/api/wire", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "text/plain", "Processing wire request...");
        },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            processJsonRequest(request, data, len, index, total, 256, [this, request](const String& jsonStr) {
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Processing wire control command");
                Serial.println("DEBUG SERVER: Received JSON: " + jsonStr);
                #endif

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, jsonStr);

                if (error) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON format\"}");
                    return;
                }

                // Sprawdź, czy parametr state istnieje
                if (!doc["state"].is<bool>()) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing state parameter\"}");
                    return;
                }

                bool state = doc["state"].as<bool>();

                #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: Wire control: %s\n", state ? "ON" : "OFF");
                #endif

                // Wyślij komendę sterowania drutem przez kolejkę
                this->sendCommand(CommandType::SET_HOTWIRE, state ? 1.0f : 0.0f, 0.0f, 0.0f);

                request->send(200, "application/json", "{\"success\":true}");
                });
        }
    );

    // Endpoint do sterowania wentylatorem
    server->on("/api/fan", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "text/plain", "Processing fan request...");
        },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            processJsonRequest(request, data, len, index, total, 256, [this, request](const String& jsonStr) {
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER STATUS: Processing fan control command");
                Serial.println("DEBUG SERVER: Received JSON: " + jsonStr);
                #endif

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, jsonStr);

                if (error) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON format\"}");
                    return;
                }

                // Sprawdź, czy parametr state istnieje
                if (!doc["state"].is<bool>()) {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing state parameter\"}");
                    return;
                }

                bool state = doc["state"].as<bool>();

                #ifdef DEBUG_SERVER_ROUTES
                Serial.printf("DEBUG SERVER STATUS: Fan control: %s\n", state ? "ON" : "OFF");
                #endif

                // Wyślij komendę sterowania wentylatorem przez kolejkę
                this->sendCommand(CommandType::SET_FAN, state ? 1.0f : 0.0f, 0.0f, 0.0f);

                request->send(200, "application/json", "{\"success\":true}");
                });
        }
    );

    // Endpoint do bazowania maszyny
    server->on("/api/home", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Home command requested");
        #endif

        // Wyślij komendę bazowania przez kolejkę
        this->sendCommand(CommandType::HOME);

        request->send(200, "application/json", "{\"success\":true}");
        });

    // Endpoint do zerowania pozycji
    server->on("/api/zero", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: Zero command requested");
        #endif

        // Wyślij komendę zerowania przez kolejkę
        this->sendCommand(CommandType::ZERO);

        request->send(200, "application/json", "{\"success\":true}");
        });
}

void WebServerManager::setupProjectsRoutes() {

    server->on("/api/sd_content", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!request->hasParam("file")) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing file parameter\"}");
            return;
        }


        String filename = request->getParam("file")->value();
        String filePath = String(CONFIG::PROJECTS_DIR) + filename;

        #ifdef DEBUG_SERVER_ROUTES
        Serial.printf("DEBUG SERVER: Requested SD file content: %s\n", filename.c_str());
        #endif

        if (!this->sdManager->takeSD()) {
            request->send(503, "application/json", "{\"error\":\"SD busy\"}");
            return;
        }

        if (!SD.exists(filePath)) {
            this->sdManager->giveSD();
            request->send(404, "application/json", "{\"error\":\"File not found\"}");
            return;
        }

        this->busy = true;
        AsyncWebServerResponse* response = request->beginResponse(SD, filePath, "text/plain");
        response->addHeader("Cache-Control", "no-cache");

        request->onDisconnect([this]() {
            this->busy = false;
            this->sdManager->giveSD();
            });

        request->send(response);
        });

    // Lista plików
    server->on("/api/list-files", HTTP_GET, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: File list requested");
        #endif

        if (!this->sdManager) {
            if (sdManager == nullptr) {
                #ifdef DEBUG_SERVER_ROUTES
                Serial.println("DEBUG SERVER ERROR: SD Manager not initialized");
                #endif
                request->send(500, "application/json", "{\"success\":false,\"message\":\"SD Manager not initialized\"}");
                return;
            }
        }
        else if (sdManager->isCardInitialized() == false) {
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
        for (size_t i { 0 }; i < files.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + String(files[i].c_str()) + "\"";
        }
        json += "]";

        request->send(200, "application/json", "{\"success\":true,\"message\":\"Files retrieved successfully\",\"files\":" + json + "}");
        });

    // Wysyłanie pliku
    server->on("/api/upload-file", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Upload complete\"}");
        },
        [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
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
    server->on("/api/refresh-files", HTTP_POST, [this](AsyncWebServerRequest* request) {
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
    server->on("/api/select-file", HTTP_POST, [this](AsyncWebServerRequest* request) {
        #ifdef DEBUG_SERVER_ROUTES
        Serial.println("DEBUG SERVER STATUS: File selection requested");
        #endif

        if (!request->hasParam("file")) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"File parameter missing\"}");
            return;
        }

        std::string filename { request->getParam("file")->value().c_str() };
        #ifdef DEBUG_SERVER_ROUTES
        Serial.printf("DEBUG SERVER STATUS: Selected file: %s\n", filename.c_str());
        #endif

        SDManagerStatus status = this->sdManager->setSelectedProject(filename);
        bool success = (status == SDManagerStatus::OK);

        String response = "{\"success\":" + String(success ? "true" : "false");
        if (success) {
            response += ",\"file\":\"" + String(filename.c_str()) + "\"";
        }
        else {
            response += ",\"message\":\"Failed to select file\"";
        }
        response += "}";

        request->send(success ? 200 : 400, "application/json", response);
        });

    // Usuwanie plików
    server->on("/api/delete-file", HTTP_POST, [this](AsyncWebServerRequest* request) {
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
            }
            else {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Failed to delete file\"}");
            }
        }
        else {
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

void WebServerManager::broadcastMachineStatus(MachineState currentState) {

    if (!events || !eventsInitialized) return;

    // Sprawdź czy są połączeni klienci
    if (events->count() == 0) {
        return;
    }

    char jsonBuffer[1024];

    static MachineState lastSentState {};
    static bool firstSend = true;

    // Porównaj kluczowe pola stanu
    bool stateChanged = firstSend ||
        lastSentState.state != currentState.state ||
        lastSentState.isPaused != currentState.isPaused ||
        abs(lastSentState.currentX - currentState.currentX) > 0.01f ||
        abs(lastSentState.currentY - currentState.currentY) > 0.01f ||
        lastSentState.hotWireOn != currentState.hotWireOn ||
        lastSentState.fanOn != currentState.fanOn ||
        lastSentState.jobProgress != currentState.jobProgress ||
        lastSentState.currentLine != currentState.currentLine;

    if (!stateChanged) {
        return; // Nie wysyłaj jeśli stan się nie zmienił
    }

    JsonDocument doc;
    doc["state"] = static_cast<int>(currentState.state);
    doc["isPaused"] = currentState.isPaused;
    doc["errorID"] = currentState.errorID;
    doc["currentX"] = currentState.currentX;
    doc["currentY"] = currentState.currentY;
    doc["relativeMode"] = currentState.relativeMode;
    doc["hotWireOn"] = currentState.hotWireOn;
    doc["fanOn"] = currentState.fanOn;
    doc["hotWirePower"] = currentState.hotWirePower;
    doc["fanPower"] = currentState.fanPower;
    doc["currentProject"] = String(currentState.currentProject);
    doc["jobProgress"] = currentState.jobProgress;
    doc["currentLine"] = currentState.currentLine;
    doc["totalLines"] = currentState.totalLines;
    doc["jobStartTime"] = currentState.jobStartTime;
    doc["jobRunTime"] = currentState.jobRunTime;
    doc["estopOn"] = currentState.estopOn;
    doc["limitXOn"] = currentState.limitXOn;
    doc["limitYOn"] = currentState.limitYOn;

    // Serialize directly to fixed buffer
    size_t len = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
    if (len < sizeof(jsonBuffer)) {
        sendEvent("machine-status", jsonBuffer);
    }
    lastSentState = currentState;
    firstSend = false;
}

void WebServerManager::sendEvent(const char* event, const char* data) {
    if (events && eventsInitialized && event && data) {
        events->send(data, event, millis());
    }
}

bool WebServerManager::isBusy() {
    return this->busy;
}

bool WebServerManager::processJsonRequest(AsyncWebServerRequest* request, uint8_t* data, size_t len,
    size_t index, size_t total, size_t bufferSize,
    std::function<void(const String&)> processor) {
    static char* jsonBuffer = nullptr;
    static size_t bufferPos = 0;
    static size_t currentBufferSize = 0;

    if (index == 0) {
        if (jsonBuffer) free(jsonBuffer);
        jsonBuffer = (char*)malloc(bufferSize);
        if (!jsonBuffer) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Memory allocation failed\"}");
            return false;
        }

        bufferPos = 0;
        currentBufferSize = bufferSize;
        memset(jsonBuffer, 0, bufferSize);
    }

    if (bufferPos + len >= currentBufferSize) {
        if (jsonBuffer) free(jsonBuffer);
        jsonBuffer = nullptr;
        request->send(413, "application/json", "{\"success\":false,\"message\":\"Request too large\"}");
        return false;
    }

    memcpy(jsonBuffer + bufferPos, data, len);
    bufferPos += len;

    if (index + len == total) {
        jsonBuffer[bufferPos] = '\0';
        processor(String(jsonBuffer));
        free(jsonBuffer);
        jsonBuffer = nullptr;
        return true;
    }

    return true;
}




