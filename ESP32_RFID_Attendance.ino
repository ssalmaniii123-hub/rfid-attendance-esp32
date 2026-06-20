// =====================================================
//   SISTEM ABSENSI RFID ESP32
//   Board   : ESP32 DevKit V1
//   Komponen: RFID RC522, OLED SSD1309 (Murni I2C 4-Pin), Buzzer
//   Waktu   : Sinkronisasi lokal dari Laptop/Server Backend
// =====================================================

// =====================================================
// LIBRARY
// =====================================================

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <time.h>
#include <sys/time.h>

#include <Wire.h>
#include <SPI.h>

#include <MFRC522.h>  // Library: MFRC522 by GithubCommunity

#include <Adafruit_GFX.h>      // Library: Adafruit GFX Library
#include <Adafruit_SSD1306.h>  // Library: Adafruit SSD1306

// =====================================================
// KONFIGURASI OLED
// OLED SSD1309 I2C 2.42 inch (128x64) - Versi Murni I2C 4-Pin
// =====================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1       // Tetap -1 karena OLED murni I2C tidak memiliki pin reset fisik

// --- DIUBAH DI SINI ---
// Alamat diubah ke 0x3C sesuai dengan sablonan default pabrik di belakang PCB OLED kamu
#define OLED_I2C_ADDR 0x3C 

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// =====================================================
// PIN RFID RC522
// =====================================================

#define RFID_SS_PIN 5
#define RFID_RST_PIN 15

#define SPI_SCK_PIN 18
#define SPI_MISO_PIN 19
#define SPI_MOSI_PIN 23

// =====================================================
// PIN I2C (Dipakai oleh OLED)
// =====================================================

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// =====================================================
// PIN BUZZER
// =====================================================

#define BUZZER_PIN 26

// =====================================================
// KONFIGURASI WIFI
// =====================================================

const char* WIFI_SSID = "OPPO A76";
const char* WIFI_PASSWORD = "sukasuka";

// =====================================================
// KONFIGURASI BACKEND DASHBOARD
// =====================================================

const char* DEVICE_ID = "ESP32-RFID-01";
const char* ATTENDANCE_URL = "http://10.47.207.174:3001/api/attendance";
const char* HEARTBEAT_URL = "http://10.47.207.174:3001/api/device/heartbeat";
const char* TIME_URL = "http://10.47.207.174:3001/api/time";

// =====================================================
// INTERVAL WAKTU (dalam milidetik)
// =====================================================

const unsigned long TIME_SYNC_INTERVAL = 60UL * 1000UL;              // Sync jam tiap 1 menit dari server
const unsigned long HEARTBEAT_INTERVAL = 30UL * 1000UL;              // 30 detik
const unsigned long DISPLAY_HOLD_DURATION = 2000UL;                  // 2 detik untuk nahan layar absensi

// =====================================================
// OBJEK RFID
// =====================================================

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

bool timeSynced = false;

// =====================================================
// WEB SERVER ESP32 LOKAL (Port 80)
// =====================================================

WebServer server(80);

// =====================================================
// DATA KARTU RFID YANG TERDAFTAR
// =====================================================

struct CardData {
  String uid;
  String name;
};

CardData allowedCards[] = {
  { "74 4A 19 07", "Muhammad Salmani" },
  { "43 06 18 07", "Irwan" },
  { "5E CE F1 05", "Yohanes" },
  { "05 8E 5D C2 DF E2 00", "Sinta Kasih" },
  { "05 80 4F D0 AE 92 00", "Prasetya" }
};

const int ALLOWED_CARD_COUNT = sizeof(allowedCards) / sizeof(allowedCards[0]);

// =====================================================
// VARIABEL STATUS TERAKHIR
// =====================================================

String lastUID = "-";
String lastName = "-";
String lastStatus = "MENUNGGU";
String lastTime = "-";
String lastDate = "-";
String lastBackendStatus = "BELUM KIRIM";

// =====================================================
// VARIABEL TIMER
// =====================================================

unsigned long lastHeartbeat = 0;
unsigned long lastTimeSync = 0;
unsigned long displayStateTime = 0;
bool displayingResult = false;

// =====================================================
// FUNGSI UTILITY & KONTROL KOMPONEN
// =====================================================

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  return value;
}

String statusForBackend(String status) {
  if (status == "DITERIMA") return "diterima";
  return "ditolak";
}

void beepOK() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepDenied() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);
}

// =====================================================
// FUNGSI TAMPILAN OLED
// =====================================================

void oledStandby() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(4, 5);
  display.println("ABSENSI");
  display.setCursor(20, 25);
  display.println("RFID");

  display.setTextSize(1);
  display.setCursor(10, 50);
  display.println("Tempel Kartu...");

  display.display();
}

void oledAccepted(String name, String timeStr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(5, 2);
  display.println("** AKSES DITERIMA **");

  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  display.setTextSize(2);
  String displayName = name;
  if (displayName.length() > 9) {
    displayName = displayName.substring(0, 9);
  }
  int nameX = (OLED_WIDTH - (displayName.length() * 12)) / 2;
  if (nameX < 0) nameX = 0;
  display.setCursor(nameX, 20);
  display.println(displayName);

  display.setTextSize(1);
  display.setCursor(30, 50);
  display.print("Jam: ");
  display.println(timeStr);

  display.display();
}

void oledDenied(String timeStr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(8, 2);
  display.println("!! AKSES DITOLAK !!");

  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(2, 20);
  display.println("TIDAK");
  display.setCursor(2, 38);
  display.println("TERDAFTAR");

  display.setTextSize(1);
  display.setCursor(30, 56);
  display.print("Jam: ");
  display.println(timeStr);

  display.display();
}

// =====================================================
// FUNGSI JARINGAN & BACKEND
// =====================================================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("Menghubungkan ke WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    delay(500);
    Serial.print(".");
    attempt++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Terhubung!");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Gagal terhubung ke WiFi.");
  }
}

bool syncTimeFromServer() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  Serial.println("Sinkronisasi waktu dari Server Backend...");
  HTTPClient http;
  http.begin(TIME_URL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Respons waktu server: " + payload);

    // Parsing sederhana tanpa library JSON
    int yearIdx = payload.indexOf("\"year\":");
    int monthIdx = payload.indexOf("\"month\":");
    int dayIdx = payload.indexOf("\"day\":");
    int hourIdx = payload.indexOf("\"hour\":");
    int minuteIdx = payload.indexOf("\"minute\":");
    int secondIdx = payload.indexOf("\"second\":");

    if (yearIdx != -1 && monthIdx != -1 && dayIdx != -1 && hourIdx != -1 && minuteIdx != -1 && secondIdx != -1) {
      int year = payload.substring(yearIdx + 7, payload.indexOf(",", yearIdx)).toInt();
      int month = payload.substring(monthIdx + 8, payload.indexOf(",", monthIdx)).toInt();
      int day = payload.substring(dayIdx + 6, payload.indexOf(",", dayIdx)).toInt();
      int hour = payload.substring(hourIdx + 7, payload.indexOf(",", hourIdx)).toInt();
      int minute = payload.substring(minuteIdx + 9, payload.indexOf(",", minuteIdx)).toInt();
      int second = payload.substring(secondIdx + 9, payload.indexOf("}", secondIdx)).toInt();

      struct tm tm;
      tm.tm_year = year - 1900;
      tm.tm_mon = month - 1;
      tm.tm_mday = day;
      tm.tm_hour = hour;
      tm.tm_min = minute;
      tm.tm_sec = second;
      tm.tm_isdst = 0;

      time_t t = mktime(&tm);
      struct timeval now = { .tv_sec = t, .tv_usec = 0 };
      settimeofday(&now, NULL);

      timeSynced = true;
      lastTimeSync = millis();
      Serial.printf("Sukses sinkronisasi waktu: %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
      http.end();
      return true;
    }
  }
  http.end();
  Serial.println("Gagal sinkronisasi waktu dari server.");
  return false;
}

void updateDateTime() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  if (timeinfo.tm_year < 70) {
    lastTime = "00:00:00";
    lastDate = "1970-01-01";
    return;
  }

  char timeBuffer[10];
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  lastTime = String(timeBuffer);

  char dateBuffer[12];
  snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  lastDate = String(dateBuffer);
}

bool postJson(const char* url, String json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung, melewati HTTP POST.");
    return false;
  }

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  Serial.print("POST ke: ");
  Serial.println(url);
  Serial.print("Payload: ");
  Serial.println(json);

  int httpCode = http.POST(json);
  String response = http.getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  Serial.print("Response: ");
  Serial.println(response);

  http.end();
  return (httpCode >= 200 && httpCode < 300);
}

void sendHeartbeat() {
  String json = "{";
  json += "\"deviceId\":\"" + String(DEVICE_ID) + "\",";
  json += "\"name\":\"ESP32 RFID Reader\",";
  json += "\"firmware\":\"1.0.0\",";
  json += "\"ipAddress\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";

  postJson(HEARTBEAT_URL, json);
}

void sendAttendanceToDashboard() {
  String json = "{";
  json += "\"uid\":\"" + jsonEscape(lastUID) + "\",";
  json += "\"name\":\"" + jsonEscape(lastName) + "\",";
  json += "\"status\":\"" + statusForBackend(lastStatus) + "\",";
  json += "\"time\":\"" + jsonEscape(lastTime) + "\",";
  json += "\"date\":\"" + jsonEscape(lastDate) + "\",";
  json += "\"deviceId\":\"" + String(DEVICE_ID) + "\"";
  json += "}";

  bool sent = postJson(ATTENDANCE_URL, json);
  lastBackendStatus = sent ? "TERKIRIM" : "GAGAL KIRIM";
}

// =====================================================
// RFID & WEB SERVER UTILITY
// =====================================================

String getUIDString() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (i > 0) uid += " ";
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

bool checkAllowedCard(String uid, String& cardName) {
  for (int i = 0; i < ALLOWED_CARD_COUNT; i++) {
    if (uid == allowedCards[i].uid) {
      cardName = allowedCards[i].name;
      return true;
    }
  }
  cardName = "TIDAK TERDAFTAR";
  return false;
}

String createHTML() {
  String html = "";
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Absensi RFID</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#f0f2f5;margin:0;padding:20px;text-align:center;}";
  html += ".card{background:#fff;max-width:420px;margin:auto;border-radius:16px;";
  html += "box-shadow:0 4px 15px rgba(0,0,0,0.12);padding:28px;}";
  html += "h1{color:#1a1a2e;margin-bottom:20px;font-size:22px;}";
  html += ".row{display:flex;justify-content:space-between;padding:10px 0;";
  html += "border-bottom:1px solid #eee;font-size:15px;}";
  html += ".row:last-child{border-bottom:none;}";
  html += ".label{color:#666;font-weight:bold;}";
  html += ".value{color:#333;text-align:right;max-width:60%;}";
  html += ".status-ok{color:#27ae60;font-weight:bold;}";
  html += ".status-deny{color:#e74c3c;font-weight:bold;}";
  html += ".status-wait{color:#f39c12;font-weight:bold;}";
  html += ".footer{margin-top:16px;font-size:12px;color:#aaa;}";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>&#128100; ABSENSI RFID</h1>";

  String statusClass = "status-wait";
  if (lastStatus == "DITERIMA") statusClass = "status-ok";
  if (lastStatus == "DITOLAK") statusClass = "status-deny";

  html += "<div class='row'><span class='label'>Nama</span><span class='value'>" + lastName + "</span></div>";
  html += "<div class='row'><span class='label'>UID</span><span class='value'>" + lastUID + "</span></div>";
  html += "<div class='row'><span class='label'>Status</span><span class='value " + statusClass + "'>" + lastStatus + "</span></div>";
  html += "<div class='row'><span class='label'>Jam</span><span class='value'>" + lastTime + "</span></div>";
  html += "<div class='row'><span class='label'>Tanggal</span><span class='value'>" + lastDate + "</span></div>";
  html += "<div class='row'><span class='label'>Backend</span><span class='value'>" + lastBackendStatus + "</span></div>";
  html += "<div class='row'><span class='label'>IP ESP32</span><span class='value'>" + WiFi.localIP().toString() + "</span></div>";
  html += "<div class='footer'>Auto-refresh setiap 3 detik</div>";
  html += "</div></body></html>";

  return html;
}

void handleRoot() {
  server.send(200, "text/html", createHTML());
}

// =====================================================
// SETUP: INISIALISASI
// =====================================================

void setup() {
  Serial.begin(115200);
  Serial.println("=== SISTEM ABSENSI RFID MULAI ===");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  Serial.println("Memulai OLED SSD1309 I2C (0x3C)...");

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("ERROR: OLED tidak ditemukan pada alamat 0x3C!");
    while (true) {
      delay(1000);
    }
  } else {
    Serial.println("OLED 2.42\" (0x3C) berhasil diinisialisasi.");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 25);
    display.println("OLED BERHASIL");
    display.display();

    delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20);
  display.println("Sistem Absensi");
  display.setCursor(30, 35);
  display.println("Memulai...");
  display.display();
  delay(1500);

  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, RFID_SS_PIN);
  mfrc522.PCD_Init();
  Serial.println("RFID RC522 siap.");

  connectWiFi();
  syncTimeFromServer();
  updateDateTime();

  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web Server aktif di port 80.");

  if (WiFi.status() == WL_CONNECTED) {
    sendHeartbeat();
  }

  beepOK();
  oledStandby();

  Serial.println("Sistem siap. Tempelkan kartu RFID...");
  Serial.println("=====================================");
}

// =====================================================
// LOOP: PROGRAM UTAMA
// =====================================================

void loop() {
  server.handleClient();
  updateDateTime();

  // Reset layar ke standby setelah durasi selesai
  if (displayingResult && (millis() - displayStateTime >= DISPLAY_HOLD_DURATION)) {
    displayingResult = false;
    oledStandby();
  }

  // Rutinitas sinkronisasi waktu periodik dari Laptop/Server Backend
  if (WiFi.status() == WL_CONNECTED) {
    if (!timeSynced || (millis() - lastTimeSync > TIME_SYNC_INTERVAL)) {
      syncTimeFromServer();
    }
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }

  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getUIDString();
  String cardName = "";
  bool allowed = checkAllowedCard(uid, cardName);

  Serial.println("-------------------------------------");
  Serial.print("UID Terdeteksi : ");
  Serial.println(uid);
  Serial.print("Nama           : ");
  Serial.println(cardName);

  lastUID = uid;
  lastName = cardName;

  updateDateTime();

  if (allowed) {
    lastStatus = "DITERIMA";
    Serial.println("Status         : AKSES DITERIMA");
    beepOK();
    oledAccepted(cardName, lastTime);
  } else {
    lastStatus = "DITOLAK";
    Serial.println("Status         : AKSES DITOLAK");
    beepDenied();
    oledDenied(lastTime);
  }

  displayingResult = true;
  displayStateTime = millis();

  sendAttendanceToDashboard();
  Serial.print("Backend Status : ");
  Serial.println(lastBackendStatus);
  Serial.println("-------------------------------------");

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(1000);
}
