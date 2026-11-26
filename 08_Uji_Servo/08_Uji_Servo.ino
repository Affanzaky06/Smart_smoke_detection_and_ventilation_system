// ============================================================
// PENGUJIAN KOMPONEN: Servo ventilasi 1 & 2
// Pin servo 1 : GPIO 23     Pin servo 2 : GPIO 22
// Library     : ESP32Servo
// Posisi      : TUTUP = 0 derajat, BUKA = 90 derajat
// Serial 115200:
//   o = buka kedua servo     c = tutup kedua servo
//   1 = toggle servo 1       2 = toggle servo 2
//   w = sweep 0-90-0 (uji gerak halus)
//   s = uji otomatis
// Catatan: beri catu daya 5V terpisah untuk servo (GND disatukan dengan ESP32).
// ============================================================
#include <Arduino.h>
#include <ESP32Servo.h>

const int PIN_SERVO1 = 23;
const int PIN_SERVO2 = 22;

const int SERVO_CLOSED = 0;
const int SERVO_OPEN   = 90;

Servo servo1, servo2;
bool s1Open = false, s2Open = false;

void setServo1(bool open) {
  s1Open = open;
  servo1.write(open ? SERVO_OPEN : SERVO_CLOSED);
  Serial.printf("Servo 1 -> %s\n", open ? "BUKA (90)" : "TUTUP (0)");
}

void setServo2(bool open) {
  s2Open = open;
  servo2.write(open ? SERVO_OPEN : SERVO_CLOSED);
  Serial.printf("Servo 2 -> %s\n", open ? "BUKA (90)" : "TUTUP (0)");
}

void sweepTest() {
  Serial.println("Sweep 0 -> 90 -> 0");
  for (int a = SERVO_CLOSED; a <= SERVO_OPEN; a += 5) {
    servo1.write(a); servo2.write(a); delay(40);
  }
  for (int a = SERVO_OPEN; a >= SERVO_CLOSED; a -= 5) {
    servo1.write(a); servo2.write(a); delay(40);
  }
  s1Open = s2Open = false;
}

void autoTest() {
  Serial.println("--- Uji otomatis servo ---");
  setServo1(false); setServo2(false); delay(1000);
  setServo1(true);  delay(1500);
  setServo1(false); delay(1000);
  setServo2(true);  delay(1500);
  setServo2(false); delay(1000);
  setServo1(true);  setServo2(true);  delay(1500);
  setServo1(false); setServo2(false); delay(1000);
  sweepTest();
  Serial.println("--- Selesai ---");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== UJI SERVO ===");
  Serial.println("Perintah: o=buka semua | c=tutup semua | 1/2=toggle | w=sweep | s=uji otomatis");

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  servo1.setPeriodHertz(50);
  servo1.attach(PIN_SERVO1, 500, 2400);
  servo2.setPeriodHertz(50);
  servo2.attach(PIN_SERVO2, 500, 2400);

  setServo1(false);
  setServo2(false);
  delay(800);
  autoTest();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c == 'o' || c == 'O') { setServo1(true);  setServo2(true); }
  else if (c == 'c' || c == 'C') { setServo1(false); setServo2(false); }
  else if (c == '1') setServo1(!s1Open);
  else if (c == '2') setServo2(!s2Open);
  else if (c == 'w' || c == 'W') sweepTest();
  else if (c == 's' || c == 'S') autoTest();
}
