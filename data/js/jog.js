/**
 * JavaScript for JOG control page (jog.html)
 */

// Aktualne położenie maszyny
let currentPosition = { x: 0, y: 0 };

// Aktualizacja położenia
function updatePosition() {
  fetch('/api/position')
    .then(response => response.json())
    .catch(() => {
      // Fallback - symulowane położenie
      return { 
        x: currentPosition.x, 
        y: currentPosition.y 
      };
    })
    .then(data => {
      currentPosition.x = parseFloat(data.x);
      currentPosition.y = parseFloat(data.y);
      
      // Aktualizuj wyświetlane położenie
      document.getElementById('position-x').textContent = currentPosition.x.toFixed(3);
      document.getElementById('position-y').textContent = currentPosition.y.toFixed(3);
    });
}

// Funkcja ruchu JOG
function jog(xDir, yDir) {
  // Pobierz wartość odległości z radio buttonów
  const distanceRadio = document.querySelector('input[name="jogDistance"]:checked');
  if (!distanceRadio) return;
  
  const distance = parseFloat(distanceRadio.value);
  
  // Pobierz prędkość z listy rozwijanej
  const speedSelect = document.getElementById('jogSpeed');
  const speed = parseInt(speedSelect.value);
  
  // Oblicz przesunięcie
  const xOffset = xDir * distance;
  const yOffset = yDir * distance;
  
  // Wyślij polecenie JOG
  fetch('/api/jog', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      x: xOffset,
      y: yOffset,
      speed: speed
    })
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('JOG command failed');
    }
    return response.json();
  })
  .then(data => {
    // Symuluj aktualizację położenia (zostanie zastąpione przez rzeczywiste położenie w updatePosition)
    currentPosition.x += xOffset;
    currentPosition.y += yOffset;
    
    // Aktualizuj wyświetlane położenie
    document.getElementById('position-x').textContent = currentPosition.x.toFixed(3);
    document.getElementById('position-y').textContent = currentPosition.y.toFixed(3);
    
    // Wyświetl komunikat
    if (xOffset !== 0 || yOffset !== 0) {
      let moveText = '';
      if (xOffset !== 0) moveText += `X${xOffset > 0 ? '+' : ''}${xOffset}`;
      if (xOffset !== 0 && yOffset !== 0) moveText += ', ';
      if (yOffset !== 0) moveText += `Y${yOffset > 0 ? '+' : ''}${yOffset}`;
      
      showMessage(`Movement: ${moveText} at ${speed} mm/min`);
    }
  })
  .catch(error => {
    console.error('JOG error:', error);
    showMessage('Error executing JOG command: ' + error.message, 'error');
  });
}

// Funkcja zerowania pozycji
function zeroAxes() {
  fetch('/api/zero', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Zero command failed');
    }
    return response.json();
  })
  .then(data => {
    // Aktualizuj pozycję na 0,0
    currentPosition.x = 0;
    currentPosition.y = 0;
    
    document.getElementById('position-x').textContent = '0.000';
    document.getElementById('position-y').textContent = '0.000';
    
    showMessage('Position zeroed successfully');
  })
  .catch(error => {
    console.error('Zero error:', error);
    showMessage('Error zeroing position: ' + error.message, 'error');
  });
}

// Funkcja bazowania
function homeAxes() {
  if (!confirm('Are you sure you want to home the machine? The machine will move to home position.')) {
    return;
  }
  
  fetch('/api/home', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Home command failed');
    }
    return response.json();
  })
  .then(data => {
    showMessage('Homing procedure started');
  })
  .catch(error => {
    console.error('Home error:', error);
    showMessage('Error homing machine: ' + error.message, 'error');
  });
}

// Funkcja zatrzymania awaryjnego
function emergencyStop() {
  fetch('/api/emergency-stop', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Emergency stop command failed');
    }
    return response.json();
  })
  .then(data => {
    showMessage('Emergency stop triggered!', 'warning');
  })
  .catch(error => {
    console.error('Emergency stop error:', error);
    showMessage('Error triggering emergency stop: ' + error.message, 'error');
  });
}

// Funkcja włączania/wyłączania drutu grzejnego
function toggleWire() {
  const wireSwitch = document.getElementById('wireSwitch');
  const wireState = wireSwitch.checked;
  
  fetch('/api/wire', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      state: wireState
    })
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Wire control command failed');
      wireSwitch.checked = !wireState; // Przywróć poprzedni stan przełącznika
    }
    return response.json();
  })
  .then(data => {
    showMessage(`Wire heating ${wireState ? 'enabled' : 'disabled'}`);
  })
  .catch(error => {
    console.error('Wire control error:', error);
    showMessage('Error controlling wire: ' + error.message, 'error');
    wireSwitch.checked = !wireState; // Przywróć poprzedni stan przełącznika
  });
}

// Funkcja włączania/wyłączania wentylatora
function toggleFan() {
  const fanSwitch = document.getElementById('fanSwitch');
  const fanState = fanSwitch.checked;
  
  fetch('/api/fan', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      state: fanState
    })
  })
  .then(response => {
    if (!response.ok) {
      throw new Error('Fan control command failed');
      fanSwitch.checked = !fanState; // Przywróć poprzedni stan przełącznika
    }
    return response.json();
  })
  .then(data => {
    showMessage(`Fan ${fanState ? 'enabled' : 'disabled'}`);
  })
  .catch(error => {
    console.error('Fan control error:', error);
    showMessage('Error controlling fan: ' + error.message, 'error');
    fanSwitch.checked = !fanState; // Przywróć poprzedni stan przełącznika
  });
}

// Obsługa sterowania klawiaturą
function handleKeyboardControl(event) {
  // Nie wykonuj akcji, jeśli focus jest na polu formularza
  if (event.target.tagName === 'INPUT' || event.target.tagName === 'SELECT' || event.target.tagName === 'TEXTAREA') {
    return;
  }
  
  switch (event.key) {
    case 'ArrowUp':
      event.preventDefault();
      jog(0, 1);
      break;
    case 'ArrowDown':
      event.preventDefault();
      jog(0, -1);
      break;
    case 'ArrowLeft':
      event.preventDefault();
      jog(-1, 0);
      break;
    case 'ArrowRight':
      event.preventDefault();
      jog(1, 0);
      break;
    case 'Home':
      event.preventDefault();
      homeAxes();
      break;
    case ' ': // Spacja
      event.preventDefault();
      emergencyStop();
      break;
    case 'PageUp':
      event.preventDefault();
      // Zwiększ odległość ruchu
      selectNextDistance(1);
      break;
    case 'PageDown':
      event.preventDefault();
      // Zmniejsz odległość ruchu
      selectNextDistance(-1);
      break;
  }
}

// Funkcja zmiany odległości ruchu
function selectNextDistance(direction) {
  const distanceRadios = document.querySelectorAll('input[name="jogDistance"]');
  const currentRadio = document.querySelector('input[name="jogDistance"]:checked');
  
  if (!currentRadio) return;
  
  let currentIndex = Array.from(distanceRadios).indexOf(currentRadio);
  let nextIndex = currentIndex + direction;
  
  // Zapętl indeks, jeśli wychodzi poza zakres
  if (nextIndex < 0) nextIndex = distanceRadios.length - 1;
  if (nextIndex >= distanceRadios.length) nextIndex = 0;
  
  // Zaznacz nowy radio button
  distanceRadios[nextIndex].checked = true;
  
  // Wyświetl komunikat
  showMessage(`Movement distance set to ${distanceRadios[nextIndex].value} mm`);
}

// Funkcja aktualizacji stanu przełączników na podstawie stanu maszyny
function updateSwitchStates() {
  fetch('/api/machine-status')
    .then(response => response.json())
    .catch(() => {
      // Fallback - symulowany stan
      return { 
        wireOn: document.getElementById('wireSwitch').checked,
        fanOn: document.getElementById('fanSwitch').checked
      };
    })
    .then(data => {
      document.getElementById('wireSwitch').checked = data.wireOn;
      document.getElementById('fanSwitch').checked = data.fanOn;
    });
}

// Inicjalizacja po załadowaniu strony
document.addEventListener('DOMContentLoaded', () => {
  // Aktualizuj pozycję
  updatePosition();
  
  // Aktualizuj stan przełączników
  updateSwitchStates();
  
  // Ustaw okresową aktualizację pozycji co 1 sekundę
  setInterval(updatePosition, 1000);
  
  // Ustaw okresową aktualizację stanu przełączników co 2 sekundy
  setInterval(updateSwitchStates, 2000);
  
  // Dodaj obsługę klawiatury
  document.addEventListener('keydown', handleKeyboardControl);
});
