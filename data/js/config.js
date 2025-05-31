/**
 * JavaScript for configuration page (config.html)
 */

// Zmienna do przechowywania EventSource
let eventSource;

// --- Helper Functions ---

function handleEventSource() {
  if (eventSource) eventSource.close();
  eventSource = new EventSource("/events");
  eventSource.addEventListener("machine-status", function (e) {
    try {
      const data = JSON.parse(e.data);
      updateMachineStatus(data);
    } catch (error) {
      console.error("Error parsing EventSource data:", error);
    }
  });
  eventSource.onopen = () => console.log("EventSource connection established");
  eventSource.onerror = () => {
    console.error("EventSource error");
    setTimeout(handleEventSource, 5000);
  };
}

// --- Main Functions ---

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
      console.log("Loaded config:", config); // Debug log
      
      // X Axis configuration - POPRAWIONA STRUKTURA
      if (config.xAxis) {
        const xStepsPerMM = document.getElementById("xStepsPerMM");
        const xWorkFeedRate = document.getElementById("xWorkFeedRate");
        const xWorkAcceleration = document.getElementById("xWorkAcceleration");
        const xRapidFeedRate = document.getElementById("xRapidFeedRate");
        const xRapidAcceleration = document.getElementById("xRapidAcceleration");
        const offsetX = document.getElementById("offsetX");
        
        if (xStepsPerMM) xStepsPerMM.value = config.xAxis.stepsPerMM || 0;
        if (xWorkFeedRate) xWorkFeedRate.value = config.xAxis.workFeedRate || 0;
        if (xWorkAcceleration) xWorkAcceleration.value = config.xAxis.workAcceleration || 0;
        if (xRapidFeedRate) xRapidFeedRate.value = config.xAxis.rapidFeedRate || 0;
        if (xRapidAcceleration) xRapidAcceleration.value = config.xAxis.rapidAcceleration || 0;
        if (offsetX) offsetX.value = config.xAxis.offset || 0;
      }
      
      // Y Axis configuration - POPRAWIONA STRUKTURA
      if (config.yAxis) {
        const yStepsPerMM = document.getElementById("yStepsPerMM");
        const yWorkFeedRate = document.getElementById("yWorkFeedRate");
        const yWorkAcceleration = document.getElementById("yWorkAcceleration");
        const yRapidFeedRate = document.getElementById("yRapidFeedRate");
        const yRapidAcceleration = document.getElementById("yRapidAcceleration");
        const offsetY = document.getElementById("offsetY");
        
        if (yStepsPerMM) yStepsPerMM.value = config.yAxis.stepsPerMM || 0;
        if (yWorkFeedRate) yWorkFeedRate.value = config.yAxis.workFeedRate || 0;
        if (yWorkAcceleration) yWorkAcceleration.value = config.yAxis.workAcceleration || 0;
        if (yRapidFeedRate) yRapidFeedRate.value = config.yAxis.rapidFeedRate || 0;
        if (yRapidAcceleration) yRapidAcceleration.value = config.yAxis.rapidAcceleration || 0;
        if (offsetY) offsetY.value = config.yAxis.offset || 0;
      }
      
      // General configuration
      const useGCodeFeedRate = document.getElementById("useGCodeFeedRate");
      const delayAfterStartup = document.getElementById("delayAfterStartup");
      const deactivateESTOP = document.getElementById("deactivateESTOP");
      const deactivateLimitSwitches = document.getElementById("deactivateLimitSwitches");
      const limitSwitchType = document.getElementById("limitSwitchType");
      
      if (useGCodeFeedRate) useGCodeFeedRate.checked = config.useGCodeFeedRate || false;
      if (delayAfterStartup) delayAfterStartup.value = config.delayAfterStartup || 0;
      if (deactivateESTOP) deactivateESTOP.checked = config.deactivateESTOP || false;
      if (deactivateLimitSwitches) deactivateLimitSwitches.checked = config.deactivateLimitSwitches || false;
      if (limitSwitchType) limitSwitchType.value = config.limitSwitchType || 0;

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

function saveConfiguration(event) {
  event.preventDefault();

  if (!validateForm()) {
    showMessage("Please correct errors in the form before saving.", "error");
    return;
  }

  document.getElementById("savingSpinner").style.display = "inline-block";
  document.getElementById("saveBtn").disabled = true;

  const formData = new FormData(document.getElementById("configForm"));

  // POPRAWIONA STRUKTURA JSON DO WYSYŁANIA
  const config = {
    xAxis: {
      stepsPerMM: parseFloat(formData.get("xAxis.stepsPerMM")) || 0,
      workFeedRate: parseFloat(formData.get("xAxis.workFeedRate")) || 0,
      workAcceleration: parseFloat(formData.get("xAxis.workAcceleration")) || 0,
      rapidFeedRate: parseFloat(formData.get("xAxis.rapidFeedRate")) || 0,
      rapidAcceleration: parseFloat(formData.get("xAxis.rapidAcceleration")) || 0,
      offset: parseFloat(formData.get("xAxis.offset")) || 0,
    },
    yAxis: {
      stepsPerMM: parseFloat(formData.get("yAxis.stepsPerMM")) || 0,
      workFeedRate: parseFloat(formData.get("yAxis.workFeedRate")) || 0,
      workAcceleration: parseFloat(formData.get("yAxis.workAcceleration")) || 0,
      rapidFeedRate: parseFloat(formData.get("yAxis.rapidFeedRate")) || 0,
      rapidAcceleration: parseFloat(formData.get("yAxis.rapidAcceleration")) || 0,
      offset: parseFloat(formData.get("yAxis.offset")) || 0,
    },
    useGCodeFeedRate: formData.get("useGCodeFeedRate") === "on",
    delayAfterStartup: parseInt(formData.get("delayAfterStartup")) || 0,
    deactivateESTOP: formData.get("deactivateESTOP") === "on",
    deactivateLimitSwitches: formData.get("deactivateLimitSwitches") === "on",
    limitSwitchType: parseInt(formData.get("limitSwitchType")) || 0,
  };

  console.log("Saving config:", config); // Debug log

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

function validateForm() {
  let isValid = true;
  const inputs = document.querySelectorAll('#configForm input[type="number"]');
  inputs.forEach((input) => {
    input.classList.remove("is-invalid");
    const value = parseFloat(input.value);
    if (isNaN(value)) {
      input.classList.add("is-invalid");
      isValid = false;
    }
  });
  return isValid;
}

// --- Initialization ---

document.addEventListener("DOMContentLoaded", function () {
  handleEventSource();

  document
    .getElementById("loadBtn")
    .addEventListener("click", loadConfiguration);

  document
    .getElementById("configForm")
    .addEventListener("submit", saveConfiguration);

  document
    .querySelectorAll('#configForm input[type="number"]')
    .forEach((input) => {
      input.addEventListener("input", function () {
        if (parseFloat(this.value) < 0 && this.id !== "wireTemperature") {
          this.classList.add("is-invalid");
        } else {
          this.classList.remove("is-invalid");
        }
      });
    });

  loadConfiguration();
});

// Aktualizacja stanu kontrolek na podstawie stanu maszyny
function updateMachineStatus(data) {
  const saveBtn = document.getElementById("saveBtn");

  // Wyłącz przycisk zapisu gdy maszyna jest zajęta
  if (saveBtn) {
    saveBtn.disabled = data.state === 1 || data.state === 2 || data.state === 3;
  }
}
