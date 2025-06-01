/**
 * ========================================================================
 * WSPÓLNE FUNKCJE JAVASCRIPT DLA INTERFEJSU WEB ESP32-CNC-CONTROLLER
 * ========================================================================
 * Zawiera uniwersalne funkcje wykorzystywane na wszystkich stronach:
 * - Komunikacja EventSource z serwerem
 * - System komunikatów użytkownika
 * - Zarządzanie statusem karty SD
 * - Funkcje pomocnicze debugowania
 */

// ================= OBSŁUGA KOMUNIKACJI Z SERWEREM =================

/**
 * Zarządzanie połączeniem EventSource z możliwością callbacka dla statusu maszyny
 * @param {Function} onMachineStatus - Funkcja wykonywana przy odbiorze statusu
 */
function handleEventSource(onMachineStatus) {
  if (window.eventSource) {
    window.eventSource.close();
  }
  window.eventSource = new EventSource("/events");

  if (typeof onMachineStatus === "function") {
    window.eventSource.addEventListener("machine-status", function (e) {
      try {
        const data = JSON.parse(e.data);
        onMachineStatus(data);
      } catch (error) {
        console.error("Error parsing EventSource data:", error);
      }
    });
  }

  // Opcjonalna obsługa ogólnych wiadomości
  window.eventSource.addEventListener("message", function (e) {
    console.log("Generic message received:", e.data);
  });

  // Zarządzanie stanem połączenia
  window.eventSource.onopen = function () {
    console.log("EventSource connection established");
  };

  // Automatyczne ponowne połączenie w przypadku błędu
  window.eventSource.onerror = function () {
    console.error("EventSource error");
    setTimeout(() => {
      handleEventSource(onMachineStatus);
    }, 5000);
  };
}

// ================= SYSTEM KOMUNIKATÓW UŻYTKOWNIKA =================

/**
 * Wyświetlanie komunikatów w dedykowanym kontenerze
 * @param {string} message - Treść komunikatu
 * @param {string} type - Typ: "success", "error", "warning", "info"
 */
function showMessage(message, type = "success") {
  const messageElement = document.getElementById("message-container");
  if (!messageElement) return;

  messageElement.textContent = message;
  messageElement.style.display = "block";

  // Mapowanie typów na klasy Bootstrap
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

  // Automatyczne ukrycie po 5 sekundach
  setTimeout(() => {
    messageElement.style.display = "none";
  }, 5000);
}

// ================= ZARZĄDZANIE KARTĄ SD =================

/**
 * Aktualizacja wizualnego statusu karty SD w interfejsie
 */
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

        // Aktualizacja tekstu statusu w interfejsie
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

/**
 * Reinicjalizacja karty SD z obsługą stanu UI
 */
function reinitializeSD() {
  const button = document.querySelector('button[onclick="reinitializeSD()"]');
  if (!button) return;

  // Blokada UI podczas operacji
  const originalText = button.textContent;
  button.textContent = "Reinitializing...";
  button.disabled = true;

  fetch("/api/reinitialize-sd", { method: "POST" })
    .then((response) => response.json())
    .then((data) => {
      if (data.success) {
        console.log("SD card reinitialized successfully");
        // Odświeżenie listy plików jeśli funkcja dostępna
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
      // Przywrócenie stanu przycisku
      button.textContent = originalText;
      button.disabled = false;
    });
}

// ================= FUNKCJE POMOCNICZE =================

/**
 * Funkcja debugowania żądań HTTP z logowaniem
 * @param {string} url - Adres URL
 * @param {Object} options - Opcje fetch
 * @returns {Promise} Promise z odpowiedzią
 */
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

// ================= INICJALIZACJA =================

// Automatyczna aktualizacja statusu karty SD przy załadowaniu strony
document.addEventListener("DOMContentLoaded", () => {
  updateSDStatus();
});
