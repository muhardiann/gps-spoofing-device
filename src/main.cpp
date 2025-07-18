#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =================================================================
// --- Konfigurasi Perangkat ---
// =================================================================
static const int GPS_RX_PIN = 16;
static const int GPS_TX_PIN = 17;
static const uint32_t GPS_BAUD = 9600;
const unsigned long OUTPUT_INTERVAL = 5000;

// --- Konfigurasi LCD ---
#define SDA_LCD 21
#define SCL_LCD 22
static const int LCD_ADDRESS = 0x27;
static const int LCD_COLS = 16;
static const int LCD_ROWS = 2;

// --- Variabel Global ---
long dataCount = 0;
String sessionId;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
WiFiManager wm; //

static const int MAX_SATELLITES = 40;
TinyGPSCustom satNumber[4], elevation[4], azimuth[4], snr[4];
struct SatelliteInfo
{
  bool active = false;
  int prn = 0;
  int elevation = 0;
  int azimuth = 0;
  int snr = 0;
} sats[MAX_SATELLITES];

// --- Deklarasi Fungsi ---
void printAndDisplayData();
void sendDataToBackend();
void updateSatelliteData();
void printFloat(float val, bool valid, int len, int prec);
void printInt(unsigned long val, bool valid, int len);
void printDateTime(TinyGPSDate &d, TinyGPSTime &t);

// =================================================================
// --- Fungsi Setup ---
// =================================================================
void setup()
{
  Serial.begin(115200);
  wm.setConfigPortalTimeout(180);

  if (!wm.autoConnect("Spoofing-Detector", "12345678"))
  {
    Serial.println("Gagal terhubung. Restart dalam 3 detik.");

    delay(3000);
    ESP.restart();
  }

  Serial.println("\nWiFi Terhubung!");
  Serial.print("Terhubung ke SSID: ");
  Serial.println(WiFi.SSID());
  lcd.clear();
  lcd.print("WiFi Connected!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.SSID().substring(0, 16));
  delay(1000);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18] = {0};
  sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sessionId = String(macStr) + "-" + String(millis());
  Serial.print("Session ID untuk sesi ini: ");
  Serial.println(sessionId);

  Serial.println(F("=========================== ESP32 GPS Data Logger ==========================="));
  Serial.println(F("No.  | Tanggal      Waktu      Latitude    Longitude     Sats  HDOP"));
  Serial.println(F("-----------------------------------------------------------------------------"));

  for (int i = 0; i < 4; ++i)
  {
    satNumber[i].begin(gps, "GPGSV", 4 + 4 * i);
    elevation[i].begin(gps, "GPGSV", 5 + 4 * i);
    azimuth[i].begin(gps, "GPGSV", 6 + 4 * i);
    snr[i].begin(gps, "GPGSV", 7 + 4 * i);
  }
}

// =================================================================
// --- Fungsi Loop Utama ---
// =================================================================
void loop() {
  static unsigned long lastOutputTime = 0;

  while (gpsSerial.available() > 0)
    gps.encode(gpsSerial.read());

  if (snr[0].isUpdated())
    updateSatelliteData();

  if (millis() - lastOutputTime > OUTPUT_INTERVAL) {
    dataCount++;
    printAndDisplayData();
    sendDataToBackend(); // Mengirim data ke backend setiap interval

    for (int i = 0; i < MAX_SATELLITES; ++i) {
      sats[i].active = false;
    }
    
    lastOutputTime = millis();
  }
}

// =================================================================
// --- Fungsi Helper ---
// =================================================================

void sendDataToBackend() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Koneksi WiFi terputus. Gagal mengirim data.");
    return;
  }
  
  JsonDocument doc;

  if (gps.date.isValid() && gps.time.isValid()) {
    char isoTimestamp[25];
    sprintf(isoTimestamp, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
            gps.date.year(), gps.date.month(), gps.date.day(),
            gps.time.hour(), gps.time.minute(), gps.time.second());
    doc["timestamp"] = isoTimestamp;
  } else {
    doc["timestamp"] = nullptr;
  }

  doc["latitude"] = gps.location.lat();
  doc["longitude"] = gps.location.lng();
  doc["sats"] = gps.satellites.value();
  doc["hdop"] = gps.hdop.hdop();
  doc["session_id"] = sessionId;

   JsonArray visibleSats = doc["visible_sats"].to<JsonArray>();
  for (int i = 0; i < MAX_SATELLITES; ++i) {
    if (sats[i].active) {
      JsonObject satObject = visibleSats.add<JsonObject>();
      satObject["prn"] = sats[i].prn;
      satObject["elev"] = sats[i].elevation;
      satObject["azim"] = sats[i].azimuth;
      satObject["snr"] = sats[i].snr;
    }
  }

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  HTTPClient http;
  String serverUrl = "https://gps-spoofing-backend.vercel.app/api/signal_data";
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonPayload);

  Serial.print("Mengirim data ke backend... ");
  if (httpResponseCode > 0) {
    Serial.printf("Status: %d\n", httpResponseCode);
  } else {
    Serial.printf("Gagal, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void printAndDisplayData()
{
  // --- BAGIAN 1: Mencetak baris utama ke Serial Monitor ---
  Serial.printf("%-4ld | ", dataCount);
  printDateTime(gps.date, gps.time);
  printFloat(gps.location.lat(), gps.location.isValid(), 12, 6);
  printFloat(gps.location.lng(), gps.location.isValid(), 13, 6);
  printInt(gps.satellites.value(), gps.satellites.isValid(), 6);
  printFloat(gps.hdop.hdop(), gps.hdop.isValid(), 6, 2);
  Serial.println();

  // --- BARU: Menambahkan kembali bagian untuk mencetak detail satelit ---
  bool firstSat = true;
  for (int i = 0; i < MAX_SATELLITES; ++i)
  {
    if (sats[i].active)
    {
      if (firstSat)
      {
        Serial.println(F("  --- Detail Satelit Terlihat (PRN, Elev, Azim, SNR) ---"));
        firstSat = false;
      }
      Serial.print(F("    PRN: "));
      printInt(sats[i].prn, true, 3);
      Serial.print(F(" Elev: "));
      printInt(sats[i].elevation, true, 3);
      Serial.print(F(" Azim: "));
      printInt(sats[i].azimuth, true, 4);
      Serial.print(F(" SNR: "));
      printInt(sats[i].snr, true, 4);
      Serial.println();
    }
  }
  if (!firstSat)
    Serial.println(F("  ----------------------------------------------------"));

  if (gps.location.isValid()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(gps.location.lat(), 6);
    String satsStr = String(gps.satellites.value());
    lcd.setCursor(LCD_COLS - satsStr.length(), 0);
    lcd.print(satsStr);
    lcd.setCursor(0, 1);
    lcd.print(gps.location.lng(), 6);
    int maxSnr = 0;
    for (int i = 0; i < MAX_SATELLITES; ++i) {
      if (sats[i].active && sats[i].snr > maxSnr) {
        maxSnr = sats[i].snr;
      }
    }
    String snrStr = String(maxSnr);
    lcd.setCursor(LCD_COLS - snrStr.length(), 1);
    lcd.print(snrStr);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Mencari Sinyal");
    lcd.setCursor(0, 1);
    lcd.print("                ");
  }
} 

void updateSatelliteData()
{
  for (int i = 0; i < 4; ++i)
  {
    int prn = atoi(satNumber[i].value());
    if (prn >= 1 && prn <= 40)
    {
      sats[prn - 1].active = true;
      sats[prn - 1].prn = prn;
      sats[prn - 1].elevation = atoi(elevation[i].value());
      sats[prn - 1].azimuth = atoi(azimuth[i].value());
      sats[prn - 1].snr = atoi(snr[i].value());
    }
  }
}
void printFloat(float val, bool valid, int len, int prec)
{
  if (!valid)
  {
    while (len-- > 1)
      Serial.print('*');
    Serial.print(' ');
  }
  else
  {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1);
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3
                         : vi >= 10    ? 2
                                       : 1;
    for (int i = flen; i < len; ++i)
      Serial.print(' ');
  }
}
void printInt(unsigned long val, bool valid, int len)
{
  char sz[32] = "*****************";
  if (valid)
    sprintf(sz, "%ld", val);
  sz[len] = 0;
  for (int i = strlen(sz); i < len; ++i)
    sz[i] = ' ';
  if (len > 0)
    sz[len - 1] = ' ';
  Serial.print(sz);
}
void printDateTime(TinyGPSDate &d, TinyGPSTime &t)
{
  if (d.isValid())
  {
    char sz[32];
    sprintf(sz, "%02d/%02d/%02d ", d.day(), d.month(), d.year());
    Serial.print(sz);
  }
  else
  {
    Serial.print(F("********** "));
  }
  if (t.isValid())
  {
    char sz[32];
    sprintf(sz, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
    Serial.print(sz);
  }
  else
  {
    Serial.print(F("******** "));
  }
}