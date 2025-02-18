#pragma once
#include <string>


// ============================================================================
// Enable debugging:
#define DEBUG_CONTROL_TASK
#define DEBUG_CNC_TASK
#define DEBUG_SERVER_ROUTES
#define DEBUG_SD
#define DEBUG_WIFI


namespace CONFIG{
// ============================================================================
// Pin Definitions:
// MicroSD Card Module
// 3.3V - 3.3V
// GND - GND
constexpr uint8_t SD_CS_PIN = 5;       // Chip Select
constexpr uint8_t SD_MOSI_PIN = 23;    // Master Out Slave In
constexpr uint8_t SD_CLK_PIN = 18;     // Clock
constexpr uint8_t SD_MISO_PIN = 19;    // Master In Slave Out


// ============================================================================
// Task configuration
constexpr uint32_t CONTROLTASK_STACK_SIZE = 8192; // The size of the task stack specified as the number of bytes.
constexpr uint32_t CNCTASK_STACK_SIZE = 8192;
constexpr uint8_t CONTROLTASK_PRIORITY = 1; // The priority at which the task should run
constexpr uint8_t CNCTASK_PRIORITY = 1;
constexpr BaseType_t CORE_0 = 0;
constexpr BaseType_t CORE_1 = 1; 



// ============================================================================
    constexpr const char* PROJECTS_DIR{"/Projects/"};
}