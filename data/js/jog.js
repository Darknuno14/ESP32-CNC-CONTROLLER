/**
 * ========================================================================
 * KONTROLER STEROWANIA RUCHEM JOG - ESP32-CNC-CONTROLLER
 * ========================================================================
 * Zarządzanie ruchem manualnym maszyny CNC z funkcjami:
 * - Ruch JOG w osiach X/Y z kontrolą prędkości
 * - Bazowanie i zerowanie pozycji
 * - Sterowanie drutem grzejnym i wentylatorem
 * - Obsługa klawiatury i interfejsu dotykowego
 * - Monitoring statusów bezpieczeństwa
 */

// ================= ZMIENNE GLOBALNE =================

// Stan maszyny CNC ze wszystkimi parametrami kontrolnymi
let machineState = {
  state: 0, // 0=IDLE, 1=RUNNING, 2=JOG, 3=HOMING, 4=STOPPED, 5=ERROR
  wireOn: false,
  fanOn: false,
  eStopActive: false,
  limitXActive: false,
  limitYActive: false,
};

// Połączenie EventSource do odbioru aktualizacji stanu
let eventSource;

// ================= KOMUNIKACJA Z SERWEREM =================

/**
 * Nawiązanie połączenia EventSource dla aktualizacji w czasie rzeczywistym
 */
function initEventSource() {
  if (eventSource) {
    eventSource.close();
  }

  eventSource = new EventSource("/events");

  eventSource.addEventListener("machine-status", function (e) {
    try {
      const data = JSON.parse(e.data);
      updateMachineStatus(data);
    } catch (error) {
      console.error("Błąd parsowania danych z EventSource:", error);
    }
  });

  eventSource.onopen = function () {
    console.log("Połączenie EventSource nawiązane");
  };

  eventSource.onerror = function (e) {
    console.error("Błąd EventSource:", e);
    // Automatyczne ponowne połączenie po awarii
    setTimeout(initEventSource, 5000);
  };
}

// ================= AKTUALIZACJA INTERFEJSU =================

/**
 * Przetwarzanie danych statusu maszyny z serwera i aktualizacja UI
 * @param {Object} data - Dane statusu otrzymane z EventSource
 */
function updateMachineStatus(data) {
  // Aktualizacja stanu maszyny
  machineState.state = data.state;
  machineState.wireOn = data.hotWireOn || false;
  machineState.fanOn = data.fanOn || false;
  machineState.eStopActive = data.eStopActive || false;
  machineState.limitXActive = data.limitXActive || false;
  machineState.limitYActive = data.limitYActive || false;

  // Aktualizacja przełączników stanu urządzeń
  document.getElementById("wireSwitch").checked = machineState.wireOn;
  document.getElementById("fanSwitch").checked = machineState.fanOn;

  // Odświeżenie wskaźników bezpieczeństwa
  updateSafetyStatus();

  // Wyświetlenie aktualnego statusu maszyny
  const statusText = document.getElementById("machine-status-text");
  switch (data.state) {
    case 0:
      statusText.textContent = "Bezczynny";
      break;
    case 1:
      statusText.textContent = data.isPaused ? "Wstrzymany" : "Pracuje";
      break;
    case 2:
      statusText.textContent = "Sterowanie JOG";
      break;
    case 3:
      statusText.textContent = "Bazowanie";
      break;
    case 4:
      statusText.textContent = "Zatrzymany";
      break;
    case 5:
      statusText.textContent = "Błąd";
      break;
    default:
      statusText.textContent = "Nieznany";
  }

  // Dostosowanie dostępności przycisków do aktualnego stanu
  updateButtonStates(data.state);
}

/**
 * Zarządzanie dostępnością przycisków kontrolnych według stanu maszyny
 * @param {number} machineState - Kod stanu maszyny (0-5)
 */
function updateButtonStates(machineState) {
  const jogButtons = document.querySelectorAll(".jog-button:not(.jog-center)");
  const homeBtn = document.getElementById("homeBtn");
  const zeroBtn = document.getElementById("zeroBtn");
  const wireSwitch = document.getElementById("wireSwitch");
  const fanSwitch = document.getElementById("fanSwitch");
  const jogDistance = document.getElementById("jogDistance");
  const speedModeButtons = document.querySelectorAll('input[name="jogSpeedMode"]');

  // Sprawdzenie czy dozwolony jest ruch JOG (IDLE lub aktywny JOG)
  const canJog = machineState === 0 || machineState === 2;

  // Dostępność przycisków kierunkowych JOG
  jogButtons.forEach((button) => {
    button.disabled = !canJog;
  });

  // Funkcje specjalne dostępne tylko w stanie spoczynku
  homeBtn.disabled = machineState !== 0;
  zeroBtn.disabled = machineState !== 0;
  
  // Kontrolki parametrów ruchu
  jogDistance.disabled = !canJog;
  speedModeButtons.forEach((radio) => {
    radio.disabled = !canJog;
  });

  // Przełączniki urządzeń - blokowane tylko przy błędzie krytycznym
  wireSwitch.disabled = machineState === 5;
  fanSwitch.disabled = machineState === 5;
}

/**
 * Odświeżenie wskaźników stanu systemów bezpieczeństwa
 */
function updateSafetyStatus() {
  // Stan przycisku bezpieczeństwa E-STOP
  const eStopIndicator = document.getElementById("estop-status");
  const eStopText = document.getElementById("estop-status-text");

  if (machineState.eStopActive) {
    eStopIndicator.className = "status-indicator error";
    eStopText.textContent = "AKTYWNY";
  } else {
    eStopIndicator.className = "status-indicator success";
    eStopText.textContent = "Nieaktywny";
  }

  // Status krańcówki osi X
  const limitXIndicator = document.getElementById("limitx-status");
  const limitXText = document.getElementById("limitx-status-text");

  if (machineState.limitXActive) {
    limitXIndicator.className = "status-indicator warning";
    limitXText.textContent = "Zadziałana";
  } else {
    limitXIndicator.className = "status-indicator success";
    limitXText.textContent = "Niezadziałana";
  }

  // Status krańcówki osi Y
  const limitYIndicator = document.getElementById("limity-status");
  const limitYText = document.getElementById("limity-status-text");

  if (machineState.limitYActive) {
    limitYIndicator.className = "status-indicator warning";
    limitYText.textContent = "Zadziałana";
  } else {
    limitYIndicator.className = "status-indicator success";
    limitYText.textContent = "Niezadziałana";
  }
}

// ================= FUNKCJE STEROWANIA RUCHEM =================

/**
 * Wykonanie ruchu JOG w określonym kierunku z walidacją parametrów
 * @param {number} xDir - Kierunek X (-1, 0, 1)
 * @param {number} yDir - Kierunek Y (-1, 0, 1)
 */
function jog(xDir, yDir) {
  // Walidacja wprowadzonej odległości ruchu
  const distanceInput = document.getElementById("jogDistance");
  const distance = parseFloat(distanceInput.value);
  
  if (isNaN(distance) || distance <= 0) {
    showMessage("Wprowadź poprawną odległość ruchu", "error");
    return;
  }

  // Sprawdzenie wybranego trybu prędkości
  const speedModeRadio = document.querySelector('input[name="jogSpeedMode"]:checked');
  if (!speedModeRadio) {
    showMessage("Wybierz tryb prędkości", "error");
    return;
  }
  
  const speedMode = speedModeRadio.value;

  // Kalkulacja wektorów przesunięcia
  const xOffset = xDir * distance;
  const yOffset = yDir * distance;

  // Wysłanie komendy do kontrolera
  fetch("/api/jog", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      x: xOffset,
      y: yOffset,
      speedMode: speedMode,
    }),
  })
    .then((response) => {
      if (!response.ok) {
        throw new Error("Błąd wykonania komendy JOG");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        // Formatowanie komunikatu o wykonanym ruchu
        let moveText = "";
        if (xOffset !== 0) moveText += `X${xOffset > 0 ? "+" : ""}${xOffset}`;
        if (xOffset !== 0 && yOffset !== 0) moveText += ", ";
        if (yOffset !== 0) moveText += `Y${yOffset > 0 ? "+" : ""}${yOffset}`;

        const speedText = speedMode === "rapid" ? "RAPID" : "WORK";
        showMessage(`Ruch: ${moveText} mm w trybie ${speedText}`);
      } else {
        showMessage(
          "Nie udało się wykonać ruchu: " + (data.message || "Nieznany błąd"),
          "error"
        );
      }
    })
    .catch((error) => {
      console.error("Błąd JOG:", error);
      showMessage("Błąd wykonania ruchu: " + error.message, "error");
    });
}

/**
 * Wyzerowanie aktualnej pozycji roboczej (bez ruchu fizycznego)
 */
function zeroAxes() {
  fetch("/api/zero", {
    method: "POST",
  })
    .then((response) => {
      if (!response.ok) {
        throw new Error("Błąd komendy zerowania");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage("Pozycja wyzerowana");
      } else {
        showMessage(
          "Nie udało się wyzerować pozycji: " +
            (data.message || "Nieznany błąd"),
          "error"
        );
      }
    })
    .catch((error) => {
      console.error("Błąd zerowania:", error);
      showMessage("Błąd wyzerowania pozycji: " + error.message, "error");
    });
}

/**
 * Procedura bazowania - powrót do punktu odniesienia maszynowego
 */
function homeAxes() {
  if (
    !confirm(
      "Czy na pewno chcesz wykonać bazowanie? Maszyna przesunie się do pozycji bazowej."
    )
  ) {
    return;
  }

  fetch("/api/home", {
    method: "POST",
  })
    .then((response) => {
      if (!response.ok) {
        throw new Error("Błąd komendy bazowania");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage("Rozpoczęto procedurę bazowania");
      } else {
        showMessage(
          "Nie udało się uruchomić bazowania: " +
            (data.message || "Nieznany błąd"),
          "error"
        );
      }
    })
    .catch((error) => {
      console.error("Błąd bazowania:", error);
      showMessage("Błąd uruchomienia bazowania: " + error.message, "error");
    });
}

// ================= STEROWANIE URZĄDZENIAMI =================

/**
 * Przełączenie stanu drutu grzejnego z synchronizacją UI
 */
function toggleWire() {
  const wireSwitch = document.getElementById("wireSwitch");
  const wireState = wireSwitch.checked;

  fetch("/api/wire", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      state: wireState,
    }),
  })
    .then((response) => {
      if (!response.ok) {
        // Przywrócenie poprzedniego stanu przy błędzie
        wireSwitch.checked = !wireState;
        throw new Error("Błąd sterowania drutem");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage(`Drut grzejny ${wireState ? "włączony" : "wyłączony"}`);
        machineState.wireOn = wireState;
      } else {
        wireSwitch.checked = !wireState;
        showMessage(
          "Nie udało się przełączyć drutu: " +
            (data.message || "Nieznany błąd"),
          "error"
        );
      }
    })
    .catch((error) => {
      console.error("Błąd sterowania drutem:", error);
      showMessage("Błąd sterowania drutem: " + error.message, "error");
      wireSwitch.checked = !wireState;
    });
}

/**
 * Przełączenie stanu wentylatora z synchronizacją UI
 */
function toggleFan() {
  const fanSwitch = document.getElementById("fanSwitch");
  const fanState = fanSwitch.checked;

  fetch("/api/fan", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      state: fanState,
    }),
  })
    .then((response) => {
      if (!response.ok) {
        // Przywrócenie poprzedniego stanu przy błędzie
        fanSwitch.checked = !fanState;
        throw new Error("Błąd sterowania wentylatorem");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage(`Wentylator ${fanState ? "włączony" : "wyłączony"}`);
        machineState.fanOn = fanState;
      } else {
        fanSwitch.checked = !fanState;
        showMessage(
          "Nie udało się przełączyć wentylatora: " +
            (data.message || "Nieznany błąd"),
          "error"
        );
      }
    })
    .catch((error) => {
      console.error("Błąd sterowania wentylatorem:", error);
      showMessage("Błąd sterowania wentylatorem: " + error.message, "error");
      fanSwitch.checked = !fanState;
    });
}

// ================= OBSŁUGA KLAWIATURY =================

/**
 * Mapowanie klawiszy na funkcje sterowania (gdy focus nie jest na polach formularza)
 * @param {KeyboardEvent} event - Zdarzenie klawiatury
 */
function handleKeyboardControl(event) {
  // Wyłączenie sterowania gdy użytkownik pisze w polach formularza
  if (
    event.target.tagName === "INPUT" ||
    event.target.tagName === "SELECT" ||
    event.target.tagName === "TEXTAREA"
  ) {
    return;
  }

  // Mapowanie klawiszy na akcje sterowania
  switch (event.key) {
    case "ArrowUp":
      event.preventDefault();
      jog(0, 1);
      break;
    case "ArrowDown":
      event.preventDefault();
      jog(0, -1);
      break;
    case "ArrowLeft":
      event.preventDefault();
      jog(-1, 0);
      break;
    case "ArrowRight":
      event.preventDefault();
      jog(1, 0);
      break;
    case "Home":
      event.preventDefault();
      homeAxes();
      break;
    case "End":
      event.preventDefault();
      zeroAxes();
      break;
    case "w":
    case "W":
      event.preventDefault();
      document.getElementById("wireSwitch").checked =
        !document.getElementById("wireSwitch").checked;
      toggleWire();
      break;
    case "f":
    case "F":
      event.preventDefault();
      document.getElementById("fanSwitch").checked =
        !document.getElementById("fanSwitch").checked;
      toggleFan();
      break;
  }
}

// ================= INICJALIZACJA =================

/**
 * Konfiguracja interfejsu po załadowaniu strony
 */
document.addEventListener("DOMContentLoaded", function () {
  // Nawiązanie połączenia z serwerem dla aktualizacji czasu rzeczywistego
  initEventSource();

  // Konfiguracja przycisków kierunkowych JOG
  document.querySelectorAll(".jog-button").forEach((button) => {
    if (button.classList.contains("jog-center")) return;

    button.addEventListener("click", function () {
      const xDir = parseInt(this.getAttribute("data-x") || "0");
      const yDir = parseInt(this.getAttribute("data-y") || "0");
      jog(xDir, yDir);
    });
  });

  // Konfiguracja przycisków funkcji specjalnych
  document.getElementById("zeroBtn").addEventListener("click", zeroAxes);
  document.getElementById("homeBtn").addEventListener("click", homeAxes);

  // Konfiguracja przełączników urządzeń
  document.getElementById("wireSwitch").addEventListener("change", toggleWire);
  document.getElementById("fanSwitch").addEventListener("change", toggleFan);

  // Aktywacja sterowania klawiaturą
  document.addEventListener("keydown", handleKeyboardControl);
});
