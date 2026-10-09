// Browser build of the board's screens: the same app.cpp / game.cpp / render.cpp / ui_gfx.cpp, compiled to
// WebAssembly. The page (web.js) plays the part of the sketch: buttons, steps, sleep and storage.
#include "../firmware/PocketRPG/app.h"
#include "../firmware/PocketRPG/render.h"
#include <stdlib.h>
#include <string.h>

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))
#define IMPORT(name) extern "C" __attribute__((import_module("env"), import_name(#name)))

IMPORT(js_save) void js_save(int kind, const void* p, int len);   // 0 hero, 1 settings, 2 game
IMPORT(js_erase) void js_erase();
IMPORT(js_random) uint32_t js_random(uint32_t n);
IMPORT(js_brightness) void js_brightness(int level);
IMPORT(js_radio) void js_radio(int on);
IMPORT(js_radio_send) void js_radio_send(const uint8_t* mac, const uint8_t* data, int len);   // mac null = everyone

void platformSaveHero(const Hero& h) { js_save(0, &h, sizeof h); }
void platformEraseHero() { js_erase(); }
void platformSaveSettings(const Settings& s) { js_save(1, &s, sizeof s); }
void platformSaveGame(const Game& g) { js_save(2, &g, sizeof g); }
void platformApplyBrightness(uint8_t level) { js_brightness(level); }
uint32_t platformRandom(uint32_t n) { return js_random(n); }
void platformRadio(bool on) { js_radio(on); }
void platformRadioSend(const uint8_t* mac, const uint8_t* data, int len) { js_radio_send(mac, data, len); }
int platformBatteryLog(BatSample* out, int max) {   // a made-up night, to show the screen
  int n = 0; float pct = 96;
  for (int i = 0; i < 90 && n < max; i++) {
    uint8_t m = i < 6 ? BM_ON : i < 54 ? BM_AOD : BM_OFF;
    pct -= m == BM_ON ? 2.2f : m == BM_AOD ? 0.9f : 0.15f;
    out[n++] = { (uint16_t)(pct * 10), m, (uint8_t)(m == BM_ON ? 0 : 96) };
  }
  return n;
}
void platformUpdate(int action) {   // no WiFi here: the page only shows the screen
  if (action == UA_CHECK || action == UA_INSTALL) appUpdateStatus(U_LATEST, "Board only", "Updates run on the real board", -1);
  else if (action == UA_SETUP) appUpdateStatus(U_SETUP, "PocketRPG-AB12", "", -1);
}

static uint16_t* fb;
static float* work;
static Hero heroBuf;
static Settings settingsBuf = { 0, 1, 1, 0 };
static Game gameBuf;

EXPORT(web_hero_buf) void* web_hero_buf() { return &heroBuf; }
EXPORT(web_hero_size) int web_hero_size() { return sizeof(Hero); }
EXPORT(web_settings_buf) void* web_settings_buf() { return &settingsBuf; }
EXPORT(web_settings_size) int web_settings_size() { return sizeof(Settings); }
EXPORT(web_game_buf) void* web_game_buf() { return &gameBuf; }
EXPORT(web_game_size) int web_game_size() { return sizeof(Game); }
EXPORT(web_game_old_size) int web_game_old_size() { return GAME_OLD_SIZE; }
EXPORT(web_fb) void* web_fb() { return fb; }

static void buffers() { if (!fb) { fb = (uint16_t*)malloc(SCREEN_W * SCREEN_H * 2); work = (float*)malloc(sizeof(float) * RENDER_WORK_FLOATS); } }
EXPORT(web_splash) void web_splash() { buffers(); appSplash(fb); }
EXPORT(web_begin) void web_begin(int haveHero, int haveGame) {
  buffers();
  appBegin(fb, work, haveHero ? &heroBuf : nullptr, settingsBuf, haveHero && haveGame ? &gameBuf : nullptr);
}
EXPORT(web_tap) int web_tap(int x, int y) { return appTap(x, y); }
EXPORT(web_tick) int web_tick(uint32_t now) { return appTick(now); }
EXPORT(web_draw) void web_draw(uint32_t now) { appDraw(now); }
EXPORT(web_settings_button) void web_settings_button() { appSettingsButton(); }
EXPORT(web_set_steps) void web_set_steps(uint32_t steps, uint32_t day) { appSetSteps(steps, day); }
EXPORT(web_set_battery) void web_set_battery(int pct, int charging, int usb) { appSetBattery(pct, charging, usb); }
EXPORT(web_ambient) void web_ambient(int on) { appAmbient(on); }
EXPORT(web_is_ambient) int web_is_ambient() { return appIsAmbient(); }
EXPORT(web_always_on) int web_always_on() { return appSettings().alwaysOn; }
EXPORT(web_timeout) int web_timeout() { return TIMEOUT_SECONDS[appSettings().timeout]; }
EXPORT(web_brightness) int web_brightness() { return BRIGHTNESS_LEVELS[appSettings().brightness]; }
EXPORT(web_upside_down) int web_upside_down() { return appSettings().upsideDown; }

// radio: the page copies an incoming message (6-byte sender address + data) here, then calls web_radio_receive
static uint8_t radioBuf[6 + 256];
EXPORT(web_radio_buf) void* web_radio_buf() { return radioBuf; }
EXPORT(web_radio_receive) int web_radio_receive(int len) { return appRadioReceive(radioBuf, radioBuf + 6, len); }
EXPORT(web_keep_awake) int web_keep_awake() { return appKeepAwake(); }
EXPORT(web_screen_off) void web_screen_off() { appScreenOff(); }
EXPORT(web_touch_raw) int web_touch_raw(int x, int y, int down) { return appTouchRaw(x, y, down); }
EXPORT(web_swipe) int web_swipe(int dir) { return appSwipe(dir); }
