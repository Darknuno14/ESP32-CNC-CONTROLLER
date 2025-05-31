/**
 * JavaScript for main page (index.html)
 */

// --- Ścieżka ruchu maszyny ---
let pathPoints = [];
let lastX = null, lastY = null;
let performanceMetrics = null;

// --- Helper Functions ---

// Machine status callback for EventSource
function onMachineStatusUpdate(data) {
  updateUIWithMachineState(data);
  updatePathCanvas(data.currentX, data.currentY);
}

// Performance metrics callback for EventSource
function onPerformanceUpdate(data) {
  performanceMetrics = data;
  updatePerformanceDisplay(data);
}

// Update performance display if performance section exists
function updatePerformanceDisplay(metrics) {
  const perfSection = document.getElementById("performance-section");
  if (!perfSection) return;

  // Update memory usage
  const memoryElement = document.getElementById("memory-usage");
  const memoryProgressElement = document.getElementById("memory-progress");
  if (memoryElement && metrics.memory) {
    const totalHeap = metrics.memory.totalHeapSize || 0;
    const freeHeap = metrics.memory.freeHeap || 0;
    const memoryUsagePercent = totalHeap > 0 ? ((totalHeap - freeHeap) / totalHeap * 100).toFixed(1) : "0.0";
    
    memoryElement.textContent = `${memoryUsagePercent}% (${(freeHeap / 1024).toFixed(1)}KB free)`;
    
    if (memoryProgressElement) {
      memoryProgressElement.style.width = `${memoryUsagePercent}%`;
      memoryProgressElement.setAttribute('aria-valuenow', memoryUsagePercent);
      memoryProgressElement.className = 'progress-bar'; // Reset classes
      if (parseFloat(memoryUsagePercent) > 80) {
        memoryProgressElement.classList.add('bg-danger');
      } else if (parseFloat(memoryUsagePercent) > 60) {
        memoryProgressElement.classList.add('bg-warning');
      } else {
        memoryProgressElement.classList.add('bg-success'); // Use bg-success for normal
      }
    }
    
    if (metrics.memory.alertTriggered) {
      memoryElement.classList.add('text-warning');
    } else {
      memoryElement.classList.remove('text-warning');
    }
  }

  // Update task performance
  const taskPerfElement = document.getElementById("task-performance");
  if (taskPerfElement && metrics.task) {
    taskPerfElement.textContent = `Max Times (μs) - CNC: ${metrics.task.maxCncTime || 0}, Ctrl: ${metrics.task.maxControlTime || 0}`;
  }

  // Update queue stats
  const queueStatsElement = document.getElementById("queue-stats");
  if (queueStatsElement && metrics.queue) {
    const totalDrops = (metrics.queue.stateDrops || 0) + (metrics.queue.commandDrops || 0);
    const maxWait = Math.max(metrics.queue.maxStateQueueWait || 0, metrics.queue.maxCommandQueueWait || 0);
    queueStatsElement.textContent = `Total Drops: ${totalDrops} | Max Wait: ${maxWait}ms`;
    
    if (totalDrops > 0) {
      queueStatsElement.classList.add('text-warning');
    } else {
      queueStatsElement.classList.remove('text-warning');
    }
  }

  // Update EventSource stats
  const eventSourceElement = document.getElementById("eventsource-stats");
  if (eventSourceElement && metrics.eventSource) {
    const fullUpdates = metrics.eventSource.fullUpdates || 0;
    const deltaUpdates = metrics.eventSource.deltaUpdates || 0;
    const deltaRatio = fullUpdates > 0 ? (deltaUpdates / fullUpdates).toFixed(1) : 'N/A';
    const maxBroadcastTime = metrics.eventSource.maxBroadcastTime || 0;
    eventSourceElement.textContent = `Delta Ratio: ${deltaRatio}:1 | Max Bcast Time: ${maxBroadcastTime}μs`;
  }
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
  // Initialize tooltips
  const tooltipTriggerList = [].slice.call(document.querySelectorAll('[data-bs-toggle="tooltip"]'));
  tooltipTriggerList.map(function (tooltipTriggerEl) {
    return new bootstrap.Tooltip(tooltipTriggerEl);
  });

  // Set up global callback and initialize EventSource with performance monitoring
  window.onMachineStatusCallback = onMachineStatusUpdate;
  handleEventSource(onMachineStatusUpdate, onPerformanceUpdate);

  document.getElementById("resetPathBtn")?.addEventListener("click", () => {
    pathPoints = [];
    lastX = null;
    lastY = null;
    drawPath();
  });

  document.getElementById("startBtn")?.addEventListener("click", startProcessing);
  document.getElementById("pauseBtn")?.addEventListener("click", pauseProcessing);
  document.getElementById("stopBtn")?.addEventListener("click", stopProcessing);

  // Add emergency stop button if it exists
  document.getElementById("emergencyStopBtn")?.addEventListener("click", emergencyStop);
  
  // Add system reset button if it exists
  document.getElementById("systemResetBtn")?.addEventListener("click", systemReset);

  // Add performance metrics button if it exists
  document.getElementById("showPerformanceBtn")?.addEventListener("click", () => {
    if (performanceMetrics) {
      console.log("Current performance metrics:", performanceMetrics);
    } else {
      fetchPerformanceMetrics().then(metrics => {
        if (metrics) {
          console.log("Fetched performance metrics:", metrics);
        }
      });
    }
  });

  // Add performance reset button if it exists
  document.getElementById("resetPerformanceBtn")?.addEventListener("click", resetPerformanceMetrics);
});