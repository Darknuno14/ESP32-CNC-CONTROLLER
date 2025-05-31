/**
 * JavaScript for main page (index.html)
 */

// --- Ścieżka ruchu maszyny ---
let pathPoints = [];
let lastX = null, lastY = null;
let eventSource;

// --- Helper Functions ---

function handleEventSource() {
  if (eventSource) eventSource.close();
  eventSource = new EventSource('/events');
  eventSource.addEventListener('machine-status', function(e) {
    try {
      const data = JSON.parse(e.data);
      updateUIWithMachineState(data);
      updatePathCanvas(data.currentX, data.currentY);
    } catch (error) {
      console.error("Error parsing EventSource data:", error);
    }
  });
  eventSource.onopen = () => console.log("EventSource connection established");
  eventSource.onerror = () => {
    console.error("EventSource error");
    setTimeout(handleEventSource, 5000);
  };
}

// --- Main Functions ---

// Aktualizacja interfejsu na podstawie danych o stanie maszyny
function updateUIWithMachineState(data) {
  // Nazwa pliku
  const selectedFileElement = document.getElementById("selected-file");
  if (selectedFileElement)
    selectedFileElement.textContent = data.currentProject && data.currentProject.length > 0
      ? data.currentProject
      : "No file selected";

  // Aktualna linia
  const currentLineElement = document.getElementById("current-line");
  if (currentLineElement)
    currentLineElement.textContent = data.currentLine ?? 0;

  // Liczba linii
  const totalLinesElement = document.getElementById("total-lines");
  if (totalLinesElement)
    totalLinesElement.textContent = data.totalLines ?? 0;

  // Czas pracy
  const jobTimeElement = document.getElementById("job-time");
  if (jobTimeElement && data.jobRunTime !== undefined) {
    const seconds = Math.floor(data.jobRunTime / 1000);
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = seconds % 60;
    jobTimeElement.textContent = `${minutes}:${remainingSeconds.toString().padStart(2, "0")}`;
  }

  // Pasek postępu
  const progressBarElement = document.getElementById("job-progress");
  if (progressBarElement) {
    const progress = data.jobProgress || 0;
    progressBarElement.style.width = `${progress}%`;
    progressBarElement.textContent = `${progress.toFixed(1)}%`;
    progressBarElement.setAttribute("aria-valuenow", progress);
  }

  // Aktualizacja stanu przycisków
  updateButtonStates(data.state, data.isPaused);
}

// Aktualizacja stanu przycisków na podstawie stanu maszyny
function updateButtonStates(machineState, isPaused) {
  const startBtn = document.getElementById("startBtn");
  const pauseBtn = document.getElementById("pauseBtn");
  const stopBtn = document.getElementById("stopBtn");

  // Stan: IDLE
  if (machineState === 0) {
    if (startBtn) startBtn.disabled = false;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
  }
  // Stan: RUNNING
  else if (machineState === 1) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) {
      pauseBtn.disabled = false;
      pauseBtn.textContent = isPaused ? "RESUME" : "PAUSE";
    }
    if (stopBtn) stopBtn.disabled = false;
  }
  // Stan: JOG, HOMING
  else if (machineState === 2 || machineState === 3) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = false;
  }
  // Stan: STOPPED, ERROR
  else if (machineState === 4 || machineState === 5) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
  }
}

// Rysowanie ścieżki na canvasie
function updatePathCanvas(x, y) {
  if (x == null || y == null) return;
  if (lastX !== x || lastY !== y) {
    pathPoints.push({ x, y });
    lastX = x;
    lastY = y;
    drawPath();
  }
}

function drawPath() {
  const canvas = document.getElementById("machine-path-canvas");
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Rysuj siatkę
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

  // Rysuj osie od lewego dolnego rogu
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

  // Rysuj ścieżkę (transformacja Y)
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

// Funkcje sterowania maszyną
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

// Prosta funkcja do pokazywania komunikatów
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

// --- Initialization ---

document.addEventListener("DOMContentLoaded", () => {
  handleEventSource();

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