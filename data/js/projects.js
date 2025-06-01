/*
===============================================================================
                   ESP32 CNC CONTROLLER - ZARZĄDZANIE PROJEKTAMI
===============================================================================
Plik obsługuje stronę zarządzania projektami G-code (projects.html):
- Wyświetlanie listy plików z karty SD
- Podgląd i wizualizacja ścieżek G-code na canvasie
- Upload nowych plików na kartę SD
- Usuwanie plików i wybór aktywnego projektu
- Asynchroniczne przetwarzanie dużych plików G-code

Autor: ESP32 CNC Controller Project
===============================================================================
*/

// ===============================================================================
// ZMIENNE GLOBALNE - Stan aplikacji i kontrola operacji
// ===============================================================================
let selectedFilename = null;        // Aktualnie wybrany plik do wykonania
let eventSource;                    // Połączenie EventSource z serwerem
let gcodePreviewAbortController = null;  // Kontrola anulowania podglądu G-code

// ===============================================================================
// ZARZĄDZANIE WYBOREM PLIKU - Funkcje obsługi aktywnego projektu
// ===============================================================================

/**
 * Ustawienie wybranego pliku jako aktywny projekt
 * @param {string} filename - Nazwa pliku do ustawienia lub null do wyczyszczenia
 */
function setSelectedFile(filename) {
  selectedFilename = filename;
  localStorage.setItem("selectedFile", filename || "");
  // Synchronizacja z interfejsem użytkownika
  const radioButton = document.querySelector(`input[value="${filename}"]`);
  if (radioButton) radioButton.checked = true;
  // Aktywacja przycisku potwierdzenia wyboru
  const confirmBtn = document.getElementById("confirmFileBtn");
  if (confirmBtn) confirmBtn.disabled = !filename;
}

/**
 * Nawiązanie połączenia EventSource z automatycznym ponownym łączeniem
 */
function handleEventSource() {
  if (eventSource) eventSource.close();
  eventSource = new EventSource("/events");
  eventSource.onopen = () => console.log("EventSource connection established");
  eventSource.onerror = () => {
    console.error("EventSource error");
    setTimeout(handleEventSource, 5000);
  };
}

// ===============================================================================
// LISTA PLIKÓW - Funkcje zarządzania wyświetlaniem plików z karty SD
// ===============================================================================

/**
 * Aktualizacja wyświetlanej listy plików w tabeli
 * @param {Array} files - Tablica nazw plików do wyświetlenia
 */

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
  // Uporządkowanie plików alfabetycznie
  files.sort();
  for (const file of files) {
    const row = document.createElement("tr");

    // Kolumna wyboru pliku z elementem radio
    const fileNameCell = document.createElement("td");
    fileNameCell.innerHTML = `
      <div class="form-check">
        <input class="form-check-input" type="radio" name="selectedFile" id="file-${file}" value="${file}" 
          ${selectedFilename === file ? "checked" : ""}>
        <label class="form-check-label" for="file-${file}">${file}</label>
      </div>
    `;

    // Kolumna akcji dla operacji na plikach
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

    // Obsługa zmiany wyboru pliku
    row.querySelector(`input[value="${file}"]`).addEventListener("change", () => {
      setSelectedFile(file);
    });
  }
}

/**
 * Pobieranie aktualnej listy plików z serwera
 */
function fetchFileList() {
  fetch("/api/list-files")
    .then((response) => response.json())
    .then((data) => {
      if (data && data.success && Array.isArray(data.files)) {
        updateFileList(data.files);
        const storedFile = localStorage.getItem("selectedFile");
        // Przywrócenie poprzednio wybranego pliku jeśli nadal istnieje
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

/**
 * Odświeżenie listy plików z wymuszeniem ponownego skanowania karty SD
 */
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

/**
 * Potwierdzenie wyboru pliku i ustawienie go jako aktywny projekt
 */
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

/**
 * Wyświetlenie zawartości G-code w modalnym oknie
 * @param {string} filename - Nazwa pliku do wyświetlenia
 */
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

// ===============================================================================
// WIZUALIZACJA G-CODE - Podgląd ścieżek i analiza plików
// ===============================================================================

/**
 * Generowanie wizualnego podglądu ścieżki G-code z obsługą anulowania
 * @param {string} filename - Nazwa pliku do wizualizacji
 */
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
        // Obsługa anulowania operacji podczas przetwarzania
        if (loadingIndicator) loadingIndicator.style.display = "none";
        // Wyświetlenie komunikatu o anulowaniu na canvasie
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.font = "16px Arial";
        ctx.fillStyle = "#333";
        ctx.textAlign = "center";
        ctx.fillText("Generowanie podglądu anulowane.", canvas.width / 2, canvas.height / 2);
        if (previewContainer) previewContainer.style.display = "block";
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

/**
 * Funkcja zastępcza dla kompatybilności - zalecane używanie previewFile()
 * @param {string} filename - Nazwa pliku do wizualizacji
 */
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

/**
 * Asynchroniczne przetwarzanie i renderowanie G-code z obsługą anulowania
 * @param {string} gcode - Zawartość pliku G-code
 * @param {string} canvasId - ID elementu canvas do renderowania
 * @param {AbortSignal} abortSignal - Sygnał anulowania operacji
 */
async function processGCodeChunked(gcode, canvasId, abortSignal) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) {
    console.error("Canvas element with ID '" + canvasId + "' not found.");
    // Aktualizacja wskaźnika ładowania w przypadku błędu elementu canvas
    const loadingIndicator = document.getElementById("previewLoadingIndicator");
    if (loadingIndicator && document.getElementById("gCodePreviewModal")?.classList.contains('show')) {
        loadingIndicator.innerHTML = `<p class="text-danger">Błąd wewnętrzny: Nie znaleziono elementu canvas podglądu.</p>`;
    }
    return;
  }
  const ctx = canvas.getContext("2d");
  const gcodeLines = gcode.split("\n");

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // === FAZA 1: Parsowanie G-code i wyznaczenie granic ===
  let currentX = 0, currentY = 0;
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const points = [];
  let movesCount = 0;

  for (const lineContent of gcodeLines) {
    if (abortSignal.aborted) { console.log("Parsowanie anulowane"); return; }
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

  if (abortSignal.aborted) { console.log("Przetwarzanie anulowane po parsowaniu punktów."); return; }

  if (points.length === 0 || movesCount === 0) {
    ctx.font = "16px Arial"; ctx.fillStyle = "#333"; ctx.textAlign = "center";
    ctx.fillText("Brak prawidłowych ruchów G-code w pliku", canvas.width / 2, canvas.height / 2);
    return;
  }

  // === Obliczenie skali i przesunięcia dla dopasowania do canvasa ===
  const canvasWidth = canvas.width - 40; const canvasHeight = canvas.height - 40;
  const contentWidth = maxX - minX || 1; const contentHeight = maxY - minY || 1;
  const scaleX = canvasWidth / contentWidth; const scaleY = canvasHeight / contentHeight;
  const scale = Math.min(scaleX, scaleY);
  const offsetX = (canvas.width - contentWidth * scale) / 2;
  const offsetY = (canvas.height - contentHeight * scale) / 2;

  // === Rysowanie elementów pomocniczych (siatka, osie) ===
  drawGrid(ctx, canvas.width, canvas.height, 20, "#E0E0E0");
  drawAxes(ctx, canvas.width, canvas.height);

  // === FAZA 2: Renderowanie punktów w kawałkach ===
  const chunkSize = 100; // Przetwarzanie 100 punktów przed oddaniem kontroli
  ctx.lineWidth = 2;
  
  let firstMoveInPath = true; // Śledzenie pierwszego ruchu w segmencie ścieżki

  // Rozpoczęcie głównej ścieżki rysowania
  ctx.beginPath(); 

  for (let i = 0; i < points.length; i++) {
    if (abortSignal.aborted) {
      console.log("Pętla renderowania anulowana.");
      ctx.stroke(); // Narysuj to co zostało przetworzone
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
      // Zmiana koloru/typu ALBO aktualny ruch to szybki ruch (G0)
      if (point.rapid || point.rapid !== prevPoint.rapid) {
        ctx.stroke(); // Zakończ poprzedni segment
        ctx.beginPath(); // Rozpocznij nowy segment
        // Przenieś do końca poprzedniego segmentu (współrzędne ekranowe prevPoint)
        const prevScreenX = (prevPoint.x - minX) * scale + offsetX;
        const prevScreenY = canvas.height - ((prevPoint.y - minY) * scale + offsetY);
        ctx.moveTo(prevScreenX, prevScreenY);
      }
      
      ctx.strokeStyle = point.rapid ? "#FF5722" : "#1E88E5";
      if (point.rapid) { // Jeśli to szybki ruch
        ctx.moveTo(x, y); // Tylko przenieś, nie rysuj linii
      } else { // Jeśli to ruch tnący
        ctx.lineTo(x, y);
      }
    }
    
    // Narysuj aktualny segment przed dodaniem markera punktu
    ctx.stroke(); 

    // Narysuj marker punktu (małe kółko)
    ctx.fillStyle = point.rapid ? "#FF5722" : "#1E88E5";
    ctx.beginPath(); // Nowa ścieżka dla okręgu
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();

    // Przygotuj do następnego segmentu - ustaw aktualną pozycję na ten punkt
    ctx.beginPath(); 
    ctx.moveTo(x, y);

    // Oddaj kontrolę przeglądarce co `chunkSize` punktów
    if ((i + 1) % chunkSize === 0 && i < points.length -1) { // Unikaj oddawania po ostatnim punkcie
      await new Promise(resolve => setTimeout(resolve, 10));
      if (abortSignal.aborted) { // Sprawdź ponownie po oddaniu kontroli
          console.log("Renderowanie anulowane po oddaniu kontroli.");
          ctx.stroke(); // Narysuj to co zostało przetworzone
          return;
      }
    }
  }
  // Finalne narysowanie ostatniego segmentu
  ctx.stroke(); 

  if (abortSignal.aborted) {
      console.log("Przetwarzanie anulowane przed rysowaniem legendy/punktu startowego.");
      return;
  }

  // Dodanie wskaźnika punktu startowego
  if (points.length > 0) {
    const startX = (points[0].x - minX) * scale + offsetX;
    const startY = canvas.height - ((points[0].y - minY) * scale + offsetY);
    ctx.beginPath(); ctx.fillStyle = "#4CAF50"; ctx.arc(startX, startY, 5, 0, 2 * Math.PI); ctx.fill();
    ctx.font = "12px Arial"; ctx.fillStyle = "#000"; ctx.fillText("Start", startX + 10, startY);
  }

  // Dodanie informacji o zakresie współrzędnych
  const infoText = `X: ${minX.toFixed(2)} do ${maxX.toFixed(2)}, Y: ${minY.toFixed(2)} do ${maxY.toFixed(2)}`;
  ctx.font = "12px Arial"; ctx.fillStyle = "#000"; ctx.textAlign = "left"; ctx.fillText(infoText, 10, 20);

  // Dodanie legendy kolorów
  const legendY = canvas.height - 20;
  ctx.beginPath(); ctx.strokeStyle = "#1E88E5"; ctx.lineWidth = 2; ctx.moveTo(10, legendY); ctx.lineTo(40, legendY); ctx.stroke();
  ctx.font = "12px Arial"; ctx.fillStyle = "#000"; ctx.textAlign = "left"; ctx.fillText("Ruch tnący (G1)", 45, legendY + 4);
  ctx.beginPath(); ctx.strokeStyle = "#FF5722"; ctx.lineWidth = 2; ctx.moveTo(150, legendY); ctx.lineTo(180, legendY); ctx.stroke();
  ctx.fillText("Szybki ruch (G0)", 185, legendY + 4);
}

/**
 * Generowanie siatki pomocniczej na canvasie
 * @param {CanvasRenderingContext2D} ctx - Kontekst 2D canvasa
 * @param {number} width - Szerokość canvasa
 * @param {number} height - Wysokość canvasa
 * @param {number} step - Odstęp między liniami siatki
 * @param {string} color - Kolor linii siatki
 */
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

/**
 * Rysowanie głównych osi układu współrzędnych
 * @param {CanvasRenderingContext2D} ctx - Kontekst 2D canvasa
 * @param {number} width - Szerokość canvasa
 * @param {number} height - Wysokość canvasa
 */
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

// ===============================================================================
// OPERACJE NA PLIKACH - Usuwanie i przesyłanie plików
// ===============================================================================

/**
 * Usunięcie wybranego pliku z karty SD z potwierdzeniem
 * @param {string} filename - Nazwa pliku do usunięcia
 */
function deleteFile(filename) {
  if (!confirm(`Czy na pewno chcesz usunąć plik "${filename}"?`)) return;
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
          // Anulowanie generowania podglądu jeśli usuwany plik był podglądany
          if (gcodePreviewAbortController) {
            gcodePreviewAbortController.abort();
            gcodePreviewAbortController = null;
          }
          // Wyczyszczenie canvasa w modalu dla usuniętego pliku
          const modalCanvas = document.getElementById("gcodePreviewCanvas");
          if (modalCanvas) {
            const ctxModal = modalCanvas.getContext("2d");
            ctxModal.clearRect(0, 0, modalCanvas.width, modalCanvas.height);
          }
          // Ukrycie kontenera podglądu i wyświetlenie komunikatu o usunięciu
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

/**
 * Przesłanie nowego pliku G-code na kartę SD z paskiem postępu
 */
function uploadFile() {
  const fileInput = document.getElementById("fileInput");
  const progressContainer = document.getElementById("progress-container");
  const progressBar = document.getElementById("progress-bar");
  const uploadMessage = document.getElementById("upload-message");
  if (fileInput.files.length === 0) {
    uploadMessage.textContent = "Proszę wybrać plik.";
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
      uploadMessage.textContent = "Plik przesłany pomyślnie!";
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
      uploadMessage.textContent = "Przesyłanie nie powiodło się: " + error.message;
      uploadMessage.className = "alert alert-danger";
      uploadMessage.style.display = "block";
      progressContainer.style.display = "none";
    });
}

// ===============================================================================
// INICJALIZACJA - Konfiguracja strony i obsługa zdarzeń
// ===============================================================================

document.addEventListener("DOMContentLoaded", () => {
  fetchFileList();
  handleEventSource();
  document.getElementById("uploadForm").addEventListener("submit", (event) => {
    event.preventDefault();
    uploadFile();
  });

  // Obsługa zamknięcia modala podglądu G-Code
  const gCodePreviewModalElement = document.getElementById('gCodePreviewModal');
  if (gCodePreviewModalElement) {
    gCodePreviewModalElement.addEventListener('hide.bs.modal', () => {
      if (gcodePreviewAbortController) {
        gcodePreviewAbortController.abort();
        console.log("Generowanie podglądu G-Code anulowane przez zamknięcie modala.");
      }
    });
  }
});
