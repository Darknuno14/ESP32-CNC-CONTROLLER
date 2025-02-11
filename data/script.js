function sendButtonPress() {
    fetch('/api/button', {
      method: 'POST',
    })
    .then(response => console.log('Button press sent'))
    .catch(error => console.error('Error:', error));
  }
  
  // Function to update the file list on the webpage
  function updateFileList(files) {
    const fileListElement = document.getElementById('file-list');
    fileListElement.innerHTML = ''; // Clear existing list items
  
    if (!files || files.length === 0) {
      const listItem = document.createElement('li');
      listItem.textContent = 'No files found on SD card.';
      fileListElement.appendChild(listItem);
      return;
    }
  
    for (const file of files) {
      const listItem = document.createElement('li');
      listItem.textContent = file;
      fileListElement.appendChild(listItem);
    }
  }

  // Fetch file list from the server upon page load
  fetch('/api/sd-files')
    .then(response => response.json())
    .then(data => updateFileList(data))
    .catch(error => console.error('Error fetching file list:', error));


    fetch('/api/sd-files')
  .then(response => response.json())
  .then(data => updateFileList(data))
  .catch(error => console.error('Error fetching file list:', error));

document.getElementById('uploadForm').addEventListener('submit', function(event) {
    event.preventDefault(); // Zapobiegamy domyślnemu zachowaniu formularza (przeładowaniu strony)

    const fileInput = document.getElementById('fileInput');
    const progressBarContainer = document.getElementById('progress-container');
    const progressBar = document.querySelector('#progress-bar .progress-bar');
    const uploadMessage = document.getElementById('upload-message');

    if (fileInput.files.length === 0) {
        uploadMessage.textContent = "Please select a file.";
        return;
    }

    const file = fileInput.files[0];
    const formData = new FormData();
    formData.append('file', file);

    progressBarContainer.style.display = 'block'; // Pokaż pasek postępu
    progressBar.style.width = '0%';
    progressBar.textContent = '0%';
    uploadMessage.textContent = "";

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
        fetch('/api/sd-files')
          .then(response => response.json())
          .then(data => updateFileList(data))
          .catch(error => console.error('Error fetching file list:', error));
    })
    .catch(error => {
        console.error('Upload error:', error);
        uploadMessage.textContent = "Upload failed: " + error;
        progressBarContainer.style.display = 'none';
    });
});

function updateFileList(files) {
  const fileListElement = document.getElementById('file-list');
  fileListElement.innerHTML = '';
  files.forEach((file, index) => {
    // Create radio buttons for each file
    const li = document.createElement('li');
    li.innerHTML = `
      <input type="radio" name="selectedFile" id="file-${index}" value="${file}">
      <label for="file-${index}">${file}</label>
    `;
    fileListElement.appendChild(li);
  });
}

function submitFileSelection() {
    const radios = document.getElementsByName('selectedFile');
    let selectedFilename = null;
    
    for (let i = 0; i < radios.length; i++) {
        if (radios[i].checked) {
            selectedFilename = radios[i].value;
            break;
        }
    }
    
    if (!selectedFilename) {
        alert('Please select a file first.');
        return;
    }

    // Fix: Send filename directly in URL-encoded form
    fetch('/api/select-file?file=' + encodeURIComponent(selectedFilename), {
        method: 'POST'
    })
    .then(response => response.text())
    .then(data => {
        console.log('File selected:', data);
        document.getElementById('selected-file').textContent = `Selected file: ${selectedFilename}`;
    })
    .catch(error => {
        console.error('Selection error:', error);
        document.getElementById('selected-file').textContent = 'Error selecting file';
    });
}

function refreshFileList() {
    // First trigger manual refresh on ESP32
    fetch('/api/refresh-files', { 
        method: 'POST' 
    })
    .then(response => {
        if (!response.ok) {
            throw new Error('Failed to refresh files on ESP32');
        }
        // Then get the updated file list
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
    })
    .catch(error => {
        alert('Failed to refresh file list: ' + error.message);
    });
}

function visualizeSelectedGCode() {
    const radios = document.getElementsByName('selectedFile');
    let selectedFilename = null;

    for (let i = 0; i < radios.length; i++) {
        if (radios[i].checked) {
            selectedFilename = radios[i].value;
            break;
        }
    }

    if (!selectedFilename) {
        alert('Please select a file first.');
        return;
    }

    // Fix: Use correct endpoint format
    fetch('/api/preview?file=' + encodeURIComponent(selectedFilename))
        .then(response => {
            if (!response.ok) throw new Error('Failed to fetch file');
            return response.text();
        })
        .then(gcode => visualizeGCode(gcode))
        .catch(error => {
            console.error('Error fetching G-Code file:', error);
            alert('Failed to fetch G-Code file: ' + error.message);
        });
}

function visualizeGCode(gcode) {
  const canvas = document.getElementById('canvas');
  const ctx = canvas.getContext('2d');
  const gcodeLines = gcode.split('\n');

  // Clear canvas
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Set up drawing parameters
  ctx.strokeStyle = '#000';
  ctx.lineWidth = 1;

  // Initialize coordinates
  let currentX = 0;
  let currentY = 0;
  let firstMove = true;

  // Find min/max coordinates for scaling
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const points = [];

  // First pass to collect all points
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

  // Calculate scaling factors
  const canvasWidth = canvas.width - 20;
  const canvasHeight = canvas.height - 20;
  const contentWidth = maxX - minX;
  const contentHeight = maxY - minY;

  const scaleX = contentWidth ? canvasWidth / contentWidth : 1;
  const scaleY = contentHeight ? canvasHeight / contentHeight : 1;
  const scale = Math.min(scaleX, scaleY);

  // Calculate offset to center the drawing
  const offsetX = (canvas.width - (contentWidth * scale)) / 2 - minX * scale;
  const offsetY = (canvas.height - (contentHeight * scale)) / 2 - minY * scale;

  // Second pass to draw
  ctx.beginPath();
  points.forEach(point => {
    const x = point.x * scale + offsetX;
    const y = canvas.height - (point.y * scale + offsetY); // Flip Y axis

    if (firstMove) {
      ctx.moveTo(x, y);
      firstMove = false;
    } else {
      ctx.lineTo(x, y);
    }
  });
  ctx.stroke();
}

function startProcessing() {
    fetch('/api/start', { method: 'POST' })
        .then(response => response.text())
        .then(data => console.log('Processing started:', data))
        .catch(error => console.error('Start error:', error));
}

function stopProcessing() {
    fetch('/api/stop', { method: 'POST' })
        .then(response => response.text())
        .then(data => console.log('Processing stopped:', data))
        .catch(error => console.error('Stop error:', error));
}