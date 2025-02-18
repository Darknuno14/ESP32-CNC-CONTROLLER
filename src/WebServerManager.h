#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SDManager.h"

enum class WebServerStatus{
    OK,
    ALREADY_INITIALIZED,
    SERVER_ALLOCATION_FAILED,
    EVENT_SOURCE_FAILED,
    UNKNOWN_ERROR,
};

class WebServerManager {
private:

    AsyncWebServer* server{nullptr};
    AsyncEventSource* events{nullptr};
    SDCardManager* sdManager{nullptr};

    // Track initialization status
    bool serverInitialized{false};

    // Track events initialization status
    bool eventsInitialized{false};

    // Track server startup status
    bool serverStarted{false};

    // Sets up all server routes and handlers
    void setupRoutes();

    // Endpoints
    bool startUserCommand{false}; // Start button status
    bool stopUserCommand{false}; // Stop button status
   
public:

    // Construct a new Web Server Manager Pointer to initialized SD card manager
    WebServerManager(SDCardManager* sdManager);

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

    // Get Start button status
    // true = start button has been pressed
    bool getStartCommand();

    // Get Stop button status
    // true = stop button has been pressed
    bool getStopCommand();
};
