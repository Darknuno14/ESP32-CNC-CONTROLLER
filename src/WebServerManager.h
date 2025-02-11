#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "FSManager.h"
#include "SDManager.h"
#include <LittleFS.h>

#include "CONFIGURATION.h"

/**
 * @brief Enumeration of possible WebServer error states
 * 
 * Used to track and handle various error conditions that may occur
 * during WebServer initialization and operation.
 */
enum class WebServerError {
    OK,
    ALREADY_INITIALIZED,
    SERVER_ALLOCATION_FAILED,
    EVENT_SOURCE_FAILED,
    UNKNOWN_ERROR
};

/**
 * @brief Web Server Manager for ESP32 CNC Controller
 * 
 * Manages the AsyncWebServer instance, handling:
 * - Server initialization and startup
 * - WROK IN PROGRESS
 * 
 */
class WebServerManager {
private:
    AsyncWebServer* server;
    AsyncEventSource* events{nullptr};
    SDCardManager* sdManager;
    bool serverInitialized{false};
    bool eventsInitialized{false};
    bool serverStarted{false};
    /**
     * @brief Sets up all server routes and handlers
     * 
     * Called internally during initialization.
    */
    void setupRoutes();
    bool startUserCommand{false};
    bool stopUserCommand{false};
   
public:
    /**
     * @brief Construct a new Web Server Manager
     * 
     * @param sdManager Pointer to initialized SD card manager
     */
    WebServerManager(SDCardManager* sdManager);
    /**
     * @brief Destroy and clean up allocated resources of the Web Server Manager
     * 
    */
    ~WebServerManager();
    /**
     * @brief Initialize the web server
     * 
     * Sets up the AsyncWebServer and EventSource.
     * 
     * @return WebServerError OK if successful, error code otherwise
    */
    WebServerError init();
    /**
     * @brief Start the web server
     * 
     * Sets up routes and begins listening for connections.
     * 
     * @return WebServerError OK if successful, error code otherwise
    */
    WebServerError begin();
    /**
     * @brief Check if the server has been started
     * 
     * @return true if the server has been started
     * @return false if the server has not been started
     */
    bool isServerStarted();
    /**
     * @brief Check if the server has been initialized
     * 
     * @return true if the server has been initialized
     * @return false if the server has not been initialized
     */
    bool isServerInitialized();
    /**
     * @brief Check if the events have been initialized
     * 
     * @return true if the events have been initialized
     * @return false if the events have not been initialized
     */
    bool isEventsInitialized();

    bool getStartCommand();
    bool getStopCommand();

};
