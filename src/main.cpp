#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =================================================================
// --- Konfigurasi Perangkat ---
// =================================================================
static const int GPS_RX_PIN = 16;
static const int GPS_TX_PIN = 17;
static const uint32_t GPS_BAUD = 9600;
const unsigned long OUTPUT_INTERVAL = 2000;

// --- Konfigurasi LCD ---
#define SDA_LCD 21
#define SCL_LCD 22
static const int LCD_ADDRESS = 0x27;
static const int LCD_COLS = 16;
static const int LCD_ROWS = 2;

// --- Counter Data ---
long dataCount = 0;

// =================================================================
// --- Inisialisasi Objek ---
// =================================================================
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

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

// --- Function Prototypes (Deklarasi Fungsi) ---
void updateSatelliteData();
void printAndDisplayData();
void printFloat(float val, bool valid, int len, int prec);
void printInt(unsigned long val, bool valid, int len);
void printDateTime(TinyGPSDate &d, TinyGPSTime &t);

// =================================================================
// --- Fungsi Setup ---
// =================================================================
void setup()
{
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Wire.begin(SDA_LCD, SCL_LCD);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("GPS Data Logger");

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
void loop()
{
  static unsigned long lastOutputTime = 0;

  while (gpsSerial.available() > 0)
    gps.encode(gpsSerial.read());
  if (snr[0].isUpdated())
    updateSatelliteData();

  if (millis() - lastOutputTime > OUTPUT_INTERVAL)
  {
    if (gps.location.isValid())
    {
      dataCount++;
      printAndDisplayData();
    }
    else
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Mencari Sinyal");
    }
    lastOutputTime = millis();
  }
}

// =================================================================
// --- Fungsi Helper ---
// =================================================================

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

  // --- BAGIAN 2: Mengupdate LCD ---
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(gps.location.lat(), 6);

  String satsStr = String(gps.satellites.value());
  lcd.setCursor(LCD_COLS - satsStr.length(), 0);
  lcd.print(satsStr);

  lcd.setCursor(0, 1);
  lcd.print(gps.location.lng(), 6);

  int maxSnr = 0;
  for (int i = 0; i < MAX_SATELLITES; ++i)
  {
    if (sats[i].active && sats[i].snr > maxSnr)
    {
      maxSnr = sats[i].snr;
    }
  }
  String snrStr = String(maxSnr);
  lcd.setCursor(LCD_COLS - snrStr.length(), 1);
  lcd.print(snrStr);

  // --- BAGIAN 3: Reset data satelit ---
  for (int i = 0; i < MAX_SATELLITES; ++i)
  {
    sats[i].active = false;
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