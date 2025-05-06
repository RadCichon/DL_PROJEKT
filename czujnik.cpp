#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <algorithm>  // Włącza bibliotekę algorithm dla sortowania
#include "BluetoothSerial.h"

// Ustawienia Bluetooth
BluetoothSerial SerialBT;
String device_name = "ESP32-Rain_Intensity";

// Sprawdzenie dostępności Bluetooth
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip
#endif

// Ustaw adres I2C wyświetlacza (najczęściej 0x27 lub 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Używamy adresu 0x27 dla I2C LCD

#define AO_PIN 34  // Pin analogowy podłączony do wyjścia AO czujnika deszczu (GPIO34)
#define NUM_SAMPLES 10  // Liczba próbek
#define NUM_HISTORY 20  // Liczba cykli do analizy trendu

int rain_values[NUM_SAMPLES];  // Tablica przechowująca próbki deszczu
int history[NUM_HISTORY];  // Historia średnich wartości deszczu
int previous_avg_rain_value = 0;  // Poprzednia średnia wartość deszczu
unsigned long lastUpdateTime = 0;  // Czas ostatniej aktualizacji
String rain_trend = "Stable";  // Trend deszczu (możliwe wartości: "Rising", "Falling", "Stable")

void setup() {
  // Inicjalizacja komunikacji szeregowej (przydatna do debugowania)
  Serial.begin(115200);
  SerialBT.begin(device_name);
  Serial.printf("The device with name \"%s\" is started.\nNow you can pair it with Bluetooth!\n", device_name.c_str());

  
  // Inicjalizacja wyświetlacza LCD
  lcd.init();
  lcd.backlight();  // Włącz podświetlenie LCD
  lcd.setCursor(0, 0);  // Ustaw kursor na początku pierwszego wiersza
  lcd.print("Rain Sensor Test");  // Wyświetl początkowy tekst na ekranie
  delay(2000);  // Opóźnienie, aby tekst był widoczny przez chwilę

  // Inicjalizacja pinu AO jako wejścia
  pinMode(AO_PIN, INPUT);  // Konfigurujemy pin AO_PIN jako wejście

  // Zainicjalizuj tablicę wartości deszczu
  for (int i = 0; i < NUM_SAMPLES; i++) {
    rain_values[i] = 0;  // Wypełniamy tablicę zerami na początek
  }
  for (int i = 0; i < NUM_HISTORY; i++) {
    history[i] = 0;  // Wypełniamy historię zerami na początek
  }
}

void loop() {
  unsigned long CurrentMillis = millis();

  // Odczyt wartości z czujnika deszczu (piny analogowe odczytują wartości 0-4095)
  int rain_value = analogRead(AO_PIN);  // Odczyt z pinu analogowego AO_PIN

  // Zapisz próbkę do tablicy
  for (int i = NUM_SAMPLES - 1; i > 0; i--) {
    rain_values[i] = rain_values[i - 1];
  }
  rain_values[0] = rain_value;  // Nowa próbka na początku tablicy

  // Wyświetlanie uśrednionych danych co 0.5s
  if (CurrentMillis - lastUpdateTime >= 500) {
    // Obliczanie mediany z ostatnich próbek
    int sorted_values[NUM_SAMPLES];
    memcpy(sorted_values, rain_values, NUM_SAMPLES * sizeof(int));  // Tworzymy kopię tablicy, aby posortować
    std::sort(sorted_values, sorted_values + NUM_SAMPLES);  // Sortujemy tablicę

    int median = sorted_values[NUM_SAMPLES / 2];

    // Obliczanie średniej i odchylenia standardowego
    long sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      sum += rain_values[i];
    }
    float mean = sum / NUM_SAMPLES;

    float variance = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      variance += pow(rain_values[i] - mean, 2);
    }
    float stddev = sqrt(variance / NUM_SAMPLES);

    // Odrzucanie wartości, które są poza 2 odchyleniami standardowymi
    int valid_samples = 0;
    long valid_sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      if (abs(rain_values[i] - mean) <= 2 * stddev) {
        valid_samples++;
        valid_sum += rain_values[i];
      }
    }

    if (valid_samples > 0) {
      int avg_rain_value = valid_sum / valid_samples;  // Średnia z prawidłowych próbek

      // Dodaj średnią do historii
      for (int i = NUM_HISTORY - 1; i > 0; i--) {
        history[i] = history[i - 1];
      }
      history[0] = avg_rain_value;

      // Wyświetlanie wartości na LCD (zamiast napisu "Avg")
      lcd.clear();  // Czyść ekran LCD
      lcd.setCursor(0, 0);  // Ustaw kursor na pierwszym wierszu

      // Określenie kategorii deszczu
      String rain_category = "No rain";
      if (avg_rain_value < 1000) {
        rain_category = "Big rain";
      } else if (avg_rain_value < 2000) {
        rain_category = "Med. rain";
      } else if (avg_rain_value < 3000) {
        rain_category = "Small rain";
      }

      lcd.print(rain_category);  // Wyświetl kategorię deszczu
      lcd.print(": ");
      int avg_rain_value_p = map(avg_rain_value, 0, 4095, 100, 0);  // Przekształć wartość na procenty
      lcd.print(avg_rain_value_p);  // Wyświetl odczytaną wartość w procentach
      lcd.print(" %");

      // Określenie trendu deszczu na podstawie historii
      int avg_history = 0;
      for (int i = 0; i < NUM_HISTORY; i++) {
        avg_history += history[i];
      }
      avg_history /= NUM_HISTORY;

      // Określanie, czy deszcz rośnie, maleje, czy jest stabilny
      if (avg_rain_value > 1.05 * avg_history) {
        rain_trend = "Falling";  // Trend opadający
      } else if (avg_rain_value < 0.95 * avg_history) {
        rain_trend = "Rising";  // Trend rosnący
      } else {
        rain_trend = "Stable";  // Trend stabilny
      }

      // Wyświetlanie trendu w drugim rzędzie
      lcd.setCursor(0, 1);  // Ustaw kursor na drugą linię
      lcd.print("Trend: ");
      lcd.print(rain_trend);  // Wyświetl trend deszczu

      previous_avg_rain_value = avg_rain_value;  // Zapisujemy obecną średnią wartość jako poprzednią
    
      String bt_message = String(rain_category) + ": " + String(avg_rain_value_p) + "%\nTrend: " + String(rain_trend) + "\n";
      
      SerialBT.print(bt_message);
  }

  // Obsługa komunikacji Bluetooth
  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }
  if (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }


    lastUpdateTime = CurrentMillis;  // Zaktualizuj czas ostatniej aktualizacji
    delay(500);  // Opóźnienie 0.5s, aby nie czytać zbyt często
  }
}

