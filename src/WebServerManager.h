#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

class WebServerManager {
public:
    static AsyncWebServer server;
    static void init();
    static void begin();
    static AsyncWebServer* getServer();
    static AsyncEventSource* getEvents();

private:
    static AsyncEventSource* events;
    static void setupRoutes();
};
