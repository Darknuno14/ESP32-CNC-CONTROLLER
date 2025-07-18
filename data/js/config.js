/**
 * ========================================================================
 * ZARZĄDZANIE KONFIGURACJĄ MASZYNY CNC - ESP32-CNC-CONTROLLER
 * ========================================================================
 * Obsługa interfejsu konfiguracyjnego z funkcjami:
 * - Ładowanie i zapisywanie parametrów osi X/Y
 * - Konfiguracja prędkości pracy i szybkich ruchów
 * - Ustawienia bezpieczeństwa (E-STOP, krańcówki)
 * - Kontrola mocy drutu grzejnego i wentylatora
 * - Walidacja wprowadzanych danych
 */

// ================= ZMIENNE GLOBALNE =================

// Połączenie EventSource do monitorowania stanu maszyny
let eventSource;

// ================= KOMUNIKACJA Z SERWEREM =================

/**
 * Nawiązanie połączenia EventSource z automatycznym ponownym łączeniem
 */

function handleEventSource() {
  if (eventSource) eventSource.close();
  eventSource = new EventSource("/events");
  
  // Odbiór statusu maszyny dla aktualizacji UI
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
    // Automatyczne ponowne połączenie po awarii
    setTimeout(handleEventSource, 5000);
  };
}

// ================= OPERACJE NA KONFIGURACJI =================

/**
 * Pobieranie aktualnej konfiguracji z kontrolera i wypełnienie formularza
 */

function loadConfiguration() {
  // Aktywacja wskaźnika ładowania
  document.getElementById("loadingSpinner").style.display = "inline-block";
  document.getElementById("loadBtn").disabled = true;

  fetch("/api/config")
    .then((response) => {
      if (!response.ok)
        throw new Error(`HTTP error! Status: ${response.status}`);
      return response.json();
    })
    .then((config) => {
      console.log("Loaded config:", config);
      
      // Konfiguracja osi X - wypełnienie wszystkich pól parametrów
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
      
      // Konfiguracja osi Y - wypełnienie wszystkich pól parametrów
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
      
      // Ustawienia ogólne systemu
      const useGCodeFeedRate = document.getElementById("useGCodeFeedRate");
      const delayAfterStartup = document.getElementById("delayAfterStartup");
      const deactivateESTOP = document.getElementById("deactivateESTOP");
      const deactivateLimitSwitches = document.getElementById("deactivateLimitSwitches");
      const limitSwitchType = document.getElementById("limitSwitchType");
      const hotWirePower = document.getElementById("hotWirePower");
      const fanPower = document.getElementById("fanPower");
      
      if (useGCodeFeedRate) useGCodeFeedRate.checked = config.useGCodeFeedRate || false;
      if (delayAfterStartup) delayAfterStartup.value = config.delayAfterStartup || 0;
      if (deactivateESTOP) deactivateESTOP.checked = config.deactivateESTOP || false;
      if (deactivateLimitSwitches) deactivateLimitSwitches.checked = config.deactivateLimitSwitches || false;
      if (limitSwitchType) limitSwitchType.value = config.limitSwitchType || 0;
      if (hotWirePower) hotWirePower.value = config.hotWirePower || 0;
      if (fanPower) fanPower.value = config.fanPower || 0;

      showMessage("Configuration loaded successfully");
      document.getElementById("saveBtn").disabled = false;
    })
    .catch((error) => {
      console.error("Error loading configuration:", error);
      showMessage(`Failed to load configuration: ${error.message}`, "error");
    })
    .finally(() => {
      // Wyłączenie wskaźnika ładowania
      document.getElementById("loadingSpinner").style.display = "none";
      document.getElementById("loadBtn").disabled = false;
    });
}

/**
 * Zapisanie konfiguracji z walidacją i strukturyzacją danych
 * @param {Event} event - Zdarzenie submit formularza
 */

function saveConfiguration(event) {
  event.preventDefault();

  if (!validateForm()) {
    showMessage("Please correct errors in the form before saving.", "error");
    return;
  }

  // Aktywacja wskaźnika zapisywania
  document.getElementById("savingSpinner").style.display = "inline-block";
  document.getElementById("saveBtn").disabled = true;

  const formData = new FormData(document.getElementById("configForm"));

  // Strukturyzacja danych konfiguracyjnych dla API
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
    hotWirePower: parseFloat(formData.get("hotWirePower")) || 0,
    fanPower: parseFloat(formData.get("fanPower")) || 0,
  };

  console.log("Saving config:", config);

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
      // Wyłączenie wskaźnika zapisywania
      document.getElementById("savingSpinner").style.display = "none";
      document.getElementById("saveBtn").disabled = false;
    });
}

// ================= WALIDACJA DANYCH =================

/**
 * Sprawdzenie poprawności wszystkich pól numerycznych w formularzu
 * @returns {boolean} true jeśli wszystkie pola są poprawne
 */

function validateForm() {
  let isValid = true;
  const inputs = document.querySelectorAll('#configForm input[type="number"]');
  
  // Sprawdzenie każdego pola numerycznego
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

/**
 * Aktualizacja dostępności kontrolek na podstawie stanu maszyny
 * @param {Object} data - Dane statusu maszyny
 */
function updateMachineStatus(data) {
  const saveBtn = document.getElementById("saveBtn");

  // Blokada zapisu konfiguracji podczas pracy maszyny
  if (saveBtn) {
    saveBtn.disabled = data.state === 1 || data.state === 2 || data.state === 3;
  }
}

// ================= INICJALIZACJA =================

/**
 * Konfiguracja interfejsu po załadowaniu strony
 */

document.addEventListener("DOMContentLoaded", function () {
  // Nawiązanie połączenia z serwerem
  handleEventSource();

  // Konfiguracja przycisków akcji
  document
    .getElementById("loadBtn")
    .addEventListener("click", loadConfiguration);

  document
    .getElementById("configForm")
    .addEventListener("submit", saveConfiguration);

  // Walidacja w czasie rzeczywistym dla pól numerycznych
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

  // Automatyczne załadowanie konfiguracji przy starcie
  loadConfiguration();
});
