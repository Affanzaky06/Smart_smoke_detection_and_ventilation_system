#ifndef ENABLE_WIFI_MQTT
#define ENABLE_WIFI_MQTT 1  // 0 = tes lokal saja, 1 = aktifkan WiFi + MQTT
#endif

#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <Preferences.h>

#if ENABLE_WIFI_MQTT
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>  // captive portal konfigurasi WiFi
#include <Preferences.h>  // penyimpanan IP/port MQTT di NVS ESP32
#endif

// ======================= PENGATURAN JARINGAN =======================
#if ENABLE_WIFI_MQTT
// Kredensial WiFi tidak lagi ditulis di firmware.
// WiFiManager menyimpan SSID/password WiFi di penyimpanan internal ESP32.
const char* WIFI_AP_NAME = "SMART_SMOKE_SETUP";
const char* WIFI_AP_PASS = "infokopag";  // ubah menjadi password unik (minimal 8 karakter)

// Broker MQTT dapat diubah dari dashboard Node-RED dan disimpan di NVS.
char mqttHost[64] = "192.168.1.5";  // nilai awal jika belum pernah dikonfigurasi
uint16_t mqttPort = 1883;

// Kredensial autentikasi broker tetap seperti konfigurasi awal.
// Ganti jika username/password broker Anda berbeda.
const char* MQTT_USER = "nodered";
const char* MQTT_PASS = "noderedpass";
const char* DEVICE_ID = "esp32-smoke-01";

Preferences networkPrefs;
WiFiManager wifiManager;

bool wifiResetRequested = false;
bool wifiPortalActive = false;
unsigned long lastWifiTry = 0;
unsigned long lastMqttTry = 0;

const char* T_SENSOR = "smoke/room1/sensor";
const char* T_STATUS = "smoke/room1/status";
const char* T_CONTROL = "smoke/room1/control";
const char* T_ONLINE = "smoke/room1/online";
const char* T_AKTUATOR = "smoke/room1/aktuator";  // keputusan aktuator dari Node-RED
#endif

// ======================= PIN =======================
const int PIN_MQ2 = 34;       // ADC1, input-only
const int PIN_MQ135 = 35;     // ADC1, input-only
const int PIN_MQ7 = 32;       // ADC1
const int PIN_FLAME_DO = 17;  // flame sensor, keluaran digital
const int PIN_DHT = 18;
const int PIN_RELAY = 4;
const int PIN_RELAY2 = 16;
const int PIN_RELAY3 = 33;
const int PIN_RELAY4 = 25;
const int PIN_BUZZER = 5;
const int PIN_SERVO = 23;
const int PIN_SERVO2 = 22;

// ======================= KOMPONEN YANG TERPASANG =======================
const bool USE_MQ2 = true;
const bool USE_MQ135 = true;
const bool USE_MQ7 = true;
const bool USE_FLAME = true;
const bool USE_DHT = true;
const bool USE_SERVO = true;

#define DHT_TYPE DHT22

// ======================= POLARITAS MODUL =======================
const bool RELAY_ACTIVE_LOW = true;  // true: relay menyala saat pin LOW
const bool FLAME_ACTIVE_LOW = true;  // true: api terdeteksi saat pin LOW
const bool RELAY_HIGHZ_OFF = false;  // true bila relay opto 5 V tidak mati sempurna di 3,3 V
// Buzzer diasumsikan buzzer aktif (HIGH = bunyi)

// ======================= WAKTU =======================
const unsigned long WARMUP_MS = 120000;  // kelipatan 10 detik
const unsigned long READ_INTERVAL_MS = 1000;
const unsigned long DHT_INTERVAL_MS = 2000;      // DHT minimal 2 detik
const unsigned long PUBLISH_INTERVAL_MS = 5000;  // status dikirim berkala

// ======================= MODE CLOUD / LOKAL =======================
const unsigned long SENSOR_PUBLISH_MS = 1000;  // data sensor ke Node-RED tiap detik (agar keputusan cepat)
const unsigned long CLOUD_TIMEOUT_MS = 5000;   // tanpa keputusan dari Node-RED selama ini -> kembali LOKAL
// true  = api / suhu tinggi / gas berbahaya tetap memicu alarm LOKAL walau mode CLOUD aktif
// false = semua keputusan murni dari Node-RED selama mode CLOUD (sesuai permintaan)
const bool LOCAL_SAFETY_OVERRIDE = false;

// ======================= AMBANG (relatif terhadap baseline, skala ADC 0-4095) =======================
// HARUS sama dengan konstanta di function "Logika Cloud" di Node-RED.
const int SMOKE_WARN_DELTA = 300;
const int SMOKE_DANGER_DELTA = 500;
const int AIR_WARN_DELTA = 300;
const int CO_DANGER_DELTA = 500;
const float TEMP_DANGER_C = 50.0;
const float TEMP_WARN_C = 40.0;
const int AIR_DANGER_DELTA = 500;
const int CO_WARN_DELTA = 300;

const int HYST_DELTA = 80;
const int CONFIRM_UP = 2;
const int CONFIRM_DOWN = 5;

const int SERVO_CLOSED = 0;
const int SERVO_OPEN = 90;

// ======================= OBJEK & STATUS =======================
DHT dht(PIN_DHT, DHT_TYPE);
Servo ventServo;
Servo ventServo2;

Preferences baselinePrefs;

enum Status { AMAN = 0,
              WASPADA = 1,
              BAHAYA = 2 };
Status status = AMAN;

bool manualMode = false;
bool manualFan = false;
bool fanOn = false;
bool fan2On = false;
bool fan3On = false;
bool fan4On = false;

bool fan1Manual = false;
bool fan2Manual = false;
bool fan3Manual = false;
bool fan4Manual = false;

bool fan1Cmd = false;
bool fan2Cmd = false;
bool fan3Cmd = false;
bool fan4Cmd = false;

bool servo1Open = false;
bool servo2Open = false;

bool servo1Manual = false;
bool servo2Manual = false;

bool servo1Cmd = false;
bool servo2Cmd = false;

bool buzzerOn = false;

bool servoOpen = false;    // posisi servo saat ini (true = BUKA)
bool servoManual = false;  // true = servo diatur tombol, false = servo mengikuti kipas
bool servoCmd = false;     // posisi yang diminta saat servoManual = true

bool cmdTest = false;
bool cmdCalibrate = false;

bool cloudMode = false;         // mode otak yang sedang dipakai
unsigned long lastCloudMs = 0;  // kapan keputusan Node-RED terakhir diterima

int baseSmoke = 0, baseAir = 0, baseCO = 0;
int rawSmoke = 0, rawAir = 0, rawCO = 0;
bool flame = false;
float temp = NAN, hum = NAN;

int upCount = 0, downCount = 0;
unsigned long lastRead = 0, lastDht = 0;

#if ENABLE_WIFI_MQTT
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
unsigned long lastPublish = 0, lastSensorPub = 0;
#endif

// ======================= PROTOTIPE =======================
void publishStatus();
void publishSensors();
bool cloudActive();

// ======================= FUNGSI BANTU =======================
const char* statusName(Status s) {
  if (s == BAHAYA) return "BAHAYA";
  if (s == WASPADA) return "WASPADA";
  return "AMAN";
}

const char* fmt1(float v, char* buf, size_t n) {
  if (isnan(v)) snprintf(buf, n, "null");
  else snprintf(buf, n, "%.1f", v);
  return buf;
}

int readAvg(int pin, int n = 16) {
  long sum = 0;
  for (int i = 0; i < n; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (int)(sum / n);
}

// ======================= AKTUATOR =======================
void writeRelayPin(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;

  if (RELAY_ACTIVE_LOW && RELAY_HIGHZ_OFF && !on) {
    pinMode(pin, INPUT);
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
  }
}

void writeRelay(bool fan1, bool fan2, bool fan3, bool fan4) {
  writeRelayPin(PIN_RELAY, fan1);
  writeRelayPin(PIN_RELAY2, fan2);
  writeRelayPin(PIN_RELAY3, fan3);
  writeRelayPin(PIN_RELAY4, fan4);
}

const char* relayPinText(bool on) {
  if (RELAY_ACTIVE_LOW && RELAY_HIGHZ_OFF) return on ? "LOW" : "lepas (high-Z)";
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  return level ? "HIGH" : "LOW";
}

void setFanHW(bool fan1, bool fan2, bool fan3, bool fan4) {
  writeRelay(fan1, fan2, fan3, fan4);
}

// Mengendalikan servo 1 secara terpisah
void setServo1HW(bool open) {
  if (USE_SERVO) {
    ventServo.write(open ? SERVO_OPEN : SERVO_CLOSED);
  }
}

// Mengendalikan servo 2 secara terpisah
void setServo2HW(bool open) {
  if (USE_SERVO) {
    ventServo2.write(open ? SERVO_OPEN : SERVO_CLOSED);
  }
}

// Mengendalikan kedua servo sekaligus jika diperlukan
void setServoHW(bool open) {
  setServo1HW(open);
  setServo2HW(open);
}

void setBuzzerHW(bool on) {
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

void allActuatorsOff() {
  fanOn = false;
  fan2On = false;
  fan3On = false;
  fan4On = false;

  buzzerOn = false;

  servo1Open = false;
  servo2Open = false;
  servoOpen = false;

  setFanHW(false, false, false, false);
  setBuzzerHW(false);

  setServo1HW(false);
  setServo2HW(false);
}

// Menghapus seluruh pengaturan manual.
// Dipakai ketika cloud terputus agar ESP32 kembali otomatis lokal.
void resetManualControl() {
  manualMode = false;
  manualFan = false;

  fan1Manual = false;
  fan2Manual = false;
  fan3Manual = false;
  fan4Manual = false;

  fan1Cmd = false;
  fan2Cmd = false;
  fan3Cmd = false;
  fan4Cmd = false;

  servo1Manual = false;
  servo2Manual = false;

  servo1Cmd = false;
  servo2Cmd = false;

  servoManual = false;
  servoCmd = false;

  Serial.println("[MODE] Semua kontrol manual dihapus.");
  Serial.println("[MODE] ESP32 menggunakan otomatis lokal.");
}

// Menerapkan keadaan aktuator tertentu (dipakai mode CLOUD, mode LOKAL, dan override darurat)
// Menerapkan kondisi 4 kipas, buzzer, dan 2 servo
void applyCloudState(bool f1, bool f2, bool f3, bool f4, bool buz, bool s1, bool s2) {
  fanOn = f1;
  fan2On = f2;
  fan3On = f3;
  fan4On = f4;

  buzzerOn = buz;

  servo1Open = s1;
  servo2Open = s2;

  // Memperbarui kondisi hardware
  setFanHW(fanOn, fan2On, fan3On, fan4On);
  setBuzzerHW(buzzerOn);

  setServo1HW(servo1Open);
  setServo2HW(servo2Open);

  // Memperbarui status servo gabungan untuk kompatibilitas kode lama
  servoOpen = servo1Open || servo2Open;
}



void testActuators() {

  // Pengujian hanya boleh dilakukan saat AMAN dan tidak ada api.
  if (status != AMAN || flame) {
    Serial.println("[TEST] Ditolak: sistem harus AMAN dan tidak ada api.");
    return;
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("   PENGUJIAN SELURUH AKTUATOR");
  Serial.println("================================");

  // Pastikan semua aktuator mati sebelum pengujian
  setFanHW(false, false, false, false);
  setBuzzerHW(false);
  setServo1HW(false);
  setServo2HW(false);

  // ----------------------------------------
  // 1. TES BUZZER
  // ----------------------------------------

  Serial.println("[TEST] Buzzer ON");
  setBuzzerHW(true);
  delay(500);
  setBuzzerHW(false);
  delay(300);

  // ----------------------------------------
  // 2. TES KIPAS 1
  // ----------------------------------------

  Serial.println("[TEST] Kipas 1 ON");
  setFanHW(true, false, false, false);
  delay(1500);
  setFanHW(false, false, false, false);
  delay(500);

  // ----------------------------------------
  // 3. TES KIPAS 2
  // ----------------------------------------

  Serial.println("[TEST] Kipas 2 ON");
  setFanHW(false, true, false, false);
  delay(1500);
  setFanHW(false, false, false, false);
  delay(500);

  // ----------------------------------------
  // 4. TES KIPAS 3
  // ----------------------------------------

  Serial.println("[TEST] Kipas 3 ON");
  setFanHW(false, false, true, false);
  delay(1500);
  setFanHW(false, false, false, false);
  delay(500);

  // ----------------------------------------
  // 5. TES KIPAS 4
  // ----------------------------------------

  Serial.println("[TEST] Kipas 4 ON");
  setFanHW(false, false, false, true);
  delay(1500);
  setFanHW(false, false, false, false);
  delay(500);

  // ----------------------------------------
  // 6. TES SERVO 1
  // ----------------------------------------

  Serial.println("[TEST] Servo 1 BUKA");
  setServo1HW(true);
  delay(1000);

  Serial.println("[TEST] Servo 1 TUTUP");
  setServo1HW(false);
  delay(500);

  // ----------------------------------------
  // 7. TES SERVO 2
  // ----------------------------------------

  Serial.println("[TEST] Servo 2 BUKA");
  setServo2HW(true);
  delay(1000);

  Serial.println("[TEST] Servo 2 TUTUP");
  setServo2HW(false);
  delay(500);

  // ----------------------------------------
  // 8. SELESAI
  // ----------------------------------------

  fanOn = false;
  fan2On = false;
  fan3On = false;
  fan4On = false;

  servo1Open = false;
  servo2Open = false;

  setFanHW(false, false, false, false);
  setServo1HW(false);
  setServo2HW(false);
  setBuzzerHW(false);

  Serial.println("================================");
  Serial.println("[TEST] SELURUH AKTUATOR SELESAI");
  Serial.println("[TEST] Semua kipas OFF");
  Serial.println("[TEST] Servo 1 dan 2 TUTUP");
  Serial.println("================================");
}



// ======================= KALIBRASI =======================

// ======================= PENYIMPANAN BASELINE =======================

// Memuat baseline dari memori permanen ESP32
bool loadBaseline() {
  baselinePrefs.begin("smokeBase", true);

  bool valid = baselinePrefs.getBool("valid", false);

  int savedSmoke = baselinePrefs.getInt("bs", 0);
  int savedAir = baselinePrefs.getInt("ba", 0);
  int savedCO = baselinePrefs.getInt("bc", 0);

  baselinePrefs.end();

  // Periksa apakah nilai baseline masuk akal
  if (USE_MQ2 && (savedSmoke <= 0 || savedSmoke >= 4095)) valid = false;
  if (USE_MQ135 && (savedAir <= 0 || savedAir >= 4095)) valid = false;
  if (USE_MQ7 && (savedCO <= 0 || savedCO >= 4095)) valid = false;

  if (!valid) {
    Serial.println("[BASELINE] Belum ada baseline tersimpan atau datanya tidak valid.");
    return false;
  }

  baseSmoke = USE_MQ2 ? savedSmoke : 0;
  baseAir = USE_MQ135 ? savedAir : 0;
  baseCO = USE_MQ7 ? savedCO : 0;

  Serial.println("[BASELINE] Berhasil memuat baseline dari NVS ESP32.");
  Serial.printf(
    "[BASELINE] MQ2=%d | MQ135=%d | MQ7=%d\n",
    baseSmoke, baseAir, baseCO);

  return true;
}


// Menyimpan baseline terbaru ke memori permanen ESP32
void saveBaseline() {
  baselinePrefs.begin("smokeBase", false);

  baselinePrefs.putInt("bs", baseSmoke);
  baselinePrefs.putInt("ba", baseAir);
  baselinePrefs.putInt("bc", baseCO);
  baselinePrefs.putBool("valid", true);

  baselinePrefs.end();

  Serial.println("[BASELINE] Berhasil disimpan ke NVS ESP32.");
}


// Melakukan kalibrasi baseline baru
void calibrateBaseline() {
  Serial.println("[BASELINE] Kalibrasi dimulai...");
  Serial.println("[BASELINE] Pastikan kondisi udara bersih.");

  baseSmoke = USE_MQ2 ? readAvg(PIN_MQ2, 32) : 0;
  baseAir = USE_MQ135 ? readAvg(PIN_MQ135, 32) : 0;
  baseCO = USE_MQ7 ? readAvg(PIN_MQ7, 32) : 0;

  // Tolak hasil kalibrasi jika sensor aktif menghasilkan nilai jenuh
  bool valid = true;

  if (USE_MQ2 && (baseSmoke <= 0 || baseSmoke >= 4095)) valid = false;
  if (USE_MQ135 && (baseAir <= 0 || baseAir >= 4095)) valid = false;
  if (USE_MQ7 && (baseCO <= 0 || baseCO >= 4095)) valid = false;

  if (!valid) {
    Serial.println("[BASELINE] GAGAL: nilai ADC tidak valid atau mencapai batas.");
    Serial.println("[BASELINE] Baseline lama tidak ditimpa.");
    return;
  }

  Serial.printf(
    "[BASELINE] Hasil: MQ2=%d | MQ135=%d | MQ7=%d\n",
    baseSmoke, baseAir, baseCO);

  saveBaseline();
}


// Pemanasan sensor lalu memuat atau membuat baseline
void warmupAndCalibrate() {
  Serial.printf(
    "[SENSOR] Pemanasan selama %lu detik...\n",
    WARMUP_MS / 1000);

  for (long s = (long)(WARMUP_MS / 1000); s > 0; s -= 10) {
    Serial.printf("  Sisa %ld detik\n", s);
    delay(10000);
  }

  Serial.println("[SENSOR] Pemanasan selesai.");

  // Gunakan baseline lama jika tersedia
  if (loadBaseline()) {
    Serial.println("[BASELINE] Menggunakan baseline tersimpan.");
  } else {
    // Kalibrasi pertama kali jika belum ada baseline
    Serial.println("[BASELINE] Melakukan kalibrasi pertama...");
    calibrateBaseline();
  }
}

// ======================= BACA SENSOR =======================
void readSensors() {
  rawSmoke = USE_MQ2 ? readAvg(PIN_MQ2) : 0;
  rawAir = USE_MQ135 ? readAvg(PIN_MQ135) : 0;
  rawCO = USE_MQ7 ? readAvg(PIN_MQ7) : 0;

  if (USE_FLAME) {
    int d = digitalRead(PIN_FLAME_DO);
    flame = FLAME_ACTIVE_LOW ? (d == LOW) : (d == HIGH);
  } else {
    flame = false;
  }

  if (USE_DHT && (millis() - lastDht >= DHT_INTERVAL_MS)) {
    lastDht = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temp = t;
    if (!isnan(h)) hum = h;
  }
}

// ======================= LOGIKA LOKAL =======================

Status evaluateRaw() {

  // Hitung selisih ADC terhadap baseline
  int deltaSmoke = rawSmoke - baseSmoke;
  int deltaAir = rawAir - baseAir;
  int deltaCO = rawCO - baseCO;

  // Histeresis
  int hd = (status == BAHAYA) ? HYST_DELTA : 0;
  int hw = (status != AMAN) ? HYST_DELTA : 0;

  float ht = (status == BAHAYA) ? 2.0f : 0.0f;

  // =====================================
  // DETEKSI BAHAYA
  // Satu sensor saja cukup memicu BAHAYA
  // =====================================

  bool danger =
    (USE_MQ2 && deltaSmoke >= (SMOKE_DANGER_DELTA - hd)) || (USE_MQ135 && deltaAir >= (AIR_DANGER_DELTA - hd)) || (USE_MQ7 && deltaCO >= (CO_DANGER_DELTA - hd)) || (USE_DHT && !isnan(temp) && temp >= (TEMP_DANGER_C - ht)) || (USE_FLAME && flame);

  if (danger) {
    return BAHAYA;
  }

  // =====================================
  // DETEKSI WASPADA
  // =====================================

  bool warn =
    (USE_MQ2 && deltaSmoke >= (SMOKE_WARN_DELTA - hw)) || (USE_MQ135 && deltaAir >= (AIR_WARN_DELTA - hw)) || (USE_MQ7 && deltaCO >= (CO_WARN_DELTA - hw));
  (USE_DHT && !isnan(temp) && temp >= (TEMP_WARN_C - ht));

  return warn ? WASPADA : AMAN;
}


// Kondisi darurat murni dari pembacaan saat ini (tanpa histeresis), untuk override opsional.
bool lokalDarurat() {

  int deltaSmoke = rawSmoke - baseSmoke;
  int deltaAir = rawAir - baseAir;
  int deltaCO = rawCO - baseCO;

  return (USE_MQ2 && deltaSmoke >= SMOKE_DANGER_DELTA) || (USE_MQ135 && deltaAir >= AIR_DANGER_DELTA) || (USE_MQ7 && deltaCO >= CO_DANGER_DELTA) || (USE_DHT && !isnan(temp) && temp >= TEMP_DANGER_C) || (USE_FLAME && flame);
}

void updateStatus() {

  // API langsung memicu BAHAYA.
  if (USE_FLAME && flame) {
    status = BAHAYA;
    upCount = 0;
    downCount = 0;
    return;
  }

  Status cand = evaluateRaw();

  if (cand == status) {
    upCount = 0;
    downCount = 0;
  } else if (cand > status) {
    downCount = 0;

    if (++upCount >= CONFIRM_UP) {
      status = cand;
      upCount = 0;
    }
  } else {
    upCount = 0;

    if (++downCount >= CONFIRM_DOWN) {
      status = cand;
      downCount = 0;
    }
  }
}


bool applyActuators() {

  bool wantFan1 = false;
  bool wantFan2 = false;
  bool wantFan3 = false;
  bool wantFan4 = false;

  bool wantBuzzer = false;

  bool wantServo1 = false;
  bool wantServo2 = false;

  // =====================================
  // PENGATURAN KIPAS DAN BUZZER
  // =====================================

  // Prioritas tertinggi: kondisi BAHAYA
  if (status == BAHAYA) {

    wantFan1 = true;
    wantFan2 = true;
    wantFan3 = true;
    wantFan4 = true;

    wantBuzzer = true;

  }

  // Mode MANUAL
  else if (manualMode) {

    wantFan1 = fan1Manual && fan1Cmd;
    wantFan2 = fan2Manual && fan2Cmd;
    wantFan3 = fan3Manual && fan3Cmd;
    wantFan4 = fan4Manual && fan4Cmd;

  }

  // Mode AUTO - WASPADA
  else if (status == WASPADA) {

    wantFan1 = true;
    wantFan2 = true;

    wantFan3 = false;
    wantFan4 = false;

  }

  // Mode AUTO - AMAN
  else {

    wantFan1 = false;
    wantFan2 = false;
    wantFan3 = false;
    wantFan4 = false;
  }

  // =====================================
  // PENGATURAN SERVO
  // =====================================

  // PRIORITAS 1: API
  // Kedua servo wajib tertutup.
  if (flame) {
    wantServo1 = false;
    wantServo2 = false;
  }

  // PRIORITAS 2: BAHAYA NON-API
  // Kedua servo otomatis terbuka.
  else if (status == BAHAYA) {
    wantServo1 = true;
    wantServo2 = true;
  }

  // PRIORITAS 3: MODE MANUAL
  else {
    wantServo1 = servo1Manual && servo1Cmd;
    wantServo2 = servo2Manual && servo2Cmd;
  }
  // =====================================
  // CEK PERUBAHAN KONDISI
  // =====================================

  bool changed =
    (wantFan1 != fanOn) || (wantFan2 != fan2On) || (wantFan3 != fan3On) || (wantFan4 != fan4On) || (wantBuzzer != buzzerOn) || (wantServo1 != servo1Open) || (wantServo2 != servo2Open);

  // Simpan kondisi terbaru
  fanOn = wantFan1;
  fan2On = wantFan2;
  fan3On = wantFan3;
  fan4On = wantFan4;

  buzzerOn = wantBuzzer;

  servo1Open = wantServo1;
  servo2Open = wantServo2;

  servoOpen = servo1Open || servo2Open;

  // Terapkan ke hardware
  setFanHW(fanOn, fan2On, fan3On, fan4On);
  setBuzzerHW(buzzerOn);

  setServo1HW(servo1Open);
  setServo2HW(servo2Open);

  return changed;
}

void forceSync() {

  setFanHW(
    fanOn,
    fan2On,
    fan3On,
    fan4On);

  setBuzzerHW(buzzerOn);

  setServo1HW(servo1Open);
  setServo2HW(servo2Open);
}

// Dipanggil setelah perintah (Serial/MQTT).
// Mode CLOUD: hanya meneruskan status manual terbaru ke Node-RED, yang memutuskan.
// Mode LOKAL: langsung diterapkan.
void reportCommand(const char* what) {
  if (cloudActive()) {
    Serial.printf("[CMD] %s -> diteruskan ke Node-RED (mode CLOUD)\n", what);
    publishSensors();  // kirim status manual terbaru agar keputusan segera diperbarui
    return;
  }
  applyActuators();
  forceSync();
  Serial.printf("[CMD] %s -> status=%s kipas=%d buzzer=%d servo=%s mode=%s | pin relay=%s (relay harus %s)\n",
                what, statusName(status), fanOn ? 1 : 0, buzzerOn ? 1 : 0,
                servoOpen ? "BUKA" : "TUTUP",
                manualMode ? "MANUAL" : "AUTO", relayPinText(fanOn), fanOn ? "ON" : "OFF");
  if (status == BAHAYA) {
    Serial.println(
      flame
        ? "[INFO] BAHAYA + API: kipas ON, buzzer ON, servo TUTUP"
        : "[INFO] BAHAYA tanpa api: kipas ON, buzzer ON, servo BUKA");
  }
}

// ======================= SERIAL =======================
void printSerial() {
  char tb[12], hb[12];
  if (USE_MQ2) Serial.printf("MQ2=%d(%+d) ", rawSmoke, rawSmoke - baseSmoke);
  if (USE_MQ135) Serial.printf("MQ135=%d(%+d) ", rawAir, rawAir - baseAir);
  if (USE_MQ7) Serial.printf("MQ7=%d(%+d) ", rawCO, rawCO - baseCO);
  if (USE_FLAME) Serial.printf("api=%d ", flame ? 1 : 0);
  if (USE_DHT) Serial.printf("suhu=%s hum=%s ", fmt1(temp, tb, sizeof(tb)), fmt1(hum, hb, sizeof(hb)));
  Serial.printf("| %s kipas=%d buzzer=%d servo=%s mode=%s otak=%s\n",
                statusName(status), fanOn ? 1 : 0, buzzerOn ? 1 : 0,
                (USE_SERVO ? (servoOpen ? "BUKA" : "TUTUP") : "nonaktif"),
                manualMode ? "MANUAL" : "AUTO",
                cloudMode ? "CLOUD" : "LOKAL");
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '1') {
      manualMode = true;
      manualFan = true;
      reportCommand("kipas ON (manual)");
    } else if (c == '0') {
      manualMode = true;
      manualFan = false;
      reportCommand("kipas OFF (manual)");
    } else if (c == 'a') {
      manualMode = false;
      servoManual = false;
      reportCommand("mode otomatis");
    } else if (c == 'o') {
      manualMode = true;

      servo1Manual = true;
      servo2Manual = true;

      servo1Cmd = true;
      servo2Cmd = true;

      reportCommand("kedua servo BUKA (manual)");

    } else if (c == 'c') {
      manualMode = true;

      servo1Manual = true;
      servo2Manual = true;

      servo1Cmd = false;
      servo2Cmd = false;

      reportCommand("kedua servo TUTUP (manual)");
    } else if (c == 'k') {
      calibrateBaseline();
    } else if (c == 't') {
      cmdTest = true;
    }
  }
}


void handleQueuedCommands() {
  if (cmdTest) {
    cmdTest = false;
    if (status == AMAN) testActuators();
    else Serial.println("[TEST] ditolak: status bukan AMAN");
  }
  if (cmdCalibrate) {
    cmdCalibrate = false;
    if (status == AMAN) calibrateBaseline();
    else Serial.println("[KALIBRASI] ditolak: status bukan AMAN (baseline harus diambil di udara bersih)");
  }
}

// ======================= WIFI + MQTT =======================
#if ENABLE_WIFI_MQTT

// Memuat konfigurasi broker yang terakhir disimpan.
void loadNetworkConfig() {
  networkPrefs.begin("smokeCfg", true);
  String savedHost = networkPrefs.getString("mqttHost", mqttHost);
  uint16_t savedPort = networkPrefs.getUShort("mqttPort", mqttPort);
  networkPrefs.end();

  if (savedHost.length() > 0 && savedHost.length() < sizeof(mqttHost)) {
    savedHost.toCharArray(mqttHost, sizeof(mqttHost));
  }
  if (savedPort > 0) mqttPort = savedPort;

  Serial.printf("[CONFIG] Broker MQTT: %s:%u\n", mqttHost, mqttPort);
}

void saveMqttConfig(const String& host, uint16_t port) {
  networkPrefs.begin("smokeCfg", false);
  networkPrefs.putString("mqttHost", host);
  networkPrefs.putUShort("mqttPort", port);
  networkPrefs.end();

  host.toCharArray(mqttHost, sizeof(mqttHost));
  mqttPort = port;

  // Putuskan koneksi lama agar koneksi berikutnya memakai broker baru.
  mqtt.disconnect();
  mqtt.setServer(mqttHost, mqttPort);
  lastMqttTry = 0;
  lastCloudMs = 0;
  Serial.printf("[CONFIG] Broker baru tersimpan: %s:%u\n", mqttHost, mqttPort);
}

void startWifiConfigPortal() {
  WiFi.mode(WIFI_STA);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConnectTimeout(8);
  wifiManager.setWiFiAutoReconnect(true);

  Serial.println("[WiFi] Membuka portal konfigurasi...");
  Serial.printf("[WiFi] Hubungkan HP/laptop ke SSID: %s\n", WIFI_AP_NAME);
  Serial.println("[WiFi] Password AP tersedia pada konstanta WIFI_AP_PASS di firmware.");
  Serial.println("[WiFi] Buka http://192.168.4.1 jika portal tidak muncul otomatis.");

  wifiPortalActive = wifiManager.startConfigPortal(WIFI_AP_NAME, WIFI_AP_PASS);
  // Pada mode non-blocking, fungsi kembali segera; portal diproses di loop().
  wifiPortalActive = wifiManager.getConfigPortalActive();
}

void beginWiFiProvisioning() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConnectTimeout(8);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setHostname("esp32-smoke-01");

  Serial.println("[WiFi] Memeriksa kredensial WiFi tersimpan...");
  bool connected = wifiManager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASS);

  wifiPortalActive = wifiManager.getConfigPortalActive();
  if (connected || WiFi.status() == WL_CONNECTED) {
    wifiPortalActive = false;
    Serial.print("[WiFi] Terhubung. IP ESP32: ");
    Serial.println(WiFi.localIP());
  } else if (wifiPortalActive) {
    Serial.println("[WiFi] Portal konfigurasi aktif; sensor tetap berjalan.");
    Serial.println("[WiFi] Hubungkan HP/laptop ke SMART_SMOKE_SETUP.");
  } else {
    Serial.println("[WiFi] Belum terhubung. Portal akan dicoba kembali dari loop.");
  }
}

void processWiFiManager() {
  if (wifiManager.getConfigPortalActive()) {
    wifiManager.process();  // wajib dipanggil terus untuk portal non-blocking
    wifiPortalActive = wifiManager.getConfigPortalActive();
  }

  if (WiFi.status() == WL_CONNECTED && wifiPortalActive) {
    wifiManager.stopConfigPortal();
    wifiPortalActive = false;
  }
}

void processWiFiResetRequest() {
  if (!wifiResetRequested) return;
  wifiResetRequested = false;

  Serial.println("[WiFi] Reset diminta. Menghapus kredensial WiFi tersimpan...");
  mqtt.disconnect();
  lastCloudMs = 0;
  wifiManager.resetSettings();
  WiFi.disconnect(true, true);
  delay(200);

  startWifiConfigPortal();
}

// Mode CLOUD aktif hanya jika MQTT tersambung DAN keputusan Node-RED masih segar.
bool cloudActive() {
  return mqtt.connected() && lastCloudMs != 0 && (millis() - lastCloudMs <= CLOUD_TIMEOUT_MS);
}

// Pengurai JSON sederhana (tanpa library tambahan)
int jsonInt(const String& s, const char* key, int def) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k);
  if (i < 0) return def;
  return s.substring(i + k.length()).toInt();
}

String jsonStr(const String& s, const char* key) {
  String k = String("\"") + key + "\":\"";
  int i = s.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = s.indexOf('"', i);
  return (j < 0) ? "" : s.substring(i, j);
}

// Keputusan aktuator dari Node-RED
// Keputusan aktuator dari Node-RED
void handleCloudDecision(const String& s) {

  String st = jsonStr(s, "status");

  if (st != "AMAN" && st != "WASPADA" && st != "BAHAYA") {
    return;
  }

  Status ns =
    (st == "BAHAYA") ? BAHAYA : (st == "WASPADA") ? WASPADA
                                                  : AMAN;

  // Membaca 4 kipas dari Node-RED
  bool wantFan1 = jsonInt(s, "fan1", 0) != 0;
  bool wantFan2 = jsonInt(s, "fan2", 0) != 0;
  bool wantFan3 = jsonInt(s, "fan3", 0) != 0;
  bool wantFan4 = jsonInt(s, "fan4", 0) != 0;

  // Membaca buzzer
  bool wantBuzzer = jsonInt(s, "buzzer", 0) != 0;

  // Membaca 2 servo
  bool wantServo1 = jsonInt(s, "servo1", 0) != 0;
  bool wantServo2 = jsonInt(s, "servo2", 0) != 0;

  // Pengaman: BAHAYA memaksa semua kipas menyala

  bool wasCloud = cloudActive();

  lastCloudMs = millis();

  if (!wasCloud) {
    Serial.println("[MODE] CLOUD aktif: keputusan dari Node-RED");
  }

  bool changed =
    (ns != status) || (wantFan1 != fanOn) || (wantFan2 != fan2On) || (wantFan3 != fan3On) || (wantFan4 != fan4On) || (wantBuzzer != buzzerOn) || (wantServo1 != servo1Open) || (wantServo2 != servo2Open);

  status = ns;

  upCount = 0;
  downCount = 0;

  applyCloudState(
    wantFan1,
    wantFan2,
    wantFan3,
    wantFan4,
    wantBuzzer,
    wantServo1,
    wantServo2);

  if (changed || !wasCloud) {
    publishStatus();
  }
}

void onMessage(char* topic, byte* payload, unsigned int len) {
  String raw;
  for (unsigned int i = 0; i < len; i++) raw += (char)payload[i];

  if (strcmp(topic, T_AKTUATOR) == 0) {  // keputusan dari Node-RED
    handleCloudDecision(raw);
    return;
  }

  // Perintah konfigurasi jaringan dari Node-RED.
  // Contoh: {"cmd":"SET_MQTT","host":"192.168.1.10","port":1883}
  //         {"cmd":"RESET_WIFI"}
  if (strcmp(topic, T_CONTROL) == 0 && raw.startsWith("{")) {
    String configCmd = jsonStr(raw, "cmd");
    configCmd.toUpperCase();

    if (configCmd == "SET_MQTT") {
      String newHost = jsonStr(raw, "host");
      int newPort = jsonInt(raw, "port", 0);

      newHost.trim();
      if (newHost.length() > 0 && newHost.length() < sizeof(mqttHost) && newPort >= 1 && newPort <= 65535) {
        saveMqttConfig(newHost, (uint16_t)newPort);
      } else {
        Serial.println("[CONFIG] Ditolak: host atau port MQTT tidak valid.");
      }
      return;
    }

    if (configCmd == "RESET_WIFI") {
      wifiResetRequested = true;  // diproses di loop, bukan membuka portal dari callback
      return;
    }
  }

  String msg = raw;
  msg.trim();
  msg.toUpperCase();
  Serial.printf("[MQTT] %s -> %s\n", topic, msg.c_str());

  if (msg == "FAN1_ON") {
    fan1Manual = true;
    fan1Cmd = true;
    manualMode = true;
  } else if (msg == "FAN1_OFF") {
    fan1Manual = true;
    fan1Cmd = false;
    manualMode = true;
  }

  else if (msg == "FAN2_ON") {
    fan2Manual = true;
    fan2Cmd = true;
    manualMode = true;
  } else if (msg == "FAN2_OFF") {
    fan2Manual = true;
    fan2Cmd = false;
    manualMode = true;
  }

  else if (msg == "FAN3_ON") {
    fan3Manual = true;
    fan3Cmd = true;
    manualMode = true;
  } else if (msg == "FAN3_OFF") {
    fan3Manual = true;
    fan3Cmd = false;
    manualMode = true;
  }

  else if (msg == "FAN4_ON") {
    fan4Manual = true;
    fan4Cmd = true;
    manualMode = true;
  } else if (msg == "FAN4_OFF") {
    fan4Manual = true;
    fan4Cmd = false;
    manualMode = true;
  }

  else if (msg == "SERVO1_OPEN") {
    servo1Manual = true;
    servo1Cmd = true;
    manualMode = true;
  } else if (msg == "SERVO1_CLOSE") {
    servo1Manual = true;
    servo1Cmd = false;
    manualMode = true;
  }

  else if (msg == "SERVO2_OPEN") {
    servo2Manual = true;
    servo2Cmd = true;
    manualMode = true;
  } else if (msg == "SERVO2_CLOSE") {
    servo2Manual = true;
    servo2Cmd = false;
    manualMode = true;
  }

  else if (msg == "MODE_AUTO") {
    manualMode = false;

    fan1Manual = false;
    fan2Manual = false;
    fan3Manual = false;
    fan4Manual = false;

    fan1Cmd = false;
    fan2Cmd = false;
    fan3Cmd = false;
    fan4Cmd = false;

    servo1Manual = false;
    servo2Manual = false;

    servo1Cmd = false;
    servo2Cmd = false;
  }

  else if (msg == "TEST") {
    cmdTest = true;
  }

  else if (msg == "CALIBRATE") {
    cmdCalibrate = true;
  }

  reportCommand("Kontrol aktuator MQTT");
  publishSensors();

  reportCommand("MQTT");
  publishStatus();
}

void ensureConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    // Jangan memanggil WiFi.begin() dengan SSID/password hardcode.
    // Jika belum tersambung, buka portal konfigurasi agar pengguna dapat memilih WiFi.
    if (!wifiManager.getConfigPortalActive() && millis() - lastWifiTry >= 10000) {
      lastWifiTry = millis();
      Serial.println("[WiFi] Tidak terhubung. Menyiapkan portal konfigurasi...");
      startWifiConfigPortal();
    }
    return;
  }
  if (mqtt.connected()) return;
  if (millis() - lastMqttTry < 5000) return;
  lastMqttTry = millis();

  Serial.printf("[MQTT] menyambung ke %s:%u...\n", mqttHost, mqttPort);
  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASS, T_ONLINE, 1, true, "offline");
  } else {
    ok = mqtt.connect(DEVICE_ID, NULL, NULL, T_ONLINE, 1, true, "offline");
  }
  if (ok) {
    Serial.println("[MQTT] tersambung");
    mqtt.publish(T_ONLINE, "online", true);
    mqtt.subscribe(T_CONTROL);
    mqtt.subscribe(T_AKTUATOR);
    publishStatus();
  } else {
    Serial.printf("[MQTT] gagal, state=%d, coba lagi nanti\n", mqtt.state());
  }
}

// Data sensor + baseline + status manual: bahan lengkap bagi Node-RED untuk memutuskan.
void publishSensors() {

  if (!mqtt.connected()) return;

  char tb[12];
  char hb[12];
  char buf[512];

  snprintf(
    buf,
    sizeof(buf),

    "{\"smoke\":%d,\"air\":%d,\"co\":%d,\"flame\":%d,"
    "\"temp\":%s,\"hum\":%s,"
    "\"bs\":%d,\"ba\":%d,\"bc\":%d,\"up\":%lu,"

    "\"mm\":%d,"

    "\"f1m\":%d,\"f1c\":%d,"
    "\"f2m\":%d,\"f2c\":%d,"
    "\"f3m\":%d,\"f3c\":%d,"
    "\"f4m\":%d,\"f4c\":%d,"

    "\"s1m\":%d,\"s1c\":%d,"
    "\"s2m\":%d,\"s2c\":%d}",

    rawSmoke,
    rawAir,
    rawCO,
    flame ? 1 : 0,

    fmt1(temp, tb, sizeof(tb)),
    fmt1(hum, hb, sizeof(hb)),

    baseSmoke,
    baseAir,
    baseCO,
    millis() / 1000UL,

    manualMode ? 1 : 0,

    fan1Manual ? 1 : 0,
    fan1Cmd ? 1 : 0,

    fan2Manual ? 1 : 0,
    fan2Cmd ? 1 : 0,

    fan3Manual ? 1 : 0,
    fan3Cmd ? 1 : 0,

    fan4Manual ? 1 : 0,
    fan4Cmd ? 1 : 0,

    servo1Manual ? 1 : 0,
    servo1Cmd ? 1 : 0,

    servo2Manual ? 1 : 0,
    servo2Cmd ? 1 : 0);

  mqtt.publish(T_SENSOR, buf);
}

void publishStatus() {

  if (!mqtt.connected()) return;

  char buf[256];

  snprintf(
    buf,
    sizeof(buf),

    "{\"status\":\"%s\","
    "\"fan1\":%d,"
    "\"fan2\":%d,"
    "\"fan3\":%d,"
    "\"fan4\":%d,"
    "\"buzzer\":%d,"
    "\"servo1\":%d,"
    "\"servo2\":%d,"
    "\"mode\":\"%s\","
    "\"cloud\":%d}",

    statusName(status),

    fanOn ? 1 : 0,
    fan2On ? 1 : 0,
    fan3On ? 1 : 0,
    fan4On ? 1 : 0,

    buzzerOn ? 1 : 0,

    servo1Open ? 1 : 0,
    servo2Open ? 1 : 0,

    manualMode ? "MANUAL" : "AUTO",

    cloudMode ? 1 : 0);

  mqtt.publish(T_STATUS, buf, true);
}
#else
bool cloudActive() {
  return false;
}
void publishSensors() {}
void publishStatus() {}
#endif

// ======================= SETUP & LOOP =======================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== Smart Smoke Detection & Air Ventilation System ===");
  Serial.printf("Komponen aktif: MQ2=%d MQ135=%d MQ7=%d FLAME=%d DHT=%d SERVO=%d\n",
                USE_MQ2, USE_MQ135, USE_MQ7, USE_FLAME, USE_DHT, USE_SERVO);

  // 1) Inisialisasi 4 relay dalam kondisi MATI
  writeRelayPin(PIN_RELAY, false);
  writeRelayPin(PIN_RELAY2, false);
  writeRelayPin(PIN_RELAY3, false);
  writeRelayPin(PIN_RELAY4, false);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BUZZER, OUTPUT);

  // 2) Servo: pasang lalu pindahkan ke posisi TUTUP
  if (USE_SERVO) {
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);

    ventServo.setPeriodHertz(50);
    ventServo.attach(PIN_SERVO, 500, 2400);

    ventServo2.setPeriodHertz(50);
    ventServo2.attach(PIN_SERVO2, 500, 2400);
  }

  // 3) Kondisi awal: kipas OFF, buzzer OFF, servo TUTUP
  status = AMAN;
  manualMode = false;
  manualFan = false;
  allActuatorsOff();
  Serial.printf("Kondisi awal: kipas OFF, buzzer OFF, servo TUTUP (pin relay = %s saat OFF)\n",
                relayPinText(false));

  pinMode(PIN_FLAME_DO, FLAME_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  dht.begin();

  warmupAndCalibrate();

#if ENABLE_WIFI_MQTT
  loadNetworkConfig();
  mqtt.setServer(mqttHost, mqttPort);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);
  beginWiFiProvisioning();
#endif

  Serial.println("Sistem siap. Perintah: 1=kipas ON, 0=kipas OFF, a=otomatis, o=servo buka, c=servo tutup, k=kalibrasi, t=uji aktuator");
}

void loop() {
  handleSerialCommands();

#if ENABLE_WIFI_MQTT
  processWiFiResetRequest();
  processWiFiManager();
  ensureConnection();
  if (mqtt.connected()) mqtt.loop();
#endif

  handleQueuedCommands();

  unsigned long now = millis();
  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;
    readSensors();

    // Pergantian mode otak
    bool cloud = cloudActive();
    if (cloud != cloudMode) {
      cloudMode = cloud;
      if (cloud) {
        Serial.println("[MODE] CLOUD");
      } else {
        Serial.println("[MODE] LOKAL: Node-RED / MQTT tidak tersedia, logika lokal aktif");
        upCount = 0;
        downCount = 0;
      }
      publishStatus();
    }

    bool statusChanged = false, actuatorChanged = false;
    if (!cloud) {
      Status before = status;
      updateStatus();
      statusChanged = (status != before);
      actuatorChanged = applyActuators();
    }
    // else if (LOCAL_SAFETY_OVERRIDE && lokalDarurat() && status != BAHAYA) {

    //   status = BAHAYA;

    //   applyCloudState(
    //     true,
    //     true,
    //     true,
    //     true,
    //     true,
    //     flame ? false : servo1Open,
    //     flame ? false : servo2Open);

    //   statusChanged = true;
    // }

    // PENGAMAN SERVO: api selalu memaksa servo TUTUP,
    // baik dalam mode CLOUD maupun mode LOKAL.
    // if (flame && (servo1Open || servo2Open)) {

    //   servo1Open = false;
    //   servo2Open = false;
    //   servoOpen = false;

    //   setServo1HW(false);
    //   setServo2HW(false);

    //   actuatorChanged = true;

    //   Serial.println("[KESELAMATAN] Api terdeteksi! Kedua servo dipaksa TUTUP.");
    // }

    setBuzzerHW(buzzerOn);
    // Inisialisasi empat relay dalam kondisi OFF
    writeRelay(fanOn, fan2On, fan3On, fan4On);
    printSerial();

#if ENABLE_WIFI_MQTT
    if (statusChanged || actuatorChanged) publishStatus();
#else
    (void)statusChanged;
    (void)actuatorChanged;
#endif
  }

#if ENABLE_WIFI_MQTT
  // Data sensor tiap detik, selalu (juga saat mode LOKAL) supaya Node-RED bisa mengambil alih lagi
  if (mqtt.connected() && (now - lastSensorPub >= SENSOR_PUBLISH_MS)) {
    lastSensorPub = now;
    publishSensors();
  }
  if (mqtt.connected() && (now - lastPublish >= PUBLISH_INTERVAL_MS)) {
    lastPublish = now;
    publishStatus();
  }
#endif
}
