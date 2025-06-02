# Sterownik CNC oparty na ESP32

Projekt sterownika CNC bazującego na mikrokontrolerze ESP32, wyposażonego w interfejs webowy, dwuosiowe sterowanie silnikami krokowymi oraz funkcje bezpieczeństwa. System został zaprojektowany z myślą o sterowaniu maszynami CNC, w szczególności systemami do cięcia pianki gorącym drutem.

## Główne Funkcjonalności Projektu

### Podstawowe Sterowanie CNC
- Dwuosiowe sterowanie silnikami krokowymi (osie X, Y) z wykorzystaniem biblioteki AccelStepper.
- Przetwarzanie podstawowych komend G-code (G0, G1, M3, M5, M30, F).
- Precyzyjne pozycjonowanie (konfigurowalna liczba kroków na milimetr).
- Kontrola prędkości posuwu (parametr F w G-code).
- Sterowanie ruchem w czasie rzeczywistym (przerwania sprzętowe timera 10kHz).

### Systemy Bezpieczeństwa
- Przycisk zatrzymania awaryjnego (E-STOP) z natychmiastowym wyłączeniem systemu.
- Wyłączniki krańcowe dla osi X i Y (konfigurowalne jako normalnie otwarte/zwarte).
- Możliwość dezaktywacji funkcji bezpieczeństwa w celach testowych.

### Interfejs Webowy
- Panel główny umożliwiający monitorowanie stanu maszyny w czasie rzeczywistym oraz podstawowe sterowanie.
- Zarządzanie projektami: przesyłanie, pobieranie, podgląd i usuwanie plików G-code.
- Sterowanie ręczne: pozycjonowanie osi (jogging), bazowanie (homing).
- Konfiguracja parametrów systemowych i kalibracja.
- Responsywny design, dostosowany do urządzeń desktopowych i mobilnych.
- Aktualizacje statusu maszyny w czasie rzeczywistym poprzez EventSource.

### Zarządzanie Plikami i Konfiguracją
- Integracja z kartą SD (z dostępem bezpiecznym wątkowo) do przechowywania plików G-code.
- Zapis konfiguracji systemowej w formacie JSON na karcie SD.

### Architektura Systemu
- Implementacja oparta na systemie operacyjnym FreeRTOS z wykorzystaniem dwóch rdzeni procesora ESP32:
  - Rdzeń 0: Obsługa interfejsu webowego i komunikacji WiFi.
  - Rdzeń 1: Realizacja sterowania ruchem CNC w czasie rzeczywistym.
- Komunikacja międzywątkowa z użyciem kolejek i mutexów.
- Modułowa struktura kodu źródłowego.
- Podstawowe mechanizmy obsługi błędów.

### Funkcje Sterowania Dodatkowego
- Automatyczne bazowanie (homing) z detekcją wyłączników krańcowych i procedurą odsuwania.
- Ręczne pozycjonowanie osi (jogging) z konfigurowalnymi prędkościami (tryb szybki/roboczy).
- Możliwość zerowania współrzędnych i ustawiania punktu referencyjnego.
- Sterowanie drutem oporowym/wrzecionem z regulacją mocy PWM.
- Sterowanie wentylatorem chłodzącym z niezależnymi ustawieniami mocy.
- Funkcja pauzy i wznowienia podczas wykonywania programu G-code.

## Wymagania Sprzętowe

### Płytka ESP32
- Płytka rozwojowa z mikrokontrolerem ESP32 (projekt testowano na NodeMCU-32S).
- Minimum 4MB pamięci flash.
- Zintegrowany moduł WiFi.

### Komponenty Zewnętrzne
- Moduł karty SD (interfejs SPI).
- Sterowniki silników krokowych (interfejs Step/Direction).
- Silniki krokowe (dla osi X i Y).
- Wyłączniki krańcowe (normalnie otwarte lub normalnie zwarte).
- Przycisk zatrzymania awaryjnego (E-STOP).
- Układ sterowania drutem oporowym/wrzecionem (przekaźnik + PWM).
- Wentylator chłodzący (przekaźnik + PWM).

### Konfiguracja Pinów
```
Karta SD:    CS=5, MOSI=23, CLK=18, MISO=19
Silniki:     X_STEP=32, X_DIR=33, Y_STEP=17, Y_DIR=16
Krańcówki:   X_LIMIT=34, Y_LIMIT=35, ESTOP=2
Drut oporowy:RELAY=27, PWM=25
Wentylator:  RELAY=14, PWM=26
```

## Dane Techniczne

### Wykorzystanie Zasobów (orientacyjne)
- Pamięć Flash: 72.4% (zoptymalizowane).
- Pamięć RAM: 13.7% (zoptymalizowane).

### Komunikacja
- WiFi: Standard 802.11 b/g/n.
- Serwer Web: Asynchroniczny serwer HTTP z wykorzystaniem EventSource do aktualizacji danych w czasie rzeczywistym.
- Transfer plików: HTTP (upload/download) ze wskaźnikiem postępu.

## Konfiguracja

### Konfiguracja Sieci WiFi
Należy zmodyfikować plik `include/credentials.h`, podając dane swojej sieci WiFi:
```cpp
#define WIFI_SSID "TwojaSiecWiFi"
#define WIFI_PASSWORD "TwojeHaslo"
```

## Rozwój Projektu

### System Budowania
- Projekt oparty o **PlatformIO** z frameworkiem ESP32 Arduino.
- System plików **LittleFS** do przechowywania zasobów interfejsu webowego.
- Wykorzystane biblioteki zewnętrzne: AccelStepper, ArduinoJson, ESPAsyncWebServer.

### Struktura Kodu
```
src/
├── main.cpp              # Główna aplikacja i zadania FreeRTOS
├── ConfigManager.*       # Zarządzanie konfiguracją w formacie JSON
├── SDManager.*           # Operacje na karcie SD (bezpieczne wątkowo)
├── WebServerManager.*    # Implementacja serwera HTTP i obsługa żądań
├── FSManager.*           # Zarządzanie systemem plików LittleFS
├── WiFiManager.*         # Zarządzanie połączeniem WiFi
└── SharedTypes.h         # Wspólne struktury danych i typy
```

### Proces Budowania
```bash
# Używając PlatformIO CLI
pio run

# Wgrywanie oprogramowania na ESP32
pio run --target upload

# Monitorowanie portu szeregowego
pio device monitor
```

## Przykłady Użycia

### Podstawowa Obsługa
1.  Podłącz zasilanie do systemu i poczekaj na połączenie z siecią WiFi.
2.  Otwórz interfejs webowy w przeglądarce, wpisując adres IP modułu ESP32 (np. `http://[IP_ESP32]`).
3.  Przejdź do zakładki "Projekty" i prześlij plik G-code.
4.  Wykonaj sekwencję bazowania maszyny (homing).
5.  Rozpocznij wykonywanie przesłanego programu G-code.
6.  Monitoruj postęp pracy maszyny w czasie rzeczywistym na panelu głównym.

### Sterowanie Ręczne
- Użyj zakładki "Jog" do ręcznego przemieszczania osi maszyny.
- Wybierz tryb prędkości (szybki lub roboczy).
- Ustaw aktualną pozycję jako zero dla wybranej osi lub wszystkich osi.
- Przycisk zatrzymania awaryjnego (E-STOP) jest dostępny w każdym momencie pracy systemu.

## Rozwiązywanie Problemów

### Najczęstsze Problemy
- **Karta SD nie jest wykrywana**: Sprawdź poprawność połączeń elektrycznych oraz formatowanie karty (zalecany FAT32).
- **Silniki krokowe nie poruszają się**: Zweryfikuj połączenia sterowników silników oraz zasilanie.
- **Problem z połączeniem WiFi**: Sprawdź poprawność danych logowania (SSID i hasło) oraz siłę sygnału sieci.
- **Interfejs webowy nie jest dostępny**: Upewnij się, że ESP32 ma poprawny adres IP i jest podłączony do tej samej sieci co komputer.

### Tryb Debugowania
Aby włączyć dodatkowe komunikaty diagnostyczne, można odkomentować odpowiednie definicje w pliku konfiguracyjnym projektu (np. `CONFIGURATION.H` lub innym dedykowanym pliku nagłówkowym, jeśli istnieje):
```cpp
// Przykład - nazwy makr mogą się różnić w zależności od implementacji
// #define DEBUG_CNC_TASK
// #define DEBUG_CONTROL_TASK
```