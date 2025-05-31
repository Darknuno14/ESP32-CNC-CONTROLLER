# ESP32-CNC-CONTROLLER

This project is an ESP32-based CNC controller with web server functionality. It allows users to store G-code files on an SD card and control two stepper motors via a web interface. The web server provides an easy-to-use interface for uploading G-code files and managing the CNC machine operations.

## Features

### Core Functionality
- **Dual-axis stepper motor control** with AccelStepper library
- **Web-based interface** for remote operation and monitoring
- **SD card support** for G-code file storage and management
- **Real-time machine status** with EventSource streaming
- **Hot wire and fan control** with PWM power management
- **Safety systems** including E-STOP and limit switches

### Performance Optimizations
- **Timeout-protected SD operations** preventing system blocking
- **Time-sliced G-code processing** for better system responsiveness
- **Adaptive EventSource broadcasting** with delta updates for efficiency
- **Priority command queue** for immediate safety responses
- **Stepper timeout protection** with watchdog monitoring
- **Comprehensive performance monitoring** with memory and stack tracking

### Web Interface
- **Multi-page interface**: Home, Projects, Manual Control, Configuration
- **Real-time performance metrics** display
- **File management** with upload, preview, and visualization
- **Manual jogging controls** with keyboard shortcuts
- **Configuration management** with live validation

### Safety Features
- **Emergency stop** with highest priority handling
- **Limit switch monitoring** with configurable NO/NC types
- **Memory overflow protection** with automatic alerts
- **Stack monitoring** for task safety
- **Timeout-based operation protection**

## Performance Monitoring

The system includes comprehensive performance monitoring:

### Memory Management
- Real-time heap usage tracking
- Memory leak detection
- Low memory alerts (< 20KB free heap)
- Visual memory usage indicators

### Task Performance
- CNC and Control task execution time monitoring
- Maximum execution time tracking
- Task cycle counting
- Performance bottleneck identification

### Queue Performance
- State and command queue drop monitoring
- Queue wait time measurement
- Adaptive queue sizing based on load

### EventSource Optimization
- Delta-based state updates (reduces bandwidth by 70-80%)
- Adaptive broadcast intervals based on machine state
- Broadcast time monitoring
- Connection efficiency tracking

### SD Card Performance
- Operation timeout tracking
- SD access time monitoring
- Failed operation counting
- Automatic retry mechanisms

## Technical Specifications

### Hardware Requirements
- ESP32 development board (NodeMCU-32S recommended)
- MicroSD card module
- 2x Stepper motors with drivers
- Hot wire and fan control outputs
- Safety switches (E-STOP, limit switches)

### Memory Usage
- **RAM**: ~13.7% (44,916 bytes) of 320KB
- **Flash**: ~73.1% (958,045 bytes) of 1.31MB
- Optimized for continuous operation without memory leaks

### Performance Characteristics
- **EventSource update rates**: 50ms (JOG) to 500ms (IDLE)
- **G-code processing**: Time-sliced with 10ms max per line
- **SD operations**: 5-second timeout protection
- **Stepper operations**: 1-second watchdog timeout

## Getting Started

### Build Requirements
- PlatformIO IDE
- ESP32 development framework
- Libraries: ESPAsyncWebServer, AccelStepper, ArduinoJson

### Configuration
1. Copy `CREDENTIALS.H.template` to `CREDENTIALS.H` and configure WiFi
2. Adjust pin assignments in `CONFIGURATION.H`
3. Configure machine parameters via web interface
4. Upload firmware and web files

### Usage
1. Access web interface at ESP32 IP address
2. Upload G-code files via Projects page
3. Configure machine parameters in Configuration page
4. Use Manual Control for jogging and testing
5. Monitor performance via Home page metrics

## API Endpoints

### Core Operations
- `POST /api/start` - Start G-code execution
- `POST /api/pause` - Pause/resume execution
- `POST /api/stop` - Stop execution
- `POST /api/emergency-stop` - Emergency halt

### Performance Monitoring
- `GET /api/performance` - Get detailed performance metrics
- `POST /api/performance/reset` - Reset performance counters

### File Management
- `GET /api/sd_list` - List SD card files
- `POST /api/upload` - Upload G-code file
- `GET /api/sd_content` - Get file content

## Advanced Features

### Adaptive Broadcasting
The system automatically adjusts EventSource update rates based on machine state:
- **IDLE**: 500ms intervals (low CPU usage)
- **RUNNING**: 100ms intervals (smooth progress updates)
- **JOG**: 50ms intervals (responsive manual control)

### Delta Updates
State changes are broadcast as delta objects, reducing bandwidth usage:
```javascript
// Instead of full state (200+ bytes), send only changes (20-50 bytes)
{
  "currentX": 15.5,
  "currentLine": 42
}
```

### Priority Command Queue
Safety commands get immediate processing:
- **EMERGENCY**: Highest priority (E-STOP)
- **HIGH_PRIORITY**: Safety operations (STOP, PAUSE)
- **NORMAL**: Regular operations (START, JOG)
- **LOW_PRIORITY**: Background tasks (CONFIG)

## Troubleshooting

### Performance Issues
- Monitor memory usage via performance display
- Check for queue drops indicating overload
- Verify SD card speed and connections
- Review EventSource delta ratio efficiency

### Safety Concerns
- Ensure E-STOP is properly wired and tested
- Verify limit switch operation and type configuration
- Test emergency stop functionality regularly
- Monitor stack usage for potential overflows

## License

This project is open source. Please refer to the license file for details.