#include "FSManager.h"

bool FSManager::init() {
    if (!LittleFS.begin(true)) {
        return false;
    }
    return true;
 }