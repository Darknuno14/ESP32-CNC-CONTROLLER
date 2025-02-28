/**
 * JavaScript for configuration page (config.html)
 */

// Zmienna do przechowywania EventSource
let eventSource;

// Funkcja do wczytywania konfiguracji z ESP32
function loadConfiguration() {
  // Pokaż spinner ładowania
  document.getElementById('loadingSpinner').style.display = 'inline-block';
  document.getElementById('loadBtn').disabled = true;
  
  fetch('/api/config')
    .then(response => {
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      return response.json();
    })
    .then(config => {
      // Wypełnij formularz danymi konfiguracyjnymi
      if (config.xAxis) {
        document.getElementById('xStepsPerMM').value = config.xAxis.stepsPerMM;
        document.getElementById('xMaxFeedRate').value = config.xAxis.workFeedRate || config.xAxis.maxFeedRate;
        document.getElementById('xMaxAcceleration').value = config.xAxis.workAcceleration || config.xAxis.maxAcceleration;
        document.getElementById('xRapidFeedRate').value = config.xAxis.rapidFeedRate;
        document.getElementById('xRapidAcceleration').value = config.xAxis.rapidAcceleration;
      }
      
      if (config.yAxis) {
        document.getElementById('yStepsPerMM').value = config.yAxis.stepsPerMM;
        document.getElementById('yMaxFeedRate').value = config.yAxis.workFeedRate || config.yAxis.maxFeedRate;
        document.getElementById('yMaxAcceleration').value = config.yAxis.workAcceleration || config.yAxis.maxAcceleration;
        document.getElementById('yRapidFeedRate').value = config.yAxis.rapidFeedRate;
        document.getElementById('yRapidAcceleration').value = config.yAxis.rapidAcceleration;
      }
      
      document.getElementById('useGCodeFeedRate').checked = config.useGCodeFeedRate;
      document.getElementById('delayAfterStartup').value = config.delayAfterStartup;
      
      // Dodatkowe pola, jeśli istnieją
      if ('wireTemperature' in config) {
        document.getElementById('wireTemperature').value = config.wireTemperature;
      }
      if ('enableFan' in config) {
        document.getElementById('enableFan').checked = config.enableFan;
      }
      if ('fanSpeed' in config) {
        document.getElementById('fanSpeed').value = config.fanSpeed;
      }
      if ('deactivateESTOP' in config) {
        document.getElementById('deactivateESTOP').checked = config.deactivateESTOP;
      }
      if ('deactivateLimitSwitches' in config) {
        document.getElementById('deactivateLimitSwitches').checked = config.deactivateLimitSwitches;
      }
      if ('limitSwitchType' in config) {
        document.getElementById('limitSwitchType').value = config.limitSwitchType;
      }
      
      // Wyświetl komunikat o sukcesie
      showMessage('Configuration loaded successfully');
      
      // Włącz przycisk zapisz po załadowaniu konfiguracji
      document.getElementById('saveBtn').disabled = false;
    })
    .catch(error => {
      console.error('Error loading configuration:', error);
      showMessage(`Failed to load configuration: ${error.message}`, 'error');
    })
    .finally(() => {
      // Ukryj spinner ładowania
      document.getElementById('loadingSpinner').style.display = 'none';
      document.getElementById('loadBtn').disabled = false;
    });
}

// Funkcja do zapisywania konfiguracji
function saveConfiguration(event) {
  event.preventDefault();
  
  // Sprawdź walidację formularza
  if (!validateForm()) {
    showMessage('Please correct errors in the form before saving.', 'error');
    return;
  }
  
  // Pokaż spinner zapisywania
  document.getElementById('savingSpinner').style.display = 'inline-block';
  document.getElementById('saveBtn').disabled = true;
  
  // Zbierz dane z formularza
  const formData = new FormData(document.getElementById('configForm'));
  
  // Przygotuj obiekt konfiguracyjny
  const config = {
    xAxis: {
      stepsPerMM: parseFloat(formData.get('xAxis.stepsPerMM')),
      workFeedRate: parseFloat(formData.get('xAxis.maxFeedRate')),
      workAcceleration: parseFloat(formData.get('xAxis.maxAcceleration')),
      rapidFeedRate: parseFloat(formData.get('xAxis.rapidFeedRate')),
      rapidAcceleration: parseFloat(formData.get('xAxis.rapidAcceleration'))
    },
    yAxis: {
      stepsPerMM: parseFloat(formData.get('yAxis.stepsPerMM')),
      workFeedRate: parseFloat(formData.get('yAxis.maxFeedRate')),
      workAcceleration: parseFloat(formData.get('yAxis.maxAcceleration')),
      rapidFeedRate: parseFloat(formData.get('yAxis.rapidFeedRate')),
      rapidAcceleration: parseFloat(formData.get('yAxis.rapidAcceleration'))
    },
    useGCodeFeedRate: formData.get('useGCodeFeedRate') === 'on',
    delayAfterStartup: parseInt(formData.get('delayAfterStartup'))
  };
  
  // Dodaj dodatkowe pola, jeśli istnieją
  if (document.getElementById('wireTemperature')) {
    config.wireTemperature = parseFloat(formData.get('wireTemperature'));
  }
  if (document.getElementById('enableFan')) {
    config.enableFan = formData.get('enableFan') === 'on';
  }
  if (document.getElementById('fanSpeed')) {
    config.fanSpeed = parseInt(formData.get('fanSpeed'));
  }
  if (document.getElementById('deactivateESTOP')) {
    config.deactivateESTOP = formData.get('deactivateESTOP') === 'on';
  }
  if (document.getElementById('deactivateLimitSwitches')) {
    config.deactivateLimitSwitches = formData.get('deactivateLimitSwitches') === 'on';
  }
  if (document.getElementById('limitSwitchType')) {
    config.limitSwitchType = parseInt(formData.get('limitSwitchType'));
  }
  
  // Wyślij dane do ESP32
  fetch('/api/config', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(config)
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return response.json();
  })
  .then(data => {
    if (data.success) {
      showMessage('Configuration saved successfully');
    } else {
      showMessage('Failed to save configuration: ' + (data.message || 'Unknown error'), 'error');
    }
  })
  .catch(error => {
    console.error('Error saving configuration:', error);
    showMessage(`Failed to save configuration: ${error.message}`, 'error');
  })
  .finally(() => {
    // Ukryj spinner zapisywania
    document.getElementById('savingSpinner').style.display = 'none';
    document.getElementById('saveBtn').disabled = false;
  });
}

// Funkcja do resetowania konfiguracji do wartości domyślnych
function resetConfiguration() {
  if (confirm('Are you sure you want to reset all settings to default values?')) {
    // Wartości domyślne
    const defaultConfig = {
      xAxis: {
        stepsPerMM: 200.0,
        workFeedRate: 3000.0,
        workAcceleration: 500.0,
        rapidFeedRate: 5000.0,
        rapidAcceleration: 1000.0
      },
      yAxis: {
        stepsPerMM: 200.0,
        workFeedRate: 3000.0,
        workAcceleration: 500.0,
        rapidFeedRate: 5000.0,
        rapidAcceleration: 1000.0
      },
      useGCodeFeedRate: true,
      delayAfterStartup: 1000,
      wireTemperature: 300.0,
      enableFan: true,
      fanSpeed: 255,
      deactivateESTOP: false,
      deactivateLimitSwitches: false,
      limitSwitchType: 0
    };
    
    // Wypełnij formularz domyślnymi wartościami
    document.getElementById('xStepsPerMM').value = defaultConfig.xAxis.stepsPerMM;
    document.getElementById('xMaxFeedRate').value = defaultConfig.xAxis.workFeedRate;
    document.getElementById('xMaxAcceleration').value = defaultConfig.xAxis.workAcceleration;
    document.getElementById('xRapidFeedRate').value = defaultConfig.xAxis.rapidFeedRate;
    document.getElementById('xRapidAcceleration').value = defaultConfig.xAxis.rapidAcceleration;
    
    document.getElementById('yStepsPerMM').value = defaultConfig.yAxis.stepsPerMM;
    document.getElementById('yMaxFeedRate').value = defaultConfig.yAxis.workFeedRate;
    document.getElementById('yMaxAcceleration').value = defaultConfig.yAxis.workAcceleration;
    document.getElementById('yRapidFeedRate').value = defaultConfig.yAxis.rapidFeedRate;
    document.getElementById('yRapidAcceleration').value = defaultConfig.yAxis.rapidAcceleration;
    
    document.getElementById('useGCodeFeedRate').checked = defaultConfig.useGCodeFeedRate;
    document.getElementById('delayAfterStartup').value = defaultConfig.delayAfterStartup;
    
    // Dodatkowe pola, jeśli istnieją
    if (document.getElementById('wireTemperature')) {
      document.getElementById('wireTemperature').value = defaultConfig.wireTemperature;
    }
    if (document.getElementById('enableFan')) {
      document.getElementById('enableFan').checked = defaultConfig.enableFan;
    }
    if (document.getElementById('fanSpeed')) {
      document.getElementById('fanSpeed').value = defaultConfig.fanSpeed;
    }
    if (document.getElementById('deactivateESTOP')) {
      document.getElementById('deactivateESTOP').checked = defaultConfig.deactivateESTOP;
    }
    if (document.getElementById('deactivateLimitSwitches')) {
      document.getElementById('deactivateLimitSwitches').checked = defaultConfig.deactivateLimitSwitches;
    }
    if (document.getElementById('limitSwitchType')) {
      document.getElementById('limitSwitchType').value = defaultConfig.limitSwitchType;
    }
    
    // Włącz przycisk zapisz
    document.getElementById('saveBtn').disabled = false;
    
    showMessage('Settings reset to defaults (not saved)', 'info');
  }
}

// Funkcja do sprawdzania poprawności wprowadzonych wartości
function validateForm() {
  let isValid = true;
  const inputs = document.querySelectorAll('#configForm input[type="number"]');
  
  inputs.forEach(input => {
    // Usuń poprzednie klasy walidacji
    input.classList.remove('is-invalid');
    
    const value = parseFloat(input.value);
    
    if (isNaN(value)) {
      input.classList.add('is-invalid');
      isValid = false;
    } else {
      // Sprawdź zakres dla konkretnych pól
      if (input.id === 'fanSpeed' && (value < 0 || value > 255)) {
        input.classList.add('is-invalid');
        isValid = false;
      }
      // Sprawdź wartości ujemne dla wszystkich pól oprócz temperatury (która może być ujemna)
      if (input.id !== 'wireTemperature' && value < 0) {
        input.classList.add('is-invalid');
        isValid = false;
      }
    }
  });
  
  return isValid;
}

// Aktualizacja zależności fanSpeed od enableFan
function updateFanSpeedState() {
  const enableFan = document.getElementById('enableFan');
  const fanSpeed = document.getElementById('fanSpeed');
  
  if (enableFan && fanSpeed) {
    fanSpeed.disabled = !enableFan.checked;
  }
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
  const saveBtn = document.getElementById('saveBtn');
  
  // Wyłącz przycisk zapisu gdy maszyna jest zajęta
  if (saveBtn) {
    saveBtn.disabled = (data.state === 1 || data.state === 2 || data.state === 3);
  }
}

// Podłącz zdarzenia po załadowaniu strony
document.addEventListener('DOMContentLoaded', function() {
  // Inicjalizacja EventSource
  initEventSource();
  
  // Obsługa przycisku wczytywania konfiguracji
  document.getElementById('loadBtn').addEventListener('click', loadConfiguration);
  
  // Obsługa zapisywania konfiguracji
  document.getElementById('configForm').addEventListener('submit', saveConfiguration);
  
  // Obsługa resetowania konfiguracji
  document.getElementById('resetBtn').addEventListener('click', resetConfiguration);
  
  // Obsługa zależności fanSpeed od enableFan
  const enableFanCheckbox = document.getElementById('enableFan');
  if (enableFanCheckbox) {
    enableFanCheckbox.addEventListener('change', updateFanSpeedState);
    updateFanSpeedState(); // Inicjalizacja stanu
  }
  
  // Dodaj walidację do wszystkich pól numerycznych
  document.querySelectorAll('#configForm input[type="number"]').forEach(input => {
    input.addEventListener('input', function() {
      // Walidacja podczas wprowadzania danych
      if (parseFloat(this.value) < 0 && this.id !== 'wireTemperature') {
        this.classList.add('is-invalid');
      } else {
        this.classList.remove('is-invalid');
      }
      
      // Specjalna walidacja dla fanSpeed
      if (this.id === 'fanSpeed' && (parseFloat(this.value) < 0 || parseFloat(this.value) > 255)) {
        this.classList.add('is-invalid');
      }
    });
  });
  
  // Wczytaj konfigurację przy załadowaniu strony
  loadConfiguration();
});