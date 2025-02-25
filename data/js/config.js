/**
 * JavaScript for configuration page (config.html)
 */

// Funkcja do wczytywania konfiguracji z ESP32
function loadConfiguration() {
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
        document.getElementById('xMaxFeedRate').value = config.xAxis.maxFeedRate;
        document.getElementById('xMaxAcceleration').value = config.xAxis.maxAcceleration;
        document.getElementById('xRapidFeedRate').value = config.xAxis.rapidFeedRate;
        document.getElementById('xRapidAcceleration').value = config.xAxis.rapidAcceleration;
      }
      
      if (config.yAxis) {
        document.getElementById('yStepsPerMM').value = config.yAxis.stepsPerMM;
        document.getElementById('yMaxFeedRate').value = config.yAxis.maxFeedRate;
        document.getElementById('yMaxAcceleration').value = config.yAxis.maxAcceleration;
        document.getElementById('yRapidFeedRate').value = config.yAxis.rapidFeedRate;
        document.getElementById('yRapidAcceleration').value = config.yAxis.rapidAcceleration;
      }
      
      document.getElementById('useGCodeFeedRate').checked = config.useGCodeFeedRate;
      document.getElementById('delayAfterStartup').value = config.delayAfterStartup;
      document.getElementById('wireTemperature').value = config.wireTemperature;
      document.getElementById('enableFan').checked = config.enableFan;
      document.getElementById('fanSpeed').value = config.fanSpeed;
      
      showMessage('Configuration loaded successfully');
    })
    .catch(error => {
      console.error('Error loading configuration:', error);
      showMessage(`Failed to load configuration: ${error.message}`, 'error');
    });
}

// Funkcja do zapisywania konfiguracji
function saveConfiguration(event) {
  event.preventDefault();
  
  // Zbierz dane z formularza
  const formData = new FormData(document.getElementById('configForm'));
  const config = {
    xAxis: {
      stepsPerMM: parseFloat(formData.get('xAxis.stepsPerMM')),
      maxFeedRate: parseFloat(formData.get('xAxis.maxFeedRate')),
      maxAcceleration: parseFloat(formData.get('xAxis.maxAcceleration')),
      rapidFeedRate: parseFloat(formData.get('xAxis.rapidFeedRate')),
      rapidAcceleration: parseFloat(formData.get('xAxis.rapidAcceleration'))
    },
    yAxis: {
      stepsPerMM: parseFloat(formData.get('yAxis.stepsPerMM')),
      maxFeedRate: parseFloat(formData.get('yAxis.maxFeedRate')),
      maxAcceleration: parseFloat(formData.get('yAxis.maxAcceleration')),
      rapidFeedRate: parseFloat(formData.get('yAxis.rapidFeedRate')),
      rapidAcceleration: parseFloat(formData.get('yAxis.rapidAcceleration'))
    },
    useGCodeFeedRate: formData.get('useGCodeFeedRate') === 'on',
    delayAfterStartup: parseInt(formData.get('delayAfterStartup')),
    wireTemperature: parseFloat(formData.get('wireTemperature')),
    enableFan: formData.get('enableFan') === 'on',
    fanSpeed: parseInt(formData.get('fanSpeed'))
  };
  
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
      showMessage('Failed to save configuration', 'error');
    }
  })
  .catch(error => {
    console.error('Error saving configuration:', error);
    showMessage(`Failed to save configuration: ${error.message}`, 'error');
  });
}

// Funkcja do resetowania konfiguracji do wartości domyślnych
function resetConfiguration() {
  if (confirm('Are you sure you want to reset all settings to default values?')) {
    // Domyślne wartości (takie same jak w ConfigManager::setDefaultConfig)
    const defaultConfig = {
      xAxis: {
        stepsPerMM: 200.0,
        maxFeedRate: 3000.0,
        maxAcceleration: 500.0,
        rapidFeedRate: 5000.0,
        rapidAcceleration: 1000.0
      },
      yAxis: {
        stepsPerMM: 200.0,
        maxFeedRate: 3000.0,
        maxAcceleration: 500.0,
        rapidFeedRate: 5000.0,
        rapidAcceleration: 1000.0
      },
      useGCodeFeedRate: true,
      delayAfterStartup: 1000,
      wireTemperature: 300.0,
      enableFan: true,
      fanSpeed: 255
    };
    
    // Wypełnij formularz domyślnymi wartościami
    document.getElementById('xStepsPerMM').value = defaultConfig.xAxis.stepsPerMM;
    document.getElementById('xMaxFeedRate').value = defaultConfig.xAxis.maxFeedRate;
    document.getElementById('xMaxAcceleration').value = defaultConfig.xAxis.maxAcceleration;
    document.getElementById('xRapidFeedRate').value = defaultConfig.xAxis.rapidFeedRate;
    document.getElementById('xRapidAcceleration').value = defaultConfig.xAxis.rapidAcceleration;
    
    document.getElementById('yStepsPerMM').value = defaultConfig.yAxis.stepsPerMM;
    document.getElementById('yMaxFeedRate').value = defaultConfig.yAxis.maxFeedRate;
    document.getElementById('yMaxAcceleration').value = defaultConfig.yAxis.maxAcceleration;
    document.getElementById('yRapidFeedRate').value = defaultConfig.yAxis.rapidFeedRate;
    document.getElementById('yRapidAcceleration').value = defaultConfig.yAxis.rapidAcceleration;
    
    document.getElementById('useGCodeFeedRate').checked = defaultConfig.useGCodeFeedRate;
    document.getElementById('delayAfterStartup').value = defaultConfig.delayAfterStartup;
    document.getElementById('wireTemperature').value = defaultConfig.wireTemperature;
    document.getElementById('enableFan').checked = defaultConfig.enableFan;
    document.getElementById('fanSpeed').value = defaultConfig.fanSpeed;
    
    showMessage('Settings reset to defaults (not saved)', 'info');
  }
}

// Podłącz zdarzenia po załadowaniu strony
document.addEventListener('DOMContentLoaded', function() {
  // Wczytaj konfigurację
  loadConfiguration();
  
  // Obsługa zapisywania konfiguracji
  document.getElementById('configForm').addEventListener('submit', saveConfiguration);
  
  // Obsługa resetowania konfiguracji
  document.getElementById('resetButton').addEventListener('click', resetConfiguration);
});
