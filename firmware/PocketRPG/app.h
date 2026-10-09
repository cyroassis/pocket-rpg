// Pocket RPG screens: hero setup (first run) and the home placeholder. No hardware code in here,
// so the screens can also be drawn on a PC for checking.
#pragma once
#include <stdint.h>
#include "game.h"

struct Hero {
  uint8_t body, skin, eye, hair, hairColor;
  int8_t prof;      // 0 Blacksmith, 1 Armorer, 2 Tailor
  char name[11];    // up to 10 letters
};
#define HERO_MAX_NAME 10

#define PROFESSION_COUNT 3
extern const char* const PROFESSION_NAMES[PROFESSION_COUNT];

struct Settings {
  uint8_t alwaysOn;    // 1 = after the timeout, show the always-on screen instead of turning off
  uint8_t timeout;     // index into TIMEOUT_SECONDS
  uint8_t brightness;  // index into BRIGHTNESS_LEVELS
  uint8_t upsideDown;  // 1 = screen turned 180 degrees (touch follows)
};
extern const uint16_t TIMEOUT_SECONDS[3];   // 15, 30, 60
extern const uint8_t BRIGHTNESS_LEVELS[3];  // panel brightness for Low, Medium, High

void appSplash(uint16_t* fb);   // the start-up picture, drawn into fb (before appBegin)
void appBegin(uint16_t* fb, float* work, const Hero* saved, const Settings& s, const Game* savedGame);  // saved = nullptr starts the setup
bool appTap(int x, int y);       // a finished tap; true = the screen changed
bool appSwipe(int dir);          // a sideways swipe: +1 = finger moved left (next), -1 = right; true = changed
bool appTouchRaw(int x, int y, bool down);   // every touch reading (for the touch test); true = redraw
void appSetBoardName(const char* name);      // shown on the touch report (board version)
bool appTick(uint32_t now);      // true = something animates and needs a redraw
void appDraw(uint32_t now);      // draws the current screen into fb
void appSettingsButton();        // BOOT: open or close Settings
const Settings& appSettings();
void appSetSteps(uint32_t stepsToday, uint32_t day);   // day = yyyymmdd; new steps turn into finds
void appSetBattery(int percent, bool charging, bool usb);   // percent -1 = no battery
void appShowLowBattery();        // "battery low" screen before the board switches off
void appSetClock(bool valid, int hour, int minute);
void appAmbient(bool on);        // always-on screen: black, the hero's name and steps
bool appIsAmbient();
void appScreenOff();             // the screen turned off (an open trade ends)
bool appKeepAwake();             // true while trading: no screen timeout
bool appRadioReceive(const uint8_t* mac, const uint8_t* data, int len);   // a radio message (trade); true = redraw

// Software update over WiFi (Settings > Update). The sketch does the WiFi and download work and reports here.
enum UpdateState { U_IDLE, U_NO_WIFI, U_BUSY, U_LATEST, U_AVAILABLE, U_DOWNLOAD, U_DONE, U_ERROR, U_SETUP };
enum UpdateAction { UA_CHECK, UA_INSTALL, UA_SETUP, UA_STOP };
void appUpdateStatus(int state, const char* line1, const char* line2, int progress);   // progress 0..100, -1 = none
bool appUpdateOpen();            // the Update screen is showing

// Provided by the sketch (or the PC test)
void platformSaveHero(const Hero& h);
void platformEraseHero();
void platformSaveSettings(const Settings& s);
void platformSaveGame(const Game& g);
void platformRadio(bool on);     // the short-range radio for trading (off the rest of the time)
void platformRadioSend(const uint8_t* mac, const uint8_t* data, int len);   // mac = nullptr: to every board nearby
void platformApplyBrightness(uint8_t level);
void platformUpdate(int action); // UpdateAction: check GitHub, install, set up WiFi, stop (WiFi off)
uint32_t platformRandom(uint32_t n);
