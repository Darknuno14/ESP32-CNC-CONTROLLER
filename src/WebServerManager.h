#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "SDManager.h"
#include "ConfigManager.h"

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

    ConfigManager* configManager{nullptr};

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
    WebServerManager(SDCardManager* sdManager, ConfigManager* configManager = nullptr);

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


    /////

        // Flagi dla kontroli JOG
        bool commandJog{false};      // Flaga wskazująca, czy został wysłany komenda JOG
        float jogX{0.0f};            // Odległość ruchu JOG dla osi X
        float jogY{0.0f};            // Odległość ruchu JOG dla osi Y
        float jogSpeed{1000.0f};     // Prędkość ruchu JOG
        
        // Flagi dla innych komend
        bool pauseUserCommand{false};      // Flaga wstrzymania przetwarzania
        bool commandZero{false};           // Flaga zerowania pozycji
        bool commandHoming{false};         // Flaga bazowania
        bool commandEmergencyStop{false};  // Flaga zatrzymania awaryjnego
        
        // Stan maszyny do raportowania
        String machineState{"idle"};       // Stan maszyny (idle, running, error)
        bool wireOn{false};                // Stan drutu grzejnego
        bool fanOn{false};                 // Stan wentylatora
        
        // Aktualna pozycja maszyny
        float positionX{0.0f};             // Aktualna pozycja X
        float positionY{0.0f};             // Aktualna pozycja Y
        
        // Status zadania
        float jobProgress{0.0f};           // Postęp zadania (0-100%)
        
        // Gettery dla nowych komend
        bool getPauseCommand();
        bool getJogCommand();
        bool getZeroCommand();
        bool getHomingCommand();
        bool getEmergencyStopCommand();
        
        // Getter i setter dla stanu drutu
        bool getWireState();
        void setWireState(bool state);
        
        // Getter i setter dla stanu wentylatora
        bool getFanState();
        void setFanState(bool state);
        
        // Aktualizacja statusu maszyny
        void updateMachineState(const String& state);
        
        // Aktualizacja pozycji
        void updatePosition(float x, float y);
        
        // Aktualizacja postępu zadania
        void updateJobProgress(float progress);
};
