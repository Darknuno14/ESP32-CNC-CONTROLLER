/**
 * JavaScript for main page (index.html)
 */

// Aktualizacja statusu maszyny
function updateMachineStatus() {
  // W przyszłości tutaj zostanie dodane endpoint API do pobierania statusu
  // Na razie używamy symulowanych danych
  const statusElement = document.getElementById('machine-status');
  const statusTextElement = document.getElementById('machine-status-text');
  
  if (statusElement && statusTextElement) {
    // Pobierz status z API (docelowo)
    fetch('/api/machine-status')
      .then(response => response.json())
      .catch(() => {
        // Fallback - symulowany status jeśli endpoint nie istnieje
        return { 
          status: Math.random() > 0.5 ? 'idle' : 'running',
          wireOn: Math.random() > 0.7,
          fanOn: Math.random() > 0.7,
          position: { x: (Math.random() * 200).toFixed(3), y: (Math.random() * 200).toFixed(3) }
        };
      })
      .then(data => {
        // Aktualizuj status maszyny
        statusElement.classList.remove('status-on', 'status-off', 'status-warning');
        if (data.status === 'running') {
          statusElement.classList.add('status-on');
          statusTextElement.textContent = 'Running';
        } else if (data.status === 'error') {
          statusElement.classList.add('status-warning');
          statusTextElement.textContent = 'Error';
        } else {
          statusElement.classList.add('status-off');
          statusTextElement.textContent = 'Idle';
        }
        
        // Aktualizuj status drutu
        const wireStatusElement = document.getElementById('wire-status');
        const wireStatusTextElement = document.getElementById('wire-status-text');
        if (wireStatusElement && wireStatusTextElement) {
          wireStatusElement.classList.remove('status-on', 'status-off');
          wireStatusElement.classList.add(data.wireOn ? 'status-on' : 'status-off');
          wireStatusTextElement.textContent = data.wireOn ? 'On' : 'Off';
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
        if (positionXElement && positionYElement && data.position) {
          positionXElement.textContent = data.position.x;
          positionYElement.textContent = data.position.y;
        }
      });
  }
}

// Aktualizacja statusu zadania
function updateJobStatus() {
  // W przyszłości tutaj zostanie dodane endpoint API do pobierania statusu zadania
  // Na razie używamy symulowanych danych
  fetch('/api/job-status')
    .then(response => response.json())
    .catch(() => {
      // Fallback - symulowany status jeśli endpoint nie istnieje
      return { 
        file: localStorage.getItem('selectedFile') || 'No file selected',
        progress: Math.min(parseFloat(localStorage.getItem('jobProgress') || 0), 100)
      };
    })
    .then(data => {
      // Aktualizuj nazwę pliku
      const selectedFileElement = document.getElementById('selected-file');
      if (selectedFileElement) {
        selectedFileElement.textContent = data.file === '' ? 'No file selected' : `File: ${data.file}`;
      }
      
      // Aktualizuj pasek postępu
      const progressBarElement = document.getElementById('job-progress');
      if (progressBarElement) {
        const progress = data.progress || 0;
        progressBarElement.style.width = `${progress}%`;
        progressBarElement.textContent = `${progress.toFixed(1)}%`;
        progressBarElement.setAttribute('aria-valuenow', progress);
      }
    });
}

// Funkcje sterowania maszyną
function startProcessing() {
  fetch('/api/start', { method: 'POST' })
    .then(response => response.text())
    .then(data => {
      console.log('Processing started:', data);
      showMessage('Processing started');
      localStorage.setItem('jobProgress', 0);
      // Aktualizuj status po uruchomieniu
      setTimeout(updateMachineStatus, 500);
      setTimeout(updateJobStatus, 500);
    })
    .catch(error => {
      console.error('Start error:', error);
      showMessage('Error starting processing', 'error');
    });
}

function pauseProcessing() {
  fetch('/api/pause', { method: 'POST' })
    .then(response => response.text())
    .then(data => {
      console.log('Processing paused:', data);
      showMessage('Processing paused');
      // Aktualizuj status po wstrzymaniu
      setTimeout(updateMachineStatus, 500);
    })
    .catch(error => {
      console.error('Pause error:', error);
      showMessage('Error pausing processing', 'error');
    });
}

function stopProcessing() {
  fetch('/api/stop', { method: 'POST' })
    .then(response => response.text())
    .then(data => {
      console.log('Processing stopped:', data);
      showMessage('Processing stopped');
      // Aktualizuj status po zatrzymaniu
      setTimeout(updateMachineStatus, 500);
    })
    .catch(error => {
      console.error('Stop error:', error);
      showMessage('Error stopping processing', 'error');
    });
}

// Inicjalizacja po załadowaniu strony
document.addEventListener('DOMContentLoaded', () => {
  // Aktualizuj status maszyny i zadania
  updateMachineStatus();
  updateJobStatus();
  
  // Ustaw okresową aktualizację statusu co 2 sekundy
  setInterval(() => {
    updateMachineStatus();
    updateJobStatus();
  }, 2000);
});
