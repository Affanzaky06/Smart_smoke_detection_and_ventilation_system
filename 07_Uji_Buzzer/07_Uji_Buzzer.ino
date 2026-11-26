// ============================================================
// PENGUJIAN KOMPONEN: Buzzer aktif (HIGH = bunyi)
// Pin : GPIO 5
// Serial 115200:
//   1 = ON terus   0 = OFF
//   w = pola WASPADA (bip lambat)
//   b = pola BAHAYA  (bip cepat)
//   s = uji otomatis (semua pola)
// ============================================================
#include <Arduino.h>

const int PIN_BUZZER = 5;

void setBuzzer(bool on) {
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

void beep(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    setBuzzer(true);
    delay(onMs);
    setBuzzer(false);
    delay(offMs);
  }
}

void autoTest() {
  Serial.println("Uji: bunyi 1 detik");
  setBuzzer(true);  delay(1000);  setBuzzer(false);  delay(700);
  Serial.println("Uji: pola WASPADA");
  beep(3, 400, 600);
  delay(700);
  Serial.println("Uji: pola BAHAYA");
  beep(8, 120, 120);
  Serial.println("Uji selesai");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== UJI BUZZER ===");
  Serial.println("Perintah: 1=ON | 0=OFF | w=waspada | b=bahaya | s=uji otomatis");
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  autoTest();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c == '1') { setBuzzer(true);  Serial.println("Buzzer ON"); }
  else if (c == '0') { setBuzzer(false); Serial.println("Buzzer OFF"); }
  else if (c == 'w' || c == 'W') { Serial.println("Pola WASPADA"); beep(3, 400, 600); }
  else if (c == 'b' || c == 'B') { Serial.println("Pola BAHAYA");  beep(8, 120, 120); }
  else if (c == 's' || c == 'S') autoTest();
}
