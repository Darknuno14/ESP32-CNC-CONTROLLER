/**
 * JavaScript dla strony sterowania ręcznego (jog.html)
 * ESP32 CNC Controller
 */

// Parametry globalne dla JOG
let currentPosition = { x: 0, y: 0 };
let machineState = {
  state: 0, // 0=IDLE, 1=RUNNING, 2=JOG, 3=HOMING, 4=STOPPED, 5=ERROR
  wireOn: false,
  fanOn: false,
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
  // Aktualizacja pozycji
  currentPosition.x = data.currentX || 0;
  currentPosition.y = data.currentY || 0;

  document.getElementById("position-x").textContent =
    currentPosition.x.toFixed(3);
  document.getElementById("position-y").textContent =
    currentPosition.y.toFixed(3);

  // Aktualizacja stanu maszyny
  machineState.state = data.state;
  machineState.wireOn = data.hotWireOn || false;
  machineState.fanOn = data.fanOn || false;

  // Aktualizacja przełączników
  document.getElementById("wireSwitch").checked = machineState.wireOn;
  document.getElementById("fanSwitch").checked = machineState.fanOn;

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
  const jogButtons = document.querySelectorAll(".jog-button");
  const zeroBtn = document.getElementById("zeroBtn");
  const homeBtn = document.getElementById("homeBtn");
  const wireSwitch = document.getElementById("wireSwitch");
  const fanSwitch = document.getElementById("fanSwitch");

  // Jeśli maszyna jest w stanie innym niż IDLE lub JOG, wyłącz przyciski JOG
  const enableJog = machineState === 0 || machineState === 2;

  jogButtons.forEach((button) => {
    button.disabled = !enableJog;
  });

  // Przyciski zero i home aktywne tylko w stanie IDLE
  zeroBtn.disabled = machineState !== 0;
  homeBtn.disabled = machineState !== 0;

  // Przełączniki urządzeń są zawsze aktywne
  wireSwitch.disabled = false;
  fanSwitch.disabled = false;
}

/**
 * Wykonanie ruchu JOG
 */
function jog(xDir, yDir) {
  // Pobierz wybraną odległość
  const distanceRadio = document.querySelector(
    'input[name="jogDistance"]:checked'
  );
  if (!distanceRadio) return;
  const distance = parseFloat(distanceRadio.value);

  // Pobierz wybraną prędkość
  const speedSelect = document.getElementById("jogSpeed");
  const speed = parseInt(speedSelect.value);

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
      speed: speed,
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

        showMessage(`Ruch: ${moveText} z prędkością ${speed} mm/min`);

        // Przewidywana nowa pozycja - zostanie zaktualizowana przez EventSource
        currentPosition.x += xOffset;
        currentPosition.y += yOffset;
        document.getElementById("position-x").textContent =
          currentPosition.x.toFixed(3);
        document.getElementById("position-y").textContent =
          currentPosition.y.toFixed(3);
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

        // Aktualizuj pozycję lokalnie
        currentPosition.x = 0;
        currentPosition.y = 0;
        document.getElementById("position-x").textContent = "0.000";
        document.getElementById("position-y").textContent = "0.000";
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
      if (event.shiftKey) {
        jog(0, 10); // Dalszy ruch gdy Shift jest wciśnięty
      } else {
        jog(0, 1);
      }
      break;
    case "ArrowDown":
      event.preventDefault();
      if (event.shiftKey) {
        jog(0, -10);
      } else {
        jog(0, -1);
      }
      break;
    case "ArrowLeft":
      event.preventDefault();
      if (event.shiftKey) {
        jog(-10, 0);
      } else {
        jog(-1, 0);
      }
      break;
    case "ArrowRight":
      event.preventDefault();
      if (event.shiftKey) {
        jog(10, 0);
      } else {
        jog(1, 0);
      }
      break;
    case "Home":
      event.preventDefault();
      homeAxes();
      break;
    case "End":
      event.preventDefault();
      zeroAxes();
      break;
    case "PageUp":
      event.preventDefault();
      selectNextDistance(1); // Zwiększ odległość
      break;
    case "PageDown":
      event.preventDefault();
      selectNextDistance(-1); // Zmniejsz odległość
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
 * Funkcja do przełączania odległości ruchu
 */
function selectNextDistance(direction) {
  const distanceRadios = document.querySelectorAll('input[name="jogDistance"]');
  const currentRadio = document.querySelector(
    'input[name="jogDistance"]:checked'
  );

  if (!currentRadio) return;

  let currentIndex = Array.from(distanceRadios).indexOf(currentRadio);
  let nextIndex = currentIndex + direction;

  // Zapętl indeks, jeśli wychodzi poza zakres
  if (nextIndex < 0) nextIndex = distanceRadios.length - 1;
  if (nextIndex >= distanceRadios.length) nextIndex = 0;

  // Zaznacz nowy radio button
  distanceRadios[nextIndex].checked = true;

  // Wyświetl komunikat
  showMessage(
    `Zmieniono odległość ruchu na ${distanceRadios[nextIndex].value} mm`
  );
}

/**
 * Aktualizacja pozycji w interfejsie
 */
function updatePositionDisplay() {
  fetch("/api/position")
    .then((response) => {
      if (!response.ok) {
        throw new Error("Błąd pobierania pozycji");
      }
      return response.json();
    })
    .then((data) => {
      if (data.x !== undefined && data.y !== undefined) {
        currentPosition.x = parseFloat(data.x);
        currentPosition.y = parseFloat(data.y);

        document.getElementById("position-x").textContent =
          currentPosition.x.toFixed(3);
        document.getElementById("position-y").textContent =
          currentPosition.y.toFixed(3);
      }
    })
    .catch((error) => {
      console.error("Błąd pobierania pozycji:", error);
    });
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

  // Aktualizacja pozycji na starcie
  updatePositionDisplay();

  // Okresowa aktualizacja pozycji (na wszelki wypadek, gdyby EventSource nie działał)
  setInterval(updatePositionDisplay, 2000);
});
