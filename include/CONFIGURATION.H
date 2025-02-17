#pragma once

// ============================================================================
// Pin Definitions:

// MicroSD Card Module
// 3.3V - 3.3V
// GND - GND
constexpr int SD_CS_PIN = 5;       // Chip Select
constexpr int SD_MOSI_PIN = 23;    // Master Out Slave In
constexpr int SD_CLK_PIN = 18;     // Clock
constexpr int SD_MISO_PIN = 19;    // Master In Slave Out

// ============================================================================
// Enable debugging:
#define DEBUG_CNC
#define DEBUG_SERVER_ROUTES
#define DEBUG_SD
#define DEBUG_WIFI
