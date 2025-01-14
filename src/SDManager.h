#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <string>

#include "CONFIGURATION.h"

enum class SDCardError {
    OK,
    INIT_FAILED,
    DIRECTORY_CREATE_FAILED,
    DIRECTORY_OPEN_FAILED,
    FILE_OPEN_FAILED,
    CARD_NOT_INITIALIZED,
    UNKNOWN_ERROR  
};

class SDCardManager {
private:
    bool cardInitialized{false};
    const char*  projectsDirectory{"Projects"};

    bool createProjectsDirectory();
    std::vector<std::string> projectFiles; // Według gemini String z Arduino może powodować fragmentację pamięci. Unikanie String z Arduino: Klasa String z Arduino alokuje pamięć dynamicznie, co w dłuższym okresie może prowadzić do fragmentacji pamięci i niestabilności systemu. 
public:
    SDCardManager() = default;
    SDCardError init();
    bool isCardInitialized();
    SDCardError listProjectFiles(); // Funkcja do listowania plików w katalogu "Projekty"
    std::vector<std::string> getProjectFiles() const;
};

