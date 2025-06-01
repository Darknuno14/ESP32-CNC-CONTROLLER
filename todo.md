# ESP32 CNC Hot-Wire Controller - Implementation Plan

## Overview
This document outlines the missing functionality and implementation plan for the ESP32 CNC Hot-Wire Controller project. The analysis revealed that while the project has a solid foundation with working stepper control, web interface, and basic G-code processing, several critical components need to be completed.

## Current Status
✅ **Completed:**
- Basic project structure and manager classes
- Web interface (HTML/CSS/JavaScript) with jog controls
- Configuration system with ConfigManager
- Stepper motor driver setup and pin configurations
- Basic G-code parser framework (supports G0, G1, M30)
- Safety features (E-STOP, limit switches)
- WebServer with EventSource for real-time updates
- Queue-based communication between tasks

❌ **Missing/Incomplete:**
- Critical M-code implementations
- Complete G-code positioning modes
- Actual stepper movement execution
- Hardware output control integration
- Comprehensive testing framework

## Priority 1: Critical M-Code Implementation

### 1.1 Hot-Wire Control (M3/M5)
**Files to modify:** `src/main.cpp`
**Location:** Around line 1124 in `processGCode()` function

**Implementation:**
```cpp
case 3: // M3 - Hot-wire ON
  machineState.hotWireOn = true;
  // Set PWM for hot-wire control
  ledcWrite(0, map(machineState.hotWirePower, 0, 100, 0, 255));
  break;

case 5: // M5 - Hot-wire OFF
  machineState.hotWireOn = false;
  ledcWrite(0, 0);
  break;
```

**Dependencies:**
- Verify PWM channel setup in `setup()` function
- Ensure `machineState.hotWireOn` is included in EventSource updates
- Connect to web interface toggle functionality

### 1.2 Fan Control (M106/M107)
**Files to modify:** `src/main.cpp`

**Implementation:**
```cpp
case 106: // M106 - Fan ON
  if (gcode.hasS()) {
    machineState.fanSpeed = constrain(gcode.getS(), 0, 255);
  } else {
    machineState.fanSpeed = 255; // Full speed if no S parameter
  }
  machineState.fanOn = (machineState.fanSpeed > 0);
  ledcWrite(1, machineState.fanSpeed);
  break;

case 107: // M107 - Fan OFF
  machineState.fanOn = false;
  machineState.fanSpeed = 0;
  ledcWrite(1, 0);
  break;
```

**Dependencies:**
- Configure second PWM channel for fan control
- Update `MachineState` structure in `SharedTypes.h`

### 1.3 Program Pause Control (M0/M1)
**Files to modify:** `src/main.cpp`

**Implementation:**
```cpp
case 0:  // M0 - Unconditional pause
case 1:  // M1 - Optional pause
  machineState.isPaused = true;
  machineState.state = MACHINE_STATE_STOPPED;
  Serial.println("Program paused - press continue to resume");
  break;
```

**Dependencies:**
- Add resume functionality via web interface
- Implement pause/resume API endpoints in WebServerManager

## Priority 2: G-Code Parser Enhancement

### 2.1 Positioning Modes (G90/G91)
**Files to modify:** `src/main.cpp`
**Location:** Line 1098 in `processGCode()` function

**Current issue:** Missing G90/G91 implementation
**Implementation:**
```cpp
case 90: // G90 - Absolute positioning
  machineState.absoluteMode = true;
  break;

case 91: // G91 - Relative positioning
  machineState.absoluteMode = false;
  break;
```

**Dependencies:**
- Add `absoluteMode` boolean to `MachineState` structure
- Modify coordinate calculation in `processLinearMove()`

### 2.2 Homing Command (G28)
**Files to modify:** `src/main.cpp`

**Current status:** Partially implemented, needs completion
**Required enhancements:**
- Complete homing sequence implementation
- Add proper limit switch handling
- Integrate with existing safety systems

## Priority 3: Movement Execution Fix

### 3.1 Stepper Movement Implementation
**Files to modify:** `src/main.cpp`
**Location:** Line ~1300 in `processLinearMove()` function

**Critical issue:** Stepper movement calls are commented out
**Current code:**
```cpp
// FIXME: Uncomment when stepper control is ready
// stepperX.moveTo(targetX);
// stepperY.moveTo(targetY);
```

**Required fix:**
```cpp
// Calculate target positions based on positioning mode
float targetX, targetY;
if (machineState.absoluteMode) {
  targetX = (gcode.hasX()) ? gcode.getX() : machineState.currentX;
  targetY = (gcode.hasY()) ? gcode.getY() : machineState.currentY;
} else {
  targetX = machineState.currentX + ((gcode.hasX()) ? gcode.getX() : 0);
  targetY = machineState.currentY + ((gcode.hasY()) ? gcode.getY() : 0);
}

// Execute movement
stepperX.moveTo(targetX * machineState.stepsPerMmX);
stepperY.moveTo(targetY * machineState.stepsPerMmY);

// Wait for movement completion
while (stepperX.isRunning() || stepperY.isRunning()) {
  stepperX.run();
  stepperY.run();
}

// Update position
machineState.currentX = targetX;
machineState.currentY = targetY;
```

## Priority 4: API Endpoint Implementation

### 4.1 Missing API Endpoints
**Files to modify:** `src/WebServerManager.cpp`

**Required endpoints:**
- `/api/wire` - Hot-wire control (referenced in jog.js line 304)
- `/api/fan` - Fan control (referenced in jog.js line 344)
- `/api/pause` - Pause/resume functionality
- `/api/position` - Position reporting (referenced in jog.js line 434)

### 4.2 EventSource Data Updates
**Files to modify:** `src/WebServerManager.cpp`

**Required data fields:**
```cpp
// Add to machine status event
data += "\"hotWireOn\":" + String(machineState.hotWireOn ? "true" : "false") + ",";
data += "\"fanOn\":" + String(machineState.fanOn ? "true" : "false") + ",";
data += "\"isPaused\":" + String(machineState.isPaused ? "true" : "false") + ",";
```

## Priority 5: Hardware Integration

### 5.1 PWM Channel Configuration
**Files to modify:** `src/main.cpp`
**Location:** `setup()` function

**Required setup:**
```cpp
// Hot-wire PWM (Channel 0)
ledcSetup(0, 1000, 8); // 1kHz, 8-bit resolution
ledcAttachPin(HOT_WIRE_PIN, 0);

// Fan PWM (Channel 1)
ledcSetup(1, 25000, 8); // 25kHz, 8-bit resolution
ledcAttachPin(FAN_PIN, 1);
```

### 5.2 Pin Definitions
**Files to modify:** `include/CONFIGURATION.H`

**Verify pin assignments:**
- `HOT_WIRE_PIN` - Hot-wire control output
- `FAN_PIN` - Fan control output
- Ensure no conflicts with stepper pins

## Priority 6: Data Structure Updates

### 6.1 MachineState Enhancement
**Files to modify:** `src/SharedTypes.h`

**Add missing fields:**
```cpp
struct MachineState {
  // ...existing fields...
  bool hotWireOn;
  uint8_t hotWirePower;
  bool fanOn;
  uint8_t fanSpeed;
  bool isPaused;
  bool absoluteMode;
  // ...existing fields...
};
```

## Priority 7: Testing Framework

### 7.1 G-Code Test Files
**Create:** `test/gcode_samples/`

**Test files needed:**
- `basic_movements.gcode` - G0/G1 commands
- `positioning_modes.gcode` - G90/G91 testing
- `hot_wire_control.gcode` - M3/M5 testing
- `fan_control.gcode` - M106/M107 testing
- `pause_resume.gcode` - M0/M1 testing

### 7.2 Unit Tests
**Create:** `test/unit_tests/`

**Test modules:**
- G-code parser validation
- Movement calculation verification
- Safety system testing
- API endpoint testing

## Priority 8: Documentation Updates

### 8.1 README Enhancement
**Files to modify:** `README.md`

**Add sections:**
- Supported G-codes and M-codes
- API endpoint documentation
- Hardware connection diagram
- Troubleshooting guide

### 8.2 Configuration Documentation
**Create:** `docs/configuration.md`

**Document:**
- Pin assignments and hardware requirements
- Configuration parameters
- Calibration procedures

## Implementation Timeline

### Phase 1 (Week 1): Core Functionality
1. Implement critical M-codes (M3/M5, M106/M107, M0/M1)
2. Fix stepper movement execution
3. Add missing API endpoints

### Phase 2 (Week 2): G-Code Enhancement
1. Complete G90/G91 positioning modes
2. Finish G28 homing implementation
3. Update data structures

### Phase 3 (Week 3): Integration & Testing
1. Hardware PWM setup
2. EventSource data integration
3. Create test framework

### Phase 4 (Week 4): Polish & Documentation
1. Comprehensive testing
2. Documentation updates
3. Performance optimization

## Risk Assessment

**High Risk:**
- Stepper movement timing and coordination
- Real-time performance during complex G-code execution

**Medium Risk:**
- PWM frequency conflicts with stepper timing
- Memory usage with complex G-code files

**Low Risk:**
- Web interface integration
- Configuration system updates

## Success Criteria

✅ **Functional:**
- All M-codes working with hardware outputs
- Smooth stepper movement execution
- Web interface fully operational
- Safety systems integrated

✅ **Quality:**
- No memory leaks or crashes
- Responsive web interface
- Proper error handling
- Comprehensive test coverage

## Notes

- Reference implementations available in `test/oldmain.cpp` and `src/mainbackup.txt`
- Web interface (`jog.js`) already expects full functionality
- Configuration system is complete and working
- Safety features (E-STOP, limits) are implemented

## Next Steps

1. Start with Priority 1 (M-code implementation)
2. Test each component individually before integration
3. Use existing reference code as implementation guide
4. Maintain backward compatibility with current configuration

---
*Last updated: June 1, 2025*
*Project: ESP32 CNC Hot-Wire Controller*
