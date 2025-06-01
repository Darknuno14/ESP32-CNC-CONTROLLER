// ================================================================================
//                            MENADŻER KARTY SD
// ================================================================================
// Zarządzanie dostępem do karty SD w środowisku wielowątkowym
// Obsługuje projekty G-code, konfigurację oraz synchronizację dostępu

#include "SDManager.h"
#include "CONFIGURATION.H"

#include <SD.h>
#include <Arduino.h>

// ================================================================================
//                           KONSTRUKTOR I DESTRUKTOR
// ================================================================================

SDCardManager::~SDCardManager() {
    // Zwolnienie zasobów FreeRTOS
    if (sdMutex != nullptr) {
        vSemaphoreDelete(sdMutex);
    }
}

// ================================================================================
//                            INICJALIZACJA SYSTEMU
// ================================================================================

SDManagerStatus SDCardManager::init() {
    // Inicjalizacja interfejsu SPI dla karty SD z optymalnymi parametrami
    if (!SD.begin(PINCONFIG::SD_CS_PIN, SPI, 25000000)) {
        return SDManagerStatus::INIT_FAILED;
    }

    // Tworzenie struktury katalogów jeśli nie istnieją
    if (!SD.exists(CONFIG::PROJECTS_DIR)) {
        if (!createDirectory(CONFIG::PROJECTS_DIR)) {
            return SDManagerStatus::DIRECTORY_CREATE_FAILED;
        }
    }

    if (!SD.exists(CONFIG::CONFIG_DIR)) {
        if (!createDirectory(CONFIG::CONFIG_DIR)) {
            return SDManagerStatus::DIRECTORY_CREATE_FAILED;
        }
    }

    // Tworzenie mutexa dla thread-safe dostępu do karty SD
    this->sdMutex = xSemaphoreCreateMutex();

    if (this->sdMutex == nullptr) {
        return SDManagerStatus::MUTEX_CREATE_FAILED;
    }

    this->cardInitialized = true;

    // Wczytanie listy dostępnych projektów
    SDManagerStatus listStatus = this->updateProjectList();
    #ifdef DEBUG_SD
    if (listStatus != SDManagerStatus::OK) {
        Serial.println("WARNING: Zainicjalizowano SD, ale nie wczytano listy projektow.");
    }
    #endif

    return SDManagerStatus::OK;
}

// ================================================================================
//                          OPERACJE NA SYSTEMIE PLIKÓW
// ================================================================================

bool SDCardManager::createDirectory(const std::string& path) {
    // Normalizacja ścieżki - usunięcie końcowego ukośnika
    std::string localPath { path };
    if (!localPath.empty() && localPath.back() == '/') {
        localPath.pop_back();
    }
    return SD.mkdir(localPath.c_str());
}

bool SDCardManager::isCardInitialized() const {
    return cardInitialized;
}

// ================================================================================
//                          ZARZĄDZANIE PROJEKTAMI G-CODE
// ================================================================================

SDManagerStatus SDCardManager::updateProjectList() {
    // Walidacja stanu karty SD
    if (!this->isCardInitialized()) {
        return SDManagerStatus::CARD_NOT_INITIALIZED;
    }

    // Normalizacja ścieżki katalogu projektów
    std::string path { CONFIG::PROJECTS_DIR };
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    // Thread-safe dostęp do karty SD
    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }

    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        giveSD();
        return SDManagerStatus::DIRECTORY_OPEN_FAILED;
    }

    // Skanowanie katalogu i budowa listy plików projektów
    this->projectFiles.clear();

    File entry { dir.openNextFile() };
    while (entry) {
        // Uwzględnianie tylko plików (pomijanie podkatalogów)
        if (!entry.isDirectory()) {
            projectFiles.push_back(entry.name());
        }
        entry.close();
        entry = dir.openNextFile();
    }

    dir.close();
    giveSD();

    return SDManagerStatus::OK;
}

SDManagerStatus SDCardManager::getProjectFiles(std::vector<std::string>& projectList) {
    // Walidacja stanu karty SD
    if (!this->isCardInitialized()) {
        return SDManagerStatus::CARD_NOT_INITIALIZED;
    }

    // Thread-safe kopiowanie listy projektów
    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }

    projectList = projectFiles;
    giveSD();
    return SDManagerStatus::OK;
}

// ================================================================================
//                          SYNCHRONIZACJA DOSTĘPU (MUTEX)
// ================================================================================

bool SDCardManager::takeSD() {
    // Blokada mutexa do czasu zwolnienia przez giveSD()
    if (this->sdMutex != nullptr) {
        return xSemaphoreTake(this->sdMutex, portMAX_DELAY) == pdTRUE;
    }
    return false;
}

void SDCardManager::giveSD() {
    // Zwolnienie mutexa - umożliwienie dostępu innym wątkom
    if (this->sdMutex != nullptr) {
        xSemaphoreGive(this->sdMutex);
    }
}

// ================================================================================
//                          ZARZĄDZANIE WYBRANYM PROJEKTEM
// ================================================================================

bool SDCardManager::isProjectSelected() const {
    return this->projectIsSelected;
}

SDManagerStatus SDCardManager::getSelectedProject(std::string& projectName) {
    // Thread-safe odczyt nazwy aktualnie wybranego projektu
    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }
    projectName = this->selectedProject;
    giveSD();
    return SDManagerStatus::OK;
}

SDManagerStatus  SDCardManager::setSelectedProject(const std::string& filename) {
    // Walidacja istnienia karty SD
    if (!isCardInitialized()) {
        return SDManagerStatus::CARD_NOT_INITIALIZED;
    }

    // Thread-safe ustawienie wybranego projektu z walidacją istnienia pliku
    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }

    std::string fullPath { CONFIG::PROJECTS_DIR + filename };
    if (!SD.exists(fullPath.c_str())) {
        giveSD();
        return SDManagerStatus::FILE_NOT_FOUND;
    }

    this->selectedProject = filename;
    this->projectIsSelected = true;
    giveSD();
    return SDManagerStatus::OK;
}

void SDCardManager::clearSelectedProject() {
    // Wyczyszczenie stanu wybranego projektu
    this->selectedProject = "";
    this->projectIsSelected = false;
}