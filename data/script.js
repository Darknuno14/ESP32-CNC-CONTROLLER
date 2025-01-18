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

  // Send to server and update <p id="selected-file">
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