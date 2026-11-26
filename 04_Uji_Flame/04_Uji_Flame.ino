// ============================================================
// PENGUJIAN KOMPONEN: Flame Sensor (keluaran digital DO)
// Pin DO  : GPIO 17
// Polaritas: FLAME_ACTIVE_LOW = true -> api terdeteksi saat pin LOW
// Cara uji: dekatkan api (korek/lilin) dengan jarak aman ke sensor,
//           atur potensiometer modul bila sensitivitas kurang.
// ============================================================
#include <Arduino.h>

const int PIN_FLAME_DO = 17;
const bool FLAME_ACTIVE_LOW = true;

bool lastState = false;
unsigned long lastPrint = 0;

bool readFlame() {
  int v = digitalRead(PIN_FLAME_DO);
  return FLAME_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== UJI FLAME SENSOR ===");
  pinMode(PIN_FLAME_DO, FLAME_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  lastState = readFlame();
}

void loop() {
  bool f = readFlame();

  // Cetak saat terjadi perubahan
  if (f != lastState) {
    lastState = f;
    Serial.println(f ? ">>> API TERDETEKSI <<<" : "Api hilang / tidak terdeteksi");
  }

  // Cetak berkala
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.printf("Pin DO = %d | Api = %s\n", digitalRead(PIN_FLAME_DO), f ? "YA" : "TIDAK");
  }
}
