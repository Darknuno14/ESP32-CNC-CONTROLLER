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