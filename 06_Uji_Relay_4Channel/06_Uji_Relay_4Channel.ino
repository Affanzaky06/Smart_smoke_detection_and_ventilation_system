// ============================================================
// PENGUJIAN KOMPONEN: Modul Relay 4 Kanal (kipas 1-4)
// Pin IN1..IN4 : GPIO 4, 16, 33, 25
// Polaritas    : RELAY_ACTIVE_LOW = true (relay menyala saat pin LOW)
// Serial 115200:
//   1..4 = toggle relay/kipas nomor tersebut
//   a    = semua ON     o = semua OFF
//   s    = jalankan uji urut otomatis (1 -> 4)
// ============================================================
#include <Arduino.h>

const int PIN_RELAY[4] = { 4, 16, 33, 25 };

const bool RELAY_ACTIVE_LOW = true;
const bool RELAY_HIGHZ_OFF  = false;  // true bila relay opto 5V tidak mati sempurna di 3,3V

bool relayState[4] = { false, false, false, false };

void writeRelayPin(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  if (RELAY_ACTIVE_LOW && RELAY_HIGHZ_OFF && !on) {
    pinMode(pin, INPUT);
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
  }
}

void setRelay(int idx, bool on) {
  relayState[idx] = on;
  writeRelayPin(PIN_RELAY[idx], on);
  Serial.printf("Relay/Kipas %d (GPIO %d) -> %s\n", idx + 1, PIN_RELAY[idx], on ? "ON" : "OFF");
}

void setAll(bool on) {
  for (int i = 0; i < 4; i++) setRelay(i, on);
}

void sequenceTest() {
  Serial.println("--- Uji urut dimulai ---");
  setAll(false);
  delay(500);
  for (int i = 0; i < 4; i++) {
    setRelay(i, true);
    delay(1500);
    setRelay(i, false);
    delay(500);
  }
  Serial.println("--- Uji urut selesai ---");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== UJI RELAY 4 KANAL ===");
  Serial.println("Perintah: 1-4 toggle | a=semua ON | o=semua OFF | s=uji urut");
  setAll(false);
  sequenceTest();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '1' && c <= '4') {
    int i = c - '1';
    setRelay(i, !relayState[i]);
  } else if (c == 'a' || c == 'A') {
    setAll(true);
  } else if (c == 'o' || c == 'O') {
    setAll(false);
  } else if (c == 's' || c == 'S') {
    sequenceTest();
  }
}
