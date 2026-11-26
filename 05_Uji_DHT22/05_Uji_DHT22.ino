// ============================================================
// PENGUJIAN KOMPONEN: DHT22 (suhu & kelembapan)
// Pin DATA : GPIO 18
// Library  : "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
// Catatan  : DHT22 minimal 2 detik antar pembacaan.
// ============================================================
#include <Arduino.h>
#include <DHT.h>

const int PIN_DHT = 18;
#define DHT_TYPE DHT22

const float TEMP_WARN_C   = 40.0;
const float TEMP_DANGER_C = 50.0;
const unsigned long DHT_INTERVAL_MS = 2000;

DHT dht(PIN_DHT, DHT_TYPE);
unsigned long lastDht = 0;
int okCount = 0, failCount = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== UJI DHT22 ===");
  dht.begin();
}

void loop() {
  if (millis() - lastDht < DHT_INTERVAL_MS) return;
  lastDht = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    failCount++;
    Serial.printf("[GAGAL] Pembacaan DHT22 gagal (OK=%d, GAGAL=%d). Cek kabel DATA & pull-up 10k.\n",
                  okCount, failCount);
    return;
  }

  okCount++;
  const char* st = "AMAN";
  if (t >= TEMP_DANGER_C) st = "BAHAYA";
  else if (t >= TEMP_WARN_C) st = "WASPADA";

  Serial.printf("Suhu=%.1f C | Kelembapan=%.1f %% | Status suhu=%s | (OK=%d, GAGAL=%d)\n",
                t, h, st, okCount, failCount);
}
