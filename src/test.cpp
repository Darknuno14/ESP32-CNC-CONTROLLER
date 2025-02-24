
// Funkcja parsująca współrzędne X i Y z linijki G-code
float parseCoordinate(const String& line, char axis) {
    int start = line.indexOf(axis);
    if (start == -1) return NAN;  // Jeśli brak współrzędnej, zwróć NAN
    int end = line.indexOf(' ', start);
    if (end == -1) end = line.length();
    String valueStr = line.substring(start + 1, end);
    return valueStr.toFloat();
}

// Funkcja przetwarzająca pojedynczą linijkę G-code
void processGCodeLine(const String& line) {
    if (line.startsWith("G1")) {
        float x = parseCoordinate(line, 'X');
        float y = parseCoordinate(line, 'Y');
        if (!isnan(x) && !isnan(y)) {
            // Przelicz współrzędne na kroki
            long targetX = static_cast<long>(x * STEPS_PER_MM_X);
            long targetY = static_cast<long>(y * STEPS_PER_MM_Y);

            // Ustaw nowe pozycje docelowe
            stepperX.moveTo(targetX);
            stepperY.moveTo(targetY);

            // Zsynchronizuj ruch obu silników
            while (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                stepperX.setSpeed(SPEED);
                stepperY.setSpeed(SPEED);
                stepperX.runSpeedToPosition();
                stepperY.runSpeedToPosition();
            }
        }
    }
}

// Przykładowa funkcja przetwarzająca cały plik
void processGCodeFile(const char* filename) {
    File file = SD.open(filename);
    if (file) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();  // Usuń białe znaki
            if (line.length() > 0) {
                processGCodeLine(line);
            }
        }
        file.close();
    } else {
        Serial.println("Błąd otwarcia pliku!");
    }
}


bool processGCodeFile(String filePath) {
    File file = SD.open(filePath);
    if (!file) {
        Serial.println("ERROR: Nie udało się otworzyć pliku G-Code");
        return false;
    }

    while (file.available() && !eStopTriggered && !commandStop) {
        String line = file.readStringUntil('\n');
        // Tutaj parsuj i wykonuj linię G-Code (np. za pomocą parseGCode(line))
        // Przykład: parseGCode(line);
        // stepperX.moveTo(targetPosition);
        // stepperX.runSpeedToPosition();
    }

    file.close();
    return !(eStopTriggered || commandStop);  // True, jeśli zakończono pomyślnie
}

#include <AccelStepper.h>

AccelStepper stepperX(AccelStepper::DRIVER, CONFIG::STEP_PIN_X, CONFIG::DIR_PIN_X);
AccelStepper stepperY(AccelStepper::DRIVER, CONFIG::STEP_PIN_Y, CONFIG::DIR_PIN_Y);

void setupSteppers() {
    stepperX.setMaxSpeed(1000);  // Maksymalna prędkość
    stepperX.setAcceleration(500);  // Przyspieszenie
    stepperY.setMaxSpeed(1000);
    stepperY.setAcceleration(500);
}

void jogMode() {
    if (jogCommandReceived) {  // Zakładamy, że webserver ustawia tę flagę
        // Przykład: jogXDistance i jogYDistance to wartości z webservera
        stepperX.move(jogXDistance);
        stepperY.move(jogYDistance);
        stepperX.run();
        stepperY.run();
        jogCommandReceived = false;  // Reset flagi
    }
}

void homingMode() {
    // Oś X
    while (digitalRead(HOME_SENSOR_X) == HIGH && !eStopTriggered) {
        stepperX.move(-10);  // Ruch w kierunku czujnika
        stepperX.run();
    }
    stepperX.setCurrentPosition(0);  // Ustaw zero dla X

    // Oś Y
    while (digitalRead(HOME_SENSOR_Y) == HIGH && !eStopTriggered) {
        stepperY.move(-10);
        stepperY.run();
    }
    stepperY.setCurrentPosition(0);  // Ustaw zero dla Y
}

void checkEStop() {
    if (digitalRead(E_STOP_PIN) == LOW) {  // LOW = naciśnięty
        eStopTriggered = true;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(E_STOP_PIN, INPUT_PULLUP);
    pinMode(HOME_SENSOR_X, INPUT_PULLUP);
    pinMode(HOME_SENSOR_Y, INPUT_PULLUP);
    setupSteppers();
    // Utwórz taskCNC na wybranym rdzeniu
    xTaskCreatePinnedToCore(taskCNC, "Task2", 10000, NULL, 1, NULL, 1);
}
void parseGCode(String line) {
    // Usuń białe znaki i puste linie/komentarze
    line.trim();
    if (line.length() == 0 || line.startsWith(";")) return;

    // Usuń komentarze w linii
    int commentIndex = line.indexOf(';');
    if (commentIndex != -1) line = line.substring(0, commentIndex);

    // Podziel linię na słowa (np. "G1", "X10", "Y20")
    String words[10];  // Maksymalnie 10 słów
    int wordCount = 0;
    int startIndex = 0;
    while (startIndex < line.length()) {
        int spaceIndex = line.indexOf(' ', startIndex);
        if (spaceIndex == -1) spaceIndex = line.length();
        words[wordCount++] = line.substring(startIndex, spaceIndex);
        startIndex = spaceIndex + 1;
    }

    // Sprawdź typ komendy
    String command = words[0];
    if (command.startsWith("G")) {
        int gCode = command.substring(1).toInt();
        switch (gCode) {
            case 0:  // G0: Szybki ruch
            case 1:  // G1: Ruch liniowy
                {
                    // Pobierz parametry
                    float targetX = getParameter(line, 'X');
                    float targetY = getParameter(line, 'Y');
                    float feedRate = getParameter(line, 'F');

                    // Jeśli parametr nie został podany, użyj bieżącej pozycji
                    if (!isnan(targetX)) targetX = currentX + targetX;
                    if (!isnan(targetY)) targetY = currentY + targetY;

                    // Ustaw prędkość, jeśli podana
                    if (!isnan(feedRate)) {
                        stepperX.setSpeed(feedRate * stepsPerMM);
                        stepperY.setSpeed(feedRate * stepsPerMM);
                    }

                    // Oblicz docelowe pozycje w krokach
                    long targetStepsX = targetX * stepsPerMM;
                    long targetStepsY = targetY * stepsPerMM;

                    // Wykonaj ruch
                    if (gCode == 0) {
                        // G0: Szybki ruch – osobno dla każdej osi
                        stepperX.moveTo(targetStepsX);
                        stepperY.moveTo(targetStepsY);
                        while (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                            stepperX.run();
                            stepperY.run();
                        }
                    } else {
                        // G1: Ruch liniowy z interpolacją
                        stepperX.moveTo(targetStepsX);
                        stepperY.moveTo(targetStepsY);
                        while (stepperX.distanceToGo() != 0 || stepperY.distanceToGo() != 0) {
                            stepperX.runSpeedToPosition();
                            stepperY.runSpeedToPosition();
                        }
                    }

                    // Zaktualizuj aktualne pozycje
                    currentX = targetX;
                    currentY = targetY;
                }
                break;

            default:
                Serial.println("ERROR: Nieobsługiwany G-Code: " + command);
                break;
        }
    } else if (command.startsWith("M")) {
        int mCode = command.substring(1).toInt();
        switch (mCode) {
            case 3:  // M3: Włącz wrzeciono
                digitalWrite(SPINDLE_PIN, HIGH);
                break;
            case 5:  // M5: Wyłącz wrzeciono
                digitalWrite(SPINDLE_PIN, LOW);
                break;
            default:
                Serial.println("ERROR: Nieobsługiwany M-Code: " + command);
                break;
        }
    } else {
        Serial.println("ERROR: Nieprawidłowa komenda: " + command);
    }
}