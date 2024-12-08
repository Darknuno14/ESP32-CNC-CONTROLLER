#include "WiFiManager.h"

/**
 * @brief Initializes the WiFi connection with the given SSID and password.
 *
 * This method sets the WiFi mode to station (WIFI_STA) and begins the connection
 * process using the provided SSID and password. It then waits for the connection
 * to be established.
 *
 * @param ssid The SSID of the WiFi network to connect to.
 * @param password The password of the WiFi network to connect to.
 */
void WiFiManager::init(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    waitForConnection();
}

/**
 * @brief Waits for the device to connect to a WiFi network.
 *
 * This method continuously checks the WiFi connection status and prints
 * a status message to the serial monitor. It prints a dot ('.') every 500 milliseconds
 * until the device is successfully connected to a WiFi network. Once connected,
 * it prints a success message along with the device's IP address.
 */
void WiFiManager::waitForConnection() {
    Serial.println("STATUS: Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.println('.');
        delay(500);
    }
    Serial.println("STATUS: Successfully connected to WiFi. IP Address: " + WiFi.localIP().toString());
} 
