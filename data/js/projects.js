/**
 * JavaScript for projects page (projects.html)
 */

// Zmienna do przechowywania aktualnie wybranego pliku
let selectedFilename = null;

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
  fetch('/api/sd-files')
    .then(response => response.json())
    .then(data => {
      updateFileList(data);
      
      // Sprawdź, czy jest wybrany plik w localStorage
      const storedFile = localStorage.getItem('selectedFile');
      if (storedFile && data.includes(storedFile)) {
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
      return fetch('/api/sd-files');
    })
    .then(response => {
      if (!response.ok) {
        throw new Error('Failed to get file list');
      }
      return response.json();
    })
    .then(data => {
      updateFileList(data);
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
  .then(response => response.text())
  .then(data => {
    console.log('File selected:', data);
    document.getElementById('selected-file').textContent = `Selected file: ${selectedFilename}`;
    showMessage(`File "${selectedFilename}" selected successfully`);
    localStorage.setItem('selectedFile', selectedFilename);
  })
  .catch(error => {
    console.error('Selection error:', error);
    showMessage('Error selecting file', 'error');
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

  fetch('/api/preview?file=' + encodeURIComponent(filename))
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
  ctx.strokeStyle = '#000';
  ctx.lineWidth = 1;

  // Inicjalizacja współrzędnych
  let currentX = 0;
  let currentY = 0;
  let firstMove = true;

  // Znajdź min/max współrzędne do skalowania
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const points = [];

  // Pierwsza pętla do zebrania wszystkich punktów
  gcodeLines.forEach(line => {
    line = line.trim().toUpperCase();
    if (line.startsWith('G0') || line.startsWith('G1')) {
      const xMatch = line.match(/X([\d\.]+)/);
      const yMatch = line.match(/Y([\d\.]+)/);

      const x = xMatch ? parseFloat(xMatch[1]) : currentX;
      const y = yMatch ? parseFloat(yMatch[1]) : currentY;

      points.push({ x, y });

      minX = Math.min(minX, x);
      minY = Math.min(minY, y);
      maxX = Math.max(maxX, x);
      maxY = Math.max(maxY, y);

      currentX = x;
      currentY = y;
    }
  });

  // Oblicz współczynniki skalowania
  const canvasWidth = canvas.width - 20;
  const canvasHeight = canvas.height - 20;
  const contentWidth = maxX - minX;
  const contentHeight = maxY - minY;

  const scaleX = contentWidth ? canvasWidth / contentWidth : 1;
  const scaleY = contentHeight ? canvasHeight / contentHeight : 1;
  const scale = Math.min(scaleX, scaleY);

  // Oblicz offset do wycentrowania rysunku
  const offsetX = (canvas.width - (contentWidth * scale)) / 2 - minX * scale;
  const offsetY = (canvas.height - (contentHeight * scale)) / 2 - minY * scale;

  // Druga pętla do rysowania
  ctx.beginPath();
  ctx.strokeStyle = '#1E88E5'; // Niebieski kolor dla lepszej widoczności
  ctx.lineWidth = 2;
  
  firstMove = true;
  points.forEach(point => {
    const x = point.x * scale + offsetX;
    const y = canvas.height - (point.y * scale + offsetY); // Odwróć oś Y

    if (firstMove) {
      ctx.moveTo(x, y);
      firstMove = false;
    } else {
      ctx.lineTo(x, y);
    }
  });
  ctx.stroke();
  
  // Dodaj wskaźnik początku ścieżki
  if (points.length > 0) {
    const startX = points[0].x * scale + offsetX;
    const startY = canvas.height - (points[0].y * scale + offsetY);
    
    ctx.beginPath();
    ctx.fillStyle = '#4CAF50'; // Zielony kolor dla punktu startowego
    ctx.arc(startX, startY, 5, 0, 2 * Math.PI);
    ctx.fill();
  }
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
  
  fetch('/api/upload', {
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
  .then(response => response.text())
  .then(data => {
    console.log('Processing started:', data);
    showMessage('Processing started');
  })
  .catch(error => {
    console.error('Start error:', error);
    showMessage('Error starting processing: ' + error.message, 'error');
  });
}

function pauseProcessing() {
  fetch('/api/pause', { method: 'POST' })
    .then(response => response.text())
    .then(data => {
      console.log('Processing paused:', data);
      showMessage('Processing paused');
    })
    .catch(error => {
      console.error('Pause error:', error);
      showMessage('Error pausing processing: ' + error.message, 'error');
    });
}

function stopProcessing() {
  fetch('/api/stop', { method: 'POST' })
    .then(response => response.text())
    .then(data => {
      console.log('Processing stopped:', data);
      showMessage('Processing stopped');
    })
    .catch(error => {
      console.error('Stop error:', error);
      showMessage('Error stopping processing: ' + error.message, 'error');
    });
}

// Inicjalizacja po załadowaniu strony
document.addEventListener('DOMContentLoaded', () => {
  // Pobierz listę plików
  fetchFileList();
  
  // Obsługa formularza uploadowania
  document.getElementById('uploadForm').addEventListener('submit', event => {
    event.preventDefault();
    uploadFile();
  });
});
