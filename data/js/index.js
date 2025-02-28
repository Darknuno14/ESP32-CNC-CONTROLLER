/**
 * JavaScript for main page (index.html)
 */

// Zmienna do przechowywania EventSource
let eventSource;

// Aktualizacja statusu maszyny
function updateMachineStatus() {
  fetch('/api/machine-status')
    .then(response => response.json())
    .catch(error => {
      console.error('Error fetching machine status:', error);
      return { 
        state: 0, // IDLE
        currentX: 0,
        currentY: 0,
        spindleOn: false,
        fanOn: false,
        jobProgress: 0,
        currentProject: ''
      };
    })
    .then(data => {
      updateUIWithMachineState(data);
    });
}

// Funkcja aktualizująca interfejs na podstawie danych o stanie maszyny
function updateUIWithMachineState(data) {
  // Aktualizuj status maszyny
  const statusElement = document.getElementById('machine-status');
  const statusTextElement = document.getElementById('machine-status-text');
  
  if (statusElement && statusTextElement) {
    statusElement.classList.remove('status-on', 'status-off', 'status-warning');
    
    // Mapowanie CNCState na odpowiednie statusy UI
    switch(data.state) {
      case 0: // IDLE
        statusElement.classList.add('status-off');
        statusTextElement.textContent = 'Idle';
        break;
      case 1: // RUNNING
        statusElement.classList.add('status-on');
        statusTextElement.textContent = data.isPaused ? 'Paused' : 'Running';
        break;
      case 2: // JOG
        statusElement.classList.add('status-on');
        statusTextElement.textContent = 'Jog Mode';
        break;
      case 3: // HOMING
        statusElement.classList.add('status-on');
        statusTextElement.textContent = 'Homing';
        break;
      case 4: // STOPPED
        statusElement.classList.add('status-warning');
        statusTextElement.textContent = 'Stopped';
        break;
      case 5: // ERROR
        statusElement.classList.add('status-warning');
        statusTextElement.textContent = 'Error';
        break;
      default:
        statusElement.classList.add('status-off');
        statusTextElement.textContent = 'Unknown';
    }
    
    // Aktualizuj status drutu
    const wireStatusElement = document.getElementById('wire-status');
    const wireStatusTextElement = document.getElementById('wire-status-text');
    if (wireStatusElement && wireStatusTextElement) {
      wireStatusElement.classList.remove('status-on', 'status-off');
      wireStatusElement.classList.add(data.spindleOn ? 'status-on' : 'status-off');
      wireStatusTextElement.textContent = data.spindleOn ? 'On' : 'Off';
    }
    
    // Aktualizuj status wentylatora
    const fanStatusElement = document.getElementById('fan-status');
    const fanStatusTextElement = document.getElementById('fan-status-text');
    if (fanStatusElement && fanStatusTextElement) {
      fanStatusElement.classList.remove('status-on', 'status-off');
      fanStatusElement.classList.add(data.fanOn ? 'status-on' : 'status-off');
      fanStatusTextElement.textContent = data.fanOn ? 'On' : 'Off';
    }
    
    // Aktualizuj pozycję
    const positionXElement = document.getElementById('position-x');
    const positionYElement = document.getElementById('position-y');
    if (positionXElement && positionYElement) {
      positionXElement.textContent = data.currentX.toFixed(3);
      positionYElement.textContent = data.currentY.toFixed(3);
    }
    
    // Aktualizuj informacje o aktualnej linii kodu
    const currentLineElement = document.getElementById('current-line');
    if (currentLineElement && data.currentLine !== undefined) {
      currentLineElement.textContent = data.currentLine;
    }
    
    // Aktualizuj informacje o czasie pracy
    const jobTimeElement = document.getElementById('job-time');
    if (jobTimeElement && data.jobRunTime !== undefined) {
      const seconds = Math.floor(data.jobRunTime / 1000);
      const minutes = Math.floor(seconds / 60);
      const remainingSeconds = seconds % 60;
      jobTimeElement.textContent = `${minutes}:${remainingSeconds.toString().padStart(2, '0')}`;
    }
    
    // Aktualizuj nazwę pliku
    const selectedFileElement = document.getElementById('selected-file');
    if (selectedFileElement) {
      if (data.currentProject && data.currentProject.length > 0) {
        selectedFileElement.textContent = `File: ${data.currentProject}`;
      } else {
        selectedFileElement.textContent = 'No file selected';
      }
    }
    
    // Aktualizuj pasek postępu
    const progressBarElement = document.getElementById('job-progress');
    if (progressBarElement) {
      const progress = data.jobProgress || 0;
      progressBarElement.style.width = `${progress}%`;
      progressBarElement.textContent = `${progress.toFixed(1)}%`;
      progressBarElement.setAttribute('aria-valuenow', progress);
    }
    
    // Aktualizuj stan przycisków zależnie od stanu maszyny
    updateButtonStates(data.state, data.isPaused);
  }
}

// Aktualizacja stanu przycisków na podstawie stanu maszyny
function updateButtonStates(machineState, isPaused) {
  const startBtn = document.getElementById('startBtn');
  const pauseBtn = document.getElementById('pauseBtn');
  const stopBtn = document.getElementById('stopBtn');
  const resetBtn = document.getElementById('resetBtn');
  const homeBtn = document.getElementById('homeBtn');
  const zeroBtn = document.getElementById('zeroBtn');
  
  // Stan: IDLE
  if (machineState === 0) {
    if (startBtn) startBtn.disabled = false;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
    if (resetBtn) resetBtn.disabled = true;
    if (homeBtn) homeBtn.disabled = false;
    if (zeroBtn) zeroBtn.disabled = false;
  }
  // Stan: RUNNING
  else if (machineState === 1) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) {
      pauseBtn.disabled = false;
      pauseBtn.textContent = isPaused ? 'RESUME' : 'PAUSE';
    }
    if (stopBtn) stopBtn.disabled = false;
    if (resetBtn) resetBtn.disabled = true;
    if (homeBtn) homeBtn.disabled = true;
    if (zeroBtn) zeroBtn.disabled = true;
  }
  // Stan: JOG, HOMING
  else if (machineState === 2 || machineState === 3) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = false;
    if (resetBtn) resetBtn.disabled = true;
    if (homeBtn) homeBtn.disabled = true;
    if (zeroBtn) zeroBtn.disabled = true;
  }
  // Stan: STOPPED, ERROR
  else if (machineState === 4 || machineState === 5) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
    if (resetBtn) resetBtn.disabled = false;
    if (homeBtn) homeBtn.disabled = true;
    if (zeroBtn) zeroBtn.disabled = true;
  }
}

// Inicjalizacja EventSource dla aktualizacji w czasie rzeczywistym
function initEventSource() {
  if (eventSource) {
    eventSource.close();
  }
  
  eventSource = new EventSource('/events');
  
  eventSource.addEventListener('machine-status', function(e) {
    try {
      const data = JSON.parse(e.data);
      updateUIWithMachineState(data);
    } catch (error) {
      console.error('Error parsing EventSource data:', error);
    }
  });
  
  eventSource.onopen = function() {
    console.log('EventSource connection established');
  };
  
  eventSource.onerror = function(e) {
    console.error('EventSource error:', e);
    // Spróbuj ponownie połączyć po 5 sekundach
    setTimeout(initEventSource, 5000);
  };
}

// Funkcje sterowania maszyną
function startProcessing() {
  fetch('/api/start', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Processing started');
      } else {
        showMessage('Failed to start processing: ' + data.message, 'error');
      }
    })
    .catch(error => {
      console.error('Start error:', error);
      showMessage('Error starting processing', 'error');
    });
}

function pauseProcessing() {
  fetch('/api/pause', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        const isPaused = document.getElementById('pauseBtn').textContent.includes('RESUME');
        showMessage(isPaused ? 'Processing resumed' : 'Processing paused');
      } else {
        showMessage('Failed to pause/resume processing: ' + data.message, 'error');
      }
    })
    .catch(error => {
      console.error('Pause error:', error);
      showMessage('Error pausing/resuming processing', 'error');
    });
}

function stopProcessing() {
  fetch('/api/stop', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Processing stopped');
      } else {
        showMessage('Failed to stop processing: ' + data.message, 'error');
      }
    })
    .catch(error => {
      console.error('Stop error:', error);
      showMessage('Error stopping processing', 'error');
    });
}

function resetMachine() {
  fetch('/api/reset', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Machine reset');
      } else {
        showMessage('Failed to reset machine: ' + data.message, 'error');
      }
    })
    .catch(error => {
      console.error('Reset error:', error);
      showMessage('Error resetting machine', 'error');
    });
}

function homeMachine() {
  if (confirm('Are you sure you want to home the machine? The machine will move to home position.')) {
    fetch('/api/home', { method: 'POST' })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          showMessage('Homing procedure started');
        } else {
          showMessage('Failed to home machine: ' + data.message, 'error');
        }
      })
      .catch(error => {
        console.error('Homing error:', error);
        showMessage('Error homing machine', 'error');
      });
  }
}

function zeroAxes() {
  fetch('/api/zero', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Position zeroed');
      } else {
        showMessage('Failed to zero position: ' + data.message, 'error');
      }
    })
    .catch(error => {
      console.error('Zero error:', error);
      showMessage('Error zeroing position', 'error');
    });
}

// Inicjalizacja po załadowaniu strony
document.addEventListener('DOMContentLoaded', () => {
  // Inicjalizacja EventSource dla aktualizacji w czasie rzeczywistym
  initEventSource();
  
  // Pierwszy raz pobierz aktualny stan maszyny
  updateMachineStatus();
  
  // Dodaj obsługę przycisków
  document.getElementById('startBtn').addEventListener('click', startProcessing);
  document.getElementById('pauseBtn').addEventListener('click', pauseProcessing);
  document.getElementById('stopBtn').addEventListener('click', stopProcessing);
  document.getElementById('resetBtn').addEventListener('click', resetMachine);
  document.getElementById('homeBtn').addEventListener('click', homeMachine);
  document.getElementById('zeroBtn').addEventListener('click', zeroAxes);
});