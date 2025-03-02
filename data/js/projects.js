/**
 * JavaScript for projects page (projects.html)
 */

// Zmienna do przechowywania aktualnie wybranego pliku
let selectedFilename = null;

// Zmienna do przechowywania EventSource
let eventSource;

// Funkcja aktualizująca listę plików na stronie
function updateFileList(files) {
  const fileListElement = document.getElementById('file-list');
  const noFilesMessage = document.getElementById('no-files-message');
  
  if (!fileListElement) return;
  
  fileListElement.innerHTML = '';
  
  if (!files || files.length === 0) {
    noFilesMessage.style.display = 'block';
    return;
  }
  
  noFilesMessage.style.display = 'none';
  
  // Sortuj pliki alfabetycznie
  files.sort();
  
  for (const file of files) {
    const row = document.createElement('tr');
    
    // Pierwsza kolumna - nazwa pliku z radio buttonem
    const fileNameCell = document.createElement('td');
    fileNameCell.innerHTML = `
      <div class="form-check">
        <input class="form-check-input" type="radio" name="selectedFile" id="file-${file}" value="${file}" 
          ${selectedFilename === file ? 'checked' : ''}>
        <label class="form-check-label" for="file-${file}">${file}</label>
      </div>
    `;
    
    // Druga kolumna - przyciski akcji
    const actionsCell = document.createElement('td');
    actionsCell.innerHTML = `
      <div class="project-actions">
        <button class="btn btn-sm btn-primary" onclick="previewFile('${file}')">Preview</button>
        <button class="btn btn-sm btn-info" onclick="viewGCode('${file}')">View Code</button>
        <button class="btn btn-sm btn-danger" onclick="deleteFile('${file}')">Delete</button>
      </div>
    `;
    
    row.appendChild(fileNameCell);
    row.appendChild(actionsCell);
    fileListElement.appendChild(row);
    
    // Dodaj listener do radio buttona
    const radioButton = row.querySelector(`input[value="${file}"]`);
    radioButton.addEventListener('change', () => {
      selectedFilename = file;
      document.getElementById('selected-file').textContent = `Selected file: ${file}`;
      localStorage.setItem('selectedFile', file);
    });
  }
}

// Pobieranie listy plików z serwera
function fetchFileList() {
  fetch('/api/list-files')
    .then(response => response.json())
    .then(data => {
      console.log('File list received:', data);
      if (Array.isArray(data)) {
        updateFileList(data);
      } else if (data.files && Array.isArray(data.files)) {
        updateFileList(data.files);
      } else if (data && data.success && data.files) {
        // Handle the actual format the server returns
        updateFileList(data.files);
      } else {
        console.error('Unexpected response format:', data);
        updateFileList([]);
      }
      
      const storedFile = localStorage.getItem('selectedFile');
      if (storedFile && (Array.isArray(data) ? data.includes(storedFile) : (data.files && data.files.includes(storedFile)))) {
        selectedFilename = storedFile;
        document.getElementById('selected-file').textContent = `Selected file: ${storedFile}`;
        const radioButton = document.querySelector(`input[value="${storedFile}"]`);
        if (radioButton) radioButton.checked = true;
      }
    })
    .catch(error => {
      console.error('Error fetching file list:', error);
      showMessage('Error fetching file list. Please check SD card connection.', 'error');
    });
}

// Odświeżanie listy plików
function refreshFileList() {
  // Najpierw wywołaj endpoint do odświeżenia na ESP32
  fetch('/api/refresh-files', { method: 'POST' })
    .then(response => {
      if (!response.ok) {
        throw new Error('Failed to refresh files on ESP32');
      }
      // Następnie pobierz zaktualizowaną listę plików
      return fetch('/api/list-files');
    })
    .then(response => {
      if (!response.ok) {
        throw new Error('Failed to get file list');
      }
      return response.json();
    })
    .then(data => {
      if (Array.isArray(data)) {
        updateFileList(data);
      } else if (data.files && Array.isArray(data.files)) {
        updateFileList(data.files);
      }
      showMessage('File list refreshed successfully');
    })
    .catch(error => {
      showMessage(`Failed to refresh file list: ${error.message}`, 'error');
    });
}

// Wybór pliku
function submitFileSelection() {
  if (!selectedFilename) {
    showMessage('Please select a file first.', 'warning');
    return;
  }

  fetch('/api/select-file?file=' + encodeURIComponent(selectedFilename), {
    method: 'POST'
  })
  .then(response => response.json())
  .then(data => {
    if (data.success) {
      document.getElementById('selected-file').textContent = `Selected file: ${selectedFilename}`;
      showMessage(`File "${selectedFilename}" selected successfully`);
      localStorage.setItem('selectedFile', selectedFilename);
    } else {
      showMessage(`Failed to select file: ${data.message || 'Unknown error'}`, 'error');
    }
  })
  .catch(error => {
    console.error('Selection error:', error);
    showMessage('Error selecting file', 'error');
  });
}

// Podgląd zawartości pliku G-code
function viewGCode(filename) {
  // Use the correct endpoint format that matches your server implementation
  fetch('/api/sd-files/' + encodeURIComponent(filename))
    .then(response => {
      if (!response.ok) throw new Error('Failed to fetch file content');
      return response.text();
    })
    .then(content => {
      const modal = new bootstrap.Modal(document.getElementById('gCodeModal'));
      document.getElementById('gCodeContent').value = content;
      document.getElementById('gCodeModalLabel').textContent = `G-Code: ${filename}`;
      modal.show();
    })
    .catch(error => {
      console.error('Error fetching G-Code content:', error);
      showMessage(`Failed to fetch G-Code content: ${error.message}`, 'error');
    });
}

// Podgląd pliku G-code
function previewFile(filename) {
  // Ustaw wybrany plik
  selectedFilename = filename;
  document.getElementById('selected-file').textContent = `Selected file: ${filename}`;
  localStorage.setItem('selectedFile', filename);
  
  // Zaznacz odpowiedni radio button
  const radioButton = document.querySelector(`input[value="${filename}"]`);
  if (radioButton) radioButton.checked = true;
  
  // Wyświetl podgląd
  visualizeGCode(filename);
  
  // Wyślij informację o wyborze do serwera
  submitFileSelection();
}

// Wizualizacja pliku G-code
function visualizeGCode(filename) {
  if (!filename) {
    filename = selectedFilename;
  }
  
  if (!filename) {
    showMessage('Please select a file first.', 'warning');
    return;
  }

  fetch('/api/sd-files/' + encodeURIComponent(filename))
    .then(response => {
      if (!response.ok) throw new Error('Failed to fetch file');
      return response.text();
    })
    .then(gcode => renderGCodePreview(gcode))
    .catch(error => {
      console.error('Error fetching G-Code file:', error);
      showMessage(`Failed to fetch G-Code file: ${error.message}`, 'error');
    });
}

// Renderowanie wizualizacji G-code
function renderGCodePreview(gcode) {
  const canvas = document.getElementById('canvas');
  const ctx = canvas.getContext('2d');
  const gcodeLines = gcode.split('\n');

  // Wyczyść canvas
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Ustaw parametry rysowania
  ctx.strokeStyle = '#1E88E5'; // Niebieski kolor dla lepszej widoczności
  ctx.lineWidth = 2;

  // Inicjalizacja współrzędnych
  let currentX = 0;
  let currentY = 0;
  let firstMove = true;

  // Znajdź min/max współrzędne do skalowania
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const points = [];
  let movesCount = 0;

  // Pierwsza pętla do zebrania wszystkich punktów
  gcodeLines.forEach(line => {
    line = line.trim().toUpperCase();
    
    // Usuń komentarze
    const commentIndex = line.indexOf(';');
    if (commentIndex !== -1) {
      line = line.substring(0, commentIndex).trim();
    }
    
    if (line.startsWith('G0') || line.startsWith('G1') || 
        line.startsWith('G00') || line.startsWith('G01')) {
      
      const xMatch = line.match(/X([-\d\.]+)/);
      const yMatch = line.match(/Y([-\d\.]+)/);

      if (xMatch || yMatch) {
        movesCount++;
        
        const x = xMatch ? parseFloat(xMatch[1]) : currentX;
        const y = yMatch ? parseFloat(yMatch[1]) : currentY;

        points.push({ 
          x, 
          y, 
          rapid: line.startsWith('G0') || line.startsWith('G00')
        });

        minX = Math.min(minX, x);
        minY = Math.min(minY, y);
        maxX = Math.max(maxX, x);
        maxY = Math.max(maxY, y);

        currentX = x;
        currentY = y;
      }
    }
  });

  // Jeśli nie ma punktów, wyświetl komunikat
  if (points.length === 0 || movesCount === 0) {
    ctx.font = '16px Arial';
    ctx.fillStyle = '#333';
    ctx.textAlign = 'center';
    ctx.fillText('No valid G-code moves found in file', canvas.width/2, canvas.height/2);
    return;
  }

  // Oblicz współczynniki skalowania
  const canvasWidth = canvas.width - 40; // Margines
  const canvasHeight = canvas.height - 40; // Margines
  const contentWidth = maxX - minX || 1; // Zabezpieczenie przed dzieleniem przez 0
  const contentHeight = maxY - minY || 1; // Zabezpieczenie przed dzieleniem przez 0

  const scaleX = canvasWidth / contentWidth;
  const scaleY = canvasHeight / contentHeight;
  const scale = Math.min(scaleX, scaleY);

  // Oblicz offset do wycentrowania rysunku
  const offsetX = (canvas.width - (contentWidth * scale)) / 2;
  const offsetY = (canvas.height - (contentHeight * scale)) / 2;

  // Narysuj siatkę pomocniczą
  drawGrid(ctx, canvas.width, canvas.height, 20, '#E0E0E0');

  // Narysuj osie
  drawAxes(ctx, canvas.width, canvas.height);

  // Druga pętla do rysowania
  ctx.beginPath();
  firstMove = true;
  
  points.forEach((point, index) => {
    const x = (point.x - minX) * scale + offsetX;
    const y = canvas.height - ((point.y - minY) * scale + offsetY); // Odwróć oś Y
    
    if (firstMove) {
      ctx.moveTo(x, y);
      firstMove = false;
    } else {
      // Zmień kolor linii dla szybkich ruchów
      if (point.rapid) {
        ctx.strokeStyle = '#FF5722'; // Czerwony dla szybkich ruchów
      } else {
        ctx.strokeStyle = '#1E88E5'; // Niebieski dla normalnych ruchów
      }
      
      // Zamknij poprzednią ścieżkę i rozpocznij nową jeśli zmienia się typ ruchu
      if (index > 0 && point.rapid !== points[index-1].rapid) {
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    
    // Rysuj punkty w miejscach zmian współrzędnych
    ctx.stroke();
    ctx.beginPath();
    ctx.fillStyle = point.rapid ? '#FF5722' : '#1E88E5';
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();
    ctx.beginPath();
    
    // Kontynuuj linię
    if (index > 0) {
      const prevX = (points[index-1].x - minX) * scale + offsetX;
      const prevY = canvas.height - ((points[index-1].y - minY) * scale + offsetY);
      ctx.moveTo(prevX, prevY);
      ctx.lineTo(x, y);
    }
  });
  
  ctx.stroke();
  
  // Dodaj wskaźnik początku ścieżki
  if (points.length > 0) {
    const startX = (points[0].x - minX) * scale + offsetX;
    const startY = canvas.height - ((points[0].y - minY) * scale + offsetY);
    
    ctx.beginPath();
    ctx.fillStyle = '#4CAF50'; // Zielony kolor dla punktu startowego
    ctx.arc(startX, startY, 5, 0, 2 * Math.PI);
    ctx.fill();
    
    // Dodaj etykietę "Start"
    ctx.font = '12px Arial';
    ctx.fillStyle = '#000';
    ctx.fillText('Start', startX + 10, startY);
  }
  
  // Dodaj informacje o zakresie współrzędnych
  const infoText = `X: ${minX.toFixed(2)} to ${maxX.toFixed(2)}, Y: ${minY.toFixed(2)} to ${maxY.toFixed(2)}`;
  ctx.font = '12px Arial';
  ctx.fillStyle = '#000';
  ctx.textAlign = 'left';
  ctx.fillText(infoText, 10, 20);
  
  // Dodaj legendę
  const legendY = canvas.height - 20;
  
  ctx.beginPath();
  ctx.strokeStyle = '#1E88E5';
  ctx.lineWidth = 2;
  ctx.moveTo(10, legendY);
  ctx.lineTo(40, legendY);
  ctx.stroke();
  
  ctx.font = '12px Arial';
  ctx.fillStyle = '#000';
  ctx.textAlign = 'left';
  ctx.fillText('Cutting move (G1)', 45, legendY + 4);
  
  ctx.beginPath();
  ctx.strokeStyle = '#FF5722';
  ctx.lineWidth = 2;
  ctx.moveTo(150, legendY);
  ctx.lineTo(180, legendY);
  ctx.stroke();
  
  ctx.fillText('Rapid move (G0)', 185, legendY + 4);
}

// Funkcja rysująca siatkę
function drawGrid(ctx, width, height, step, color) {
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 0.5;
  
  // Linie pionowe
  for (let x = step; x < width; x += step) {
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
  }
  
  // Linie poziome
  for (let y = step; y < height; y += step) {
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
  }
  
  ctx.stroke();
}

// Funkcja rysująca osie
function drawAxes(ctx, width, height) {
  ctx.beginPath();
  ctx.strokeStyle = '#000';
  ctx.lineWidth = 1;
  
  // Oś X
  ctx.moveTo(0, height / 2);
  ctx.lineTo(width, height / 2);
  
  // Oś Y
  ctx.moveTo(width / 2, 0);
  ctx.lineTo(width / 2, height);
  
  ctx.stroke();
}

// Funkcja wizualizacji wybranego pliku G-code - wywoływana przez przycisk Preview
function visualizeSelectedGCode() {
  const selectedFile = selectedFilename;
  if (!selectedFile) {
    showMessage('Please select a file first.', 'warning');
    return;
  }
  
  visualizeGCode(selectedFile);
}

// Usunięcie pliku
function deleteFile(filename) {
  if (!confirm(`Are you sure you want to delete the file "${filename}"?`)) {
    return;
  }
  
  fetch('/api/delete-file?file=' + encodeURIComponent(filename), {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Failed to delete file');
    }
    return response.json();
  })
  .then(data => {
    if (data.success) {
      showMessage(`File "${filename}" deleted successfully`);
      
      // Odśwież listę plików
      refreshFileList();
      
      // Jeśli usunięto aktualnie wybrany plik, wyczyść wybór
      if (selectedFilename === filename) {
        selectedFilename = null;
        document.getElementById('selected-file').textContent = 'No file selected';
        localStorage.removeItem('selectedFile');
        
        // Wyczyść canvas
        const canvas = document.getElementById('canvas');
        const ctx = canvas.getContext('2d');
        ctx.clearRect(0, 0, canvas.width, canvas.height);
      }
    } else {
      showMessage(`Failed to delete file: ${data.message || 'Unknown error'}`, 'error');
    }
  })
  .catch(error => {
    console.error('Error deleting file:', error);
    showMessage(`Error deleting file: ${error.message}`, 'error');
  });
}

// Funkcja uploadowania pliku
function uploadFile() {
  const fileInput = document.getElementById('fileInput');
  const progressContainer = document.getElementById('progress-container');
  const progressBar = document.getElementById('progress-bar');
  const uploadMessage = document.getElementById('upload-message');
  
  if (fileInput.files.length === 0) {
    uploadMessage.textContent = "Please select a file.";
    uploadMessage.className = 'alert alert-warning';
    uploadMessage.style.display = 'block';
    return;
  }
  
  const file = fileInput.files[0];
  const formData = new FormData();
  formData.append('file', file);
  
  progressContainer.style.display = 'block';
  progressBar.style.width = '0%';
  progressBar.textContent = '0%';
  uploadMessage.style.display = 'none';
  
  fetch('/api/upload-file', {
    method: 'POST',
    body: formData,
  })
  .then(response => {
    if (!response.ok) {
      return response.text().then(text => {throw new Error(text)});
    }
    return response.text();
  })
  .then(data => {
    console.log('Upload successful:', data);
    progressBar.style.width = '100%';
    progressBar.textContent = '100%';
    uploadMessage.textContent = "File uploaded successfully!";
    uploadMessage.className = 'alert alert-success';
    uploadMessage.style.display = 'block';
    
    // Odśwież listę plików po uploadzie
    refreshFileList();
    
    // Wyczyść input po udanym uploadzie
    fileInput.value = '';
    
    // Zamknij modal po krótkim opóźnieniu
    setTimeout(() => {
      const modal = bootstrap.Modal.getInstance(document.getElementById('uploadModal'));
      if (modal) modal.hide();
      
      // Ukryj komunikaty i progress bar
      progressContainer.style.display = 'none';
      uploadMessage.style.display = 'none';
    }, 1500);
  })
  .catch(error => {
    console.error('Upload error:', error);
    uploadMessage.textContent = "Upload failed: " + error.message;
    uploadMessage.className = 'alert alert-danger';
    uploadMessage.style.display = 'block';
    progressContainer.style.display = 'none';
  });
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
      updateMachineStatus(data);
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

// Aktualizacja stanu kontrolek na podstawie stanu maszyny
function updateMachineStatus(data) {
  const startBtn = document.getElementById('startBtn');
  const pauseBtn = document.getElementById('pauseBtn');
  const stopBtn = document.getElementById('stopBtn');
  const resetBtn = document.getElementById('resetBtn');
  
  // Stan: IDLE
  if (data.state === 0) {
    if (startBtn) startBtn.disabled = false;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
    if (resetBtn) resetBtn.disabled = true;
  }
  // Stan: RUNNING
  else if (data.state === 1) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) {
      pauseBtn.disabled = false;
      pauseBtn.innerHTML = data.isPaused ? '<i class="bi bi-play-fill"></i> RESUME' : '<i class="bi bi-pause-fill"></i> PAUSE';
    }
    if (stopBtn) stopBtn.disabled = false;
    if (resetBtn) resetBtn.disabled = true;
  }
  // Stan: JOG, HOMING
  else if (data.state === 2 || data.state === 3) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = false;
    if (resetBtn) resetBtn.disabled = true;
  }
  // Stan: STOPPED, ERROR
  else if (data.state === 4 || data.state === 5) {
    if (startBtn) startBtn.disabled = true;
    if (pauseBtn) pauseBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = true;
    if (resetBtn) resetBtn.disabled = false;
  }
}

// Funkcje sterowania maszyną
function startProcessing() {
  if (!selectedFilename) {
    showMessage('Please select a file first.', 'warning');
    return;
  }
  
  // Najpierw upewnij się, że plik jest wybrany na serwerze
  fetch('/api/select-file?file=' + encodeURIComponent(selectedFilename), {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Failed to select file');
    }
    
    // Teraz uruchom przetwarzanie
    return fetch('/api/start', { method: 'POST' });
  })
  .then(response => response.json())
  .then(data => {
    if (data.success) {
      showMessage('Processing started');
    } else {
      showMessage('Failed to start processing: ' + (data.message || 'Unknown error'), 'error');
    }
  })
  .catch(error => {
    console.error('Start error:', error);
    showMessage('Error starting processing: ' + error.message, 'error');
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
        showMessage('Failed to pause/resume processing: ' + (data.message || 'Unknown error'), 'error');
      }
    })
    .catch(error => {
      console.error('Pause error:', error);
      showMessage('Error pausing processing: ' + error.message, 'error');
    });
}

function stopProcessing() {
  fetch('/api/stop', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Processing stopped');
      } else {
        showMessage('Failed to stop processing: ' + (data.message || 'Unknown error'), 'error');
      }
    })
    .catch(error => {
      console.error('Stop error:', error);
      showMessage('Error stopping processing: ' + error.message, 'error');
    });
}

function resetMachine() {
  fetch('/api/reset', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      if (data.success) {
        showMessage('Machine reset');
      } else {
        showMessage('Failed to reset machine: ' + (data.message || 'Unknown error'), 'error');
      }
    })
    .catch(error => {
      console.error('Reset error:', error);
      showMessage('Error resetting machine: ' + error.message, 'error');
    });
}

// Inicjalizacja po załadowaniu strony
document.addEventListener('DOMContentLoaded', () => {
  // Pobierz listę plików
  fetchFileList();
  
  // Inicjalizacja EventSource
  initEventSource();
  
  // Obsługa formularza uploadowania
  document.getElementById('uploadForm').addEventListener('submit', event => {
    event.preventDefault();
    uploadFile();
  });
  
  // Dodaj obsługę przycisków sterowania
  document.getElementById('startBtn').addEventListener('click', startProcessing);
  document.getElementById('pauseBtn').addEventListener('click', pauseProcessing);
  document.getElementById('stopBtn').addEventListener('click', stopProcessing);
  document.getElementById('resetBtn').addEventListener('click', resetMachine);
});