/**
 * JavaScript dla strony sterowania ręcznego (jog.html)
 * ESP32 CNC Controller
 */

// Parametry globalne dla JOG
let machineState = {
  state: 0, // 0=IDLE, 1=RUNNING, 2=JOG, 3=HOMING, 4=STOPPED, 5=ERROR
  wireOn: false,
  fanOn: false,
  eStopActive: false,
  limitXActive: false,
  limitYActive: false,
};

// EventSource do odbierania aktualizacji stanu maszyny
let eventSource;

/**
 * Inicjalizacja EventSource dla aktualizacji w czasie rzeczywistym
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
    // Próba ponownego połączenia po 5 sekundach
    setTimeout(initEventSource, 5000);
  };
}

/**
 * Aktualizacja stanu maszyny na podstawie danych z EventSource
 */
function updateMachineStatus(data) {
  // Aktualizacja stanu maszyny
  machineState.state = data.state;
  machineState.wireOn = data.hotWireOn || false;
  machineState.fanOn = data.fanOn || false;
  machineState.eStopActive = data.eStopActive || false;
  machineState.limitXActive = data.limitXActive || false;
  machineState.limitYActive = data.limitYActive || false;

  // Aktualizacja przełączników
  document.getElementById("wireSwitch").checked = machineState.wireOn;
  document.getElementById("fanSwitch").checked = machineState.fanOn;

  // Aktualizacja statusów bezpieczeństwa
  updateSafetyStatus();

  // Aktualizacja statusu maszyny
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

  // Aktywacja/dezaktywacja przycisków zgodnie ze stanem maszyny
  updateButtonStates(data.state);
}

/**
 * Aktualizacja stanu przycisków w zależności od stanu maszyny
 */
function updateButtonStates(machineState) {
  const jogButtons = document.querySelectorAll(".jog-button:not(.jog-center)");
  const homeBtn = document.getElementById("homeBtn");
  const zeroBtn = document.getElementById("zeroBtn");
  const wireSwitch = document.getElementById("wireSwitch");
  const fanSwitch = document.getElementById("fanSwitch");
  const jogDistance = document.getElementById("jogDistance");
  const speedModeButtons = document.querySelectorAll('input[name="jogSpeedMode"]');

  // Sprawdź czy maszyna jest dostępna do operacji JOG
  const canJog = machineState === 0 || machineState === 2; // IDLE lub JOG

  // Aktywuj/dezaktywuj przyciski JOG
  jogButtons.forEach((button) => {
    button.disabled = !canJog;
  });

  // Przyciski sterowania
  homeBtn.disabled = machineState !== 0; // Tylko IDLE
  zeroBtn.disabled = machineState !== 0; // Tylko IDLE
  
  // Kontrolki konfiguracji JOG
  jogDistance.disabled = !canJog;
  speedModeButtons.forEach((radio) => {
    radio.disabled = !canJog;
  });

  // Przełączniki urządzeń - zawsze dostępne (chyba że błąd)
  wireSwitch.disabled = machineState === 5; // Tylko ERROR
  fanSwitch.disabled = machineState === 5; // Tylko ERROR
}

/**
 * Aktualizacja statusów bezpieczeństwa
 */
function updateSafetyStatus() {
  // E-STOP
  const eStopIndicator = document.getElementById("estop-status");
  const eStopText = document.getElementById("estop-status-text");

  if (machineState.eStopActive) {
    eStopIndicator.className = "status-indicator error";
    eStopText.textContent = "AKTYWNY";
  } else {
    eStopIndicator.className = "status-indicator success";
    eStopText.textContent = "Nieaktywny";
  }

  // Krańcówka X
  const limitXIndicator = document.getElementById("limitx-status");
  const limitXText = document.getElementById("limitx-status-text");

  if (machineState.limitXActive) {
    limitXIndicator.className = "status-indicator warning";
    limitXText.textContent = "Zadziałana";
  } else {
    limitXIndicator.className = "status-indicator success";
    limitXText.textContent = "Niezadziałana";
  }

  // Krańcówka Y
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

/**
 * Wykonanie ruchu JOG
 */
function jog(xDir, yDir) {
  // Pobierz odległość z pola input
  const distanceInput = document.getElementById("jogDistance");
  const distance = parseFloat(distanceInput.value);
  
  if (isNaN(distance) || distance <= 0) {
    showMessage("Wprowadź poprawną odległość ruchu", "error");
    return;
  }

  // Pobierz wybrany tryb prędkości
  const speedModeRadio = document.querySelector('input[name="jogSpeedMode"]:checked');
  if (!speedModeRadio) {
    showMessage("Wybierz tryb prędkości", "error");
    return;
  }
  
  const speedMode = speedModeRadio.value; // "work" lub "rapid"

  // Oblicz przesunięcie
  const xOffset = xDir * distance;
  const yOffset = yDir * distance;

  // Wywołaj endpunkt API
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
        // Jeśli ruch się powiódł, wyświetl komunikat
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
 * Funkcja zerowania pozycji
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
 * Funkcja bazowania maszyny
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

/**
 * Sterowanie drutem grzejnym
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
        wireSwitch.checked = !wireState; // Przywróć poprzedni stan przełącznika
        throw new Error("Błąd sterowania drutem");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage(`Drut grzejny ${wireState ? "włączony" : "wyłączony"}`);
        machineState.wireOn = wireState;
      } else {
        wireSwitch.checked = !wireState; // Przywróć poprzedni stan przełącznika
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
      wireSwitch.checked = !wireState; // Przywróć poprzedni stan przełącznika
    });
}

/**
 * Sterowanie wentylatorem
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
        fanSwitch.checked = !fanState; // Przywróć poprzedni stan przełącznika
        throw new Error("Błąd sterowania wentylatorem");
      }
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage(`Wentylator ${fanState ? "włączony" : "wyłączony"}`);
        machineState.fanOn = fanState;
      } else {
        fanSwitch.checked = !fanState; // Przywróć poprzedni stan przełącznika
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
      fanSwitch.checked = !fanState; // Przywróć poprzedni stan przełącznika
    });
}

/**
 * Obsługa sterowania klawiaturą
 */
function handleKeyboardControl(event) {
  // Ignoruj sterowanie klawiaturą, gdy focus jest na polu formularza
  if (
    event.target.tagName === "INPUT" ||
    event.target.tagName === "SELECT" ||
    event.target.tagName === "TEXTAREA"
  ) {
    return;
  }

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

/**
 * Inicjalizacja po załadowaniu strony
 */
document.addEventListener("DOMContentLoaded", function () {
  // Inicjalizacja EventSource
  initEventSource();

  // Ustawienie obsługi przycisków JOG
  document.querySelectorAll(".jog-button").forEach((button) => {
    if (button.classList.contains("jog-center")) return;

    button.addEventListener("click", function () {
      const xDir = parseInt(this.getAttribute("data-x") || "0");
      const yDir = parseInt(this.getAttribute("data-y") || "0");
      jog(xDir, yDir);
    });
  });

  // Obsługa przycisków zerowania i bazowania
  document.getElementById("zeroBtn").addEventListener("click", zeroAxes);
  document.getElementById("homeBtn").addEventListener("click", homeAxes);

  // Obsługa przełączników
  document.getElementById("wireSwitch").addEventListener("change", toggleWire);
  document.getElementById("fanSwitch").addEventListener("change", toggleFan);

  // Obsługa klawiatury
  document.addEventListener("keydown", handleKeyboardControl);
});
