# Smart Smoke Detection & Air Ventilation System

## Tema Proyek

**Internet of Things (IoT) untuk keselamatan ruangan**: deteksi dini asap, gas berbahaya, api, dan suhu tinggi, disertai ventilasi udara otomatis dan peringatan jarak jauh.

## Deskripsi Singkat

Sistem ini memantau kondisi udara sebuah ruangan memakai ESP32 dengan sensor MQ-2 (asap/LPG), MQ-135 (kualitas udara), MQ-7 (CO), flame sensor, dan DHT22 (suhu & kelembapan). Data dikirim lewat MQTT (broker **Mosquitto**) ke **Node-RED**, yang menentukan status ruangan (**AMAN / WASPADA / BAHAYA**), lalu mengendalikan 4 kipas (relay), buzzer, dan 2 servo ventilasi.

Fungsi utama:

- **Deteksi berbasis delta ADC**: nilai sensor dibandingkan dengan baseline hasil kalibrasi udara bersih.
- **Mode hybrid**: saat ESP32 terhubung ke Node-RED, keputusan diambil di Node-RED (mode CLOUD). Jika koneksi putus (timeout 5 detik), ESP32 otomatis memakai logika lokal sendiri (mode LOKAL).
- **Dashboard web** (Node-RED Dashboard): gauge, grafik, status aktuator, log kejadian, kontrol manual, dan halaman riwayat dari database.
- **Penyimpanan data** ke MySQL (`sensor_log`, `event_log`).
- **Bot Telegram**: notifikasi perubahan status, kontrol jarak jauh, dan pendaftaran pengguna dengan persetujuan admin.
- **WiFiManager**: SSID/password WiFi dan alamat broker diatur lewat portal konfigurasi, tanpa menulis kredensial di firmware.

## Arsitektur (4 Layer IoT)

| Layer | Isi |
|-------|-----|
| Smart Things | Sensor MQ-2, MQ-135, MQ-7, Flame, DHT22; aktuator relay (4 kipas), buzzer, servo 1 & 2 |
| Network (Gateway) | ESP32 + WiFi, publish/subscribe MQTT |
| Middleware | Mosquitto, Node-RED (logika keputusan), MySQL |
| Application | Dashboard Node-RED dan Bot Telegram |

## Berkas dalam Repositori

| Berkas | Keterangan |
|--------|------------|
| `Smart_Smoke_Detection_WiFiManager_NodeRED.ino` | Firmware ESP32 |
| `node-red.json` | Flow Node-RED (diimpor lewat menu Import) |
| `Pengujian_Komponen/` | Sketch uji per komponen (MQ-2, MQ-135, MQ-7, Flame, DHT22, Relay, Buzzer, Servo, WiFi/MQTT) |

## Perangkat Keras dan Pin ESP32

| Komponen | Pin |
|----------|-----|
| MQ-2 / MQ-135 / MQ-7 (AO) | GPIO 34 / 35 / 32 |
| Flame sensor (DO) | GPIO 17 |
| DHT22 | GPIO 18 |
| Relay 1-4 (kipas) | GPIO 4, 16, 33, 25 (active-low) |
| Buzzer aktif | GPIO 5 |
| Servo 1 / Servo 2 | GPIO 23 / 22 |

## Berkas dan Konfigurasi Pendukung

### 1. Library Arduino IDE

Board: **ESP32** (Arduino core). Library yang dibutuhkan:

- DHT sensor library (Adafruit) dan Adafruit Unified Sensor
- ESP32Servo
- PubSubClient
- WiFiManager (tzapu)

(`Preferences` dan `WiFi` sudah termasuk di core ESP32.)

### 2. Broker Mosquitto

Buat user dan password broker:

```bash
mosquitto_passwd -c /etc/mosquitto/passwd nodered
```

(Di Windows, gunakan lokasi folder instalasi Mosquitto, misalnya `C:\mosquitto\passwd`.)

Isi `mosquitto.conf`:

```conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Jalankan ulang Mosquitto, dan buka port **1883** di firewall/VPS agar ESP32 dapat terhubung.

### 3. Database MySQL

Buat database `smoke_db` lalu tabel berikut (kolom disesuaikan dengan query pada flow):

```sql
CREATE DATABASE IF NOT EXISTS smoke_db;
USE smoke_db;

CREATE TABLE IF NOT EXISTS sensor_log (
  id INT AUTO_INCREMENT PRIMARY KEY,
  waktu TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  smoke INT, air INT, co INT,
  bs INT, ba INT, bc INT,
  smoke_delta INT, air_delta INT, co_delta INT,
  flame TINYINT,
  suhu FLOAT, kelembapan FLOAT,
  detection_method VARCHAR(30),
  threshold_version VARCHAR(30)
);

CREATE TABLE IF NOT EXISTS event_log (
  id INT AUTO_INCREMENT PRIMARY KEY,
  waktu TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  jenis VARCHAR(20),
  keterangan VARCHAR(255),
  kipas TINYINT, buzzer TINYINT, servo TINYINT,
  `mode` VARCHAR(10)
);

CREATE TABLE IF NOT EXISTS telegram_users (
  chat_id VARCHAR(30) PRIMARY KEY,
  phone_number VARCHAR(30),
  first_name VARCHAR(100),
  status ENUM('pending','active','rejected') DEFAULT 'pending'
);
```

### 4. Node-RED

Node tambahan (Manage palette):

- `@flowfuse/node-red-dashboard` (Dashboard 2.0)
- `node-red-contrib-stackhero-mysql`
- `node-red-contrib-telegrambot`

Setelah **Import** `flows.json`, atur konfigurasi berikut (kredensial tidak ikut terekspor di JSON):

| Config node | Yang perlu diisi |
|-------------|------------------|
| Broker MQTT | Host (`localhost` bila Mosquitto satu mesin), port 1883, username & password sesuai langkah 2 |
| Stackhero-MySQL-Server | Host, port 3306, database `smoke_db`, user & password MySQL |
| Telegram bot | Token dari @BotFather |

Tambahan: ganti `ADMIN_ID` / chat ID admin pada function **Notifikasi Admin**, **deteksi perintah admin**, dan **proses tombol admin** dengan chat ID Telegram Anda sendiri.

### 5. Firmware ESP32

Sesuaikan konstanta di awal file `.ino`:

| Konstanta | Keterangan |
|-----------|------------|
| `mqttHost` | Alamat IP broker Mosquitto (nilai awal; dapat diubah dari dashboard/Node-RED dan disimpan di NVS) |
| `MQTT_USER`, `MQTT_PASS` | Harus sama dengan user Mosquitto |
| `WIFI_AP_PASS` | Password portal konfigurasi WiFi (minimal 8 karakter), sebaiknya diganti |
| `ENABLE_WIFI_MQTT` | `1` = WiFi + MQTT aktif, `0` = uji lokal saja |

Ambang deteksi pada firmware **harus sama** dengan yang ada di function **Logika Cloud** di Node-RED.

## Cara Menjalankan

1. Rakit rangkaian sesuai tabel pin (catu 5 V terpisah untuk servo, GND disatukan dengan ESP32).
2. Jalankan Mosquitto, MySQL, dan Node-RED.
3. Import `flows.json`, isi konfigurasi pada langkah 4, lalu klik **Deploy**.
4. Unggah firmware ke ESP32 dan buka Serial Monitor (115200 baud).
5. Saat pertama kali menyala, sambungkan HP/laptop ke WiFi **`SMART_SMOKE_SETUP`**, pilih jaringan WiFi, lalu simpan.
6. Biarkan pemanasan sensor selesai (120 detik) dan kalibrasi baseline berjalan di udara bersih.
7. Buka dashboard: `http://<alamat-node-red>:1880/dashboard/monitor` (riwayat: `/dashboard/Riwayat`).

## Topik MQTT

| Topik | Arah | Isi |
|-------|------|-----|
| `smoke/room1/sensor` | ESP32 → Node-RED | Data sensor, baseline, status manual (tiap 1 detik) |
| `smoke/room1/status` | ESP32 → Node-RED | Status, kondisi aktuator, mode (retained) |
| `smoke/room1/online` | ESP32 → Node-RED | `online` / `offline` (Last Will) |
| `smoke/room1/aktuator` | Node-RED → ESP32 | Keputusan kipas, buzzer, servo |
| `smoke/room1/control` | Node-RED → ESP32 | Perintah manual dan konfigurasi |

Perintah kontrol: `FAN1_ON`/`FAN1_OFF` ... `FAN4_*`, `SERVO1_OPEN`/`SERVO1_CLOSE`, `SERVO2_*`, `MODE_AUTO`, `TEST`, `CALIBRATE`. Konfigurasi (JSON): `{"cmd":"SET_MQTT","host":"192.168.1.10","port":1883}` dan `{"cmd":"RESET_WIFI"}`.

## Logika Status

| Status | Kondisi (salah satu) | Aksi |
|--------|----------------------|------|
| AMAN | Semua di bawah ambang | Kipas mati, servo tutup |
| WASPADA | Delta gas ≥ 300 atau suhu ≥ 40 °C | Kipas 1 dan 2 menyala |
| BAHAYA | Delta gas ≥ 500, suhu ≥ 50 °C, atau api terdeteksi | Semua kipas dan buzzer menyala, servo terbuka (tertutup bila ada api) |

## Perintah Bot Telegram

`/start`, `/help`, `/status`, `/riwayat`, `/fan1_on` ... `/fan4_off`, `/servo1_buka`, `/servo1_tutup`, `/servo2_buka`, `/servo2_tutup`, `/auto`, `/tes`, `/kalibrasi`, `/kalibrasi_ya`. Perintah admin: `/setujui CHAT_ID` dan `/tolak CHAT_ID`.
