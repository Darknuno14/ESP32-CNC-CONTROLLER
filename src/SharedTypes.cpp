// filepath: c:\Users\darkn\Documents\PlatformIO\Projects\ESP32-CNC-CONTROLLER\src\SharedTypes.cpp
#include "SharedTypes.h"
#include <Arduino.h>
#include <ESP.h>
#include <algorithm>

// Implementation of PerformanceMetrics methods
void PerformanceMetrics::updateMemoryMetrics() {
    freeHeap = ESP.getFreeHeap();
    totalHeapSize = ESP.getHeapSize();
    maxHeapUsed = std::max(maxHeapUsed, totalHeapSize - freeHeap);
    minFreeHeap = std::min(minFreeHeap, freeHeap);
    
    // Trigger memory alert if free heap drops below 20KB
    if (freeHeap < 20480 && !memoryAlertTriggered) {
        memoryAlertTriggered = true;
        lastMemoryAlert = millis();
    } else if (freeHeap > 30720) {  // Reset alert when above 30KB
        memoryAlertTriggered = false;
    }
}

void PerformanceMetrics::updateStackMetrics() {
    UBaseType_t stackFree = uxTaskGetStackHighWaterMark(nullptr);
    minStackFree = std::min(minStackFree, stackFree);
    
    // Trigger stack alert if free stack drops below 512 bytes
    if (stackFree < 512 && !stackAlertTriggered) {
        stackAlertTriggered = true;
        stackOverflowDetected = true;
        lastStackAlert = millis();
    } else if (stackFree > 1024) {  // Reset alert when above 1KB
        stackAlertTriggered = false;
    }
}
