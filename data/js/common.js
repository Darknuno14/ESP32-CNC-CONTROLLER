/**
 * Wspólne funkcje dla wszystkich stron ESP32 CNC
 */

function initEventSource() {
  if (window.eventSource) {
    console.log("Closing existing EventSource connection");
    window.eventSource.close();
  }

  console.log("Initializing EventSource connection...");
  window.eventSource = new EventSource("/events");

  window.eventSource.addEventListener("message", function (e) {
    console.log("Generic message received:", e.data);
  });

  window.eventSource.addEventListener("machine-status", function (e) {
    console.log("Machine status update received:", e.data);
    try {
      const data = JSON.parse(e.data);
      // Handle based on which page we're on
      if (typeof updateUIWithMachineState === "function") {
        updateUIWithMachineState(data);
      } else if (typeof updateMachineStatus === "function") {
        // Some pages might have different update functions
        updateMachineStatus(data);
      }
    } catch (error) {
      console.error("Error parsing EventSource data:", error);
    }
  });

  window.eventSource.onopen = function () {
    console.log("EventSource connection established");
  };

  window.eventSource.onerror = function (e) {
    console.error("EventSource error:", e);

    // If connection closed, try to reconnect after delay
    setTimeout(function () {
      console.log("Attempting to reconnect EventSource...");
      initEventSource();
    }, 5000);
  };
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

  // Ustaw okresową aktualizację statusu co 5 sekund
  setInterval(updateSDStatus, 5000);
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
