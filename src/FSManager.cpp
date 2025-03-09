#include "FSManager.h"
#include <LittleFS.h>

FSManagerStatus FSManager::init() {
    if (!LittleFS.begin(false)) {
        return FSManagerStatus::MOUNT_FAILED;
    }

    return FSManagerStatus::OK;
}