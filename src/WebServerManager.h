#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "FSManager.h"
#include "SDManager.h"
#include <LittleFS.h>

class WebServerManager {
private:
    AsyncWebServer* server;
    AsyncEventSource* events{nullptr};
    SDCardManager* sdManager;
    void setupRoutes();
   
public:
    WebServerManager(SDCardManager* sdManager);
    ~WebServerManager();
    bool init();
    bool begin();
};
