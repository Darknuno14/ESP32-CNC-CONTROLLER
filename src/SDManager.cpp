#include "SDManager.h"
#include "CONFIGURATION.H"

#include <SD.h>
#include <Arduino.h>

SDMenagerStatus SDCardManager::init() {
    if (!SD.begin(CONFIG::SD_CS_PIN)) {
        return SDMenagerStatus::INIT_FAILED;
    }

    if (!SD.exists(CONFIG::PROJECTS_DIR)) {
        if (!createProjectsDirectory()) {
            return SDMenagerStatus::DIRECTORY_CREATE_FAILED;
        }
    }

    this->sdMutex = xSemaphoreCreateMutex();
    if (this->sdMutex == nullptr) {
        return SDMenagerStatus::MUTEX_CREATE_FAILED;
    }
    
    this->cardInitialized = true;
    return SDMenagerStatus::OK;
}

bool SDCardManager::createProjectsDirectory() {
    std::string path = CONFIG::PROJECTS_DIR;
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    return SD.mkdir(path.c_str());
}

bool SDCardManager::isCardInitialized() const {
      return cardInitialized;
}

SDMenagerStatus SDCardManager::listProjectFiles() {
    if (!this->isCardInitialized()) {
        return SDMenagerStatus::CARD_NOT_INITIALIZED;
    }

    std::string path = CONFIG::PROJECTS_DIR;
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        return SDMenagerStatus::DIRECTORY_OPEN_FAILED;
    }

    this->projectFiles.clear();

    File entry {dir.openNextFile()};
    while (entry) {
        if (!entry.isDirectory()) {
            projectFiles.push_back(entry.name());
        }
        entry.close();
        entry = dir.openNextFile();
    }

    dir.close();

    return SDMenagerStatus::OK;
}

std::vector<std::string> SDCardManager::getProjectFiles() const {
    return projectFiles;
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

const std::string& SDCardManager::getSelectedProject() {
    return this->selectedProject;
}

void SDCardManager::setSelectedProject(const std::string& filename) {
    this->selectedProject = filename;
    this->projectIsSelected = true;
}

void SDCardManager::clearSelectedProject() {
    this->selectedProject = "";
    this->projectIsSelected = false;
}