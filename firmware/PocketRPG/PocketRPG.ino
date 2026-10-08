// Pocket RPG for the Waveshare ESP32-S3-Touch-AMOLED-1.8
// Works on both board versions (detected automatically by the touch chip):
//   V1: SH8601 display + FT3168 touch (0x38)      V2: CO5300 display + CST816 touch (0x15)
//
// Arduino IDE: Board "ESP32S3 Dev Module", PSRAM "OPI PSRAM", Flash Size "16MB",
//              Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)", USB CDC On Boot "Enabled"
// Library:     "GFX Library for Arduino" (by Moon On Our Nation) 1.6.x
//
// Libraries: "GFX Library for Arduino" 1.6.x and "SensorLib" (by Lewis He) 0.3.x
//
// First run: hero setup (body, skin, eyes, hair, hair color, profession, name). The hero is then saved
// in flash and the board opens on the home screen. Steps are counted by the motion sensor all day.
// Game: every 200 steps gives 5 XP and a material (20 materials a day); Craft turns 5..50 materials into
// an item (more materials, better odds); Bag holds 20 items; tap the hero for the gear being worn.
// Trade: Bag > Trade on two boards side by side; they talk over ESP-NOW (radio on only while trading).
// Buttons:
//   PWR  short press: screen off / on      (long press still powers the board off)
//   BOOT press: Settings (always-on screen, timeout, brightness, start over)
// After the timeout the screen turns off, or shows the always-on screen if that is enabled.

#include <Arduino.h>
#include <algorithm>
#include <Wire.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include <SensorQMI8658.hpp>
#include <SensorPCF85063.hpp>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "render.h"
#include "app.h"
#include "ota.h"
#include "version.h"
#include <esp_ota_ops.h>

// ---------------------------------------------------------------- pins
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS 12
#define IIC_SDA 15
#define IIC_SCL 14
#define EXPANDER_ADDR 0x20
#define TOUCH_V1_ADDR 0x38   // FT3168
#define TOUCH_V2_ADDR 0x15   // CST816
#define PMU_ADDR 0x34        // AXP2101 power chip (the PWR button goes to it)
#define BOOT_PIN 0           // BOOT button
#define TP_INT 21            // touch chip interrupt: goes low when touched

Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_OLED* gfx = nullptr;
uint8_t touchAddr = 0;

uint16_t* fb = nullptr;
float* work = nullptr;
bool screenOn = true;
Preferences prefs;
SensorQMI8658 imu;
SensorPCF85063 rtc;
bool imuOk = false, rtcOk = false;
uint32_t lastActivity = 0;
uint32_t ambientDrawnAt = 0;
static const uint8_t AMBIENT_BRIGHTNESS = 40;
// The clock is set from the time this file was compiled when it has never been set. The build machine
// is on UTC; this is the offset to local time (Central Daylight Time = -5).
static const int BUILD_TZ_OFFSET_HOURS = -5;

// ---------------------------------------------------------------- I2C helpers
static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}
static void i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// The display power/reset lines sit on the TCA9554 expander (pins 0, 1, 2)
static void powerDisplay() {
  i2cWrite(EXPANDER_ADDR, 0x03, 0xF8);  // pins 0..2 outputs
  i2cWrite(EXPANDER_ADDR, 0x01, 0x00);  // low
  delay(20);
  i2cWrite(EXPANDER_ADDR, 0x01, 0x07);  // high
  delay(50);
}

// Returns true while a finger is down, with its position
static bool readTouch(int* x, int* y) {
  uint8_t d[5];
  if (!i2cRead(touchAddr, 0x02, d, 5)) return false;
  if ((d[0] & 0x0F) == 0) return false;
  *x = ((d[1] & 0x0F) << 8) | d[2];
  *y = ((d[3] & 0x0F) << 8) | d[4];
  // Calibration, measured with Settings > Touch test (24 dots). On V2 boards the CST816 reports about 1.28x
  // too far from the middle on both axes (rep = 1.286 x - 49, rep = 1.273 y - 65), and stops at 0 and at the
  // screen size, so the outer ~40 px of each side read as the edge itself. Undo the stretch here.
  if (touchAddr == TOUCH_V2_ADDR) {
    *x = (int)lroundf((*x + 49) / 1.286f);
    *y = (int)lroundf((*y + 65) / 1.273f);
  }
  if (appSettings().upsideDown) { *x = SCREEN_W - 1 - *x; *y = SCREEN_H - 1 - *y; }
  return true;
}

// ---------------------------------------------------------------- buttons
bool pmuOk = false;
// BOOT is watched by an interrupt on every edge, so a press is never missed while the screen is being
// drawn (a full redraw can take a few hundred ms). The contact bounces on press AND on release, so:
//  - the button is "armed" only after it has been released and quiet for 80 ms;
//  - a press counts only if it starts after that quiet release (bounces of the release never count);
//  - after a press, it must be released and quiet again before the next one counts.
volatile uint32_t bootEdgeAt = 0;   // last edge of any kind
volatile uint32_t bootFallAt = 0;   // last press edge (pin went low)
void IRAM_ATTR onBoot() {
  uint32_t t = millis();
  bootEdgeAt = t;
  if (gpio_get_level((gpio_num_t)BOOT_PIN) == 0) bootFallAt = t;
}

static bool bootPressed(bool wokeByButton) {
  static bool armed = true;
  static uint32_t armedAt = 0;   // when the last release settled
  uint32_t now = millis();
  bool low = digitalRead(BOOT_PIN) == LOW;
  if (!armed) {
    if (!low && now - bootEdgeAt > 80) { armed = true; armedAt = bootEdgeAt; }
    return false;
  }
  bool pressed = low || wokeByButton || (int32_t)(bootFallAt - armedAt) > 30;
  if (!pressed) return false;
  armed = false;
  return true;
}

static void pmuInit() {
  uint8_t en = 0;
  pmuOk = i2cRead(PMU_ADDR, 0x41, &en, 1);
  if (!pmuOk) { Serial.println("No AXP2101 found: PWR button read from the expander"); return; }
  for (uint8_t r = 0x48; r <= 0x4A; r++) i2cWrite(PMU_ADDR, r, 0xFF);   // clear old events
  i2cWrite(PMU_ADDR, 0x41, en | 0x08);                                    // power key short press event on
  // battery: fuel gauge, battery detection and battery voltage measurement on
  uint8_t v = 0;
  if (i2cRead(PMU_ADDR, 0x18, &v, 1)) i2cWrite(PMU_ADDR, 0x18, v | 0x08);
  if (i2cRead(PMU_ADDR, 0x68, &v, 1)) i2cWrite(PMU_ADDR, 0x68, v | 0x01);
  if (i2cRead(PMU_ADDR, 0x30, &v, 1)) i2cWrite(PMU_ADDR, 0x30, v | 0x01);
  // power-off voltage back to the factory setting (2.6 V); the low-battery check below switches off first
  if (i2cRead(PMU_ADDR, 0x24, &v, 1)) i2cWrite(PMU_ADDR, 0x24, v & 0xF8);
  // what the power chip says at start (for problems with switching on)
  uint8_t r00 = 0, r01 = 0, r20 = 0, r21 = 0, r24 = 0, r27 = 0, h = 0, l = 0;
  i2cRead(PMU_ADDR, 0x00, &r00, 1); i2cRead(PMU_ADDR, 0x01, &r01, 1); i2cRead(PMU_ADDR, 0x20, &r20, 1);
  i2cRead(PMU_ADDR, 0x21, &r21, 1); i2cRead(PMU_ADDR, 0x24, &r24, 1); i2cRead(PMU_ADDR, 0x27, &r27, 1);
  i2cRead(PMU_ADDR, 0x34, &h, 1); i2cRead(PMU_ADDR, 0x35, &l, 1);
  Serial.printf("power chip: status %02X %02X, on-source %02X, off-source %02X, voff %02X, keys %02X, battery %d mV\n",
                r00, r01, r20, r21, r24, r27, ((h & 0x1F) << 8) | l);
  // charge up to 4.20 V, the full voltage of a standard LiPo cell (same as Waveshare's examples)
  if (i2cRead(PMU_ADDR, 0x64, &v, 1)) i2cWrite(PMU_ADDR, 0x64, (v & 0xF8) | 0x03);
}
static void saveSteps();
static void redraw();

// Battery level.
// The power chip has its own fuel gauge (register 0xA4), but on this board it is not set up with the
// battery's parameters and reads 0 even on a full cell, so it is used only when it reports 1..100.
// Otherwise the level comes from the battery voltage, using the resting-voltage curve of a LiPo cell
// (4.20 V full, ~3.86 V half, ~3.70 V around 10 %, ~3.5 V empty). The reading is taken while the board
// is working, which pulls the voltage down a little, and while charging the voltage reads high, so both
// are corrected before looking up the curve. It is an estimate: expect about +/-10 %.
static int percentFromVoltage(int mv) {
  static const int MV[13]  = { 3500, 3600, 3650, 3700, 3750, 3790, 3830, 3860, 3920, 3970, 4030, 4100, 4180 };
  static const int PCT[13] = {    0,    3,    6,   11,   20,   30,   42,   52,   60,   67,   76,   88,  100 };
  if (mv <= MV[0]) return 0;
  if (mv >= MV[12]) return 100;
  int i = 0; while (mv > MV[i + 1]) i++;
  return PCT[i] + (PCT[i + 1] - PCT[i]) * (mv - MV[i]) / (MV[i + 1] - MV[i]);
}
static void readBattery() {
  static float smoothMv = 0;
  if (!pmuOk) { appSetBattery(-1, false, true); return; }
  uint8_t s1 = 0, s2 = 0, h = 0, l = 0, gauge = 0;
  i2cRead(PMU_ADDR, 0x00, &s1, 1);
  i2cRead(PMU_ADDR, 0x01, &s2, 1);
  bool battery = s1 & 0x08, usb = s1 & 0x20, charging = (s2 >> 5) == 0x01;
  if (!battery) { appSetBattery(-1, false, usb); return; }
  i2cRead(PMU_ADDR, 0x34, &h, 1); i2cRead(PMU_ADDR, 0x35, &l, 1); i2cRead(PMU_ADDR, 0xA4, &gauge, 1);
  int mv = ((h & 0x1F) << 8) | l;
  if (mv < 2500 || mv > 4500) return;                       // no reading yet
  smoothMv = smoothMv == 0 ? mv : smoothMv * 0.8f + mv * 0.2f;
  // correct for the load (screen on draws more) and for charging, then look up the curve
  int restMv = (int)smoothMv + (charging ? -90 : (screenOn && !appIsAmbient() ? 30 : 10));
  int pct = (gauge >= 1 && gauge <= 100) ? gauge : percentFromVoltage(restMv);
  if (usb && (s2 & 0x07) == 4) pct = 100;   // plugged in and the power chip reports "charge done"
  // Low battery: below 3.35 V on battery alone for 5 readings in a row (10 s), save, warn for 4 s and
  // switch the board off. Not in the first minute after switching on, when the readings are still settling.
  static int lowCount = 0;
  if (!usb && !charging && millis() > 60000 && smoothMv < 3350) lowCount++; else lowCount = 0;
  if (lowCount >= 5) {
    Serial.printf("battery low (%d mV): powering off\n", (int)smoothMv);
    saveSteps();
    if (appIsAmbient()) appAmbient(false);
    if (!screenOn) { screenOn = true; gfx->displayOn(); }
    setCpuFrequencyMhz(240);
    gfx->setBrightness(120);
    appShowLowBattery();
    redraw();
    delay(4000);
    uint8_t c = 0;
    if (i2cRead(PMU_ADDR, 0x10, &c, 1)) i2cWrite(PMU_ADDR, 0x10, c | 0x01);   // power chip: switch off
    while (true) delay(1000);
  }
  static uint32_t loggedAt = 0;
  if (millis() - loggedAt > 30000) { loggedAt = millis(); Serial.printf("battery %d mV -> %d%% (chip says %d%%)%s%s\n", mv, pct, gauge, charging ? ", charging" : "", usb ? ", USB" : ""); }
  appSetBattery(pct, charging, usb);
}

// true once per short press of PWR. The power chip keeps the event until we read it, so no press is lost.
static bool pwrShortPress() {
  if (pmuOk) {
    uint8_t st = 0;
    if (i2cRead(PMU_ADDR, 0x49, &st, 1) && (st & 0x08)) { i2cWrite(PMU_ADDR, 0x49, 0x08); return true; }
    return false;
  }
  static bool wasDown = false; static uint32_t downAt = 0;   // fallback: PWR key on expander input 4
  uint8_t in = 0; bool hit = false;
  if (i2cRead(EXPANDER_ADDR, 0x00, &in, 1)) {
    bool down = in & 0x10;
    if (down && !wasDown) downAt = millis();
    if (!down && wasDown && millis() - downAt < 1000) hit = true;
    wasDown = down;
  }
  return hit;
}

// ---------------------------------------------------------------- hero storage (flash)
static const uint8_t HERO_VERSION = 1;
void platformSaveHero(const Hero& h) {
  prefs.begin("pocketrpg", false);
  prefs.putUChar("ver", HERO_VERSION);
  prefs.putBytes("hero", &h, sizeof h);
  prefs.end();
  Serial.printf("hero saved: %s\n", h.name);
}
void platformEraseHero() {
  prefs.begin("pocketrpg", false);
  prefs.clear();
  prefs.end();
  Serial.println("hero erased");
}
uint32_t platformRandom(uint32_t n) { return esp_random() % n; }
void platformSaveSettings(const Settings& s) {
  prefs.begin("pocketrpg", false);
  prefs.putBytes("settings", &s, sizeof s);
  prefs.end();
}
void platformApplyBrightness(uint8_t level) { if (screenOn && !appIsAmbient()) gfx->setBrightness(level); }
static Settings loadSettings() {
  Settings s = { 1, 1, 1, 0 };   // always-on on, 30 s, medium brightness, normal orientation
  prefs.begin("pocketrpg", true);
  if (prefs.getBytesLength("settings") == sizeof s) prefs.getBytes("settings", &s, sizeof s);
  prefs.end();
  return s;
}

// ---------------------------------------------------------------- clock and steps
static uint32_t today() {   // yyyymmdd, or 0 when there is no clock
  if (!rtcOk) return 0;
  RTC_DateTime t = rtc.getDateTime();
  return (uint32_t)t.getYear() * 10000 + t.getMonth() * 100 + t.getDay();
}
static void setClockFromBuild() {
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = { __DATE__[0], __DATE__[1], __DATE__[2], 0 };
  struct tm t = {};
  t.tm_mon = (strstr(months, mon) - months) / 3;
  t.tm_mday = atoi(__DATE__ + 4);
  t.tm_year = atoi(__DATE__ + 7) - 1900;
  t.tm_hour = atoi(__TIME__); t.tm_min = atoi(__TIME__ + 3); t.tm_sec = atoi(__TIME__ + 6);
  time_t utc = mktime(&t) + BUILD_TZ_OFFSET_HOURS * 3600;   // mktime works in UTC here (no time zone set)
  struct tm l; gmtime_r(&utc, &l);
  rtc.setDateTime(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min, l.tm_sec);
  Serial.printf("clock set from build time: %04d-%02d-%02d %02d:%02d\n", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min);
}

uint32_t stepsToday = 0, stepsDay = 0, stepsRaw = 0, stepsSaved = 0;
uint32_t stepsSavedAt = 0;
static void loadSteps() {
  prefs.begin("pocketrpg", true);
  stepsDay = prefs.getUInt("stepsDay", 0);
  stepsToday = prefs.getUInt("steps", 0);
  prefs.end();
  if (stepsDay != today()) { stepsToday = 0; stepsDay = today(); }
  stepsSaved = stepsToday;
}
static void saveSteps() {
  prefs.begin("pocketrpg", false);
  prefs.putUInt("stepsDay", stepsDay);
  prefs.putUInt("steps", stepsToday);
  prefs.end();
  stepsSaved = stepsToday; stepsSavedAt = millis();
}
static void imuInit() {
  imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  Wire.setClock(400000);
  if (!imuOk) { Serial.println("Motion sensor not found: no step counting"); return; }
  imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_62_5Hz);
  imu.enableAccelerometer();
  // step detection at 62.5 Hz: window 50 samples, peaks of 200 mg, a step lasts 0.32..3.2 s,
  // counting starts after 10 steps in a row (so shaking the board or a car ride count less)
  imu.configPedometer(50, 200, 100, 200, 20, 10, 0, 1);
  imu.enablePedometer();
  stepsRaw = imu.getPedometerCounter();
  Serial.println("Motion sensor ready: counting steps");
}
// true when today's steps changed
static bool updateSteps() {
  bool changed = false;
  uint32_t d = today();
  if (d && d != stepsDay) { stepsDay = d; stepsToday = 0; changed = true; saveSteps(); }
  if (imuOk) {
    uint32_t raw = imu.getPedometerCounter();
    uint32_t delta = raw >= stepsRaw ? raw - stepsRaw : raw;   // the sensor restarted its count
    stepsRaw = raw;
    if (delta) { stepsToday += delta; changed = true; }
  }
  if (stepsToday - stepsSaved >= 100 || (stepsToday != stepsSaved && millis() - stepsSavedAt > 300000)) saveSteps();
  appSetSteps(stepsToday, stepsDay);
  if (rtcOk) { RTC_DateTime t = rtc.getDateTime(); appSetClock(true, t.getHour(), t.getMinute()); }
  return changed;
}
// game progress (XP, materials, bag, gear): saved on every find and every change
void platformSaveGame(const Game& g) {
  prefs.begin("pocketrpg", false);
  prefs.putBytes("game", &g, sizeof g);
  prefs.end();
}
static bool loadGame(Game* g) {
  prefs.begin("pocketrpg", true);
  memset(g, 0, sizeof(Game));
  size_t n = prefs.getBytesLength("game");   // a save from before the step total is shorter: the new fields stay 0
  bool ok = (n == sizeof(Game) || n == GAME_OLD_SIZE) && prefs.getBytes("game", g, n) == n;
  prefs.end();
  return ok && gameValid(*g);
}
static bool loadHero(Hero* h) {
  prefs.begin("pocketrpg", true);
  bool ok = prefs.getUChar("ver", 0) == HERO_VERSION && prefs.getBytesLength("hero") == sizeof(Hero) && prefs.getBytes("hero", h, sizeof(Hero)) == sizeof(Hero);
  prefs.end();
  return ok;
}

// ---------------------------------------------------------------- screen
static void redraw() {
  uint32_t t0 = millis();
  appDraw(millis());
  // upside down: send the picture reversed (a 180 degree turn), then put it back for the next draw
  bool flip = appSettings().upsideDown;
  if (flip) std::reverse(fb, fb + SCREEN_W * SCREEN_H);
  gfx->draw16bitRGBBitmap(0, 0, fb, SCREEN_W, SCREEN_H);
  if (flip) std::reverse(fb, fb + SCREEN_W * SCREEN_H);
  uint32_t dt = millis() - t0;
  if (dt > 120) Serial.printf("frame: %lu ms\n", (unsigned long)dt);
}
// Light sleep: the CPU stops (PSRAM, screen image and step counting stay), and wakes after `ms` or on
// BOOT. Used while the screen is off or on the always-on view (touch is off there).
// The USB serial port drops while sleeping, so logs pause in these modes.
static bool napWokeByButton = false;
static void nap(uint32_t ms, bool wakeOnBoot) {
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  if (wakeOnBoot) {
    detachInterrupt(digitalPinToInterrupt(BOOT_PIN));
    gpio_wakeup_enable((gpio_num_t)BOOT_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  esp_light_sleep_start();
  napWokeByButton = false;
  if (wakeOnBoot) {
    napWokeByButton = digitalRead(BOOT_PIN) == LOW;
    gpio_wakeup_disable((gpio_num_t)BOOT_PIN);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onBoot, CHANGE);
  }
}

static void setAmbient(bool on) {
  if (on) otaStop();
  appAmbient(on);
  setCpuFrequencyMhz(on ? 80 : 240);   // slower CPU while the always-on screen idles
  gfx->setBrightness(on ? AMBIENT_BRIGHTNESS : BRIGHTNESS_LEVELS[appSettings().brightness]);
  redraw();
}
static void setScreen(bool on) {
  screenOn = on;
  lastActivity = millis();
  if (on) {
    if (appIsAmbient()) { appAmbient(false); setCpuFrequencyMhz(240); }
    gfx->displayOn(); gfx->setBrightness(BRIGHTNESS_LEVELS[appSettings().brightness]); redraw();
  } else { otaStop(); appScreenOff(); gfx->setBrightness(0); gfx->displayOff(); }
}

// ---------------------------------------------------------------- radio for trading (ESP-NOW)
// Only on while the Trade screen is open. Boards talk directly to each other on WiFi channel 1, no router.
// Messages arrive on the WiFi task; they are queued here and handed to the game in loop().
struct RadioMsg { uint8_t mac[6]; uint8_t len; uint8_t data[250]; };
static RadioMsg radioQueue[8];
static volatile uint8_t radioHead = 0, radioTail = 0;
static bool radioOn = false;
static uint32_t radioSent = 0, radioFailed = 0, radioGot = 0, radioLoggedAt = 0;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void onRadioReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len <= 0 || len > 250) return;
  uint8_t next = (radioHead + 1) & 7;
  if (next == radioTail) return;   // queue full: drop (the other board sends again in 250 ms)
  RadioMsg& m = radioQueue[radioHead];
  memcpy(m.mac, info->src_addr, 6); m.len = (uint8_t)len; memcpy(m.data, data, len);
  radioHead = next; radioGot++;
}
static void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6); p.channel = 0; p.encrypt = false; p.ifidx = WIFI_IF_STA;   // 0 = the current channel
  esp_now_add_peer(&p);
}
void platformRadio(bool on) {
  if (on == radioOn) return;
  radioOn = on;
  if (on) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_err_t ch = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    Serial.printf("radio: my address %s, channel 1 %s\n", WiFi.macAddress().c_str(), ch == ESP_OK ? "ok" : "FAILED");
    if (esp_now_init() != ESP_OK) { Serial.println("radio: ESP-NOW failed"); return; }
    esp_now_register_recv_cb(onRadioReceive);
    addPeer(BROADCAST);
    radioHead = radioTail = 0;
    Serial.println("radio on");
  } else {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    Serial.println("radio off");
  }
}
void platformRadioSend(const uint8_t* mac, const uint8_t* data, int len) {
  if (!radioOn) return;
  const uint8_t* to = mac ? mac : BROADCAST;
  addPeer(to);
  if (esp_now_send(to, data, len) == ESP_OK) radioSent++; else radioFailed++;
  if (millis() - radioLoggedAt > 3000) {
    radioLoggedAt = millis();
    Serial.printf("radio: sent %lu, failed %lu, received %lu\n", (unsigned long)radioSent, (unsigned long)radioFailed, (unsigned long)radioGot);
  }
}
static bool radioPoll() {   // hand queued messages to the game; true = redraw
  bool redraw = false;
  while (radioTail != radioHead) {
    RadioMsg& m = radioQueue[radioTail];
    if (appRadioReceive(m.mac, m.data, m.len)) redraw = true;
    radioTail = (radioTail + 1) & 7;
  }
  return redraw;
}

// ---------------------------------------------------------------- setup / loop
// A version that arrived over WiFi is only kept once it has drawn its first screen: if it crashes before
// that, the board restarts into the version it had before.
extern "C" bool verifyRollbackLater() { return true; }

// Start-up log: why the board last reset, then each start-up step, so a board that keeps restarting
// shows where it stops. Waits up to 4 s for a serial monitor to connect first.
static void step(const char* what) { Serial.printf("start: %s (%lu ms, free %u / psram %u)\n", what, (unsigned long)millis(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram()); Serial.flush(); }
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) delay(10);
  delay(300);
  static const char* const REASONS[] = { "unknown", "power on", "external", "software", "crash (panic)", "interrupt watchdog",
                                         "task watchdog", "other watchdog", "deep sleep", "brownout (low power)", "SDIO", "USB", "JTAG" };
  int rr = (int)esp_reset_reason();
  Serial.printf("\n=== Pocket RPG v%d, running from %s\n", FW_VERSION, esp_ota_get_running_partition()->label);
  Serial.printf("=== Pocket RPG start. Last reset: %s (%d)\n", rr >= 0 && rr < 13 ? REASONS[rr] : "?", rr);
  step("serial");
  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  powerDisplay();
  step("display power");
  pmuInit();
  step("power chip");
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onBoot, CHANGE);

  // Which board version? V1 has the FT3168 at 0x38, V2 the CST816 at 0x15.
  bool v1 = false;
  for (int i = 0; i < 5 && !v1; i++) { v1 = i2cPresent(TOUCH_V1_ADDR); if (!v1) delay(20); }
  if (v1) {
    touchAddr = TOUCH_V1_ADDR;
    gfx = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, SCREEN_W, SCREEN_H);
  } else {
    touchAddr = TOUCH_V2_ADDR;
    gfx = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, SCREEN_W, SCREEN_H, 16, 0, 0, 0);
    i2cWrite(TOUCH_V2_ADDR, 0xFE, 0x01);  // CST816: no auto sleep (so polling always answers)
    i2cWrite(TOUCH_V2_ADDR, 0xFA, 0x40);  // CST816: report periodically while touched
  }
  Serial.println(v1 ? "Board V1: SH8601 + FT3168" : "Board V2: CO5300 + CST816");
  appSetBoardName(v1 ? "V1" : "V2");

  if (!gfx->begin()) Serial.println("Display init failed");
  step("display");
  gfx->fillScreen(RGB565_BLACK);
  gfx->setBrightness(255);

  fb = (uint16_t*)ps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t));
  work = (float*)ps_malloc(RENDER_WORK_FLOATS * sizeof(float));
  if (!fb || !work) {
    gfx->setTextColor(RGB565_RED); gfx->setTextSize(2); gfx->setCursor(20, 200);
    gfx->print("No PSRAM! Enable it in");
    gfx->setCursor(20, 224); gfx->print("Tools > PSRAM");
    Serial.println("PSRAM allocation failed: enable PSRAM (OPI) in the Tools menu");
    while (true) delay(1000);
  }

  step("memory");
  rtcOk = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  Wire.setClock(400000);
  if (rtcOk && rtc.getDateTime().getYear() < 2026) setClockFromBuild();
  if (!rtcOk) Serial.println("Clock not found: steps never reset by day");
  imuInit();
  loadSteps();
  step("clock, steps");

  Hero saved;
  bool have = loadHero(&saved);
  Serial.println(have ? "hero found" : "no hero yet: setup");
  Settings settings = loadSettings();
  static Game savedGame;
  bool haveGame = have && loadGame(&savedGame);
  step(haveGame ? "hero + game loaded" : "hero loaded, no game");
  appBegin(fb, work, have ? &saved : nullptr, settings, haveGame ? &savedGame : nullptr);
  gfx->setBrightness(BRIGHTNESS_LEVELS[settings.brightness]);
  step("game ready");
  updateSteps();
  step("steps");
  readBattery();
  lastActivity = millis();
  redraw();
  step("first screen drawn");
  esp_ota_mark_app_valid_cancel_rollback();
  otaBegin(redraw);
}

void loop() {
  static bool down = false, wakeTouch = false;
  static int sx = 0, sy = 0, lx = 0, ly = 0;   // where the finger went down, and where it is now
  static uint32_t stepsAt = 0, homeDrawnAt = 0;
  static bool stepsDirty = false;
  uint32_t now = millis();

  // steps and clock, every 2 seconds, screen on or off
  if (now - stepsAt > 2000) { stepsAt = now; if (updateSteps()) stepsDirty = true; readBattery(); }

  // PWR: from the normal screen to the always-on screen (or off, if that is disabled in Settings);
  // from the always-on screen or off, back to the normal screen
  if (pwrShortPress()) {
    if (!screenOn) setScreen(true);
    else if (appIsAmbient()) { lastActivity = millis(); setAmbient(false); }
    else if (appSettings().alwaysOn) { ambientDrawnAt = millis(); setAmbient(true); }
    else setScreen(false);
    return;
  }
  // screen off: sleep, waking every second to count steps and check PWR
  // (BOOT also turns it back on, as a fallback in case the PWR press is not seen)
  if (!screenOn) {
    bool b = bootPressed(napWokeByButton); napWokeByButton = false;
    if (b) { setScreen(true); return; }
    nap(1000, true);
    return;
  }

  bool boot = bootPressed(napWokeByButton); napWokeByButton = false;

  if (appIsAmbient()) {
    // touch is ignored here; only the buttons (BOOT or PWR) bring the normal screen back
    if (boot) { lastActivity = now; setAmbient(false); return; }
    if (now - ambientDrawnAt > 30000) { ambientDrawnAt = now; redraw(); }
    nap(500, true);
    return;
  }

  if (boot) { lastActivity = now; appSettingsButton(); redraw(); }

  int x, y;
  bool touching = readTouch(&x, &y);

  if (appTouchRaw(x, y, touching)) redraw();   // touch test screen
  // touch: act on release, at the spot where the finger went down
  if (touching) lastActivity = now;
  if (touching && !down) { down = true; sx = x; sy = y; }
  if (touching) { lx = x; ly = y; }
  if (!touching && down) {
    down = false;
    int dx = lx - sx, dy = ly - sy;
    if (wakeTouch) wakeTouch = false;
    else if (abs(dx) > 50 && abs(dx) > 2 * abs(dy)) { if (appSwipe(dx < 0 ? 1 : -1)) redraw(); }   // sideways swipe
    else if (appTap(sx, sy)) redraw();
  }

  otaPoll();   // software update: WiFi setup page and downloads (redraws by itself)
  bool radioRedraw = radioPoll();
  if (appTick(now) || radioRedraw) redraw();
  else if (stepsDirty && now - homeDrawnAt > 10000) { homeDrawnAt = now; stepsDirty = false; redraw(); }

  // idle: dim to the always-on screen, or turn off
  // (signed difference: lastActivity can be a little newer than `now`, e.g. right after PWR turned the
  // screen on in this same pass; unsigned math would wrap around and turn it off again at once)
  int32_t idle = (int32_t)(millis() - lastActivity);
  if (appKeepAwake()) lastActivity = now;   // trading: the screen stays on
  if (!down && idle > (int32_t)TIMEOUT_SECONDS[appSettings().timeout] * 1000) {
    if (appSettings().alwaysOn) { ambientDrawnAt = now; setAmbient(true); }
    else setScreen(false);
  }
  delay(15);
}
