/**
 * Wspólne funkcje dla wszystkich stron ESP32 CNC
 */

// Ujednolicona obsługa EventSource z możliwością przekazania callbacka
function handleEventSource(onMachineStatus, onPerformanceUpdate) {
  if (window.eventSource) {
    window.eventSource.close();
  }
  window.eventSource = new EventSource("/events");

  // Track last machine state for delta processing
  window.lastMachineState = window.lastMachineState || {};

  if (typeof onMachineStatus === "function") {
    window.eventSource.addEventListener("machine-status", function (e) {
      try {
        const data = JSON.parse(e.data);
        onMachineStatus(data);
        window.lastMachineState = data;
      } catch (error) {
        console.error("Error parsing EventSource data:", error);
      }
    });

    // Handle delta updates for optimized performance
    window.eventSource.addEventListener("machine-status-delta", function (e) {
      try {
        const deltaData = JSON.parse(e.data);
        
        // Apply delta to last known state
        if (window.lastMachineState) {
          const updatedState = applyMachineStateDelta(window.lastMachineState, deltaData);
          onMachineStatus(updatedState);
          window.lastMachineState = updatedState;
        } else {
          // If no previous state, request full update
          requestFullMachineStatus();
        }
      } catch (error) {
        console.error("Error parsing delta EventSource data:", error);
      }
    });
  }

  // Handle performance metrics updates
  if (typeof onPerformanceUpdate === "function") {
    window.eventSource.addEventListener("performance-metrics", function (e) {
      try {
        const performanceData = JSON.parse(e.data);
        onPerformanceUpdate(performanceData);
      } catch (error) {
        console.error("Error parsing performance data:", error);
      }
    });
  }

  window.eventSource.addEventListener("message", function (e) {
    console.log("Generic message received:", e.data);
  });

  window.eventSource.onopen = function () {
    console.log("EventSource connection established");
  };

  window.eventSource.onerror = function () {
    console.error("EventSource error");
    setTimeout(() => {
      handleEventSource(onMachineStatus, onPerformanceUpdate);
    }, 5000);
  };
}

// Apply delta updates to machine state
function applyMachineStateDelta(currentState, delta) {
  const updatedState = { ...currentState };
  
  if (delta.hasPositionUpdate) {
    updatedState.currentX = (updatedState.currentX || 0) + delta.deltaX;
    updatedState.currentY = (updatedState.currentY || 0) + delta.deltaY;
  }
  
  if (delta.hasStateUpdate) {
    updatedState.state = delta.newState;
    updatedState.isPaused = delta.newPauseState;
    updatedState.isHomed = delta.newHomedState;
  }
  
  if (delta.hasIOUpdate) {
    updatedState.estopOn = delta.newEstopState;
    updatedState.limitXOn = delta.newLimitXState;
    updatedState.limitYOn = delta.newLimitYState;
    updatedState.hotWireOn = delta.newHotWireState;
    updatedState.fanOn = delta.newFanState;
    updatedState.hotWirePower = delta.newHotWirePower;
    updatedState.fanPower = delta.newFanPower;
  }
  
  if (delta.hasProgressUpdate) {
    updatedState.currentLine = delta.newCurrentLine;
    updatedState.jobProgress = delta.newProgress;
  }
  
  if (delta.hasErrorUpdate) {
    updatedState.errorID = delta.newErrorID;
  }
  
  return updatedState;
}

// Request full machine status (fallback)
function requestFullMachineStatus() {
  fetch('/api/position')
    .then(response => response.json())
    .then(data => {
      if (window.lastMachineState && typeof window.onMachineStatusCallback === 'function') {
        window.lastMachineState.currentX = data.x;
        window.lastMachineState.currentY = data.y;
        window.onMachineStatusCallback(window.lastMachineState);
      }
    })
    .catch(error => console.error('Error fetching machine status:', error));
}

// Performance monitoring functions
function fetchPerformanceMetrics() {
  return fetch('/api/performance')
    .then(response => response.json())
    .catch(error => {
      console.error('Error fetching performance metrics:', error);
      return null;
    });
}

function resetPerformanceMetrics() {
  return fetch('/api/performance/reset', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Performance metrics reset successfully');
      } else {
        showMessage('Failed to reset performance metrics', 'error');
      }
      return data;
    })
    .catch(error => {
      console.error('Error resetting performance metrics:', error);
      showMessage('Error resetting performance metrics', 'error');
    });
}

// Emergency stop function
function emergencyStop() {
  fetch('/api/emergency-stop', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Emergency stop activated', 'warning');
      } else {
        showMessage('Failed to activate emergency stop', 'error');
      }
    })
    .catch(error => {
      console.error('Error activating emergency stop:', error);
      showMessage('Error activating emergency stop', 'error');
    });
}

// System reset function
function systemReset() {
  if (confirm('Are you sure you want to reset the system? This will stop all operations.')) {
    fetch('/api/reset', { method: 'POST' })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          showMessage('System reset activated', 'info');
        } else {
          showMessage('Failed to reset system', 'error');
        }
      })
      .catch(error => {
        console.error('Error resetting system:', error);
        showMessage('Error resetting system', 'error');
      });
  }
}

// Funkcja do wyświetlania komunikatów
function showMessage(message, type = "success") {
  const messageElement = document.getElementById("message-container");
  if (!messageElement) return;

  messageElement.textContent = message;
  messageElement.style.display = "block";

  // Ustaw odpowiednią klasę dla koloru tła
  messageElement.className = "alert ";
  if (type === "error") {
    messageElement.className += "alert-danger";
  } else if (type === "warning") {
    messageElement.className += "alert-warning";
  } else if (type === "info") {
    messageElement.className += "alert-info";
  } else {
    messageElement.className += "alert-success";
  }

  // Ukryj komunikat po 5 sekundach
  setTimeout(() => {
    messageElement.style.display = "none";
  }, 5000);
}

// Funkcja do aktualizacji statusu karty SD
function updateSDStatus() {
  fetch("/api/sd-status")
    .then((response) => response.json())
    .then((data) => {
      const statusElement = document.getElementById("sd-status");
      if (statusElement) {
        statusElement.classList.remove("status-on", "status-off");
        statusElement.classList.add(
          data.initialized ? "status-on" : "status-off"
        );

        // Aktualizuj tekst statusu jeśli istnieje
        const statusTextElement = document.getElementById("sd-status-text");
        if (statusTextElement) {
          statusTextElement.textContent = data.initialized
            ? "Connected"
            : "Disconnected";
        }
      }
    })
    .catch((error) => console.error("Error fetching SD status:", error));
}

// Funkcja do reinicjalizacji karty SD
function reinitializeSD() {
  // Pokaż stan ładowania
  const button = document.querySelector('button[onclick="reinitializeSD()"]');
  if (!button) return;

  const originalText = button.textContent;
  button.textContent = "Reinitializing...";
  button.disabled = true;

  fetch("/api/reinitialize-sd", { method: "POST" })
    .then((response) => response.json())
    .then((data) => {
      if (data.success) {
        console.log("SD card reinitialized successfully");
        if (typeof refreshFileList === "function") {
          refreshFileList();
        }
        updateSDStatus();
        showMessage("SD card reinitialized successfully");
      } else {
        console.error("Failed to reinitialize SD card");
        showMessage(
          "Failed to reinitialize SD card. Please check the hardware connection.",
          "error"
        );
      }
    })
    .catch((error) => {
      console.error("Error reinitializing SD:", error);
      showMessage("Error reinitializing SD card", "error");
    })
    .finally(() => {
      // Przywróć stan przycisku
      button.textContent = originalText;
      button.disabled = false;
    });
}

// Aktualizacja statusu przy załadowaniu strony
document.addEventListener("DOMContentLoaded", () => {
  updateSDStatus();
});

function debugFetch(url, options = {}) {
  console.log(`Fetching: ${url}`, options);

  return fetch(url, options)
    .then((response) => {
      console.log(`Response from ${url}:`, response.status);
      return response;
    })
    .catch((error) => {
      console.error(`Error fetching ${url}:`, error);
      throw error;
    });
}
