#pragma once
#include <vector>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class SDManagerStatus {
    OK,
    INIT_FAILED,
    DIRECTORY_CREATE_FAILED,
    DIRECTORY_OPEN_FAILED,
    MUTEX_CREATE_FAILED,
    FILE_OPEN_FAILED,
    CARD_NOT_INITIALIZED,
    SD_BUSY,
    FILE_NOT_FOUND,
    UNKNOWN_ERROR  
};

// Klasa zarządza kartą SD, która służy jako miejsce przechowywania dla plików projektów i konfiguracji.
class SDCardManager {
private:
    // Śledzi stan inicjalizacji karty
    bool cardInitialized {false};

    // Tworzy katalog na karcie SD
    // true = tworzenie katalogu powiodło się
    bool createDirectory(const std::string& path);

    // Przechowuje nazwy plików projektów
    std::vector<std::string> projectFiles {};  

    // Mutex dla dostępu do karty SD
    SemaphoreHandle_t sdMutex {};

    // Śledzi, czy projekt jest wybrany
    bool projectIsSelected {false};
    
    // Nazwa pliku z wybranym projektem 
    std::string selectedProject {};

public:

    // Domyślny konstruktor dla SDCardManager
    SDCardManager() = default;

    // Inicjalizacja menadżera karty SD
    SDManagerStatus init();

    // Zajmuje kartę SD dla wyłącznego dostępu
    bool takeSD();

    // Zwalnia kartę SD, aby inne zadania mogły z niej korzystać
    void giveSD();
    
    // Sprawdzenie czy menadżer jest zainicjalizowany
    // true = karta SD jest zainicjalizowana
    bool isCardInitialized() const;

    // Wczytanie listy plików projektów
    SDManagerStatus updateProjectList();
    
    // Get the Project Files vector
    SDManagerStatus getProjectFiles(std::vector<std::string>& projectList);

    // Sprawdzenie czy projekt jest wybrany
    // true = projekt jest wybrany
    bool isProjectSelected() const;

    // Zwraca nazwę pliku wybranego projektu
    SDManagerStatus getSelectedProject(std::string& projectName);

    // Ustawia nazwę pliku wybranego projektu
    SDManagerStatus  setSelectedProject(const std::string& filename);

    // Czyści nazwę pliku wybranego projektu
    void clearSelectedProject();
};

