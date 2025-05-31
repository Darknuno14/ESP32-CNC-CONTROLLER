#include "SDManager.h"
#include "CONFIGURATION.H"

#include <SD.h>
#include <Arduino.h>

SDCardManager::~SDCardManager() {
    if (sdMutex != nullptr) {
        vSemaphoreDelete(sdMutex);
    }
}

SDManagerStatus SDCardManager::init() {
    if (!SD.begin(PINCONFIG::SD_CS_PIN)) {
        return SDManagerStatus::INIT_FAILED;
    }

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

    this->sdMutex = xSemaphoreCreateMutex();

    if (this->sdMutex == nullptr) {
        return SDManagerStatus::MUTEX_CREATE_FAILED;
    }

    this->cardInitialized = true;

    SDManagerStatus listStatus = this->updateProjectList();
    #ifdef DEBUG_SD
    if (listStatus != SDManagerStatus::OK) {
        Serial.println("WARNING: Zainicjalizowano SD, ale nie wczytano listy projektow.");
    }
    #endif

    return SDManagerStatus::OK;
}

bool SDCardManager::createDirectory(const std::string& path) {
    std::string localPath { path };
    if (!localPath.empty() && localPath.back() == '/') {
        localPath.pop_back();
    }
    return SD.mkdir(localPath.c_str());
}

bool SDCardManager::isCardInitialized() const {
    return cardInitialized;
}

SDManagerStatus SDCardManager::updateProjectList() {
    if (!this->isCardInitialized()) {
        return SDManagerStatus::CARD_NOT_INITIALIZED;
    }

    std::string path { CONFIG::PROJECTS_DIR };
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }

    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        giveSD();
        return SDManagerStatus::DIRECTORY_OPEN_FAILED;
    }

    this->projectFiles.clear();

    File entry { dir.openNextFile() };
    while (entry) {
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
    if (!this->isCardInitialized()) {
        return SDManagerStatus::CARD_NOT_INITIALIZED;
    }

    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }

    projectList = projectFiles;
    giveSD();
    return SDManagerStatus::OK;
}

bool SDCardManager::takeSD() {
    if (this->sdMutex != nullptr) {
        return xSemaphoreTake(this->sdMutex, portMAX_DELAY) == pdTRUE;
    }
    return false;
}

void SDCardManager::giveSD() {
    if (this->sdMutex != nullptr) {
        xSemaphoreGive(this->sdMutex);
    }
}

bool SDCardManager::isProjectSelected() const {
    return this->projectIsSelected;
}

SDManagerStatus SDCardManager::getSelectedProject(std::string& projectName) {
    if (!takeSD()) {
        return SDManagerStatus::SD_BUSY;
    }
    projectName = this->selectedProject;
    giveSD();
    return SDManagerStatus::OK;
}

SDManagerStatus  SDCardManager::setSelectedProject(const std::string& filename) {
    if (!isCardInitialized()) {
        return SDManagerStatus::CARD_NOT_INITIALIZED;
    }

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
    this->selectedProject = "";
    this->projectIsSelected = false;
}