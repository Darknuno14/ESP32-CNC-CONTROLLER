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

### 6.1 Strona Główna (`index.html`)

Funkcjonalności tej strony opisuje poniższa tabela:

| Requirement ID | Description | User Story | Acceptance Criteria |
| - | - | - | - |
| I01           | Podgląd statusu maszyny i karty SD           | Jako użytkownik, chcę widzieć klarowny status maszyny (np. "Oczekiwanie na projekt", "Wykonywanie projektu: Rozgrzewanie drutu") oraz status karty SD (np. "Gotowa", "Błąd"), aby móc szybko ocenić gotowość urządzenia do pracy i zdiagnozować ewentualne problemy | <ul><li>Strona główna wyświetla w widocznym miejscu (np. na górze) aktualny stan maszyny w formie tekstowej.</li><li>Strona główna wyświetla wizualny i tekstowy wskaźnik statusu karty SD (np. ikona + tekst: "Karta SD: Gotowa do użycia" / "Karta SD: Błąd odczytu / Brak karty").</li></ul> |
| I02 | Reinicjalizaja karty SD | Jako użytkownik, chcę mieć możliwość ponownej inicjalizacji karty SD, jeśli została wyjęta, bez konieczności restartowania kontrolera | <ul><li>Na stronie dostępny jest przycisk/akcja "Zainicjalizuj kartę SD".</li><li>Po użyciu tej funkcji, kontroler próbuje ponownie zainicjalizować kartę SD.</li><li>Status karty SD (zgodnie z I001) jest aktualizowany, aby odzwierciedlić wynik operacji.</li></ul> |
| I03 |Podgląd wybranego projektu |Jako użytkownik, chcę widzieć na stronie głównej nazwę aktualnie załadowanego/wybranego projektu, aby mieć pewność, że właściwy plik jest przygotowany do uruchomienia. |<ul><li>Na stronie dostępny jest przycisk/akcja "Zainicjalizuj kartę SD".</li><li>Po użyciu tej funkcji, kontroler próbuje ponownie zainicjalizować kartę SD.</li><li>Status karty SD (zgodnie z I001) jest aktualizowany, aby odzwierciedlić wynik operacji.</li></ul>|
| I04| Przyciski kontrolne pracy|Jako użytkownik, chcę mieć przyciski START, STOP oraz PAUZA/WZNÓW, aby móc w pełni kontrolować proces wykonywania projektu na maszynie. | <ul><li>Przycisk START: Rozpoczyna wykonywanie wybranego projektu od bieżącej pozycji; aktywny tylko, gdy projekt jest wybrany i maszyna jest gotowa.</li><li>Przycisk STOP: Natychmiast przerywa i kończy wykonywanie projektu bez możliwości wznowienia; zatrzymuje wszystkie ruchy i np. grzanie drutu.</li><li>Przycisk PAUZA/WZNÓW: Działa jako przełącznik:<ul><li>PAUZA: Wstrzymuje wykonywanie projektu; maszyna zatrzymuje ruch, ale zachowuje pozycję.</li><li>WZNÓW: Kontynuuje wykonywanie projektu od miejsca wstrzymania.</li></ul></li><li>Stan przycisków (aktywny/nieaktywny) zmienia się w zależności od stanu maszyny.</li></ul>|
| I05 |Szczegóły postępu pracy |Jako użytkownik, chcę widzieć szczegółowe informacje o postępie wykonywanego projektu, takie jak procent ukończenia, numer przetwarzanej linii (np. 123/1500), oraz czas pracy, aby móc monitorować przebieg zadania i oszacować pozostały czas. | <ul><li>Strona wyświetla pasek postępu (0-100%) dla aktywnego projektu.</li><li>Strona wyświetla informację o numerze aktualnie przetwarzanej linii oraz całkowitej liczbie linii w projekcie (np. "Linia: 123 / 1500").</li><li>Strona wyświetla czas pracy liczony od rozpoczęcia bieżącego projektu (np. "Czas pracy: 00:15:32").</li></ul>|
| I06 |Wizualizacja ruchu plotera | Jako użytkownik, chcę mieć wizualny podgląd ścieżki narzędzia (ruchu plotera) na kanwie 2D w czasie rzeczywistym, aby móc obserwować, co maszyna aktualnie wykonuje i weryfikować poprawność zaplanowanej ścieżki.| <ul><li>Na stronie znajduje się obszar (kanwa 2D), na którym rysowana jest ścieżka.</li><li>Linie na kanwie pojawiają się w czasie rzeczywistym, odzwierciedlając ruchy plotera podczas wykonywania projektu.</li></ul> |
| I07 | Reset podglądu ruchu| Jako użytkownik, chcę mieć możliwość wyczyszczenia kanwy podglądu ruchu plotera, aby usunąć narysowane wcześniej ścieżki i zacząć wizualizację od nowa (np. przed rozpoczęciem nowego projektu).| <ul><li>Na stronie dostępny jest przycisk/akcja "Wyczyść podgląd".</li><li>Po użyciu tej funkcji, wszystkie linie narysowane na kanwie (z I006) są usuwane.</li></ul>|

### 6.2 Zarządzanie Projektami (`projects.html`)
Ta strona umożliwia użytkownikowi zarządzanie plikami projektów (G-code) na karcie SD.

| ID Wymagania | Funkcjonalność                     | Historyjka Użytkownika (User Story)                                                                                                                                  | Kryteria Akceptacji (Acceptance Criteria)                                                                                                                                                                                                                                                           |
| :----------- | :--------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| P01          | Wyświetlanie statusu karty SD       | **Jako** użytkownik, **chcę** widzieć aktualny status karty SD na stronie zarządzania projektami, **aby** wiedzieć, czy jest gotowa do odczytu i zapisu projektów.      | <ul><li>Strona zarządzania projektami wyświetla czytelny status karty SD (np. "Karta SD: Gotowa", "Karta SD: Brak", "Karta SD: Błąd").</li><li>Status jest aktualizowany dynamicznie, jeśli stan karty ulegnie zmianie (np. po reinicjalizacji).</li></ul>                                                               |
| P02          | Reinicjalizacja karty SD           | **Jako** użytkownik, **chcę** mieć możliwość ponownej inicjalizacji karty SD ze strony zarządzania projektami, **aby** móc ją aktywować po włożeniu lub w przypadku błędu bez restartu całego urządzenia. | <ul><li>Na stronie zarządzania projektami dostępny jest przycisk "Zainicjalizuj kartę SD".</li><li>Po kliknięciu przycisku, kontroler próbuje ponownie zainicjalizować kartę SD.</li><li>Status karty SD (zgodnie z P01) jest aktualizowany, aby odzwierciedlić wynik operacji.</li></ul>                                                  |
| P03          | Wyświetlanie listy projektów       | **Jako** użytkownik, **chcę** widzieć listę wszystkich dostępnych projektów (plików G-code) znajdujących się na karcie SD, **aby** móc wybrać jeden z nich do dalszych operacji. | <ul><li>Strona zarządzania projektami wyświetla listę plików projektów (np. nazwy plików) z karty SD.</li><li>Jeśli karta SD jest niedostępna lub pusta, wyświetlany jest odpowiedni komunikat (np. "Brak projektów na karcie SD" lub "Karta SD niedostępna").</li></ul>                                                            |
| P04          | Odświeżanie listy projektów        | **Jako** użytkownik, **chcę** mieć możliwość odświeżenia listy projektów na karcie SD, **aby** zobaczyć najnowsze zmiany (np. po przesłaniu nowego pliku lub usunięciu istniejącego) bez przeładowywania całej strony. | <ul><li>Na stronie zarządzania projektami dostępny jest przycisk "Odśwież listę projektów".</li><li>Po kliknięciu przycisku, lista projektów (zgodnie z P03) jest ponownie wczytywana z karty SD i aktualizowana na stronie.</li></ul>                                                                                                |
| P05          | Przesyłanie nowego projektu        | **Jako** użytkownik, **chcę** móc przesłać nowy plik projektu (G-code) na kartę SD poprzez interfejs webowy, **aby** łatwo dodawać nowe zadania bez konieczności fizycznego dostępu do karty. | <ul><li>Na stronie zarządzania projektami znajduje się formularz/przycisk do przesyłania plików.</li><li>Użytkownik może wybrać plik projektu ze swojego komputera.</li><li>Po przesłaniu, plik jest zapisywany na karcie SD.</li><li>Lista projektów (P03) jest aktualizowana (lub użytkownik jest informowany o konieczności odświeżenia).</li><li>Wyświetlany jest komunikat o powodzeniu lub niepowodzeniu operacji przesłania.</li></ul> |
| P06          | Wybór projektu z listy             | **Jako** użytkownik, **chcę** móc wybrać jeden projekt z wyświetlanej listy (np. za pomocą radio buttonów lub kliknięcia), **aby** móc następnie go zatwierdzić, usunąć lub podejrzeć. | <ul><li>Każdy element na liście projektów (P03) jest interaktywny i umożliwia wybór.</li><li>Tylko jeden projekt może być wybrany jednocześnie.</li><li>Wybrany projekt jest wizualnie wyróżniony na liście.</li></ul>                                                                                                                             |
| P07          | Zatwierdzenie wybranego projektu   | **Jako** użytkownik, **chcę** móc zatwierdzić (załadować) wybrany projekt, **aby** przygotować go do wykonania przez maszynę po naciśnięciu przycisku START na stronie głównej. | <ul><li>Dostępny jest przycisk "Zatwierdź projekt" / "Wybierz do wykonania", aktywny gdy projekt jest wybrany (zgodnie z P06).</li><li>Po zatwierdzeniu, wybrany projekt staje się aktywnym projektem dla maszyny.</li><li>Informacja o aktywnym projekcie jest widoczna (np. na stronie głównej, zgodnie z I003 z poprzednich wymagań).</li></ul>          |
| P08          | Usuwanie projektu z karty SD       | **Jako** użytkownik, **chcę** móc usunąć wybrany projekt z karty SD, **aby** zarządzać miejscem na karcie i usuwać niepotrzebne pliki.                                  | <ul><li>Dostępna jest opcja usunięcia dla wybranego projektu (P06) (np. przycisk "Usuń projekt").</li><li>Przed usunięciem wyświetlane jest zapytanie o potwierdzenie.</li><li>Po potwierdzeniu, plik projektu jest usuwany z karty SD.</li><li>Lista projektów (P03) jest aktualizowana.</li><li>Wyświetlany jest komunikat o powodzeniu lub niepowodzeniu operacji.</li></ul> |
| P09          | Podgląd zawartości pliku projektu  | **Jako** użytkownik, **chcę** mieć możliwość podglądu tekstowej zawartości (G-code) wybranego pliku projektu, **aby** szybko zweryfikować jego treść bez potrzeby pobierania pliku. | <ul><li>Dostępna jest opcja "Pokaż zawartość" / "Podgląd G-code" dla wybranego projektu (P06).</li><li>Po kliknięciu, zawartość pliku G-code jest wyświetlana w oknie modalnym (popup) lub dedykowanym obszarze tekstowym.</li><li>Wyświetlona treść jest tylko do odczytu.</li><li>Użytkownik może zamknąć okno podglądu.</li></ul>                            |
| P10          | Wizualny podgląd ścieżki projektu  | **Jako** użytkownik, **chcę** mieć możliwość zobaczenia wizualnego podglądu ścieżki narzędzia dla wybranego pliku projektu (G-code) na kanwie 2D, **aby** ocenić kształt i przebieg zadania przed jego uruchomieniem. | <ul><li>Dostępna jest opcja "Pokaż podgląd ścieżki" dla wybranego projektu (P06).</li><li>Po kliknięciu, webserwer analizuje plik G-code, ekstrahując koordynaty X i Y.</li><li>Na stronie (np. w oknie modalnym) wyświetlana jest kanwa 2D z narysowanymi liniami reprezentującymi ruchy narzędzia.</li><li>Użytkownik może zamknąć okno podglądu.</li></ul> |


### 6.3 Sterowanie Ręczne (`jog.html`)

Ta strona pozwala na ręczne operacje maszyną, takie jak bazowanie, zerowanie pozycji oraz bezpośrednie sterowanie ruchem osi i urządzeniami wykonawczymi.

| ID Wymagania | Funkcjonalność                               | Historyjka Użytkownika (User Story)                                                                                                                                                             | Kryteria Akceptacji (Acceptance Criteria)                                                                                                                                                                                                                                                                                             |
| :----------- | :------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| J01          | Ręczne sterowanie drutem oporowym            | **Jako** użytkownik, **chcę** móc ręcznie włączać i wyłączać drut oporowy za pomocą przycisku na stronie, **aby** móc np. przetestować jego działanie, szybko nagrzać przed pracą lub awaryjnie wyłączyć. | <ul><li>Na stronie znajduje się przycisk "Włącz/Wyłącz Drut" (lub dwa osobne przyciski).</li><li>Aktualny stan drutu (Włączony/Wyłączony) jest wizualnie sygnalizowany na stronie.</li><li>Kliknięcie przycisku zmienia stan drutu (włącza go lub wyłącza).</li><li>Stan drutu jest odzwierciedlany wskaźnikiem.</li></ul>                                           |
| J02          | Ręczne sterowanie wentylatorem               | **Jako** użytkownik, **chcę** móc ręcznie włączać i wyłączać wentylator za pomocą przycisku na stronie, **aby** kontrolować chłodzenie lub przepływ powietrza według potrzeby, niezależnie od automatycznego sterowania. | <ul><li>Na stronie znajduje się przycisk "Włącz/Wyłącz Wentylator" (lub dwa osobne przyciski).</li><li>Aktualny stan wentylatora (Włączony/Wyłączony) jest wizualnie sygnalizowany na stronie.</li><li>Kliknięcie przycisku zmienia stan wentylatora.</li><li>Stan wentylatora jest odzwierciedlany wskaźnikiem.</li></ul>                         |
| J03          | Podgląd stanu E-Stop i krańcówek           | **Jako** użytkownik, **chcę** widzieć na stronie aktualny stan przycisku E-STOP oraz czujników krańcowych (LimX, LimY), **aby** móc szybko ocenić stan bezpieczeństwa maszyny i poprawność działania czujników, zwłaszcza przed i w trakcie bazowania. | <ul><li>Strona wyświetla czytelny, aktualizowany w czasie rzeczywistym status przycisku E-STOP (np. "Aktywny" / "Nieaktywny", "Wciśnięty" / "Zwolniony").</li><li>Strona wyświetla czytelne, aktualizowane w czasie rzeczywistym statusy dla czujników krańcowych LimX i LimY (np. "Zadziałany" / "Niezadziałany").</li><li>Jest jasne, że krańcówki są jednostronne i używane do bazowania.</li></ul> |
| J04          | Zerowanie pozycji (ustawienie zera roboczego) | **Jako** użytkownik, **chcę** mieć przycisk "Zeruj Pozycje", **aby** móc ręcznie ustawić aktualną pozycję plotera jako punkt zerowy (np. G92 X0 Y0) dla osi X i Y, bez konieczności fizycznego bazowania maszyny o krańcówki. | <ul><li>Na stronie znajduje się przycisk "Zeruj Pozycje" / "Ustaw Zero Robocze".</li><li>Po kliknięciu przycisku, aktualne współrzędne maszyny (w systemie roboczym) są ustawiane na (0,0) dla osi X i Y.</li><li>Wyświetlane współrzędne robocze na stronie aktualizują się, pokazując X=0, Y=0.</li><li>Operacja nie powoduje fizycznego ruchu maszyny.</li></ul> |
| J05          | Bazowanie maszyny (homing)                   | **Jako** użytkownik, **chcę** mieć przycisk "Bazuj Maszynę", **aby** zainicjować automatyczną procedurę bazowania (homing) przy użyciu czujników krańcowych LimX i LimY, co ustawi maszynowy punkt zerowy. | <ul><li>Na stronie znajduje się przycisk "Bazuj Maszynę".</li><li>Po kliknięciu, maszyna rozpoczyna sekwencję bazowania: ruch w kierunku krańcówki X, następnie ruch w kierunku krańcówki Y.</li><li>Podczas bazowania wyświetlany jest status (np. "Bazowanie osi X...", "Bazowanie osi Y...", "Bazowanie zakończone").</li><li>Po pomyślnym zakończeniu bazowania, współrzędne maszynowe są ustawione.</li></ul> |
| J06          | Ręczne sterowanie ruchem osi (Jogging)       | **Jako** użytkownik, **chcę** mieć możliwość ręcznego sterowania ruchem maszyny (jogging) w osiach X i Y za pomocą przycisków kierunkowych, z możliwością zdefiniowania odległości przesunięcia oraz wyboru trybu ruchu (roboczy/szybki), **aby** precyzyjnie pozycjonować narzędzie. | <ul><li>Na stronie dostępne są przyciski ekranowe ze strzałkami do sterowania ruchem: +X, -X, +Y, -Y.</li><li>Dostępne jest pole do wprowadzenia wartości kroku/przesunięcia (np. w mm), z wartością domyślną (np. 10 mm).</li><li>Dostępny jest przełącznik (np. radio buttony) do wyboru trybu ruchu: "Roboczy" (z zadaną prędkością posuwu) i "Szybki" (G0 - maksymalna prędkość).</li><li>Po kliknięciu przycisku kierunkowego, maszyna przesuwa się o zadaną odległość w wybranym kierunku i trybie.</li><li>Wyświetlane współrzędne maszyny (np. robocze) są aktualizowane po każdym ruchu.</li><li>Przyciski sterowania ruchem są aktywne tylko gdy maszyna jest bezczynna (nie wykonuje projektu, nie bazuje się).</li></ul> |

### 6.4 Konfiguracja Urządzenia (`config.html`)

Strona ta umożliwia parametryzację i konfigurację różnych aspektów działania maszyny.


| ID Wymagania | Funkcjonalność                               | Historyjka Użytkownika (User Story)                                                                                                                                                             | Kryteria Akceptacji (Acceptance Criteria)                                                                                                                                                                                                                                                                                             |
| :----------- | :------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| C01          | Podgląd statusu karty SD                     | **Jako** użytkownik, **chcę** widzieć aktualny status karty SD na stronie konfiguracji, **aby** upewnić się, że konfiguracja może być prawidłowo zapisana i odczytana z karty.                      | <ul><li>Strona konfiguracji wyświetla czytelny status karty SD (np. "Karta SD: Gotowa", "Karta SD: Brak", "Karta SD: Błąd").</li><li>Status jest aktualizowany dynamicznie w czasie rzeczywistym.</li></ul>                                                                                                                             |
| C02          | Reinicjalizacja karty SD                     | **Jako** użytkownik, **chcę** mieć możliwość ponownej inicjalizacji karty SD ze strony konfiguracji, **aby** móc aktywować kartę po włożeniu lub naprawić błędy bez restartu urządzenia.            | <ul><li>Na stronie konfiguracji dostępny jest przycisk "Re-initialize SD" (lub "Zainicjalizuj ponownie kartę SD").</li><li>Po kliknięciu przycisku, kontroler próbuje ponownie zainicjalizować kartę SD.</li><li>Status karty SD jest aktualizowany po operacji.</li></ul>                                                                        |
| C03          | Wczytywanie konfiguracji                     | **Jako** użytkownik, **chcę** móc wczytać aktualną konfigurację maszyny do formularza, **aby** zobaczyć obecne ustawienia i móc je modyfikować.                                                    | <ul><li>Dostępny jest przycisk "Load Configuration" (lub "Wczytaj Konfigurację").</li><li>Po kliknięciu, wszystkie pola formularza są wypełniane aktualnymi wartościami z kontrolera.</li><li>Podczas wczytywania wyświetlany jest wskaźnik ładowania.</li><li>W przypadku błędu wyświetlany jest komunikat o niepowodzeniu.</li></ul>                                |
| C04          | Konfiguracja osi X                           | **Jako** użytkownik, **chcę** móc skonfigurować parametry osi X (kroki na milimetr, prędkości, przyspieszenia), **aby** dostosować ruch maszyny do charakterystyki napędu osi X.                   | <ul><li>Formularz zawiera sekcję "X Axis Configuration" (lub "Konfiguracja Osi X") z polami: Steps per Millimeter (Kroki na milimetr), Work Feed Rate (Prędkość robocza), Work Acceleration (Przyspieszenie robocze), Rapid Feed Rate (Prędkość szybka), Rapid Acceleration (Przyspieszenie szybkie).</li><li>Wszystkie pola mają odpowiednie walidacje (np. liczby dodatnie, wymagane).</li><li>Pola mają pomocnicze opisy wyjaśniające przeznaczenie każdego parametru i ewentualne jednostki.</li></ul> |
| C05          | Konfiguracja osi Y                           | **Jako** użytkownik, **chcę** móc skonfigurować parametry osi Y (kroki na milimetr, prędkości, przyspieszenia), **aby** dostosować ruch maszyny do charakterystyki napędu osi Y.                   | <ul><li>Formularz zawiera sekcję "Y Axis Configuration" (lub "Konfiguracja Osi Y") z polami: Steps per Millimeter (Kroki na milimetr), Work Feed Rate (Prędkość robocza), Work Acceleration (Przyspieszenie robocze), Rapid Feed Rate (Prędkość szybka), Rapid Acceleration (Przyspieszenie szybkie).</li><li>Wszystkie pola mają odpowiednie walidacje (np. liczby dodatnie, wymagane).</li><li>Pola mają pomocnicze opisy wyjaśniające przeznaczenie każdego parametru i ewentualne jednostki.</li></ul> |
| C06          | Konfiguracja offsetów pozycji początkowej      | **Jako** użytkownik, **chcę** móc ustawić przesunięcia początkowe dla osi X i Y, **aby** zdefiniować pozycję startową przed wykonaniem G-code względem punktu bazowania.                           | <ul><li>Formularz zawiera sekcję "Konfiguracja pozycji początkowej" (lub "Start Position Offsets") z polami Offset osi X i Offset osi Y.</li><li>Pola akceptują wartości liczbowe (dodatnie, ujemne, zero).</li><li>Opisy wyjaśniają, że wartości określają przesunięcie początkowe od punktu bazowania (np. w mm).</li></ul>                                           |
| C07          | Konfiguracja operacyjna                      | **Jako** użytkownik, **chcę** móc skonfigurować parametry operacyjne (użycie feed rate z G-code, opóźnienie po starcie), **aby** dostosować zachowanie maszyny do specyficznych wymagań projektów.    | <ul><li>Dostępny jest przełącznik "Use G-Code Feed Rate" (lub "Użyj prędkości z G-code") do włączenia/wyłączenia używania prędkości z plików G-code.</li><li>Dostępne jest pole "Delay After Startup" (lub "Opóźnienie po starcie") do ustawienia opóźnienia (np. w sekundach) przed rozpoczęciem pracy.</li><li>Pola mają pomocnicze opisy wyjaśniające ich funkcję.</li></ul> |
| C08          | Konfiguracja mocy urządzeń                   | **Jako** użytkownik, **chcę** móc ustawić poziomy mocy dla drutu oporowego i wentylatora, **aby** kontrolować intensywność grzania i chłodzenia podczas pracy maszyny.                          | <ul><li>Formularz zawiera sekcję "Power Configuration" (lub "Konfiguracja Mocy") z polami Hot Wire Power (%) (Moc drutu oporowego) i Fan Power (%) (Moc wentylatora).</li><li>Pola akceptują wartości od 0 do 100% (np. z krokiem 0.1 lub 1).</li><li>Walidacja zapewnia, że wartości mieszczą się w zakresie 0-100%.</li></ul>                                   |
| C09          | Konfiguracja bezpieczeństwa                   | **Jako** użytkownik, **chcę** móc skonfigurować ustawienia bezpieczeństwa (wyłączenie E-STOP, krańcówek, typ krańcówek), **aby** dostosować system bezpieczeństwa do konkretnej instalacji lub potrzeb testowych. | <ul><li>Dostępne są przełączniki "Disable Emergency Stop" (Wyłącz E-STOP) i "Disable Limit Switches" (Wyłącz krańcówki) z wyraźnymi ostrzeżeniami o używaniu tylko do celów testowych i na własne ryzyko.</li><li>Dostępna jest lista wyboru "Limit Switch Type" (Typ krańcówek) z opcjami NO (Normally Open - Normalnie Otwarte) i NC (Normally Closed - Normalnie Zamknięte).</li><li>Ostrzeżenia bezpieczeństwa są wyróżnione wizualnie (np. czerwony tekst, ikona ostrzegawcza).</li></ul> |
| C10          | Zapisywanie konfiguracji                     | **Jako** użytkownik, **chcę** móc zapisać zmodyfikowaną konfigurację, **aby** zastosować nowe ustawienia w kontrolerze maszyny.                                                                  | <ul><li>Dostępny jest przycisk "Save Configuration" (lub "Zapisz Konfigurację").</li><li>Przed zapisem przeprowadzana jest walidacja wszystkich pól formularza (zgodnie z C11).</li><li>Podczas zapisywania wyświetlany jest wskaźnik ładowania.</li><li>Po pomyślnym zapisie wyświetlany jest komunikat o powodzeniu (np. tymczasowy).</li><li>W przypadku błędu zapisu wyświetlany jest komunikat o niepowodzeniu (pozostający do interakcji).</li><li>Przycisk "Save Configuration" jest nieaktywny, gdy maszyna jest w trakcie pracy (zgodnie z C12).</li></ul> |
| C11          | Walidacja danych wejściowych                 | **Jako** użytkownik, **chcę** otrzymywać informacje o błędach walidacji w czasie rzeczywistym lub przed próbą zapisu, **aby** móc poprawić nieprawidłowe wartości przed zapisaniem konfiguracji.       | <ul><li>Pola formularza z nieprawidłowymi wartościami są wizualnie oznaczone (np. czerwona ramka, ikona błędu) po próbie zapisu lub w trakcie wprowadzania danych (on-the-fly validation).</li><li>Komunikaty o błędach walidacji są wyświetlane w pobliżu odpowiednich pól lub w zbiorczym podsumowaniu.</li><li>Walidacja uruchamia się np. podczas wpisywania dla pól numerycznych (onInput) lub po utracie fokusa (onBlur) dla innych pól, oraz zawsze przed próbą zapisu.</li><li>Przycisk "Save Configuration" może być nieaktywny, jeśli występują błędy walidacyjne.</li></ul> |
| C12          | Blokada edycji podczas pracy maszyny         | **Jako** użytkownik, **chcę** aby system uniemożliwiał zapisywanie konfiguracji podczas gdy maszyna wykonuje zadanie, **aby** zapobiec nieprzewidzianym zmianom parametrów w trakcie pracy.              | <ul><li>Przycisk "Save Configuration" (oraz potencjalnie inne pola formularza) jest automatycznie wyłączany/blokowany (disabled), gdy maszyna jest w stanie aktywnym (np. RUNNING, HOMING, JOGGING).</li><li>Status maszyny jest monitorowany w czasie rzeczywistym przez interfejs webowy.</li><li>Przycisk/pola zostają ponownie aktywowane, gdy maszyna wraca do stanu bezczynności (np. IDLE).</li></ul> |

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
