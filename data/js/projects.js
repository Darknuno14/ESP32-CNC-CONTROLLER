/**
 * JavaScript for projects page (projects.html)
 */

// Zmienna do przechowywania aktualnie wybranego pliku
let selectedFilename = null;

// Zmienna do przechowywania EventSource
let eventSource;

// --- Helper Functions ---

function setSelectedFile(filename) {
  selectedFilename = filename;
  document.getElementById("selected-file").textContent = filename
    ? `Selected file: ${filename}`
    : "No file selected";
  localStorage.setItem("selectedFile", filename || "");
  // Zaznacz radio button jeśli istnieje
  const radioButton = document.querySelector(`input[value="${filename}"]`);
  if (radioButton) radioButton.checked = true;
  // Włącz przycisk potwierdzenia
  const confirmBtn = document.getElementById("confirmFileBtn");
  if (confirmBtn) confirmBtn.disabled = !filename;
}

function handleEventSource() {
  if (eventSource) eventSource.close();
  eventSource = new EventSource("/events");
  eventSource.onopen = () => console.log("EventSource connection established");
  eventSource.onerror = () => {
    console.error("EventSource error");
    setTimeout(handleEventSource, 5000);
  };
}

// --- Main Functions ---

function updateFileList(files) {
  const fileListElement = document.getElementById("file-list");
  const noFilesMessage = document.getElementById("no-files-message");
  if (!fileListElement) return;
  fileListElement.innerHTML = "";
  if (!files || files.length === 0) {
    noFilesMessage.style.display = "block";
    return;
  }
  noFilesMessage.style.display = "none";
  // Sortuj pliki alfabetycznie
  files.sort();
  for (const file of files) {
    const row = document.createElement("tr");

    // Pierwsza kolumna - nazwa pliku z radio buttonem
    const fileNameCell = document.createElement("td");
    fileNameCell.innerHTML = `
      <div class="form-check">
        <input class="form-check-input" type="radio" name="selectedFile" id="file-${file}" value="${file}" 
          ${selectedFilename === file ? "checked" : ""}>
        <label class="form-check-label" for="file-${file}">${file}</label>
      </div>
    `;

    // Druga kolumna - przyciski akcji
    const actionsCell = document.createElement("td");
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

    // Listener do radio buttona
    row.querySelector(`input[value="${file}"]`).addEventListener("change", () => {
      setSelectedFile(file);
    });
  }
}

function fetchFileList() {
  fetch("/api/list-files")
    .then((response) => response.json())
    .then((data) => {
      if (data && data.success && Array.isArray(data.files)) {
        updateFileList(data.files);
        const storedFile = localStorage.getItem("selectedFile");
        if (storedFile && data.files.includes(storedFile)) {
          setSelectedFile(storedFile);
        }
      } else {
        updateFileList([]);
        showMessage(data && data.message ? data.message : "Received invalid data format from server", "error");
      }
    })
    .catch(() => {
      showMessage("Error fetching file list. Please check SD card connection.", "error");
    });
}

function refreshFileList() {
  fetch("/api/refresh-files", { method: "POST" })
    .then((response) => {
      if (!response.ok) throw new Error("Failed to refresh files on ESP32");
      return fetch("/api/list-files");
    })
    .then((response) => {
      if (!response.ok) throw new Error("Failed to get file list");
      return response.json();
    })
    .then((data) => {
      if (data && data.success && Array.isArray(data.files)) {
        updateFileList(data.files);
        showMessage("File list refreshed successfully");
      } else {
        showMessage("Received invalid data format from server", "warning");
      }
    })
    .catch((error) => {
      showMessage(`Failed to refresh file list: ${error.message}`, "error");
    });
}

function confirmSelectedFile() {
  if (!selectedFilename) {
    showMessage("Please select a file first.", "warning");
    return;
  }
  fetch("/api/select-file?file=" + encodeURIComponent(selectedFilename), { method: "POST" })
    .then((res) => res.json())
    .then((data) => {
      showMessage(data.success ? "Wybrano projekt!" : "Błąd: " + data.message, data.success ? "success" : "error");
    })
    .catch(() => showMessage("Błąd połączenia z serwerem", "error"));
}

function viewGCode(filename) {
  fetch("/api/sd_content?file=" + encodeURIComponent(filename))
    .then((response) => {
      if (!response.ok) throw new Error("Failed to fetch file content: " + response.status);
      return response.text();
    })
    .then((content) => {
      const modal = new bootstrap.Modal(document.getElementById("gCodeModal"));
      document.getElementById("gCodeContent").value = content;
      document.getElementById("gCodeModalLabel").textContent = `G-Code: ${filename}`;
      modal.show();
    })
    .catch((error) => {
      showMessage(`Failed to fetch G-Code content: ${error.message}`, "error");
    });
}

function previewFile(filename) {
  setSelectedFile(filename);
  visualizeGCode(filename);
}

function visualizeGCode(filename) {
  if (!filename) filename = selectedFilename;
  if (!filename) {
    showMessage("Please select a file first.", "warning");
    return;
  }
  fetch("/api/sd_content?file=" + encodeURIComponent(filename))
    .then((response) => {
      if (!response.ok) throw new Error("Failed to fetch file: " + response.status);
      return response.text();
    })
    .then((gcode) => {
      renderGCodePreview(gcode);
    })
    .catch((error) => {
      showMessage(`Failed to fetch G-Code file: ${error.message}`, "error");
    });
}

// Renderowanie wizualizacji G-code
function renderGCodePreview(gcode) {
  const canvas = document.getElementById("canvas");
  const ctx = canvas.getContext("2d");
  const gcodeLines = gcode.split("\n");

  // Wyczyść canvas
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Ustaw parametry rysowania
  ctx.strokeStyle = "#1E88E5"; // Niebieski kolor dla lepszej widoczności
  ctx.lineWidth = 2;

  // Inicjalizacja współrzędnych
  let currentX = 0;
  let currentY = 0;
  let firstMove = true;

  // Znajdź min/max współrzędne do skalowania
  let minX = Infinity,
    minY = Infinity,
    maxX = -Infinity,
    maxY = -Infinity;
  const points = [];
  let movesCount = 0;

  // Pierwsza pętla do zebrania wszystkich punktów
  gcodeLines.forEach((line) => {
    line = line.trim().toUpperCase();

    // Usuń komentarze
    const commentIndex = line.indexOf(";");
    if (commentIndex !== -1) {
      line = line.substring(0, commentIndex).trim();
    }

    if (
      line.startsWith("G0") ||
      line.startsWith("G1") ||
      line.startsWith("G00") ||
      line.startsWith("G01")
    ) {
      const xMatch = line.match(/X([-\d\.]+)/);
      const yMatch = line.match(/Y([-\d\.]+)/);

      if (xMatch || yMatch) {
        movesCount++;

        const x = xMatch ? parseFloat(xMatch[1]) : currentX;
        const y = yMatch ? parseFloat(yMatch[1]) : currentY;

        points.push({
          x,
          y,
          rapid: line.startsWith("G0") || line.startsWith("G00"),
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
    ctx.font = "16px Arial";
    ctx.fillStyle = "#333";
    ctx.textAlign = "center";
    ctx.fillText(
      "No valid G-code moves found in file",
      canvas.width / 2,
      canvas.height / 2
    );
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
  const offsetX = (canvas.width - contentWidth * scale) / 2;
  const offsetY = (canvas.height - contentHeight * scale) / 2;

  // Narysuj siatkę pomocniczą
  drawGrid(ctx, canvas.width, canvas.height, 20, "#E0E0E0");

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
        ctx.strokeStyle = "#FF5722"; // Czerwony dla szybkich ruchów
      } else {
        ctx.strokeStyle = "#1E88E5"; // Niebieski dla normalnych ruchów
      }

      // Zamknij poprzednią ścieżkę i rozpocznij nową jeśli zmienia się typ ruchu
      if (index > 0 && point.rapid !== points[index - 1].rapid) {
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
    ctx.fillStyle = point.rapid ? "#FF5722" : "#1E88E5";
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();
    ctx.beginPath();

    // Kontynuuj linię
    if (index > 0) {
      const prevX = (points[index - 1].x - minX) * scale + offsetX;
      const prevY =
        canvas.height - ((points[index - 1].y - minY) * scale + offsetY);
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
    ctx.fillStyle = "#4CAF50"; // Zielony kolor dla punktu startowego
    ctx.arc(startX, startY, 5, 0, 2 * Math.PI);
    ctx.fill();

    // Dodaj etykietę "Start"
    ctx.font = "12px Arial";
    ctx.fillStyle = "#000";
    ctx.fillText("Start", startX + 10, startY);
  }

  // Dodaj informacje o zakresie współrzędnych
  const infoText = `X: ${minX.toFixed(2)} to ${maxX.toFixed(
    2
  )}, Y: ${minY.toFixed(2)} to ${maxY.toFixed(2)}`;
  ctx.font = "12px Arial";
  ctx.fillStyle = "#000";
  ctx.textAlign = "left";
  ctx.fillText(infoText, 10, 20);

  // Dodaj legendę
  const legendY = canvas.height - 20;

  ctx.beginPath();
  ctx.strokeStyle = "#1E88E5";
  ctx.lineWidth = 2;
  ctx.moveTo(10, legendY);
  ctx.lineTo(40, legendY);
  ctx.stroke();

  ctx.font = "12px Arial";
  ctx.fillStyle = "#000";
  ctx.textAlign = "left";
  ctx.fillText("Cutting move (G1)", 45, legendY + 4);

  ctx.beginPath();
  ctx.strokeStyle = "#FF5722";
  ctx.lineWidth = 2;
  ctx.moveTo(150, legendY);
  ctx.lineTo(180, legendY);
  ctx.stroke();

  ctx.fillText("Rapid move (G0)", 185, legendY + 4);
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
  ctx.strokeStyle = "#000";
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
    showMessage("Please select a file first.", "warning");
    return;
  }

  visualizeGCode(selectedFile);
}

// Usunięcie pliku
function deleteFile(filename) {
  if (!confirm(`Are you sure you want to delete the file "${filename}"?`)) return;
  fetch("/api/delete-file?file=" + encodeURIComponent(filename), { method: "POST" })
    .then((response) => {
      if (!response.ok) throw new Error("Failed to delete file");
      return response.json();
    })
    .then((data) => {
      if (data.success) {
        showMessage(`File "${filename}" deleted successfully`);
        refreshFileList();
        if (selectedFilename === filename) {
          setSelectedFile(null);
          // Wyczyść canvas
          const canvas = document.getElementById("canvas");
          const ctx = canvas.getContext("2d");
          ctx.clearRect(0, 0, canvas.width, canvas.height);
        }
      } else {
        showMessage(`Failed to delete file: ${data.message || "Unknown error"}`, "error");
      }
    })
    .catch((error) => {
      showMessage(`Error deleting file: ${error.message}`, "error");
    });
}

function uploadFile() {
  const fileInput = document.getElementById("fileInput");
  const progressContainer = document.getElementById("progress-container");
  const progressBar = document.getElementById("progress-bar");
  const uploadMessage = document.getElementById("upload-message");
  if (fileInput.files.length === 0) {
    uploadMessage.textContent = "Please select a file.";
    uploadMessage.className = "alert alert-warning";
    uploadMessage.style.display = "block";
    return;
  }
  const file = fileInput.files[0];
  const formData = new FormData();
  formData.append("file", file);
  progressContainer.style.display = "block";
  progressBar.style.width = "0%";
  progressBar.textContent = "0%";
  uploadMessage.style.display = "none";
  fetch("/api/upload-file", { method: "POST", body: formData })
    .then((response) => {
      if (!response.ok) return response.text().then((text) => { throw new Error(text); });
      return response.text();
    })
    .then(() => {
      progressBar.style.width = "100%";
      progressBar.textContent = "100%";
      uploadMessage.textContent = "File uploaded successfully!";
      uploadMessage.className = "alert alert-success";
      uploadMessage.style.display = "block";
      refreshFileList();
      fileInput.value = "";
      setTimeout(() => {
        const modal = bootstrap.Modal.getInstance(document.getElementById("uploadModal"));
        if (modal) modal.hide();
        progressContainer.style.display = "none";
        uploadMessage.style.display = "none";
      }, 1500);
    })
    .catch((error) => {
      uploadMessage.textContent = "Upload failed: " + error.message;
      uploadMessage.className = "alert alert-danger";
      uploadMessage.style.display = "block";
      progressContainer.style.display = "none";
    });
}

// --- Initialization ---

document.addEventListener("DOMContentLoaded", () => {
  // Initialize tooltips
  const tooltipTriggerList = [].slice.call(document.querySelectorAll('[data-bs-toggle="tooltip"]'));
  tooltipTriggerList.map(function (tooltipTriggerEl) {
    return new bootstrap.Tooltip(tooltipTriggerEl);
  });

  fetchFileList();
  handleEventSource();
  document.getElementById("uploadForm").addEventListener("submit", (event) => {
    event.preventDefault();
    uploadFile();
  });
});
