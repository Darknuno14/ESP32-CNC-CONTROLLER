# **Dokument Wymagań Projektowych: Kontroler Ruchu CNC Hot-Wire**

## 1. Wprowadzenie

Niniejszy dokument określa wymagania funkcjonalne i niefunkcjonalne dla projektu kontrolera ruchu maszyny CNC typu hot-wire. Opisuje on zarówno interfejs użytkownika (webserwer), jak i logikę sterowania maszyną (kontroler). Celem dokumentu jest dostarczenie programistom jasnych wytycznych do implementacji systemu.

## 2. Cel Projektu

Głównym celem projektu jest stworzenie systemu sterowania dla plotera termicznego typu hot-wire. System będzie składał się z dwóch zasadniczych części:
*   **Webserwer:** Interfejs użytkownika dostępny przez przeglądarkę internetową, umożliwiający sterowanie urządzeniem, zarządzanie projektami, konfigurację parametrów oraz monitorowanie pracy maszyny.
*   **Kontroler (ESP32):** Moduł odpowiedzialny za fizyczne aspekty sterowania maszyną, w tym:
    *   Obsługę wejść/wyjść (I/O).
    *   Parsowanie i interpretację G-code.
    *   Precyzyjne sterowanie silnikami krokowymi.
    *   Implementację logiki bezpieczeństwa (np. zatrzymania awaryjne, krańcówki).
    *   Komunikację z webserwerem.

## 3. Zakres Projektu

### 3.1 W Zakresie (In-Scope)
*   Implementacja webserwera zgodnie z opisanymi podstronami i funkcjonalnościami.
*   Implementacja logiki kontrolera na platformie ESP32 z wykorzystaniem FreeRTOS.
*   Sterowanie dwoma osiami (X, Y) plotera.
*   Obsługa plików projektów w formacie G-code.
*   Możliwość konfiguracji parametrów maszyny i zapisywania ich na karcie SD.
*   Podstawowe funkcje bezpieczeństwa (E-STOP, krańcówki).
*   Ręczne sterowanie osiami i urządzeniami wykonawczymi (drut, wentylator).

### 3.2 Poza Zakresem (Out-of-Scope)

*   Zaawansowane algorytmy optymalizacji ścieżki narzędzia (np. kompensacja promienia drutu).
*   Sterowanie więcej niż dwiema osiami.
*   Integracja z systemami CAM/CAD (poza importem G-code).
*   Zaawansowane funkcje diagnostyczne i raportowania (poza podstawowym logowaniem).
*   Interfejsy użytkownika inne niż webowy (np. aplikacja mobilna, panel dotykowy LCD).

## 4. Główne Komponenty

1.  **Moduł ESP32:** Mikrokontroler stanowiący serce systemu.
2.  **Sterowniki Silników Krokowych:** (np. A4988, DRV8825) do napędu osi.
3.  **Karta SD:** Do przechowywania projektów G-code i plików konfiguracyjnych.
4.  **Moduł WiFi (wbudowany w ESP32):** Do komunikacji z webserwerem.
5.  **Czujniki Krańcowe:** Dla osi X i Y.
6.  **Przycisk E-STOP:** Do awaryjnego zatrzymania.
7.  **Przekaźniki/Tranzystory Mocy:** Do sterowania drutem grzejnym i wentylatorem.
8.  **Interfejs Webowy:** HTML, CSS, JavaScript działający w przeglądarce użytkownika.

## 5. Stylistyka Kodu

*   **Nazwy zmiennych:** Pisane po angielsku, preferowany `camelCase`.
*   **Komentarze:** Pisane po polsku. Komentarze powinny odnosić się przede wszystkim do *powodu* umieszczenia danego fragmentu kodu oraz jego *zasadniczego działania*. Należy unikać komentarzy opisujących to, co jest oczywiste i łatwo czytelne z samego kodu (np. `i++; // inkrementacja i`).

## 6. Webserwer

Webserwer podzielony jest na 4 zasadnicze podstrony, każda realizująca określony zestaw funkcjonalności.

## 7. Wymagania Projektowe - Kontroler Ruchu CNC (ESP32)
Ta sekcja szczegółowo opisuje wymagania dotyczące oprogramowania kontrolera działającego na ESP32.

### 7.1 Architektura Systemu

#### 7.1.1 Struktura Wielozadaniowa (FreeRTOS)

*   System oparty na co najmniej dwóch głównych zadaniach FreeRTOS działających na oddzielnych rdzeniach ESP32 (jeśli to możliwe i korzystne):
    *   `taskControl` : Obsługa webserwera, połączenia WiFi, komunikacji z klientem (odbieranie komend, wysyłanie stanu).
    *   `taskCNC` : Sterowanie silnikami krokowymi, przetwarzanie G-code, kontrola I/O, logika bezpieczeństwa.
*   Komunikacja międzyzadaniowa zrealizowana za pomocą kolejek FreeRTOS (np. `commandQueue` do przekazywania komend z `taskControl` do `taskCNC`, `stateQueue` do przekazywania aktualnego stanu z `taskCNC` do `taskControl`).

#### 7.1.2 Managery Systemu (Moduły Logiczne)

*   `FSManager`: Zarządzanie systemem plików (SPIFFS) dla zasobów webserwera (HTML, CSS, JS).
*   `SDCardManager`: Obsługa karty SD (odczyt/zapis projektów G-code, odczyt/zapis pliku konfiguracyjnego).
*   `WiFiManager`: Zarządzanie połączeniem WiFi (łączenie z siecią, obsługa trybu AP w razie potrzeby).
*   `WebServerManager`: Implementacja serwera HTTP z obsługą żądań GET/POST oraz komunikacji w czasie rzeczywistym (WebSocket lub Server-Sent Events).
*   `ConfigManager`: Zarządzanie konfiguracją maszyny (wczytywanie z pliku JSON z karty SD, parsowanie, udostępnianie parametrów, zapisywanie).
*   `GCodeParser`: Moduł odpowiedzialny za parsowanie linii G-code i ekstrakcję parametrów.
*   `MotionController`: Moduł zarządzający ruchem osi, współpracujący z biblioteką `AccelStepper`.
*   `SafetyManager`: Moduł monitorujący wejścia bezpieczeństwa i zarządzający stanami awaryjnymi.

### 7.2 Sterowanie Ruchem

#### 7.2.1 Silniki Krokowe

*   Obsługa dwóch osi (X, Y) z wykorzystaniem biblioteki `AccelStepper` lub podobnej, umożliwiającej nieblokujące sterowanie i generowanie ramp przyspieszenia/hamowania.
*   Konfigurowalne parametry dla każdej osi:
    *   Kroki na milimetr (`stepsPerMillimeter`).
    *   Prędkość robocza (`workFeedRate`).
    *   Prędkość szybka (`rapidFeedRate`).
    *   Przyspieszenie (`acceleration`).
*   Tryby ruchu:
    *   G0 (ruch szybki): Maksymalna skonfigurowana prędkość szybka.
    *   G1 (ruch roboczy): Prędkość zdefiniowana parametrem `F` w G-code lub domyślna prędkość robocza z konfiguracji.
    *   JOG (ruch ręczny): Prędkość zależna od wybranego trybu (szybki/roboczy) na stronie `jog.html`.
*   Nieblokujące wykonywanie ruchów: Główna pętla `taskCNC` musi regularnie wywoływać metodę `run()` biblioteki `AccelStepper` i kontrolować stan ruchu.

#### 7.2.2 Maszyna Stanowa G-code

Maszyna stanowa dla przetwarzania projektu G-code (uproszczony przykład, do rozwinięcia):
`IDLE` → (Otrzymano START) → `INITIALIZING` (np. sprawdzenie warunków) → `HEATING` (jeśli skonfigurowano opóźnienie) → `MOVING_TO_OFFSET` (przesunięcie do punktu startowego z offsetem) → `READING_FILE` (otwarcie pliku) → `PROCESSING_LINE` (czytanie i parsowanie linii) → `EXECUTING_MOVEMENT` (wykonywanie ruchu/komendy) → (Powrót do `PROCESSING_LINE` lub `FINISHED` po M30) → `IDLE`.

*   Przetwarzanie G-code linia po linii, bez blokowania głównej pętli `taskCNC`.
*   Obsługa offsetów początkowych (zdefiniowanych w konfiguracji) przed rozpoczęciem wykonania ścieżki z pliku G-code.
*   Kontrola prędkości: Możliwość przełączania między używaniem prędkości z parametru `F` w G-code a stałą prędkością roboczą z konfiguracji.


#### 7.2.3 Tryby Pozycjonowania

*   Obsługa pozycjonowania absolutnego (G90) - domyślny.
*   Obsługa pozycjonowania względnego (G91).
*   Procedura bazowania (homing, G28): Ruch osi do momentu aktywacji czujników krańcowych, co definiuje maszynowy punkt zerowy.
*   Zerowanie pozycji roboczej (G92 Xn Yn): Ustawienie aktualnej pozycji jako nowego zera w systemie współrzędnych roboczych.

### 7.3 Obsługa G-code

#### 7.3.1 Obsługiwane Komendy

*   `G0` / `G00`: Ruch szybki (rapid move).
*   `G1` / `G01`: Ruch liniowy interpolowany (feed move) z zadaną prędkością.
*   `G28`: Powrót do pozycji bazowej (homing).
*   `G90`: Tryb pozycjonowania absolutnego.
*   `G91`: Tryb pozycjonowania względnego.
*   `G92`: Ustawienie offsetu systemu współrzędnych (zerowanie pozycji).
*   `M0` / `M1` / `M2` / `M30`: Pauza programu / Koniec programu.
*   `M3` / `M5`: Włączenie / Wyłączenie drutu grzejnego.
*   `M106 S<val>` / `M107` (lub `M7`/`M9`): Włączenie wentylatora z zadaną mocą (S0-S255) / Wyłączenie wentylatora. (Należy wybrać spójną konwencję dla M-kodów wentylatora).
*   `F<val>`: Ustawienie prędkości posuwu (feed rate).

#### 7.3.2 Parser G-code

*   Ignorowanie komentarzy (linie rozpoczynające się od `;` lub zawarte w `( )`).
*   Ekstrakcja parametrów (liter adresowych) takich jak `X`, `Y`, `F`, `S` wraz z ich wartościami.
*   Obsługa pustych linii (ignorowanie).
*   Obsługa nieznanych lub nieimplementowanych komend (np. logowanie ostrzeżenia, ignorowanie).
*   Walidacja poprawności składniowej i wartości parametrów (np. czy współrzędne są liczbami).
*   Obsługa błędów parsowania i odpowiednie raportowanie.

### 7.4 System Bezpieczeństwa

#### 7.4.1 Wejścia Bezpieczeństwa

*   Przycisk E-STOP: Wejście cyfrowe. Jego aktywacja powinna natychmiast zatrzymać wszelkie ruchy i wyłączyć urządzenia wykonawcze. Może być konfigurowalnie wyłączalny (z ostrzeżeniem) do celów testowych.
*   Czujniki krańcowe (Limit Switches) dla osi X i Y: Wejścia cyfrowe. Służą do procedury bazowania oraz jako zabezpieczenie przed przekroczeniem zakresu ruchu.
*   Konfigurowalny typ styków dla krańcówek i E-STOP (NO - Normally Open / NC - Normally Closed).
*   System powinien reagować natychmiastowym zatrzymaniem ruchu i przejście w stan `ERROR` po aktywacji któregokolwiek z aktywnych zabezpieczeń (E-STOP, krańcówki poza procedurą bazowania).

#### 7.4.2 Kontrola Stanu

*   Ciągłe monitorowanie stanu wejść bezpieczeństwa w głównej pętli `taskCNC` (lub przez przerwania, jeśli to korzystniejsze).
*   W przypadku aktywacji E-STOP lub krańcówki (poza homingiem):
    *   Natychmiastowe zatrzymanie silników (np. przez wyłączenie sygnałów STEP/DIR lub specjalną funkcję biblioteki `AccelStepper`).
    *   Wyłączenie drutu grzejnego i wentylatora.
    *   Przejście systemu w stan `ERROR`.
    *   Wymagane potwierdzenie przez użytkownika (np. reset) do wyjścia ze stanu `ERROR` po usunięciu przyczyny.

### 7.5 Sterowanie Urządzeniami Roboczymi

#### 7.5.1 Drut Grzejny

*   Sterowanie za pomocą przekaźnika (ON/OFF) oraz modułu PWM.
*   Regulacja mocy za pomocą sygnału PWM (wartości 0-255 odpowiadające 0-100% mocy z konfiguracji).
*   Możliwość sterowania ręcznego (ze strony `jog.html`) oraz automatycznego (komendy M3/M5 w G-code).
* *WAZNE* DRUT GRZEJNY POWINIEN BYĆ ZAWSZE ZAŁĄCZANY W MOMENCIE ROZPOCZYNANIA PRZETWARZANIA PROJEKTU, NIE OCZEKUJE SIĘ KOMENDY G-CODE DLA ZAŁĄCZENIA GO, ZAKŁADA SIĘ DOMYŚLNE ZAŁĄCZENIE DRUTU W MOMENCIE URUCHOMIENIA ZADANIA

#### 7.5.2 Wentylator Chłodzący

*   Sterowanie za pomocą przekaźnika (ON/OFF) oraz modułu PWM.
*   Regulacja mocy (jeśli sprzęt na to pozwala) za pomocą sygnału PWM (wartości 0-255 odpowiadające 0-100% mocy z konfiguracji).
*   Niezależne sterowanie od drutu grzejnego.
*   Możliwość sterowania ręcznego (ze strony `jog.html`) oraz automatycznego (np. M106/M107 w G-code).
* *WAZNE* WENTYLATOR POWINIEN BYĆ ZAWSZE ZAŁĄCZANY W MOMENCIE ROZPOCZYNANIA PRZETWARZANIA PROJEKTU, NIE OCZEKUJE SIĘ KOMENDY G-CODE DLA ZAŁĄCZENIA GO, ZAKŁADA SIĘ DOMYŚLNE ZAŁĄCZENIE WENTYLATORA W MOMENCIE URUCHOMIENIA ZADANIA

### 7.6 Konfiguracja Systemu

#### 7.6.1 Parametry Konfiguracyjne (Przykładowe)

*   **Parametry Osi (dla X i Y):**
    *   `stepsPerMillimeter`
    *   `workFeedRate` (mm/s lub mm/min)
    *   `rapidFeedRate` (mm/s lub mm/min)
    *   `acceleration` (mm/s²)
    *   `maxTravel` (mm) - maksymalny zakres ruchu (do weryfikacji G-code)
*   **Offsety Początkowe:**
    *   `offsetX` (mm)
    *   `offsetY` (mm)
*   **Parametry Operacyjne:**
    *   `useGCodeFeedRate` (boolean: true/false)
    *   `delayAfterStartup` (sekundy) - opóźnienie na rozgrzanie drutu
*   **Moc Urządzeń:**
    *   `hotwirePowerPercent` (0-100%)
    *   `fanPowerPercent` (0-100%)
*   **Ustawienia Bezpieczeństwa:**
    *   `eStopEnabled` (boolean)
    *   `limitSwitchesEnabled` (boolean)
    *   `limitSwitchType` (string: "NO" / "NC")
    *   `eStopSwitchType` (string: "NO" / "NC")
*   **Parametry WiFi:**
    *   `ssid`
    *   `password`

#### 7.6.2 Zapis/Odczyt Konfiguracji

*   Konfiguracja przechowywana w pliku tekstowym w formacie JSON na karcie SD (np. `/Config/config.json`).
*   Walidacja parametrów podczas wczytywania konfiguracji z pliku.
*   W przypadku braku pliku konfiguracyjnego lub błędów w pliku, system powinien załadować wartości domyślne zdefiniowane w kodzie (np. w pliku `CONFIGURATION.H` lub podobnym).
*   Możliwość zapisu aktualnej konfiguracji (po modyfikacji przez webserwer) do pliku na karcie SD.

### 7.7 Komunikacja i Interfejs

#### 7.7.1 Komendy z Webserwera do Kontrolera

Definicja typów komend wysyłanych z webserwera do kontrolera (np. przez WebSocket/EventSource i kolejkę `commandQueue`).
```c++
enum class CommandType {
    START_PROJECT,      // Rozpocznij aktualnie wybrany projekt
    STOP_PROJECT,       // Zatrzymaj awaryjnie bieżący projekt
    PAUSE_RESUME_PROJECT, // Pauza lub wznowienie projektu
    HOME_MACHINE,       // Rozpocznij procedurę bazowania
    RESET_ERROR,        // Resetuj stan błędu (po usunięciu przyczyny)
    JOG_MOVE,           // Komenda ruchu ręcznego (z parametrami: oś, kierunek, dystans, prędkość)
    ZERO_POSITION,      // Ustaw aktualną pozycję jako zero robocze (G92)
    RELOAD_CONFIG,      // Wymuś ponowne wczytanie konfiguracji z karty SD
    SET_HOTWIRE_MANUAL, // Ręczne włączenie/wyłączenie drutu (z parametrem ON/OFF)
    SET_FAN_MANUAL,     // Ręczne włączenie/wyłączenie wentylatora (z parametrem ON/OFF)
    SD_REINIT,          // Ponowna inicjalizacja karty SD
    // ... inne potrzebne komendy
};

// Struktura dla komendy JOG_MOVE
struct JogCommandPayload {
    char axis; // 'X' lub 'Y'
    float distance; // mm
    float feedRate; // mm/s lub mm/min
    // kierunek może być zawarty w znaku distance
};
```

#### 7.7.3 Raportowanie Stanu

Kontroler powinien regularnie wysyłać aktualny `MachineStatusPayload` do podłączonych klientów webserwera (np. co 100-500ms). Dane obejmują:

*   Aktualną pozycję (X, Y).
*   Postęp zadania (numer przetwarzanej linii / całkowita liczba linii, procent ukończenia).
*   Czas wykonania bieżącego projektu.
*   Stan urządzeń wykonawczych (drut, wentylator).
*   Stan wejść bezpieczeństwa (E-STOP, krańcówki).
*   Status karty SD.
*   Nazwę aktualnie załadowanego projektu.

#### 7.8 Zarządzanie Projektami (po stronie kontrolera)

##### 7.8.1 Struktura Plików na Karcie SD

*   Katalog główny dla projektów: `/Projects/` (lub konfigurowalny).
*   Katalog dla pliku konfiguracyjnego: `/Config/` (np. `/Config/config.json`).
*   Obsługiwane rozszerzenia plików projektów: `.gcode`, `.nc`, `.txt` (lub inne popularne dla G-code).

##### 7.8.2 Operacje na Plikach (wykonywane przez kontroler na żądanie webserwera)

*   Listowanie plików z katalogu projektów.
*   Wybór aktywnego projektu (zapamiętanie ścieżki do pliku).
*   Przesyłanie nowych plików projektów na kartę SD (webserwer przekazuje dane pliku do kontrolera, który zapisuje je na SD).
*   Usuwanie projektów z karty SD.
*   Odczyt zawartości pliku projektu (do podglądu tekstowego).
*   Odczyt pliku konfiguracyjnego.
*   Zapis pliku konfiguracyjnego.

#### 7.9 Wymagania Sprzętowe

##### 7.9.1 Piny ESP32

Należy zdefiniować mapowanie pinów ESP32 dla poszczególnych funkcji:

*   Silniki: STEP/DIR dla osi X i Y.
*   Krańcówki: Wejścia cyfrowe (z wewnętrznym lub zewnętrznym pull-up/pull-down).
*   E-STOP: Wejście cyfrowe.
*   Przekaźniki (drut, wentylator): Wyjścia cyfrowe.
*   PWM (drut, wentylator): Wyjścia PWM.
*   SPI: Komunikacja z kartą SD.

##### 7.9.2 Interfejsy

*   WiFi: Do komunikacji z klientami webserwera.
*   Karta SD: Do przechowywania projektów i konfiguracji (interfejs SPI).
*   UART: Do celów debugowania i logowania (opcjonalnie, ale zalecane).

#### 7.10 Wymagania Wydajnościowe

##### 7.10.1 Timing

*   Częstotliwość generowania kroków dla silników: Wystarczająca do osiągnięcia skonfigurowanych prędkości (np. min. 1 kHz, ale zależne od mikrokroku i prędkości).
*   Częstotliwość aktualizacji stanu wysyłanego do webserwera: np. co 100-500 ms.
*   Częstotliwość sprawdzania wejść bezpieczeństwa: W każdej iteracji głównej pętli `taskCNC` lub obsługa przez przerwania dla maksymalnej responsywności.
*   Timeout dla operacji na karcie SD: Rozsądny czas oczekiwania z możliwością ponowienia próby (np. 3 próby).

##### 7.10.2 Pamięć

*   Rozmiar stosu dla `taskControl`: Należy oszacować i skonfigurować (np. 4KB - 32KB, zależnie od używanych bibliotek webserwera).
*   Rozmiar stosu dla `taskCNC`: Należy oszacować i skonfigurować (np. 4KB - 8KB).
*   Buforowanie linii G-code: Zazwyczaj przetwarzanie linia po linii, więc bufor na jedną, maksymalnie kilka linii.

#### 7.11 Obsługa Błędów

##### 7.11.1 Kategorie Błędów

*   Błędy karty SD (brak dostępu, błąd inicjalizacji, błąd odczytu/zapisu, uszkodzenie plików).
*   Błędy parsowania G-code (nieznana komenda, błędna składnia, brakujące parametry).
*   Błędy bezpieczeństwa (aktywacja E-STOP, aktywacja krańcówki poza homingiem).
*   Błędy komunikacji (utrata połączenia WiFi, błędy WebSocket).
*   Błędy konfiguracji (niepoprawny format pliku JSON, brakujące kluczowe parametry).
*   Błędy ruchu (np. próba ruchu poza zdefiniowany zakres, jeśli zaimplementowano soft-limits).

##### 7.11.2 Strategia Postępowania w Przypadku Błędu (Recovery)

*   Ponowienie próby dla operacji na karcie SD (np. 3 próby z krótkim opóźnieniem).
*   Próba automatycznego ponownego połączenia z WiFi w przypadku utraty połączenia.
*   Przejście w stan `ERROR` i zatrzymanie maszyny przy błędach bezpieczeństwa lub krytycznych błędach G-code.
*   Wyświetlanie czytelnych komunikatów o błędach dla użytkownika na interfejsie webowym.
*   Logowanie błędów do konsoli UART i/lub na kartę SD (opcjonalnie).
*   W przypadku błędu wczytania konfiguracji, użycie wartości domyślnych i poinformowanie użytkownika.

#### 7.12 Debugowanie i Monitorowanie

##### 7.12.1 Makra Debugowania

Wykorzystanie makr preprocesora do warunkowego włączania/wyłączania szczegółowego logowania dla różnych modułów:

```c++
// w pliku np. debug_flags.h
// #define DEBUG_CONTROL_TASK
// #define DEBUG_CNC_TASK
// #define DEBUG_WEBSERVER_ROUTES
// #define DEBUG_SD_CARD
// #define DEBUG_GCODE_PARSER
```

*   Logowanie przez port szeregowy (UART).

### 8. Założenia i Ograniczenia

*   Użytkownik posiada podstawową wiedzę na temat obsługi maszyn CNC i formatu G-code.
*   Sieć WiFi, do której będzie podłączone urządzenie, jest stabilna.
*   Maszyna jest poprawnie zmontowana i skalibrowana mechanicznie.
*   Projekt skupia się na podstawowej funkcjonalności plotera hot-wire, zaawansowane funkcje mogą być poza zakresem.
