/**
 * JavaScript for configuration page (config.html)
 */

// Zmienna do przechowywania EventSource
let eventSource;

// Funkcja do wczytywania konfiguracji z ESP32
function loadConfiguration() {
  document.getElementById("loadingSpinner").style.display = "inline-block";
  document.getElementById("loadBtn").disabled = true;

  fetch("/api/config")
    .then((response) => {
      if (!response.ok)
        throw new Error(`HTTP error! Status: ${response.status}`);
      return response.json();
    })
    .then((config) => {
      if (config.xAxis) {
        document.getElementById("xStepsPerMM").value = config.xAxis.stepsPerMM;
        document.getElementById("xWorkFeedRate").value =
          config.xAxis.workFeedRate;
        document.getElementById("xWorkAcceleration").value =
          config.xAxis.workAcceleration;
        document.getElementById("xRapidFeedRate").value =
          config.xAxis.rapidFeedRate;
        document.getElementById("xRapidAcceleration").value =
          config.xAxis.rapidAcceleration;
      }
      if (config.yAxis) {
        document.getElementById("yStepsPerMM").value = config.yAxis.stepsPerMM;
        document.getElementById("yWorkFeedRate").value =
          config.yAxis.workFeedRate;
        document.getElementById("yWorkAcceleration").value =
          config.yAxis.workAcceleration;
        document.getElementById("yRapidFeedRate").value =
          config.yAxis.rapidFeedRate;
        document.getElementById("yRapidAcceleration").value =
          config.yAxis.rapidAcceleration;
      }
      document.getElementById("offsetX").value = config.offsetX;
      document.getElementById("offsetY").value = config.offsetY;
      document.getElementById("useGCodeFeedRate").checked =
        config.useGCodeFeedRate;
      document.getElementById("delayAfterStartup").value =
        config.delayAfterStartup;
      document.getElementById("deactivateESTOP").checked =
        config.deactivateESTOP;
      document.getElementById("deactivateLimitSwitches").checked =
        config.deactivateLimitSwitches;
      document.getElementById("limitSwitchType").value = config.limitSwitchType;

      showMessage("Configuration loaded successfully");
      document.getElementById("saveBtn").disabled = false;
    })
    .catch((error) => {
      console.error("Error loading configuration:", error);
      showMessage(`Failed to load configuration: ${error.message}`, "error");
    })
    .finally(() => {
      document.getElementById("loadingSpinner").style.display = "none";
      document.getElementById("loadBtn").disabled = false;
    });
}

// Funkcja do zapisywania konfiguracji
function saveConfiguration(event) {
  event.preventDefault();

  if (!validateForm()) {
    showMessage("Please correct errors in the form before saving.", "error");
    return;
  }

  document.getElementById("savingSpinner").style.display = "inline-block";
  document.getElementById("saveBtn").disabled = true;

  const formData = new FormData(document.getElementById("configForm"));

  const config = {
    xAxis: {
      stepsPerMM: parseFloat(formData.get("xAxis.stepsPerMM")),
      workFeedRate: parseFloat(formData.get("xAxis.workFeedRate")),
      workAcceleration: parseFloat(formData.get("xAxis.workAcceleration")),
      rapidFeedRate: parseFloat(formData.get("xAxis.rapidFeedRate")),
      rapidAcceleration: parseFloat(formData.get("xAxis.rapidAcceleration")),
    },
    yAxis: {
      stepsPerMM: parseFloat(formData.get("yAxis.stepsPerMM")),
      workFeedRate: parseFloat(formData.get("yAxis.workFeedRate")),
      workAcceleration: parseFloat(formData.get("yAxis.workAcceleration")),
      rapidFeedRate: parseFloat(formData.get("yAxis.rapidFeedRate")),
      rapidAcceleration: parseFloat(formData.get("yAxis.rapidAcceleration")),
    },
    offsetX: parseFloat(formData.get("offsetX")),
    offsetY: parseFloat(formData.get("offsetY")),
    useGCodeFeedRate: formData.get("useGCodeFeedRate") === "on",
    delayAfterStartup: parseInt(formData.get("delayAfterStartup")),
    deactivateESTOP: formData.get("deactivateESTOP") === "on",
    deactivateLimitSwitches: formData.get("deactivateLimitSwitches") === "on",
    limitSwitchType: parseInt(formData.get("limitSwitchType")),
  };

  fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config),
  })
    .then((response) => {
      if (!response.ok)
        throw new Error(`HTTP error! Status: ${response.status}`);
      return response.json();
    })
    .then((data) => {
      if (data.success) showMessage("Configuration saved successfully");
      else
        showMessage(
          "Failed to save configuration: " + (data.message || "Unknown error"),
          "error"
        );
    })
    .catch((error) => {
      console.error("Error saving configuration:", error);
      showMessage(`Failed to save configuration: ${error.message}`, "error");
    })
    .finally(() => {
      document.getElementById("savingSpinner").style.display = "none";
      document.getElementById("saveBtn").disabled = false;
    });
}

// Funkcja do sprawdzania poprawności wprowadzonych wartości
function validateForm() {
  let isValid = true;
  const inputs = document.querySelectorAll('#configForm input[type="number"]');

  inputs.forEach((input) => {
    // Usuń poprzednie klasy walidacji
    input.classList.remove("is-invalid");

    const value = parseFloat(input.value);

    if (isNaN(value)) {
      input.classList.add("is-invalid");
      isValid = false;
    }
  });

  return isValid;
}

// Inicjalizacja EventSource dla aktualizacji w czasie rzeczywistym
function initEventSource() {
  if (eventSource) {
    eventSource.close();
  }

  eventSource = new EventSource("/events");

  eventSource.addEventListener("machine-status", function (e) {
    try {
      const data = JSON.parse(e.data);
      updateMachineStatus(data);
    } catch (error) {
      console.error("Error parsing EventSource data:", error);
    }
  });

  eventSource.onopen = function () {
    console.log("EventSource connection established");
  };

  eventSource.onerror = function (e) {
    console.error("EventSource error:", e);
    // Spróbuj ponownie połączyć po 5 sekundach
    setTimeout(initEventSource, 5000);
  };
}

// Aktualizacja stanu kontrolek na podstawie stanu maszyny
function updateMachineStatus(data) {
  const saveBtn = document.getElementById("saveBtn");

  // Wyłącz przycisk zapisu gdy maszyna jest zajęta
  if (saveBtn) {
    saveBtn.disabled = data.state === 1 || data.state === 2 || data.state === 3;
  }
}

// Podłącz zdarzenia po załadowaniu strony
document.addEventListener("DOMContentLoaded", function () {
  // Inicjalizacja EventSource
  initEventSource();

  // Obsługa przycisku wczytywania konfiguracji
  document
    .getElementById("loadBtn")
    .addEventListener("click", loadConfiguration);

  // Obsługa zapisywania konfiguracji
  document
    .getElementById("configForm")
    .addEventListener("submit", saveConfiguration);

  // Dodaj walidację do wszystkich pól numerycznych
  document
    .querySelectorAll('#configForm input[type="number"]')
    .forEach((input) => {
      input.addEventListener("input", function () {
        // Walidacja podczas wprowadzania danych
        if (parseFloat(this.value) < 0 && this.id !== "wireTemperature") {
          this.classList.add("is-invalid");
        } else {
          this.classList.remove("is-invalid");
        }
      });
    });

  // Wczytaj konfigurację przy załadowaniu strony
  loadConfiguration();
});
