#pragma once
#include <string>


// ============================================================================
// Enable debugging:
#define DEBUG_CONTROL_TASK
#define DEBUG_CNC_TASK
#define DEBUG_SERVER_ROUTES
#define DEBUG_SD
#define DEBUG_WIFI
#define DEBUG_CONFIG_MANAGER


namespace CONFIG{
// ============================================================================
// Pin Definitions:
// MicroSD Card Module
// 3.3V - 3.3V
// GND - GND
constexpr uint8_t SD_CS_PIN {5};       // Chip Select
constexpr uint8_t SD_MOSI_PIN {23};    // Master Out Slave In
constexpr uint8_t SD_CLK_PIN {18};     // Clock
constexpr uint8_t SD_MISO_PIN {19};    // Master In Slave Out

constexpr uint8_t WIRE_RELAY_PIN {12};       // Relay
constexpr uint8_t FAN_RELAY_PIN {13};           // Emergency Stop

constexpr uint8_t STEP_X_PIN {32};       // Step
constexpr uint8_t DIR_X_PIN {33};        // Direction
constexpr uint8_t STEP_Y_PIN {25};       // Step
constexpr uint8_t DIR_Y_PIN {26};        // Direction


// ============================================================================
// Task configuration
constexpr uint32_t CONTROLTASK_STACK_SIZE {8192}; // The size of the task stack specified as the number of bytes.
constexpr uint32_t CNCTASK_STACK_SIZE {8192};
constexpr uint8_t CONTROLTASK_PRIORITY {1}; // The priority at which the task should run
constexpr uint8_t CNCTASK_PRIORITY {1};
constexpr BaseType_t CORE_0 {0};
constexpr BaseType_t CORE_1 {1}; 

// ============================================================================
constexpr int JSON_DOC_SIZE {1024};
constexpr long MAX_CONNECTION_TIME {10000}; // 10 seconds 
// ============================================================================
constexpr const char* PROJECTS_DIR {"/Projects/"};
constexpr const char* CONFIG_DIR {"/Config/"};
constexpr const char* CONFIG_FILE {"config.json"};
}
