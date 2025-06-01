/*
===============================================================================
                    ESP32 CNC CONTROLLER - STRONA GŁÓWNA
===============================================================================
Plik obsługuje główną stronę kontrolera CNC (index.html):
- Wyświetlanie statusu maszyny i postępu zadań
- Wizualizacja ścieżki ruchu na canvasie
- Sterowanie podstawowymi operacjami (start/pause/stop)
- Aktualizacja interfejsu w czasie rzeczywistym przez EventSource

Autor: ESP32 CNC Controller Project
===============================================================================
*/

// ===============================================================================
// ZMIENNE GLOBALNE - Śledzenie ścieżki ruchu maszyny
// ===============================================================================
let pathPoints = [];     // Tablica punktów ścieżki ruchu maszyny
let lastX = null, lastY = null;  // Ostatnie współrzędne do optymalizacji rysowania

// ===============================================================================
// AKTUALIZACJA INTERFEJSU - Funkcje zarządzania widokiem statusu
// ===============================================================================

/**
 * Aktualizacja statusu maszyny w interfejsie użytkownika
 * @param {number} machineState - Stan maszyny (0=IDLE, 1=RUNNING, 2=JOG, 3=HOMING, 4=STOPPED, 5=ERROR)
 */
function updateMachineStatusUI(machineState) {
  const statusElement = document.getElementById("machine-status");
  const statusTextElement = document.getElementById("machine-status-text");
  
  if (statusElement && statusTextElement) {
    // Czyszczenie poprzednich klas statusu
    statusElement.classList.remove("status-on", "status-off", "status-warning");
    
    // Przypisanie odpowiedniego statusu wizualnego na podstawie stanu maszyny
    switch (machineState) {
      case 0: // IDLE
        statusElement.classList.add("status-off");
        statusTextElement.textContent = "Bezczynny";
        break;
      case 1: // RUNNING
        statusElement.classList.add("status-on");
        statusTextElement.textContent = "W pracy";
        break;
      case 2: // JOG
        statusElement.classList.add("status-warning");
        statusTextElement.textContent = "Sterowanie ręczne";
        break;
      case 3: // HOMING
        statusElement.classList.add("status-warning");
        statusTextElement.textContent = "Powrót do pozycji domowej";
        break;
      case 4: // STOPPED
        statusElement.classList.add("status-off");
        statusTextElement.textContent = "Zatrzymany";
        break;
      case 5: // ERROR
        statusElement.classList.add("status-off");
        statusTextElement.textContent = "Błąd";
        break;
      default:
        statusElement.classList.add("status-off");
        statusTextElement.textContent = "Nieznany";
    }
  }
}

// ===============================================================================
// ZARZĄDZANIE DANYMI MASZYNY - Główne funkcje aktualizacji interfejsu
// ===============================================================================

/**
 * Kompleksowa aktualizacja interfejsu na podstawie danych o stanie maszyny
 * @param {Object} data - Dane statusu maszyny z serwera
 */
function updateUIWithMachineState(data) {
  // Aktualizuj status maszyny
  updateMachineStatusUI(data.state);

  // Wyświetlenie aktywnego projektu
  const selectedFileElement = document.getElementById("selected-file");
  if (selectedFileElement)
    selectedFileElement.textContent = data.currentProject && data.currentProject.length > 0
      ? data.currentProject
      : "Nie wybrano pliku";

  // Aktualnie wykonywana linia G-code
  const currentLineElement = document.getElementById("current-line");
  if (currentLineElement)
    currentLineElement.textContent = data.currentLine ?? 0;

  // Łączna liczba linii w aktywnym pliku
  const totalLinesElement = document.getElementById("total-lines");
  if (totalLinesElement)
    totalLinesElement.textContent = data.totalLines ?? 0;

  // Formatowanie i wyświetlanie czasu wykonywania zadania
  const jobTimeElement = document.getElementById("job-time");
  if (jobTimeElement && data.jobRunTime !== undefined) {
    const seconds = Math.floor(data.jobRunTime / 1000);
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = seconds % 60;
    jobTimeElement.textContent = `${minutes}:${remainingSeconds.toString().padStart(2, "0")}`;
  }

  // Wizualizacja postępu wykonania zadania
  const progressBarElement = document.getElementById("job-progress");
  if (progressBarElement) {
    const progress = data.jobProgress || 0;
    progressBarElement.style.width = `${progress}%`;
    progressBarElement.textContent = `${progress.toFixed(1)}%`;
    progressBarElement.setAttribute("aria-valuenow", progress);
  }

  // Aktualizacja dostępności przycisków sterowania
  updateButtonStates(data.state, data.isPaused);
}

/**
 * Zarządzanie dostępnością przycisków sterowania w zależności od stanu maszyny
 * @param {number} machineState - Aktualny stan maszyny
 * @param {boolean} isPaused - Czy maszyna jest w stanie pauzy
 */
function updateButtonStates(machineState, isPaused) {
  const startBtn = document.getElementById("startBtn");
  const pauseBtn = document.getElementById("pauseBtn");
  const stopBtn = document.getElementById("stopBtn");

  // Maszyna bezczynna - dostępne tylko uruchomienie
  if (machineState === 0) {
    if (startBtn) startBtn.disabled = false;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
  }
  // Maszyna w pracy - dostępne pauza i stop
  else if (machineState === 1) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) {
      pauseBtn.disabled = false;
      pauseBtn.textContent = isPaused ? "RESUME" : "PAUSE";
    }
    if (stopBtn) stopBtn.disabled = false;
  }
  // Sterowanie ręczne lub powrót do pozycji domowej
  else if (machineState === 2 || machineState === 3) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = false;
  }
  // Maszyna zatrzymana lub błąd - tylko stop dostępny
  else if (machineState === 4 || machineState === 5) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = false;
  }
}

// ===============================================================================
// WIZUALIZACJA ŚCIEŻKI - Rysowanie ruchu maszyny na canvasie
// ===============================================================================

/**
 * Aktualizacja ścieżki ruchu maszyny z optymalizacją powtarzających się punktów
 * @param {number} x - Współrzędna X
 * @param {number} y - Współrzędna Y
 */
function updatePathCanvas(x, y) {
  if (x == null || y == null) return;
  if (lastX !== x || lastY !== y) {
    pathPoints.push({ x, y });
    lastX = x;
    lastY = y;
    drawPath();
  }
}

/**
 * Renderowanie ścieżki ruchu na canvasie z siatką współrzędnych
 */
function drawPath() {
  const canvas = document.getElementById("machine-path-canvas");
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Generowanie siatki pomocniczej co 50 pikseli
  ctx.strokeStyle = "#eee";
  ctx.lineWidth = 1;
  for (let i = 0; i <= canvas.width; i += 50) {
    ctx.beginPath();
    ctx.moveTo(i, 0);
    ctx.lineTo(i, canvas.height);
    ctx.stroke();
  }
  for (let j = 0; j <= canvas.height; j += 50) {
    ctx.beginPath();
    ctx.moveTo(0, canvas.height - j);
    ctx.lineTo(canvas.width, canvas.height - j);
    ctx.stroke();
  }

  // Rysowanie głównych osi układu współrzędnych (lewy dolny róg = 0,0)
  ctx.strokeStyle = "#bbb";
  ctx.lineWidth = 2;
  // Oś X (pozioma, na dole)
  ctx.beginPath();
  ctx.moveTo(0, canvas.height - 1);
  ctx.lineTo(canvas.width, canvas.height - 1);
  ctx.stroke();
  // Oś Y (pionowa, po lewej)
  ctx.beginPath();
  ctx.moveTo(1, 0);
  ctx.lineTo(1, canvas.height);
  ctx.stroke();
  ctx.lineWidth = 1;

  // Renderowanie ścieżki ruchu maszyny z odwróceniem osi Y
  if (pathPoints.length > 1) {
    ctx.strokeStyle = "#007bff";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(pathPoints[0].x, canvas.height - pathPoints[0].y);
    for (let i = 1; i < pathPoints.length; i++) {
      ctx.lineTo(pathPoints[i].x, canvas.height - pathPoints[i].y);
    }
    ctx.stroke();
    ctx.lineWidth = 1;
  }
}

// ===============================================================================
// STEROWANIE MASZYNĄ - Funkcje komunikacji z API kontrolera
// ===============================================================================

/**
 * Rozpoczęcie wykonywania wybranego projektu
 */
function startProcessing() {
  fetch("/api/start", { method: "POST" })
    .then((response) => response.json())
    .then((data) => {
      showMessage(data.success ? "Processing started" : "Failed to start processing: " + data.message, data.success ? "success" : "error");
    })
    .catch((error) => {
      console.error("Start error:", error);
      showMessage("Error starting processing", "error");
    });
}

/**
 * Wstrzymanie lub wznowienie wykonywania projektu
 */
function pauseProcessing() {
  fetch("/api/pause", { method: "POST" })
    .then((response) => response.json())
    .then((data) => {
      showMessage(data.success ? "Processing paused/resumed" : "Failed to pause/resume processing: " + data.message, data.success ? "success" : "error");
    })
    .catch((error) => {
      console.error("Pause error:", error);
      showMessage("Error pausing/resuming processing", "error");
    });
}

/**
 * Zatrzymanie wykonywania projektu
 */
function stopProcessing() {
  fetch("/api/stop", { method: "POST" })
    .then((response) => response.json())
    .then((data) => {
      showMessage(data.success ? "Processing stopped" : "Failed to stop processing: " + data.message, data.success ? "success" : "error");
    })
    .catch((error) => {
      console.error("Stop error:", error);
      showMessage("Error stopping processing", "error");
    });
}

// ===============================================================================
// FUNKCJE POMOCNICZE - Komunikaty i notyfikacje
// ===============================================================================

/**
 * Wyświetlenie komunikatu użytkownikowi z automatycznym ukryciem
 * @param {string} msg - Treść komunikatu
 * @param {string} type - Typ komunikatu ('success' lub 'error')
 */
function showMessage(msg, type = "success") {
  const msgContainer = document.getElementById("message-container");
  if (!msgContainer) return;
  msgContainer.textContent = msg;
  msgContainer.className = "alert alert-" + (type === "error" ? "danger" : "success");
  msgContainer.style.display = "block";
  setTimeout(() => {
    msgContainer.style.display = "none";
  }, 3000);
}

// ===============================================================================
// INICJALIZACJA - Konfiguracja strony i obsługa zdarzeń
// ===============================================================================

document.addEventListener("DOMContentLoaded", () => {
  // Inicjalizacja połączenia EventSource z funkcją callback dla aktualizacji UI
  handleEventSource((data) => {
    updateUIWithMachineState(data);
    updatePathCanvas(data.currentX, data.currentY);
  });

  document.getElementById("resetPathBtn")?.addEventListener("click", () => {
    pathPoints = [];
    lastX = null;
    lastY = null;
    drawPath();
  });

  document.getElementById("startBtn")?.addEventListener("click", startProcessing);
  document.getElementById("pauseBtn")?.addEventListener("click", pauseProcessing);
  document.getElementById("stopBtn")?.addEventListener("click", stopProcessing);
});