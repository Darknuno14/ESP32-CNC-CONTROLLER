#include "WebServerManager.h"
#include "FSManager.h"
#include <LittleFS.h>

AsyncWebServer WebServerManager::server(80);
// Initialize static members to null.
// These will be set when WebServerManager is initialized.
AsyncEventSource* WebServerManager::events = nullptr;

/**
 * @brief Initializes the web server and event source if they are not already initialized.
 * 
 * This function checks if the server is already initialized. If not, it creates a new instance
 * of AsyncWebServer on port 80 and a new instance of AsyncEventSource for handling events.
 * 
 * @return true if the server and event source were successfully initialized, false if they were already initialized.
 */
void WebServerManager::init() {
    if (!events) {
        events = new AsyncEventSource("/events");
        Serial.println("STATUS: WebServer initialized");
        return;
    }
    Serial.println("STATUS: WebServer already initialized");
    return;
}

/**
 * @brief Initializes the WebServerManager by setting up the routes and starting the server.
 * 
 * This function sets up the necessary routes for the web server and then begins the server to start listening for incoming connections.
 */
void WebServerManager::begin() {
    setupRoutes();
    server.addHandler(events);

    server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setFilter([](AsyncWebServerRequest *request) {
             // Check if file exists before allowing access
            if (!LittleFS.exists(request->url())) {
                request->send(404);
                return false;
            }
          return true;
      });

    server.begin();
    Serial.println("STATUS: WebServer started");
}

/**
 * @brief Sets up the routes for the web server.
 * 
 * This method configures the routes for handling HTTP requests on the web server.
 * It includes routes for serving static files, handling API endpoints for file operations,
 * and a default handler for not found requests.
 * 
 * Routes:
 * - Adds an event source handler to the server.
 * - Serves static files from the LittleFS filesystem, with "index.html" as the default file.
 * - API endpoint `/api/files` (HTTP GET): Returns a JSON list of files.
 * - API endpoint `/api/file` (HTTP GET): Returns the content of a specified file.
 *   - Query parameter `name` (required): The name of the file to retrieve.
 *   - Responds with 400 if the file name is missing.
 *   - Responds with 200 and the file content if the file is found.
 *   - Responds with 404 if the file is not found.
 * - Default handler for not found requests, responds with 404.
 */
void WebServerManager::setupRoutes() {
    // Handle root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    // Handle not found
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    // Add button press endpoint
    server.on("/api/button", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("STATUS: WebServer Button has been pressed");
        request->send(200, "text/plain", "OK");
    });

}
