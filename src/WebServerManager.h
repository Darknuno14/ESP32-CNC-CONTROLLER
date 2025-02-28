#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "SharedTypes.h"
#include "SDManager.h"
#include "ConfigManager.h"

enum class WebServerStatus{
    OK,
    ALREADY_INITIALIZED,
    NOT_INITIALIZED,
    SERVER_ALLOCATION_FAILED,
    EVENT_SOURCE_FAILED,
    UNKNOWN_ERROR,
};

class WebServerManager {
private:

    AsyncWebServer* server {nullptr};
    AsyncEventSource* events {nullptr};

    SDCardManager* sdManager {nullptr};
    ConfigManager* configManager  {nullptr};

    QueueHandle_t commandQueue; // Zasada Inversion of Control

    // Track initialization status
    bool serverInitialized {false};

    // Track events initialization status
    bool eventsInitialized {false};

    // Track server startup status
    bool serverStarted {false};

    // Sets up all server routes and handlers
    void setupRoutes();
    void setupIndexRoutes();
    void setupConfigRoutes();
    void setupJogRoutes();
    void setupProjectsRoutes();
   
public:

    // Construct a new Web Server Manager Pointer to initialized SD card manager
    WebServerManager(SDCardManager* sdManager, ConfigManager* configManager, QueueHandle_t extCommandQueue);

    // Destroy and clean up allocated resources of the Web Server Manager
    ~WebServerManager();

    // Sets up the AsyncWebServer and EventSource.
    WebServerStatus init();

    // Sets up routes and begins listening for connections.
    WebServerStatus begin();

    // Check if the server has been started
    // true = server has been started
    bool isServerStarted();
     
    // Check if the server has been initialized
    // true = server has been initialized
    bool isServerInitialized();
    
    // Check if the events have been initialized
    // true = events have been initialized
    bool isEventsInitialized();

    // Send a command to the CNC task
    void sendCommand(CommandType type, float param1 = 0.0f, float param2 = 0.0f, float param3 = 0.0f);

    void sendEvent(const char* event, const char* data);
};
