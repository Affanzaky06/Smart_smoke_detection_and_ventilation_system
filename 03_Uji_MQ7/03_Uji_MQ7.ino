// ============================================================
// PENGUJIAN KOMPONEN: MQ-7 (sensor karbon monoksida (CO))
// Pin AO  : GPIO 32 (ADC1, input-only)
// Fungsi  : membaca nilai ADC mentah, tegangan, baseline, dan delta
//           lalu menampilkan status AMAN / WASPADA / BAHAYA
//           dengan ambang yang sama seperti program utama.
// Serial  : 115200 baud. Kirim 'k' untuk kalibrasi ulang baseline.
// ============================================================
#include <Arduino.h>

const int PIN_SENSOR = 32;

// Ambang relatif terhadap baseline (sama dengan program utama)
const int WARN_DELTA   = 300;
const int DANGER_DELTA = 500;

const unsigned long WARMUP_MS   = 30000;  // pemanasan singkat untuk uji (program utama: 120000)
const unsigned long INTERVAL_MS = 1000;

int baseline = 0;
unsigned long lastRead = 0;

int readAvg(int pin, int n = 16) {
  long sum = 0;
  for (int i = 0; i < n; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (int)(sum / n);
}

void calibrate() {
  Serial.println("[KALIBRASI] Pastikan udara bersih, mengambil baseline...");
  long sum = 0;
  const int N = 20;
  for (int i = 0; i < N; i++) {
    sum += readAvg(PIN_SENSOR);
    delay(100);
  }
  baseline = (int)(sum / N);
  Serial.printf("[KALIBRASI] Baseline = %d\n", baseline);
}

void warmup() {
  Serial.printf("Pemanasan sensor %lu detik...\n", WARMUP_MS / 1000);
  for (unsigned long t = 0; t < WARMUP_MS; t += 5000) {
    Serial.printf("  %lu/%lu detik | ADC = %d\n", t / 1000, WARMUP_MS / 1000, readAvg(PIN_SENSOR));
    delay(5000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== UJI MQ-7 ===");

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_SENSOR, INPUT);

  warmup();
  calibrate();
  Serial.println("Mulai pembacaan. Beri paparan (mis. asap/gas) untuk melihat perubahan nilai.");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'k' || c == 'K') calibrate();
  }

  if (millis() - lastRead >= INTERVAL_MS) {
    lastRead = millis();

    int raw   = readAvg(PIN_SENSOR);
    int delta = raw - baseline;
    float volt = raw * 3.3f / 4095.0f;

    const char* st = "AMAN";
    if (delta >= DANGER_DELTA) st = "BAHAYA";
    else if (delta >= WARN_DELTA) st = "WASPADA";

    Serial.printf("ADC=%4d | V=%.2f | Baseline=%4d | Delta=%4d | Status=%s\n",
                  raw, volt, baseline, delta, st);
  }
}
