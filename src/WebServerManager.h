#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

class WebServerManager {
public:
    WebServerManager();
    ~WebServerManager();
    bool init();
    bool begin();
private:
    void setupRoutes();
};
