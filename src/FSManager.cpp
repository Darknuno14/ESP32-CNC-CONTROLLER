// ================================================================================
//                         MENADŻER SYSTEMU PLIKÓW FLASH
// ================================================================================
// Zarządzanie systemem plików LittleFS w pamięci flash ESP32
// Używany do przechowywania plików webowych i konfiguracji lokalnej

#include "FSManager.h"
#include <LittleFS.h>

// ================================================================================
//                            INICJALIZACJA SYSTEMU
// ================================================================================

FSManagerStatus FSManager::init() {
    // Montowanie systemu plików LittleFS (bez formatowania przy błędzie)
    if (!LittleFS.begin(false)) {
        return FSManagerStatus::MOUNT_FAILED;
    }

    return FSManagerStatus::OK;
}