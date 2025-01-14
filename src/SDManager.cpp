#include "SDManager.h"


SDCardError SDCardManager::init() {
    if (!SD.begin(SD_CS_PIN)) {
        return SDCardError::INIT_FAILED;
    }

    if (!SD.exists(projectsDirectory)) {
        if (!createProjectsDirectory()) {
            return SDCardError::DIRECTORY_CREATE_FAILED;
        }
    }

    cardInitialized = true;
    return SDCardError::OK;
}

bool SDCardManager::createProjectsDirectory(){
    if (SD.mkdir(projectsDirectory)) {
        return true;
    } else {
        return false;
    }
}

bool SDCardManager::isCardInitialized(){
      return cardInitialized;
}

SDCardError SDCardManager::listProjectFiles() {
    if (!cardInitialized) {
        return SDCardError::CARD_NOT_INITIALIZED;
    }

    File dir = SD.open(projectsDirectory);
    if (!dir) {
        return SDCardError::DIRECTORY_OPEN_FAILED;
    }

    projectFiles.clear();

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            projectFiles.push_back(entry.name());
        }
        entry.close();
        entry = dir.openNextFile();
    }

    dir.close();

    return SDCardError::OK;
}

std::vector<std::string> SDCardManager::getProjectFiles() const
{
    return projectFiles;
}
