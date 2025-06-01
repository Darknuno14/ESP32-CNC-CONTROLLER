/**
 * JavaScript for projects page (projects.html)
 */

// Zmienna do przechowywania aktualnie wybranego pliku
let selectedFilename = null;

// Zmienna do przechowywania EventSource
let eventSource;

// AbortController for G-Code preview
let gcodePreviewAbortController = null;

// --- Helper Functions ---

function setSelectedFile(filename) {
  selectedFilename = filename;
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
        <button class="btn btn-sm btn-primary" onclick="previewFile('${file}')">Podgląd</button>
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

async function previewFile(filename) {
  setSelectedFile(filename); // Keep for internal state and radio button check

  // If there's an ongoing preview, abort it
  if (gcodePreviewAbortController) {
    gcodePreviewAbortController.abort();
  }
  gcodePreviewAbortController = new AbortController();
  const signal = gcodePreviewAbortController.signal;

  const modalElement = document.getElementById("gCodePreviewModal");
  const modal = bootstrap.Modal.getInstance(modalElement) || new bootstrap.Modal(modalElement);
  
  const modalTitle = document.getElementById("gCodePreviewModalLabel");
  const loadingIndicator = document.getElementById("previewLoadingIndicator");
  const previewContainer = modalElement.querySelector(".gcode-preview-container");
  const canvas = document.getElementById("gcodePreviewCanvas");

  if (modalTitle) modalTitle.textContent = `Podgląd G-Code: ${filename}`;
  
  // Reset to loading state
  if (loadingIndicator) {
    loadingIndicator.style.display = "block";
    loadingIndicator.innerHTML = `
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Ładowanie...</span>
      </div>
      <p>Generowanie podglądu...</p>`;
  }
  if (previewContainer) previewContainer.style.display = "none";
  
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  modal.show();

  try {
    const response = await fetch("/api/sd_content?file=" + encodeURIComponent(filename), { signal });
    if (!response.ok) {
      throw new Error("Nie udało się pobrać pliku: " + response.statusText);
    }
    const gcode = await response.text();

    if (signal.aborted) {
      console.log("Preview fetch aborted.");
      return;
    }

    await processGCodeChunked(gcode, "gcodePreviewCanvas", signal);
    
    if (!signal.aborted) {
        if (loadingIndicator) loadingIndicator.style.display = "none";
        if (previewContainer) previewContainer.style.display = "block";
    } else {
        // Ensure loading indicator is hidden if aborted during processing
        if (loadingIndicator) loadingIndicator.style.display = "none";
        // Optionally, show a "cancelled" message or just keep canvas blank
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.font = "16px Arial";
        ctx.fillStyle = "#333";
        ctx.textAlign = "center";
        ctx.fillText("Generowanie podglądu anulowane.", canvas.width / 2, canvas.height / 2);
        if (previewContainer) previewContainer.style.display = "block"; // Show canvas with message
    }

  } catch (error) {
    if (error.name === 'AbortError') {
      console.log("Operacja generowania podglądu została anulowana.");
      if (loadingIndicator) {
        loadingIndicator.style.display = "block"; // Keep it visible or change to a cancelled message
        loadingIndicator.innerHTML = `<p class="text-info">Generowanie podglądu anulowane.</p>`;
      }
       if (previewContainer) previewContainer.style.display = "none";
    } else {
      showMessage(`Nie udało się wygenerować podglądu: ${error.message}`, "error");
      if (loadingIndicator) {
        loadingIndicator.innerHTML = `<p class="text-danger">Błąd podczas generowania podglądu: ${error.message}</p>`;
      }
      if (previewContainer) previewContainer.style.display = "none";
    }
  }
}

// This function is kept for conceptual compatibility but previewFile is the main entry point.
function visualizeGCode(filename) {
  console.warn("visualizeGCode is deprecated for modal preview. Use previewFile.");
  if (filename) {
    previewFile(filename);
  } else if (selectedFilename) {
    previewFile(selectedFilename);
  } else {
    showMessage("Proszę najpierw wybrać plik.", "warning");
  }
}

// Renderowanie wizualizacji G-code w kawałkach
async function processGCodeChunked(gcode, canvasId, abortSignal) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) {
    console.error("Canvas element with ID '" + canvasId + "' not found.");
    // Update loading indicator if modal is still open
    const loadingIndicator = document.getElementById("previewLoadingIndicator");
    if (loadingIndicator && document.getElementById("gCodePreviewModal")?.classList.contains('show')) {
        loadingIndicator.innerHTML = `<p class="text-danger">Błąd wewnętrzny: Nie znaleziono elementu canvas podglądu.</p>`;
    }
    return;
  }
  const ctx = canvas.getContext("2d");
  const gcodeLines = gcode.split("\n");

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // --- Phase 1: Parse all G-Code to get points and bounds ---
  let currentX = 0, currentY = 0;
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const points = [];
  let movesCount = 0;

  for (const lineContent of gcodeLines) {
    if (abortSignal.aborted) { console.log("Parsing aborted"); return; }
    let line = lineContent.trim().toUpperCase();
    const commentIndex = line.indexOf(";");
    if (commentIndex !== -1) line = line.substring(0, commentIndex).trim();

    if (line.startsWith("G0") || line.startsWith("G1") || line.startsWith("G00") || line.startsWith("G01")) {
      const xMatch = line.match(/X([-\d\.]+)/);
      const yMatch = line.match(/Y([-\d\.]+)/);
      if (xMatch || yMatch) {
        movesCount++;
        const x = xMatch ? parseFloat(xMatch[1]) : currentX;
        const y = yMatch ? parseFloat(yMatch[1]) : currentY;
        points.push({ x, y, rapid: line.startsWith("G0") || line.startsWith("G00") });
        minX = Math.min(minX, x); minY = Math.min(minY, y);
        maxX = Math.max(maxX, x); maxY = Math.max(maxY, y);
        currentX = x; currentY = y;
      }
    }
  }

  if (abortSignal.aborted) { console.log("Processing aborted after parsing points."); return; }

  if (points.length === 0 || movesCount === 0) {
    ctx.font = "16px Arial"; ctx.fillStyle = "#333"; ctx.textAlign = "center";
    ctx.fillText("Brak prawidłowych ruchów G-code w pliku", canvas.width / 2, canvas.height / 2);
    return;
  }

  // --- Calculate scale and offset (once) ---
  const canvasWidth = canvas.width - 40; const canvasHeight = canvas.height - 40;
  const contentWidth = maxX - minX || 1; const contentHeight = maxY - minY || 1;
  const scaleX = canvasWidth / contentWidth; const scaleY = canvasHeight / contentHeight;
  const scale = Math.min(scaleX, scaleY);
  const offsetX = (canvas.width - contentWidth * scale) / 2;
  const offsetY = (canvas.height - contentHeight * scale) / 2;

  // --- Draw static elements (grid, axes) ---
  drawGrid(ctx, canvas.width, canvas.height, 20, "#E0E0E0");
  drawAxes(ctx, canvas.width, canvas.height);

  // --- Phase 2: Render points in chunks ---
  const chunkSize = 100; // Process 100 points before yielding
  ctx.lineWidth = 2;
  
  let firstMoveInPath = true; // Tracks if this is the first move for the current path segment

  // Start the main path for the drawing.
  // Individual segments will be stroked, but this helps manage state.
  ctx.beginPath(); 

  for (let i = 0; i < points.length; i++) {
    if (abortSignal.aborted) {
      console.log("Chunked rendering loop aborted.");
      ctx.stroke(); // Draw whatever has been processed so far
      return;
    }

    const point = points[i];
    const x = (point.x - minX) * scale + offsetX;
    const y = canvas.height - ((point.y - minY) * scale + offsetY);

    if (firstMoveInPath) {
      ctx.strokeStyle = point.rapid ? "#FF5722" : "#1E88E5";
      ctx.moveTo(x, y);
      firstMoveInPath = false;
    } else {
      const prevPoint = points[i - 1];
      // If color/type changes OR current move is rapid (G0)
      if (point.rapid || point.rapid !== prevPoint.rapid) {
        ctx.stroke(); // Finish previous segment
        ctx.beginPath(); // Start new segment
        // Move to the end of the previous segment (which is prevPoint's screen coordinates)
        const prevScreenX = (prevPoint.x - minX) * scale + offsetX;
        const prevScreenY = canvas.height - ((prevPoint.y - minY) * scale + offsetY);
        ctx.moveTo(prevScreenX, prevScreenY);
      }
      
      ctx.strokeStyle = point.rapid ? "#FF5722" : "#1E88E5";
      if (point.rapid) { // If it's a rapid move
        ctx.moveTo(x, y); // Just move, don't draw line
      } else { // If it's a cutting move
        ctx.lineTo(x, y);
      }
    }
    
    // Stroke the current segment (line or move for G0) before drawing the dot
    ctx.stroke(); 

    // Draw the point marker (small circle)
    ctx.fillStyle = point.rapid ? "#FF5722" : "#1E88E5";
    ctx.beginPath(); // New path for the arc
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();

    // Prepare for the next segment by ensuring the path's current position is this point
    // This makes the current point the start of the next potential lineTo or moveTo
    ctx.beginPath(); 
    ctx.moveTo(x, y);

    // Yield to browser every `chunkSize` points
    if ((i + 1) % chunkSize === 0 && i < points.length -1) { // Avoid yielding after the very last point
      await new Promise(resolve => setTimeout(resolve, 10));
      if (abortSignal.aborted) { // Check again after yield
          console.log("Chunked rendering aborted after yield.");
          ctx.stroke(); // Draw what has been processed so far
          return;
      }
    }
  }
  // Final stroke for the very last segment (might be redundant if last point was G0).
  ctx.stroke(); 

  if (abortSignal.aborted) {
      console.log("Processing aborted before drawing legend/start point.");
      return;
  }

  // Add start indicator
  if (points.length > 0) {
    const startX = (points[0].x - minX) * scale + offsetX;
    const startY = canvas.height - ((points[0].y - minY) * scale + offsetY);
    ctx.beginPath(); ctx.fillStyle = "#4CAF50"; ctx.arc(startX, startY, 5, 0, 2 * Math.PI); ctx.fill();
    ctx.font = "12px Arial"; ctx.fillStyle = "#000"; ctx.fillText("Start", startX + 10, startY);
  }

  // Add coordinate range info
  const infoText = `X: ${minX.toFixed(2)} do ${maxX.toFixed(2)}, Y: ${minY.toFixed(2)} do ${maxY.toFixed(2)}`;
  ctx.font = "12px Arial"; ctx.fillStyle = "#000"; ctx.textAlign = "left"; ctx.fillText(infoText, 10, 20);

  // Add legend
  const legendY = canvas.height - 20;
  ctx.beginPath(); ctx.strokeStyle = "#1E88E5"; ctx.lineWidth = 2; ctx.moveTo(10, legendY); ctx.lineTo(40, legendY); ctx.stroke();
  ctx.font = "12px Arial"; ctx.fillStyle = "#000"; ctx.textAlign = "left"; ctx.fillText("Ruch tnący (G1)", 45, legendY + 4);
  ctx.beginPath(); ctx.strokeStyle = "#FF5722"; ctx.lineWidth = 2; ctx.moveTo(150, legendY); ctx.lineTo(180, legendY); ctx.stroke();
  ctx.fillText("Szybki ruch (G0)", 185, legendY + 4);
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

// Usunięcie pliku
function deleteFile(filename) {
  if (!confirm(`Czy na pewno chcesz usunąć plik "${filename}"?`)) return; // Translated confirm
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
          // If the deleted file was being previewed, abort its generation
          if (gcodePreviewAbortController) {
            gcodePreviewAbortController.abort();
            gcodePreviewAbortController = null; // Reset controller
          }
          // Wyczyść canvas w modalu, jeśli był to podglądany plik
          const modalCanvas = document.getElementById("gcodePreviewCanvas");
          if (modalCanvas) {
            const ctxModal = modalCanvas.getContext("2d");
            ctxModal.clearRect(0, 0, modalCanvas.width, modalCanvas.height);
          }
          // Ukryj kontener podglądu w modalu i pokaż (pusty) wskaźnik ładowania, jeśli modal jest otwarty
          const modalPreviewContainer = document.getElementById("gCodePreviewModal")?.querySelector(".gcode-preview-container");
          const modalLoadingIndicator = document.getElementById("previewLoadingIndicator");
          if (modalPreviewContainer) modalPreviewContainer.style.display = "none";
          if (modalLoadingIndicator && document.getElementById("gCodePreviewModal")?.classList.contains('show')) {
            modalLoadingIndicator.style.display = "block";
            modalLoadingIndicator.innerHTML = "<p>Plik został usunięty. Wybierz inny plik do podglądu.</p>";
          }
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
    uploadMessage.textContent = "Proszę wybrać plik."; // Translated
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
      uploadMessage.textContent = "Plik przesłany pomyślnie!"; // Translated
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
      uploadMessage.textContent = "Przesyłanie nie powiodło się: " + error.message; // Translated
      uploadMessage.className = "alert alert-danger";
      uploadMessage.style.display = "block";
      progressContainer.style.display = "none";
    });
}

// --- Initialization ---

document.addEventListener("DOMContentLoaded", () => {
  fetchFileList();
  handleEventSource();
  document.getElementById("uploadForm").addEventListener("submit", (event) => {
    event.preventDefault();
    uploadFile();
  });

  // Add event listener for G-Code Preview Modal close
  const gCodePreviewModalElement = document.getElementById('gCodePreviewModal');
  if (gCodePreviewModalElement) {
    gCodePreviewModalElement.addEventListener('hide.bs.modal', () => {
      if (gcodePreviewAbortController) {
        gcodePreviewAbortController.abort();
        console.log("G-Code preview generation aborted by modal close.");
        // No need to reset gcodePreviewAbortController here, previewFile will create a new one.
      }
    });
  }
});
