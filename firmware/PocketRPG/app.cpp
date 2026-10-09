#include "app.h"
#include "render.h"
#include "ui_gfx.h"
#include "names.h"
#include "version.h"
#include "ui_art.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// ---------------------------------------------------------------- look & feel (same as the prototype page)
static const Rgb C_BG = 0x0E0D13, C_PANEL = 0x1B1726, C_PANEL2 = 0x262036, C_LINE = 0x3B3350, C_INK = 0xEEE9F7,
                 C_MUTED = 0x9C93B4, C_GOLD = 0xE9B44C, C_GOLD_INK = 0x24180A, C_DIM = 0x4A4360, C_DANGER = 0xE0605A;

const char* const PROFESSION_NAMES[PROFESSION_COUNT] = { "Blacksmith", "Armorer", "Tailor" };
static const char* const PROFESSION_MAKES[PROFESSION_COUNT] = { "Forges weapons", "Crafts armor", "Sews capes" };

enum StepKey { S_BODY, S_SKIN, S_EYE, S_HAIR, S_HAIRCOLOR, S_PROF, S_NAME, STEP_COUNT };
static const char* const STEP_TITLES[STEP_COUNT] = { "BOY OR GIRL?", "SKIN TONE", "EYE COLOR", "HAIR STYLE", "HAIR COLOR", "PROFESSION", "NAME YOUR HERO" };
static const bool STEP_HEAD[STEP_COUNT] = { false, false, true, true, true, false, false };

enum Screen { WELCOME, STEP, READY, HOME, CRAFT, EXPLORE, BAG, GEAR, TRADE };
enum Overlay { NONE, SETTINGS, RESET, ITEM, TOUCHTEST, UPDATE, BATTERY };

// preview area and the two camera views (source rectangles in the 368x448 character)
static const float PV_Y = 60, PV_H = 218;
// big touch targets: every button is 66 px tall; the bottom row sits at NAV_Y, 8 px above the screen edge
// (with the touch calibrated, the bottom edge works)
static const float NAV_Y = 374, NAV_H = 66;
struct View { float sx, sy, sw, sh; };
static const View VIEW_FULL = { 0, 10, 368, 435 }, VIEW_HEAD = { 52, 12, 264, 206 };

static uint16_t* fb;
static float* work;
static Hero hero;
static Screen screen = WELCOME;
static Overlay overlay = NONE;
static int step = 0;
static uint32_t shownAt = 0, lastNow = 0;
static bool shownAtPending = false;
static Look rendered;
static bool haveRender = false;
static uint32_t renderedGen = 0;
static Settings settings = { 0, 1, 1, 0 };   // always-on off by default (it costs battery)
static uint32_t stepsToday = 0, curDay = 0;
static Game game;
static bool clockValid = false;
static int clockHour = 0, clockMinute = 0;
static bool ambient = false;
static uint32_t ambientMovedAt = 0;
static int ambientSpot = 4;
static int batteryPct = -1;
static bool lowBattery = false;
static bool batteryCharging = false, usbIn = false;
const uint16_t TIMEOUT_SECONDS[3] = { 15, 30, 60 };
const uint8_t BRIGHTNESS_LEVELS[3] = { 90, 170, 255 };
static const char* const BRIGHTNESS_NAMES[3] = { "Low", "Medium", "High" };

// ---------------------------------------------------------------- tap targets, rebuilt on every draw
typedef void (*Action)(int);
struct Hit { int16_t x, y, w, h; Action fn; int arg; };
static Hit hits[48];
static int hitCount = 0;
static Action swipeFn = nullptr;   // what a sideways swipe does on the screen drawn last (+1 = swipe to the left: next)
static void hit(float x, float y, float w, float h, Action fn, int arg = 0) {
  if (hitCount < 48) hits[hitCount++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, fn, arg };
}

// ---------------------------------------------------------------- character
static void ensureCharacter(bool gear) {
  Look l; memset(&l, 0, sizeof l);
  l.body = hero.body; l.skin = hero.skin; l.eye = hero.eye; l.hair = (int8_t)hero.hair; l.hairColor = hero.hairColor;
  l.armor = -1; l.armorTier = 1; l.weapon = -1; l.weaponTier = 1; l.element = 0; l.cape = -1; l.capeTier = 1;
  if (gear && game.gear[IT_ARMOR].tier) { l.armor = 0; l.armorTier = game.gear[IT_ARMOR].tier; }
  if (gear && game.gear[IT_WEAPON].tier) { l.weapon = (int8_t)itemKind(game.gear[IT_WEAPON]); l.weaponTier = game.gear[IT_WEAPON].tier; }
  if (gear && game.gear[IT_CAPE].tier) { l.cape = 0; l.capeTier = game.gear[IT_CAPE].tier; }
  if (haveRender && renderedGen == workGeneration && memcmp(&l, &rendered, sizeof l) == 0) return;
  renderCharacterLayers(l, work);
  rendered = l; haveRender = true; renderedGen = workGeneration;   // item pictures reuse work: then draw again
}
static void drawView(const View& v, float y, float h, int clipY0, int clipY1, int mode) {
  float s = h / v.sh, dw = v.sw * s;
  blitCharacter(work, fb, v.sx, v.sy, v.sw, v.sh, (SCREEN_W - dw) / 2, y, dw, h, clipY0, clipY1, mode, 0);
}
static void heroFull(float y, float h) { ensureCharacter(true); drawView(VIEW_FULL, y, h, 0, SCREEN_H, 0); }

// ---------------------------------------------------------------- widgets
enum BtnKind { PRIMARY, GHOST, DISABLED, DANGER };
static void button(float x, float y, float w, float h, const char* label, BtnKind k, Action fn, int arg = 0) {
  if (k == PRIMARY) roundBox(x, y, w, h, 14, C_GOLD, 1);
  else if (k == DANGER) roundBox(x, y, w, h, 14, C_DANGER, 1);
  else roundBox(x, y, w, h, 14, k == DISABLED ? C_PANEL : C_PANEL2, 1, C_LINE, 2);
  bool small = textWidth(FONT_PXB24, label) > w - 16;
  text(small ? FONT_PXB16 : FONT_PXB24, label, (int)(x + w / 2), (int)(y + h / 2 + (small ? 5 : 7)),
       k == PRIMARY ? C_GOLD_INK : k == DANGER ? 0x2A0E0C : k == DISABLED ? C_DIM : C_INK);
  if (k != DISABLED) hit(x, y, w, h, fn, arg);
}
static void header(int i) {
  const int n = STEP_COUNT, gap = 8, dw = 8, cw = 26;
  float total = (n - 1) * dw + cw + (n - 1) * gap, x = (SCREEN_W - total) / 2;
  for (int k = 0; k < n; k++) { int w = k == i ? cw : dw; roundBox(x, 12, w, 8, 4, k == i ? C_GOLD : k < i ? C_MUTED : C_DIM, 1); x += w + gap; }
  text(FONT_PX24, STEP_TITLES[i], SCREEN_W / 2, 50, C_INK);
}
static void chevron(float cx, float cy, int dir, Rgb c) {
  thickLine(cx - 7 * dir, cy - 15, cx + 8 * dir, cy, 5, c);
  thickLine(cx + 8 * dir, cy, cx - 7 * dir, cy + 15, 5, c);
}
static void arrows(Action fn) {
  float cy = PV_Y + PV_H / 2;
  roundBox(6, cy - 48, 60, 96, 16, C_PANEL, 0.82f, C_LINE, 2); chevron(36, cy, -1, C_INK);
  roundBox(SCREEN_W - 66, cy - 48, 60, 96, 16, C_PANEL, 0.82f, C_LINE, 2); chevron(SCREEN_W - 36, cy, 1, C_INK);
  // the whole left or right half of the picture (down to the dots) works as the arrow; a swipe does too
  swipeFn = fn;
  hit(0, PV_Y, SCREEN_W / 2, NAV_Y - 8 - PV_Y, fn, -1);
  hit(SCREEN_W / 2, PV_Y, SCREEN_W / 2, NAV_Y - 8 - PV_Y, fn, 1);
}
static void choiceTag(const char* label) {
  float w = textWidth(FONT_PXB16, label) + 32; if (w < 90) w = 90;
  roundBox((SCREEN_W - w) / 2, PV_Y + PV_H - 6, w, 30, 15, C_PANEL2, 1, C_LINE, 2);
  text(FONT_PXB16, label, SCREEN_W / 2, (int)(PV_Y + PV_H + 14), C_INK);
}
static const float ROW_Y = 324;   // swatches / dots under the picture: they show the choices, the arrows change them
static void swatches(const Rgb* list, int n, int cur) {
  float d = n > 6 ? 26 : 32, gap = n > 6 ? 10 : 14, total = n * d + (n - 1) * gap, x = (SCREEN_W - total) / 2, cy = ROW_Y;
  for (int i = 0; i < n; i++) {
    float cx = x + d / 2;
    if (i == cur) ring(cx, cy, d / 2 + 5, 3, C_GOLD);
    disc(cx, cy, d / 2, list[i]);
    ring(cx, cy, d / 2 - 0.75f, 1.5f, 0xFFFFFF, 0.18f);
    x += d + gap;
  }
}
static void dotsRow(int n, int cur) {
  float gap = 14, x = (SCREEN_W - (n - 1) * gap) / 2;
  for (int i = 0; i < n; i++) disc(x + i * gap, ROW_Y, i == cur ? 5 : 3.5f, i == cur ? C_GOLD : C_DIM);
}

// ---------------------------------------------------------------- icons
static int quadTo(float* pts, int n, float x0, float y0, float cx, float cy, float x1, float y1) {
  for (int i = 1; i <= 8; i++) { float t = i / 8.f, u = 1 - t;
    pts[n * 2] = u * u * x0 + 2 * u * t * cx + t * t * x1; pts[n * 2 + 1] = u * u * y0 + 2 * u * t * cy + t * t * y1; n++; }
  return n;
}
static float iconScale = 1;   // icons are drawn at 60 px; the round home buttons use them smaller
static void shift(float* pts, int n, float ox, float oy, float rot) {
  float c = cosf(rot), s = sinf(rot), k = iconScale;
  for (int i = 0; i < n; i++) { float x = pts[i * 2] * k, y = pts[i * 2 + 1] * k; pts[i * 2] = ox + x * c - y * s; pts[i * 2 + 1] = oy + x * s + y * c; }
}
static void rectPoly(float x, float y, float w, float h, float ox, float oy, float rot, Rgb col) {
  float p[8] = { x, y, x + w, y, x + w, y + h, x, y + h }; shift(p, 4, ox, oy, rot); polygon(p, 4, col);
}
enum IconKind { IC_SWORD, IC_SHIELD, IC_CAPE, IC_GEM, IC_DICE, IC_COMPASS, IC_BAG, IC_AXE, IC_MACE };
static Rgb mix(Rgb a, Rgb b, float t) {
  int r = (int)(((a >> 16) & 255) * t + ((b >> 16) & 255) * (1 - t)), g = (int)(((a >> 8) & 255) * t + ((b >> 8) & 255) * (1 - t)), bl = (int)((a & 255) * t + (b & 255) * (1 - t));
  return ((Rgb)r << 16) | ((Rgb)g << 8) | (Rgb)bl;
}
static void icon(int kind, float cx, float cy, Rgb col, Rgb hole, float scale = 1) {
  float p[80 * 2]; int n = 0; iconScale = scale; const float k = scale;
  if (kind == 0) {  // sword
    float r = -(float)M_PI / 4;
    float blade[10] = { 0, -30, 6, -22, 6, 10, -6, 10, -6, -22 }; shift(blade, 5, cx, cy, r); polygon(blade, 5, col);
    rectPoly(-15, 10, 30, 6, cx, cy, r, col); rectPoly(-3.5f, 16, 7, 13, cx, cy, r, col);
    float c[2] = { 0, 32 }; shift(c, 1, cx, cy, r); disc(c[0], c[1], 5 * k, col);
  } else if (kind == 1) {  // shield
    p[n * 2] = 0; p[n * 2 + 1] = -28; n++; p[n * 2] = 24; p[n * 2 + 1] = -20; n++;
    n = quadTo(p, n, 24, -20, 24, 14, 0, 30); n = quadTo(p, n, 0, 30, -24, 14, -24, -20);
    shift(p, n, cx, cy, 0); polygon(p, n, col);
    n = 0; p[0] = 0; p[1] = -18; p[2] = 14; p[3] = -13; n = 2; n = quadTo(p, n, 14, -13, 14, 8, 0, 19);
    shift(p, n, cx, cy, 0); polygon(p, n, hole);
  } else if (kind == 2) {  // cape
    p[0] = -10; p[1] = -26; p[2] = 10; p[3] = -26; n = 2;
    n = quadTo(p, n, 10, -26, 26, 6, 26, 26);
    for (int i = 0; i < 4; i++) { float x0 = 26 - i * 13; n = quadTo(p, n, x0, 26, x0 - 6.5f, 20, x0 - 13, 26); }
    n = quadTo(p, n, -26, 26, -26, 6, -10, -26);
    shift(p, n, cx, cy, 0); polygon(p, n, col); disc(cx, cy - 24 * k, 5 * k, hole);
  } else if (kind == 3) {  // gem
    float g[10] = { -22, -8, -12, -22, 12, -22, 22, -8, 0, 26 }; shift(g, 5, cx, cy, 0); polygon(g, 5, col);
    thickLine(cx - 22, cy - 8, cx + 22, cy - 8, 2, hole);
    thickLine(cx - 6, cy - 22, cx - 10, cy - 8, 2, hole); thickLine(cx - 10, cy - 8, cx, cy + 26, 2, hole);
    thickLine(cx, cy + 26, cx + 10, cy - 8, 2, hole); thickLine(cx + 10, cy - 8, cx + 6, cy - 22, 2, hole);
  } else if (kind == IC_AXE) {
    float r = -(float)M_PI / 4;
    rectPoly(-3, -24, 6, 56, cx, cy, r, col);   // handle
    p[0] = 2; p[1] = -24; p[2] = 16; p[3] = -32; n = 2;
    n = quadTo(p, n, 16, -32, 32, -12, 16, 6);   // the curved edge
    p[n * 2] = 2; p[n * 2 + 1] = -6; n++;
    shift(p, n, cx, cy, r); polygon(p, n, col);
    float c[2] = { 0, 34 }; shift(c, 1, cx, cy, r); disc(c[0], c[1], 4 * k, col);
  } else if (kind == IC_MACE) {
    float r = -(float)M_PI / 4;
    rectPoly(-3, -6, 6, 38, cx, cy, r, col);   // handle
    float h[2] = { 0, -16 }; shift(h, 1, cx, cy, r); disc(h[0], h[1], 13 * k, col);   // the head
    for (int i = 0; i < 6; i++) {   // spikes around the head
      float a = i * (float)M_PI / 3;
      float sp[6] = { cosf(a) * 21, -16 + sinf(a) * 21, cosf(a + 0.35f) * 11, -16 + sinf(a + 0.35f) * 11, cosf(a - 0.35f) * 11, -16 + sinf(a - 0.35f) * 11 };
      shift(sp, 3, cx, cy, r); polygon(sp, 3, col);
    }
    float c[2] = { 0, 34 }; shift(c, 1, cx, cy, r); disc(c[0], c[1], 4 * k, col);
  } else if (kind == IC_COMPASS) {
    ring(cx, cy, 17 * k, 3, col);
    float up[6] = { 0, -13, 5, 0, -5, 0 }, dn[6] = { 0, 13, 5, 0, -5, 0 };
    shift(up, 3, cx, cy, 0); polygon(up, 3, col); shift(dn, 3, cx, cy, 0); polygon(dn, 3, mix(col, hole, 0.45f));
    disc(cx, cy, 2.5f * k, col);
  } else if (kind == IC_BAG) {
    p[0] = -7; p[1] = -12; p[2] = 7; p[3] = -12; p[4] = 4; p[5] = -6; n = 3;
    n = quadTo(p, n, 4, -6, 19, 2, 17, 13); n = quadTo(p, n, 17, 13, 16, 19, 9, 19);
    p[n * 2] = -9; p[n * 2 + 1] = 19; n++;
    n = quadTo(p, n, -9, 19, -16, 19, -17, 13); n = quadTo(p, n, -17, 13, -19, 2, -4, -6);
    shift(p, n, cx, cy, 0); polygon(p, n, col);
    rectPoly(-10, -17, 20, 5, cx, cy, 0, col);
    thickLine(cx - 6 * k, cy - 6 * k, cx + 6 * k, cy - 6 * k, 2.5f, hole);
  } else {  // dice
    roundBox(cx - 13, cy - 13, 26, 26, 6, 0, 0, col, 2.5f);
    const float pip[5][2] = { { -6, -6 }, { 6, 6 }, { 0, 0 }, { 6, -6 }, { -6, 6 } };
    for (int i = 0; i < 5; i++) disc(cx + pip[i][0], cy + pip[i][1], 2.4f, col);
  }
}

// ---------------------------------------------------------------- navigation
static void go(Screen s, int st = -1) {
  screen = s; if (st >= 0) step = st;
  shownAtPending = true;
  if (s == READY) {
    platformSaveHero(hero);
    gameReset(game); game.day = curDay; game.counted = stepsToday; game.lastSteps = stepsToday;   // finds start from the steps walked from now on
    game.mats = TEST_START_MATERIALS;
    platformSaveGame(game);
  }
}
static void actNext(int) { if (step < STEP_COUNT - 1) go(STEP, step + 1); else go(READY); }
static void actBack(int) { if (step > 0) go(STEP, step - 1); else go(WELCOME); }
static void actStart(int) { go(STEP, 0); }
static void actHome(int) { go(HOME); }
static void nav(bool canNext) {
  button(10, NAV_Y, 120, NAV_H, "Back", GHOST, actBack);
  button(140, NAV_Y, 218, NAV_H, "Next", canNext ? PRIMARY : DISABLED, actNext);
}

static void fitHair();
static void actBodyFlip(int) { hero.body = (uint8_t)(1 - hero.body); fitHair(); }
static uint8_t* stepValue() { return step == S_SKIN ? &hero.skin : step == S_EYE ? &hero.eye : &hero.hairColor; }
static int stepCount() { return step == S_SKIN ? 6 : 9; }
static void actCycle(int d) { int n = stepCount(); *stepValue() = (uint8_t)((*stepValue() + d + n) % n); }
// hairstyles are split by body: the boy gets the boy styles, the girl the girl styles
static bool hairFits(int i);
static int hairsForBody() { int n = 0; for (int i = 0; i < hairCount(); i++) if (hairFits(i)) n++; return n; }
static int hairSlot() { int k = 0; for (int i = 0; i < hero.hair && i < hairCount(); i++) if (hairFits(i)) k++; return k; }
static void actHair(int d) {
  int n = hairCount();
  for (int k = 1; k <= n; k++) { int i = (hero.hair + d * k + n * k) % n; if (hairFits(i)) { hero.hair = (uint8_t)i; return; } }
}
static void actProf(int i) { hero.prof = (int8_t)i; }

// body indices in the art: BODIES are sorted by name (Female, Male)
static int bodyIndexMale() { for (int i = 0; i < bodyCount(); i++) if (!strcmp(bodyName(i), "Male")) return i; return 0; }
static bool isBoy() { return hero.body == bodyIndexMale(); }
static bool hairFits(int i) { return hairIsFemale(i) != isBoy(); }
static void fitHair() {
  if (hairFits(hero.hair)) return;
  int pick = hairIndex(isBoy() ? "Spiky" : "Ponytail");
  if (pick < 0 || !hairFits(pick)) for (pick = 0; pick < hairCount() && !hairFits(pick); pick++) {}
  hero.hair = (uint8_t)(pick < hairCount() ? pick : 0);
}

// ---------------------------------------------------------------- name (rolled with the dice)
static void actDice(int) {
  char pick[HERO_MAX_NAME + 1];
  rollName(pick, sizeof pick, hero.name);
  strcpy(hero.name, pick);
}

// ---------------------------------------------------------------- screens
static void drawWelcome() {
  drawBackground(fb);
  ensureCharacter(false);
  blitCharacter(work, fb, VIEW_FULL.sx, VIEW_FULL.sy, VIEW_FULL.sw, VIEW_FULL.sh, 92, 150, 184, 224, 0, SCREEN_H, 2, 0x1A1820);
  text(FONT_PX32, "POCKET RPG", SCREEN_W / 2, 92, C_GOLD);
  text(FONT_PX16, "Every hero starts somewhere.", SCREEN_W / 2, 126, C_MUTED);
  button(10, NAV_Y, 348, NAV_H, "Create your hero", PRIMARY, actStart);
}

static void drawProf() {
  text(FONT_PX16, "You keep it forever.", SCREEN_W / 2, 76, C_MUTED);
  for (int i = 0; i < PROFESSION_COUNT; i++) {
    // three wide cards: icon on the left, name and what it makes on the right
    float x = 10, y = 82 + i * 89, w = 348, h = 82; bool on = hero.prof == i;
    roundBox(x, y, w, h, 14, on ? C_PANEL2 : C_PANEL, 1, on ? C_GOLD : C_LINE, on ? 3 : 2);
    icon(i, x + 52, y + h / 2 + 2, on ? C_GOLD : C_INK, on ? C_PANEL2 : C_PANEL);
    char up[16]; strncpy(up, PROFESSION_NAMES[i], 15); up[15] = 0; for (char* c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    text(FONT_PX24, up, (int)(x + 100), (int)(y + 37), on ? C_GOLD : C_INK, LEFT);
    text(FONT_PX16, PROFESSION_MAKES[i], (int)(x + 100), (int)(y + 60), C_MUTED, LEFT);
    hit(x, y, w, h, actProf, i);
  }
  nav(hero.prof >= 0);
}

static void drawName(uint32_t) {
  if (!hero.name[0]) actDice(0);
  ensureCharacter(false);
  drawView(VIEW_HEAD, 60, 154, 60, 214, 1);
  roundBox(10, 220, 348, 54, 14, C_PANEL, 1, C_LINE, 2);
  text(FONT_PX24, hero.name, SCREEN_W / 2, 255, C_GOLD);
  // the dice: a big button that rolls a new name
  roundBox(10, 280, 348, 62, 14, C_PANEL2, 1, C_GOLD, 2);
  int tw = textWidth(FONT_PX24, "Roll a name");
  float x0 = (SCREEN_W - (tw + 44)) / 2;
  icon(4, x0 + 14, 311, C_GOLD, C_PANEL2);
  text(FONT_PX24, "Roll a name", (int)(x0 + 44), 318, C_INK, LEFT);
  hit(10, 280, 348, 62, actDice);
  nav(true);
}

static void drawStep(uint32_t now) {
  drawBackground(fb);
  header(step);
  if (step == S_PROF) return drawProf();
  if (step == S_NAME) return drawName(now);
  ensureCharacter(false);
  drawView(STEP_HEAD[step] ? VIEW_HEAD : VIEW_FULL, PV_Y, PV_H, (int)PV_Y, (int)(PV_Y + PV_H), STEP_HEAD[step] ? 1 : 0);
  if (step == S_BODY) {
    choiceTag(isBoy() ? "Boy" : "Girl");
    dotsRow(2, isBoy() ? 0 : 1);
    arrows(actBodyFlip);
  } else if (step == S_HAIR) {
    choiceTag(hairName(hero.hair));
    dotsRow(hairsForBody(), hairSlot());
    arrows(actHair);
  } else {
    Rgb list[9]; int n = stepCount();
    const char* name;
    for (int i = 0; i < n; i++) list[i] = step == S_SKIN ? skinSwatch(i, hero.body) : step == S_EYE ? eyeRgb(i) : hairRgb(i);
    name = step == S_SKIN ? SKIN_NAMES[hero.skin] : step == S_EYE ? EYE_NAMES[hero.eye] : HAIR_COLOR_NAMES[hero.hairColor];
    choiceTag(name);
    swatches(list, n, *stepValue());
    arrows(actCycle);
  }
  nav(true);
}

static void drawReady(uint32_t now) {
  drawBackground(fb);
  heroFull(80, 270);
  text(FONT_PX24, hero.name[0] ? hero.name : "Hero", SCREEN_W / 2, 42, C_GOLD);
  char sub[32]; snprintf(sub, sizeof sub, "the %s", PROFESSION_NAMES[hero.prof < 0 ? 0 : hero.prof]);
  text(FONT_PX16, sub, SCREEN_W / 2, 68, C_MUTED);
  // the hero starts with no gear: armor, capes and weapons come from crafting and exploring
  (void)now;
  button(10, NAV_Y, 348, NAV_H, "Start adventure", PRIMARY, actHome);
}

// ---------------------------------------------------------------- home
static void fmtThousands(uint32_t v, char* out) {
  char tmp[16]; int n = snprintf(tmp, sizeof tmp, "%lu", (unsigned long)v), o = 0;
  for (int i = 0; i < n; i++) { out[o++] = tmp[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
  out[o] = 0;
}
// footprints in pixel art: two feet, one a bit higher, drawn in blocks of `px` pixels
static void feet(float x, float y, int px, Rgb col) {
  static const char* const FOOT[9] = { "#.#.#", ".....", ".###.", "#####", "#####", "####.", ".###.", ".###.", ".##.." };
  for (int f = 0; f < 2; f++) {
    int ox = f ? 6 : 0, oy = f ? 0 : 4;
    for (int r = 0; r < 9; r++) for (int k = 0; FOOT[r][k]; k++)
      if (FOOT[r][k] == '#') fillRect((int)x + (ox + (f ? 4 - k : k)) * px, (int)y + (oy + r) * px, px, px, col);
  }
}
static const int FEET_W = 11, FEET_H = 13;   // size in blocks

// ---------------------------------------------------------------- game: shared bits
static FindEvent recent[3]; static int recentCount = 0;   // latest finds (newest last), shown on Explore
struct Banner { char title[20]; char line[28]; Rgb col; Item item; };   // item.tier 0 = no picture
static Banner banners[4]; static int bannerCount = 0; static uint32_t bannerAt = 0;   // home pop-ups, one at a time
static void pushBanner(const char* title, const char* line, Rgb col, const Item* item = nullptr) {
  if (bannerCount == 4) { memmove(banners, banners + 1, sizeof(Banner) * 3); bannerCount = 3; }
  Banner& b = banners[bannerCount++];
  strncpy(b.title, title, 19); b.title[19] = 0; strncpy(b.line, line, 27); b.line[27] = 0; b.col = col;
  memset(&b.item, 0, sizeof b.item); if (item) b.item = *item;
  if (bannerCount == 1) bannerAt = 0;
}
static void levelBanner(int gained) {
  if (gained <= 0) return;
  char l[28]; snprintf(l, sizeof l, "You are level %d", levelOf(game.xp));
  pushBanner("LEVEL UP!", l, C_GOLD);
}
static int heroLevel() { return levelOf(game.xp); }
static bool heroExists() { return screen != WELCOME && screen != STEP; }
static Rgb dimmed(Rgb c, Rgb bg) { return mix(c, bg, 0.35f); }

// pixel art: a lump of material, and a small padlock for gear above the hero's level
static void pixelArt(const char* const* rows, int nr, float x, float y, int px, Rgb a, Rgb b) {
  for (int r = 0; r < nr; r++) for (int k = 0; rows[r][k]; k++) {
    char c = rows[r][k];
    if (c == '#' || c == 'o') fillRect((int)x + k * px, (int)y + r * px, px, px, c == 'o' ? b : a);
  }
}
static const char* const ORE[6] = { "...##...", "..####..", ".##oo##.", "##oo####", "########", ".######." };
static const char* const LOCK[7] = { ".###.", "#...#", "#...#", "#####", "##.##", "##.##", "#####" };
static void matBadge(float right, float baseline) {   // material lump + count, right-aligned
  char t[12]; snprintf(t, sizeof t, "%u", game.mats);
  int tw = textWidth(FONT_PX24, t);
  text(FONT_PX24, t, (int)right, (int)baseline, C_INK, RIGHT);
  pixelArt(ORE, 6, right - tw - 10 - 24, baseline - 19, 3, 0xC98B4F, 0xF2C98A);
}
static void titleBar(const char* t) { text(FONT_PX24, t, 14, 46, C_INK, LEFT); matBadge(354, 46); }

static const int ITEM_ICON[3] = { IC_SWORD, IC_SHIELD, IC_CAPE };
static const int WEAPON_ICON[3] = { IC_SWORD, IC_AXE, IC_MACE };   // by weapon kind
static int iconFor(int type, int kind) { return type == IT_WEAPON && kind >= 0 && kind < 3 ? WEAPON_ICON[kind] : ITEM_ICON[type % 3]; }
// the item's own picture (weapons with their hand for now), dimmed when the hero can't use it yet
static void itemIcon(const Item& it, float cx, float cy, float size, Rgb, bool locked) {
  const uint8_t* px = itemThumb(it.type, itemKind(it), it.tier, hero.body, 0, work);
  blitThumb(fb, px, cx, cy, size, locked ? 0.4f : 1.f);
}
static void tierLine(const Item& it, char* out, int n) {
  char up[24]; strncpy(up, RARITY_NAMES[it.tier - 1], 23); up[23] = 0;
  for (char* q = up; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  snprintf(out, n, "TIER %d \xC2\xB7 %s", it.tier, up);
}
static void actGo(int s) { go((Screen)s); }

// ---------------------------------------------------------------- home
static const float HERO_SHIFT = 62;   // hero moved right to free a column on the left for the action icons
// Home: the hero, the name panel (top left), level and XP (top right), the three actions down the left side
// and pop-ups at the bottom right. Art from art/ui/ (tools/build_ui.py); text drawn here.
static const Rgb C_LAVENDER = 0xB4ABD2, C_OUTLINE = 0x120C1A;
// the shards you have, in a screen's top right corner: icon, then the number ending at `right`
// (the number shrinks when it would run past `left`, the end of the title panel)
static void shardCount(const char* have, int right, int cy, int left) {
  const Font& f = right - textWidth(FONT_PXB24, have) - 6 - uiW(UI_SHARD) >= left ? FONT_PXB24 : FONT_PXB16;
  text(f, have, right, cy + (&f == &FONT_PXB24 ? 12 : 8), C_INK, RIGHT);
  uiDraw(UI_SHARD, right - textWidth(f, have) - 6 - uiW(UI_SHARD), cy - uiH(UI_SHARD) / 2);
}
static void drawHome() {
  drawBackgroundAt(fb, SCREEN_W / 2 + HERO_SHIFT);
  ensureCharacter(true);
  blitCharacter(work, fb, 0, 0, SCREEN_W, SCREEN_H, HERO_SHIFT, 0, SCREEN_W, SCREEN_H, 0, SCREEN_H, 1, 0);
  hit(130, 100, 238, 280, actGo, GEAR);   // tap the hero: gear

  // name panel: grows to fit the name
  char up[HERO_MAX_NAME + 1]; strcpy(up, hero.name); for (char* q = up; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  const char* prof = PROFESSION_NAMES[hero.prof < 0 ? 0 : hero.prof];
  char profUp[16]; snprintf(profUp, sizeof profUp, "%s", prof); for (char* q = profUp; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  const Font& nf = textWidth(FONT_PXB24, up) <= 150 ? FONT_PXB24 : FONT_PXB16;
  char num[16]; fmtThousands(stepsToday, num);
  int px = uiX(UI_NAME), py = uiY(UI_NAME), pad = 18;
  int inner = textWidth(nf, up);
  if (textWidth(FONT_PX16, profUp) > inner) inner = textWidth(FONT_PX16, profUp);
  int stepsW = uiW(UI_FEET) + 6 + textWidth(FONT_PXB16, num);
  if (stepsW > inner) inner = stepsW;
  uiDrawWide(UI_NAME, px, py, inner + 2 * pad);
  int tx = px + pad;
  text(nf, up, tx, py + 35, C_GOLD, LEFT);
  text(FONT_PX16, profUp, tx, py + 57, C_LAVENDER, LEFT);
  uiDraw(UI_FEET, tx, py + 61);
  text(FONT_PXB16, num, tx + uiW(UI_FEET) + 6, py + 77, C_INK, LEFT);

  // XP bar, then the level under it (no panel: outlined text over the picture)
  int lv = heroLevel();
  float into = (float)(game.xp - xpAtLevel(lv)) / xpToNext(lv);
  if (into < 0) into = 0; if (into > 1) into = 1;
  int bx = uiX(UI_XPBAR), fx = uiX(UI_XPFILL), fullW = uiW(UI_XPBAR) - 2 * (fx - bx);
  uiDraw(UI_XPBAR, bx, uiY(UI_XPBAR));
  int fw = (int)(fullW * into + 0.5f);
  if (fw > 0) uiDrawCols(UI_XPFILL, fx, uiY(UI_XPFILL), fw);
  int right = bx + uiW(UI_XPBAR) - 6;
  textOutlined(FONT_PXB16, "LEVEL", right, 58, C_LAVENDER, C_OUTLINE, 2, RIGHT);
  char ln[8]; snprintf(ln, sizeof ln, "%d", lv);
  textOutlined(FONT_PX32, ln, right, 88, C_GOLD, C_OUTLINE, 2, RIGHT);

  // actions
  const int ids[3] = { UI_BTN_CRAFT, UI_BTN_EXPLORE, UI_BTN_BAG };
  Screen to[3] = { CRAFT, EXPLORE, BAG };
  for (int i = 0; i < 3; i++) {
    int x = uiX(ids[i]), y = uiY(ids[i]);
    uiDraw(ids[i], x, y);
    hit(x, y, uiW(ids[i]), uiH(ids[i]), actGo, to[i]);
  }

  // pop-ups (level up, items found while walking), one at a time; the panel grows to the left
  if (bannerCount) {
    if (!bannerAt) bannerAt = lastNow ? lastNow : 1;
    const Banner& b = banners[0];
    int r = uiX(UI_BANNER) + uiW(UI_BANNER), y = uiY(UI_BANNER), pic = b.item.tier ? 46 : 0, pad = 16;
    int tw = textWidth(FONT_PX16, b.title); if (textWidth(FONT_PXB16, b.line) > tw) tw = textWidth(FONT_PXB16, b.line);
    int w = pad + pic + tw + pad; if (w < uiW(UI_BANNER)) w = uiW(UI_BANNER);
    int x = r - w;
    uiDrawWide(UI_BANNER, x, y, w);
    if (pic) itemIcon(b.item, x + pad + 18, y + uiH(UI_BANNER) / 2, 40, C_PANEL, false);
    int tx2 = x + pad + pic;
    text(FONT_PX16, b.title, tx2, y + 24, C_LAVENDER, LEFT);
    text(FONT_PXB16, b.line, tx2, y + 44, b.col, LEFT);
  }
}

// ---------------------------------------------------------------- explore: what walking brings
// art from art/ui/explore.png (tools/build_ui.py), with the changing parts drawn here
static void drawExplore() {
  uiDraw(UI_EXPLORE_BG, 0, 0);
  bool done = game.finds >= DAILY_FINDS;
  uint32_t into = stepsToday >= game.counted ? stepsToday - game.counted : 0;
  if (into > STEPS_PER_FIND) into = STEPS_PER_FIND;
  char t[40];
  // top right: the shards you have
  char have[16]; fmtThousands(game.mats, have);
  shardCount(have, 350, 50, 222);
  // next find
  text(FONT_PXB16, done ? "NEXT XP" : "NEXT FIND", 37, 143, C_LAVENDER, LEFT);
  snprintf(t, sizeof t, "%lu STEPS", (unsigned long)(STEPS_PER_FIND - into));
  text(FONT_PXB24, t, 331, 146, C_GOLD, RIGHT);
  float bx0 = UI_EXPLORE_BAR_X0 + 1, by0 = UI_EXPLORE_BAR_Y0 + 1, bx1 = UI_EXPLORE_BAR_X1 - 1, by1 = UI_EXPLORE_BAR_Y1 - 1, bh = by1 - by0;
  if (into) { float w = (bx1 - bx0) * into / STEPS_PER_FIND; if (w < bh) w = bh; roundBox(bx0, by0, w, bh, bh / 2, C_GOLD, 1); }
  text(FONT_PXB16, done ? "SHARDS DONE FOR TODAY" : "+1 SHARD  +5 XP", SCREEN_W / 2, 204, C_LAVENDER);
  // today
  snprintf(t, sizeof t, "%d/%d", game.finds, DAILY_FINDS);
  text(FONT_PX32, t, 97, 293, C_GOLD);
  snprintf(t, sizeof t, "+%lu", (unsigned long)game.xpDay);
  text(FONT_PX32, t, 272, 293, C_GOLD);
  // the latest find, and all the steps ever
  if (recentCount) {
    const FindEvent& e = recent[recentCount - 1];
    if (e.kind == F_ITEM) { char nm[28]; itemName(e.item, nm, sizeof nm); for (char* q = nm; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
      snprintf(t, sizeof t, "FOUND %s", nm); text(FONT_PXB16, t, SCREEN_W / 2, 337, rarityRgb(e.item.tier)); }
    else text(FONT_PXB16, e.kind == F_MATERIAL ? "LAST FIND: +1 SHARD" : "LAST: +5 XP", SCREEN_W / 2, 337, C_INK);
  } else text(FONT_PXB16, "WALK TO FIND SHARDS", SCREEN_W / 2, 337, C_INK);
  char tot[16]; fmtThousands(totalSteps(game), tot);
  snprintf(t, sizeof t, "TOTAL STEPS %s", tot);
  text(FONT_PXB16, t, SCREEN_W / 2, 360, C_LAVENDER);
  hit(13, 369, 343, 61, actGo, HOME);   // Back (its label is in the art)
}

// ---------------------------------------------------------------- craft
static int craftAmount = CRAFT_MIN, craftPhase = 0, craftSlot = -1;   // phase: 0 choose, 1 working, 2 result
static uint32_t craftAt = 0;
static const uint32_t CRAFT_MS = 1500;
static uint8_t craftType() { return (uint8_t)(hero.prof < 0 ? 0 : hero.prof); }   // Blacksmith weapons, Armorer armor, Tailor capes
static int craftMax() { int m = game.mats / CRAFT_MIN * CRAFT_MIN; return m > CRAFT_MAX ? CRAFT_MAX : m; }
static void fitAmount() { int m = craftMax(); if (craftAmount > m) craftAmount = m; if (craftAmount < CRAFT_MIN) craftAmount = CRAFT_MIN; }
static int craftKind = 0;   // Blacksmith: which weapon (sword, axe, ...)
static void actKind(int d) { int n = weaponKindCount(); craftKind = (craftKind + d + n) % n; }
static uint32_t amountNoteAt = 0;   // when "+" was tapped without enough shards (shows a note for a moment)
static void actAmount(int d) {
  if (d > 0 && craftAmount + CRAFT_MIN > craftMax() && craftAmount < CRAFT_MAX) { amountNoteAt = lastNow ? lastNow : 1; return; }
  craftAmount += d * CRAFT_MIN; if (craftAmount > CRAFT_MAX) craftAmount = CRAFT_MIN; else if (craftAmount < CRAFT_MIN) craftAmount = craftMax() >= CRAFT_MIN ? craftMax() : CRAFT_MIN; fitAmount(); }
static void actCraft(int) {
  int lv = 0, s = gameCraft(game, craftType(), craftKind, craftAmount, &lv);
  if (s < 0) return;
  craftSlot = s; craftPhase = 1; craftAt = lastNow;
  levelBanner(lv);
  platformSaveGame(game);
}
// ---------------------------------------------------------------- item card (art/ui/item.png): new item, or an item tapped
// in the bag, the gear or a trade. The frame and button shapes are art; the rest is drawn here.
enum CardBtn { CB_DARK, CB_GOLD, CB_DANGER, CB_OFF };
static const float CARD_CX = 183.2f, CARD_CY = 154.8f, CARD_R = 69.8f;
static void cardBase(const Item& it, const char* title) {
  uiDraw(UI_CARD_BG, 0, 0);
  textOutlined(FONT_PXB24, title, SCREEN_W / 2, 68, 0xFFE9A8, C_OUTLINE, 2);
  Rgb rc = rarityRgb(it.tier);
  disc(CARD_CX, CARD_CY, CARD_R, rc, 0.16f); ring(CARD_CX, CARD_CY, CARD_R - 1.5f, 3, rc);
  itemIcon(it, CARD_CX, CARD_CY, 116, C_PANEL, false);
  char nm[28], tl[40];
  itemName(it, nm, sizeof nm); for (char* q = nm; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  tierLine(it, tl, sizeof tl);
  text(textWidth(FONT_PXB24, nm) <= 300 ? FONT_PXB24 : FONT_PXB16, nm, SCREEN_W / 2, 259, 0xFFF4DA);
  text(FONT_PXB16, tl, SCREEN_W / 2, 287, rc);
}
static void cardLine(int i, const char* s, Rgb c) { text(i ? FONT_PX16 : FONT_PXB16, s, SCREEN_W / 2, i ? 329 : 312, c); }
// buttons in a row along the bottom of the card: n of them, the i-th one
static void cardButton(int i, int n, const char* label, int kind, Action fn, int arg = 0) {
  const int left = 27, right = 341, gap = 8;
  int w = (right - left - gap * (n - 1)) / n, x = left + i * (w + gap);
  int id = kind == CB_GOLD ? UI_BTN_GOLD : UI_BTN_DARK, y = uiY(UI_BTN_DARK), h = uiH(id);
  uiDrawWide(id, x, y, w);
  bool big = textWidth(FONT_PXB24, label) <= w - 30;
  Rgb c = kind == CB_GOLD ? 0x1A1206 : kind == CB_DANGER ? C_DANGER : kind == CB_OFF ? C_DIM : 0xFFF4DA;
  text(big ? FONT_PXB24 : FONT_PXB16, label, x + w / 2, y + h / 2 + (big ? 9 : 6), c);
  if (kind != CB_OFF) hit(x, y, w, h, fn, arg);
}

static void actCraftDone(int) { craftPhase = 0; fitAmount(); }
static void actEquipCrafted(int) { if (gameEquip(game, craftSlot)) platformSaveGame(game); craftPhase = 0; fitAmount(); }
static void actCraftToBag(int) { craftPhase = 0; fitAmount(); go(BAG); }

static void drawCraft(uint32_t now) {
  drawBackground(fb);
  if (craftPhase == 1) {   // the hammer at work, on the item card: the frame rings at each strike and sparks fly
    uiDraw(UI_CARD_BG, 0, 0);
    static const char* const WORK[3] = { "FORGING", "CRAFTING", "SEWING" };
    char t[24]; int dots = (int)((now - craftAt) / 250) % 4;
    snprintf(t, sizeof t, "%s%.*s", WORK[craftType() < 3 ? craftType() : 1], dots, "...");
    textOutlined(FONT_PXB24, t, SCREEN_W / 2 - textWidth(FONT_PXB24, WORK[craftType() < 3 ? craftType() : 1]) / 2, 68, 0xFFE9A8, C_OUTLINE, 2, LEFT);
    float tt = (float)(now - craftAt) / CRAFT_MS, beat = fmodf(tt * 4, 1.f), hitK = beat < 0.2f ? 1 - beat / 0.2f : 0;
    disc(CARD_CX, CARD_CY, CARD_R + 14 * hitK, C_GOLD, 0.05f + 0.10f * hitK);   // glow at each strike
    disc(CARD_CX, CARD_CY, CARD_R, 0x0E0C1A, 1);
    ring(CARD_CX, CARD_CY, CARD_R - 1.5f, 3 + 2 * hitK, C_GOLD);
    int ic = craftType() == IT_WEAPON ? UI_ICON_SWORD + (craftKind < 3 ? craftKind : 0) : craftType() == IT_ARMOR ? UI_ICON_ARMOR : UI_ICON_CAPE;
    int shake = hitK > 0.5f ? ((int)(now / 40) % 2 ? 2 : -2) : 0;
    uiDraw(ic, (int)(CARD_CX - uiW(ic) / 2.f) + shake, (int)(CARD_CY - uiH(ic) / 2.f) + (int)(3 * hitK));
    for (int i = 0; i < 10; i++) {   // sparks
      float ang = i * 0.628f + (int)(tt * 4) * 0.45f, r = CARD_R + 6 + 46 * beat;
      if (beat < 0.6f) disc(CARD_CX + cosf(ang) * r, CARD_CY + sinf(ang) * r, 3.5f * (1 - beat / 0.6f) + 1, 0xFFE3A3, 1 - beat / 0.6f);
    }
    // the luck being rolled, and a bar that fills while it works
    snprintf(t, sizeof t, "LUCK X%d", craftRolls(craftAmount));
    text(FONT_PXB16, t, SCREEN_W / 2, 274, C_LAVENDER);
    float p = tt > 1 ? 1 : tt;
    roundBox(60, 300, 248, 18, 9, 0x1B1830, 1, 0x3B3350, 2);
    if (p > 0.02f) roundBox(62, 302, 244 * p < 14 ? 14 : 244 * p, 14, 7, C_GOLD, 1);
    snprintf(t, sizeof t, "-%d", craftAmount);
    int tw = textWidth(FONT_PXB24, t), x0 = (SCREEN_W - (uiW(UI_SHARD) + 8 + tw)) / 2;
    uiDraw(UI_SHARD, x0, 372 - uiH(UI_SHARD) / 2);
    text(FONT_PXB24, t, x0 + uiW(UI_SHARD) + 8, 382, C_INK, LEFT);
    return;
  }
  if (craftPhase == 2) {   // the new item
    const Item& it = game.bag[craftSlot];
    cardBase(it, "NEW ITEM");
    bool ok = canEquip(game, it);
    char lk[32]; if (ok) snprintf(lk, sizeof lk, "FITS YOUR LEVEL"); else snprintf(lk, sizeof lk, "NEEDS LEVEL %d", it.tier);
    cardLine(0, lk, ok ? C_LAVENDER : C_DANGER);
    cardButton(0, 2, "OK", CB_DARK, actCraftDone);
    if (ok) cardButton(1, 2, "EQUIP", CB_GOLD, actEquipCrafted);
    else cardButton(1, 2, "BAG", CB_DARK, actCraftToBag);
    return;
  }
  // the picker: art from art/ui/craft.png (tools/build_ui.py), with the changing parts drawn here
  uiDraw(UI_CRAFT_BG, 0, 0);
  char t[40];
  snprintf(t, sizeof t, "%s", PROFESSION_MAKES[craftType()]); for (char* q = t; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  text(FONT_PXB16, t, 32, 94, C_LAVENDER, LEFT);
  // top right: the shards you have (and the weapon kind)
  char have[16]; fmtThousands(game.mats, have);
  shardCount(have, 352, 42, 226);
  if (craftType() == IT_WEAPON) {
    char up[16]; strncpy(up, weaponKindName(craftKind), 15); up[15] = 0; for (char* q = up; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    text(FONT_PXB16, up, 352, 90, C_GOLD, RIGHT);
  }
  // what you are making, in the round frame (art/ui/icons)
  int ic = craftType() == IT_WEAPON ? UI_ICON_SWORD + (craftKind < 3 ? craftKind : 0) : craftType() == IT_ARMOR ? UI_ICON_ARMOR : UI_ICON_CAPE;
  uiDraw(ic, (int)(UI_CRAFT_CX - uiW(ic) / 2.f + 0.5f), (int)(UI_CRAFT_CY - uiH(ic) / 2.f + 0.5f));
  if (craftType() == IT_WEAPON && weaponKindCount() > 1) {   // which weapon: arrows beside the picture
    uiDraw(UI_CRAFT_ARROW_L, uiX(UI_CRAFT_ARROW_L), uiY(UI_CRAFT_ARROW_L));
    uiDraw(UI_CRAFT_ARROW_R, uiX(UI_CRAFT_ARROW_R), uiY(UI_CRAFT_ARROW_R));
    hit(40, 104, 144, 108, actKind, -1); hit(184, 104, 144, 108, actKind, 1);
    swipeFn = actKind;
  }
  // how many shards go in: the whole half of the panel works as the arrow
  fitAmount();
  snprintf(t, sizeof t, "%d", craftAmount);
  text(FONT_PXB48, t, SCREEN_W / 2, 276, C_GOLD);
  text(FONT_PXB16, "SHARDS", SCREEN_W / 2, 296, C_LAVENDER);
  hit(14, 214, 170, 96, actAmount, -1); hit(184, 214, 170, 96, actAmount, 1);
  bool full = bagFreeSlot(game) < 0, poor = game.mats < CRAFT_MIN;
  bool note = amountNoteAt && lastNow - amountNoteAt < 1800;
  if (poor) text(FONT_PXB16, "WALK TO FIND SHARDS", SCREEN_W / 2, 336, C_DANGER);
  else if (note) { snprintf(t, sizeof t, "YOU HAVE %u SHARDS", (unsigned)game.mats); text(FONT_PXB16, t, SCREEN_W / 2, 336, C_DANGER); }
  else if (full) text(FONT_PXB16, "YOUR BAG IS FULL", SCREEN_W / 2, 336, C_DANGER);
  else {
    int r = craftRolls(craftAmount);
    snprintf(t, sizeof t, "LUCK X%d", r);
    char x2[16]; snprintf(x2, sizeof x2, "%d XP", r * XP_PER_ROLL);
    int w1 = textWidth(FONT_PXB16, t), w2 = textWidth(FONT_PXB16, x2), gap = 26, x0 = (SCREEN_W - (w1 + gap + w2)) / 2;
    text(FONT_PXB16, t, x0, 336, C_INK, LEFT);
    text(FONT_PXB16, "+", x0 + w1 + gap / 2, 336, C_GOLD);
    text(FONT_PXB16, x2, x0 + w1 + gap, 336, C_INK, LEFT);
  }
  // buttons (their frames are in the background)
  text(FONT_PXB24, "BACK", 87, 388, C_INK); hit(16, 346, 143, 66, actGo, HOME);
  if (poor || full) { fillRect(166, 346, 187, 66, 0x000000, 0.6f); text(FONT_PXB24, "CRAFT", 259, 388, 0x5A4A20); }
  else { text(FONT_PXB24, "CRAFT", 259, 388, C_GOLD_INK); hit(166, 346, 187, 66, actCraft); }
}

// ---------------------------------------------------------------- bag: 20 slots on two pages
static int bagPage = 0, itemSel = -1;
enum ItemFrom { FROM_BAG, FROM_GEAR, FROM_TRADE };   // the item card shows a bag item, a worn item or the other hero's offer
static int itemFrom = FROM_BAG;
static bool salvageArmed = false;
static bool bagPicking = false;   // the bag opened from Trade to choose an item to offer
static void actBagPage(int) { bagPage ^= 1; }
static void actBagSwipe(int d) { bagPage = d > 0 ? 1 : 0; }
static void actOpenItem(int i) { itemSel = i; itemFrom = FROM_BAG; salvageArmed = false; overlay = ITEM; }
static void actOpenGear(int i) { itemSel = i; itemFrom = FROM_GEAR; salvageArmed = false; overlay = ITEM; }
static void actOpenTradeItem(int i) { itemSel = i; itemFrom = FROM_TRADE; overlay = ITEM; }
static bool tradeOffers(int bagSlot);
static void actPickItem(int idx);
static void actPickBack(int);
static void actTradeOpen(int);

// art from art/ui/bag.png (tools/build_ui.py): 4 x 3 slots, the items drawn here
static const float BAG_CX[4] = { 52.5f, 140.9f, 227.7f, 314.5f }, BAG_CY[3] = { 118.2f, 204.4f, 289.9f }, BAG_SLOT = 76;
static bool tradeLocked(int bagSlot);
static void drawBag() {
  uiDraw(UI_BAG_BG, 0, 0);
  char t[16]; snprintf(t, sizeof t, "%d/%d", bagCount(game), BAG_SIZE);
  const char* count = bagPicking ? "PICK" : t;
  text(FONT_PXB24, count, 136, 53, C_LAVENDER, LEFT);
  char have[16]; fmtThousands(game.mats, have);
  int room = 350 - (136 + textWidth(FONT_PXB24, count) + 14) - uiW(UI_SHARD) - 6;   // big numbers shrink to fit
  const Font& hf = textWidth(FONT_PXB24, have) <= room ? FONT_PXB24 : FONT_PXB16;
  text(hf, have, 350, &hf == &FONT_PXB24 ? 54 : 50, C_INK, RIGHT);
  uiDraw(UI_SHARD, 350 - textWidth(hf, have) - 6 - uiW(UI_SHARD), 44 - uiH(UI_SHARD) / 2);
  swipeFn = actBagSwipe;
  int lv = heroLevel();
  for (int i = 0; i < 12; i++) {
    int idx = bagPage * 12 + i;
    if (idx >= BAG_SIZE) break;
    float cx = BAG_CX[i % 4], cy = BAG_CY[i / 4], x = cx - BAG_SLOT / 2, y = cy - BAG_SLOT / 2;
    const Item& it = game.bag[idx];
    if (!it.tier) continue;
    Rgb rc = rarityRgb(it.tier); bool locked = it.tier > lv;
    itemIcon(it, cx, cy - 2, 64, C_PANEL2, locked);
    char tn[4]; snprintf(tn, sizeof tn, "%d", it.tier);
    textOutlined(FONT_PXB16, tn, (int)(x + BAG_SLOT - 6), (int)(y + BAG_SLOT - 6), locked ? dimmed(rc, C_PANEL2) : rc, C_OUTLINE, 1, RIGHT);
    if (locked) pixelArt(LOCK, 7, x + 8, y + 8, 2, C_MUTED, C_MUTED);
    if (tradeLocked(idx)) {   // part of an unfinished trade
      roundBox(x + 4, y + 4, BAG_SLOT - 8, BAG_SLOT - 8, 8, C_BG, 0.55f);
      text(FONT_PXB16, "TRADE", (int)cx, (int)cy + 6, C_GOLD);
      if (bagPicking) continue;
    }
    if (bagPicking && tradeOffers(idx)) { roundBox(x + 2, y + 2, BAG_SLOT - 4, BAG_SLOT - 4, 8, C_BG, 0.6f, C_GOLD, 3); continue; }   // already offered
    hit(x, y, BAG_SLOT, BAG_SLOT, bagPicking ? actPickItem : actOpenItem, idx);
  }
  for (int i = 0; i < 2; i++) disc(173.3f + i * 20.7f, 350.5f, i == bagPage ? 5.5f : 4.5f, i == bagPage ? C_GOLD : 0x3A3458);
  // buttons: the frames and the BACK / TRADE labels are in the art
  text(FONT_PXB24, bagPage ? "PREV" : "NEXT", 302, 404, 0xFFF4DA);
  hit(247, 366, 109, 57, actBagPage);
  if (bagPicking) {
    hit(11, 366, 111, 57, actPickBack);
    fillRect(129, 366, 111, 57, 0x000000, 0.65f);   // no trading from inside the trade
  } else {
    hit(11, 366, 111, 57, actGo, HOME);
    hit(129, 366, 111, 57, actTradeOpen);
  }
}

// ---------------------------------------------------------------- gear: what the hero wears (tap the hero)
static const char* const SLOT_NAMES[GEAR_SLOTS] = { "WEAPON", "ARMOR", "CAPE" };
static void drawGear() {
  drawBackground(fb);
  text(FONT_PX24, "GEAR", 14, 46, C_INK, LEFT);
  int lv = heroLevel();
  char t[40]; snprintf(t, sizeof t, "LEVEL %d", lv);
  text(FONT_PX24, t, 354, 46, C_GOLD, RIGHT);
  for (int i = 0; i < GEAR_SLOTS; i++) {
    float y = 66 + i * 84;
    const Item& it = game.gear[i];
    roundBox(10, y, 348, 76, 14, C_PANEL, 1, it.tier ? mix(rarityRgb(it.tier), C_LINE, 0.45f) : C_LINE, 2);
    if (it.tier) itemIcon(it, 50, y + 38, 68, C_PANEL, false);
    else icon(ITEM_ICON[i], 50, y + 38, C_DIM, C_PANEL, 0.62f);
    text(FONT_PX16, SLOT_NAMES[i], 92, (int)(y + 30), C_MUTED, LEFT);
    if (it.tier) {
      char nm[28]; itemName(it, nm, sizeof nm);
      text(FONT_PXB16, nm, 92, (int)(y + 56), C_INK, LEFT);
      char tn[4]; snprintf(tn, sizeof tn, "%d", it.tier);
      text(FONT_PXB24, tn, 340, (int)(y + 48), rarityRgb(it.tier), RIGHT);
      hit(10, y, 348, 76, actOpenGear, i);
    } else text(FONT_PX16, "Empty", 92, (int)(y + 56), C_DIM, LEFT);
  }
  uint32_t into = game.xp - xpAtLevel(lv);
  snprintf(t, sizeof t, "%lu / %lu XP", (unsigned long)into, (unsigned long)xpToNext(lv));
  text(FONT_PX16, t, SCREEN_W / 2, 330, C_MUTED);
  button(10, NAV_Y, 348, NAV_H, "Back", GHOST, actGo, HOME);
}

// ---------------------------------------------------------------- item card (over the Bag or Gear)
static void actItemBack(int) { overlay = NONE; }
static void actItemEquip(int) { if (gameEquip(game, itemSel)) platformSaveGame(game); overlay = NONE; }
static void actItemTakeOff(int) { if (gameUnequip(game, itemSel)) platformSaveGame(game); overlay = NONE; }
static void actItemSalvage(int) {
  if (!salvageArmed) { salvageArmed = true; return; }   // second tap confirms
  gameSalvage(game, itemSel); platformSaveGame(game); overlay = NONE;
}
static Item tPeerOffer[3];
// give up on an unfinished trade (asks first): the items unlock. If the other board did finish it, both
// heroes end up with these items, so this is the last resort when the boards can't meet again.
static void finishPending(bool doIt);
static void actUnlockTrade(int) {
  if (!salvageArmed) { salvageArmed = true; return; }
  salvageArmed = false; finishPending(false); overlay = NONE;
}
static void drawItemCard() {
  const Item& it = itemFrom == FROM_GEAR ? game.gear[itemSel] : itemFrom == FROM_TRADE ? tPeerOffer[itemSel] : game.bag[itemSel];
  const bool itemIsGear = itemFrom == FROM_GEAR;
  if (!it.tier) { overlay = NONE; return; }
  static const char* const TITLES[3] = { "WEAPON", "ARMOR", "CAPE" };
  cardBase(it, itemFrom == FROM_TRADE ? "THEIR ITEM" : it.type < 3 ? TITLES[it.type] : "ITEM");
  char st[40];
  bool ok = canEquip(game, it);
  if (itemIsGear) { snprintf(st, sizeof st, "WORN BY %s", hero.name); for (char* q = st; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32; cardLine(0, st, C_GOLD); }
  else if (ok) cardLine(0, "FITS YOUR LEVEL", C_LAVENDER);
  else { snprintf(st, sizeof st, "NEEDS LEVEL %d", it.tier); cardLine(0, st, C_DANGER); }
  if (itemFrom == FROM_TRADE) { cardButton(0, 1, "BACK", CB_DARK, actItemBack); return; }   // the other hero's item: look only
  if (itemFrom == FROM_BAG && tradeLocked(itemSel)) {   // unfinished trade: look only, or give up on it
    snprintf(st, sizeof st, "Locked: trade with %s", game.trade.name); cardLine(1, st, C_GOLD);
    cardButton(0, 2, "BACK", CB_DARK, actItemBack);
    cardButton(1, 2, salvageArmed ? "SURE?" : "UNLOCK", salvageArmed ? CB_DANGER : CB_DARK, actUnlockTrade);
    return;
  }
  if (!itemIsGear) { snprintf(st, sizeof st, "Salvage gives +%d shards", salvageValue(it)); cardLine(1, st, C_MUTED); }
  else if (bagFreeSlot(game) < 0) cardLine(1, "Bag full: no room to take off", C_DANGER);
  if (itemIsGear) {
    cardButton(0, 2, "BACK", CB_DARK, actItemBack);
    cardButton(1, 2, "TAKE OFF", bagFreeSlot(game) < 0 ? CB_OFF : CB_DARK, actItemTakeOff);
  } else {
    cardButton(0, 3, "BACK", CB_DARK, actItemBack);
    cardButton(1, 3, salvageArmed ? "SURE?" : "SALVAGE", salvageArmed ? CB_DANGER : CB_DARK, actItemSalvage);
    cardButton(2, 3, "EQUIP", ok ? CB_GOLD : CB_OFF, actItemEquip);
  }
}

// ---------------------------------------------------------------- trade: two boards side by side (radio)
// Open Trade on both boards. They find each other, then there is a 60 s window: each hero puts 0 to 3 items
// from the bag on the table and approves. Any change (on either side) removes both approvals. When both
// have approved the same two offers, the items swap.
//
// Every 250 ms each board sends its whole state. Offers carry a version number; an approval names the
// other side's offer version it was given for, so it stops counting as soon as that offer changes.
// "ready" = I approved and I see the other approval for my current offer.
//
// Finishing safely (both boards must swap, or neither): when both are ready, each board COMMITS: it saves
// the trade as pending (the offered items lock) and says "commit". A board swaps only once it hears the
// other board's commit, then says "swapped" for a while. If the boards lose each other in between, the
// trade stays pending on the board that didn't swap; the next time the two boards meet (Trade open on
// both), they compare notes: if the other board finished it, this one finishes too; otherwise both cancel.
struct __attribute__((packed)) TradeMsg {
  char magic[2];      // 'P','R'
  uint8_t ver, kind;  // kind: 0 looking, 1 state, 2 leaving
  uint32_t id, peer;  // this board's random id, and the board it is trading with (0 while looking)
  char name[HERO_MAX_NAME + 1];
  uint8_t level, approved, ready, done;
  uint16_t offerVer, approvedFor;   // my offer version; the other offer version my approval is for
  Item offer[3];
  uint8_t commit, swapped;          // finishing: I committed / I already swapped
  uint32_t pendKey;                 // my unfinished trade with anyone (0 = none)
  uint32_t doneKeys[4];             // the last trades I finished
};
enum TradeState { T_OFF, T_SEARCH, T_OPEN, T_COMMIT, T_DONE, T_ENDED };
static const uint32_t TRADE_WINDOW_MS = 60000, TRADE_LOST_MS = 6000, TRADE_SEND_MS = 200, TRADE_COMMIT_MS = 10000, TRADE_DONE_MS = 8000;
static int tState = T_OFF;
static uint32_t tId = 0, tPeer = 0, tEndsAt = 0, tLastHeard = 0, tLastSent = 0, tDoneAt = 0, tLastSecond = 0;
static uint8_t tPeerMac[6];
static char tPeerName[HERO_MAX_NAME + 1];
static uint8_t tPeerLevel = 0;
static int8_t tMySlot[3] = { -1, -1, -1 };   // bag slots I offer
static Item tMyItems[3];                      // copies, so the swap can check nothing moved
static uint16_t tMyVer = 1, tPeerVer = 0, tMyApprovedFor = 0, tPeerApprovedFor = 0;
static bool tMyApproved = false, tPeerApproved = false, tPeerReady = false, tDirty = false, tPeerCommit = false;
static uint32_t tCommitAt = 0;
static bool tPendingEnd = false;   // the trade ended unfinished (pending)
static Item tGot[3]; static int tGotCount = 0;
static const char* tEndText = "";

static bool tradeOffers(int bagSlot) { for (int i = 0; i < 3; i++) if (tMySlot[i] == bagSlot) return true; return false; }
static int myOfferCount() { int n = 0; for (int i = 0; i < 3; i++) if (tMySlot[i] >= 0) n++; return n; }
static int peerOfferCount() { int n = 0; for (int i = 0; i < 3; i++) if (tPeerOffer[i].tier) n++; return n; }
static bool myApprovalValid() { return tMyApproved && tMyApprovedFor == tPeerVer; }
static bool peerApprovalValid() { return tPeerApproved && tPeerApprovedFor == tMyVer; }
static bool tradeReady() { return tState == T_OPEN && myApprovalValid() && peerApprovalValid(); }
static bool tradeLocked(int bagSlot) {   // part of an unfinished trade: can't be salvaged, equipped or offered
  if (!game.trade.key) return false;
  for (int i = 0; i < 3; i++) if (game.trade.slots[i] == bagSlot) return true;
  return false;
}
static bool roomForTrade() { return BAG_SIZE - bagCount(game) + myOfferCount() >= peerOfferCount(); }

static void tradeSend(uint8_t kind) {
  TradeMsg m; memset(&m, 0, sizeof m);
  m.magic[0] = 'P'; m.magic[1] = 'R'; m.ver = 2; m.kind = kind;
  m.id = tId; m.peer = tState == T_SEARCH ? 0 : tPeer;
  strncpy(m.name, hero.name, HERO_MAX_NAME); m.level = (uint8_t)heroLevel();
  bool done = tState == T_DONE, committed = tState == T_COMMIT || done;
  m.approved = committed || myApprovalValid(); m.ready = committed || tradeReady(); m.done = done;
  m.offerVer = tMyVer; m.approvedFor = tMyApprovedFor;
  for (int i = 0; i < 3; i++) m.offer[i] = tMyItems[i];
  m.commit = committed; m.swapped = done;
  m.pendKey = game.trade.key;
  memcpy(m.doneKeys, game.trade.done, sizeof m.doneKeys);
  // always sent to everyone nearby: the ids in the message say who it is for (simpler and sturdier than
  // addressed messages, which need the other board registered on the right channel)
  platformRadioSend(nullptr, (const uint8_t*)&m, sizeof m);
  tLastSent = lastNow;
}
static void tradeEnd(const char* why) {
  if (tState == T_OPEN || tState == T_SEARCH) tradeSend(2);
  platformRadio(false);
  tState = T_ENDED; tEndText = why; tDirty = true;
  if (overlay == ITEM && itemFrom == FROM_TRADE) overlay = NONE;
  bagPicking = false;
}
static void actTradeOpen(int) {
  memset(tMySlot, -1, sizeof tMySlot); memset(tMyItems, 0, sizeof tMyItems); memset(tPeerOffer, 0, sizeof tPeerOffer);
  tId = platformRandom(0x7FFFFFFF) | 1; tPeer = 0; tMyVer = 1; tPeerVer = 0;
  tMyApproved = tPeerApproved = tPeerReady = tPeerCommit = tPendingEnd = false; tMyApprovedFor = tPeerApprovedFor = 0; tGotCount = 0;
  tState = T_SEARCH; tEndsAt = lastNow + TRADE_WINDOW_MS; tLastSent = 0;
  platformRadio(true);
  go(TRADE);
}
static void myOfferChanged() { tMyVer++; tMyApproved = false; tDirty = true; }
static void actTradeSlot(int i) {   // my slot: empty opens the bag to pick, filled takes the item back
  if (tradeReady()) return;
  if (tMySlot[i] >= 0) { tMySlot[i] = -1; memset(&tMyItems[i], 0, sizeof(Item)); myOfferChanged(); return; }
  bagPicking = true; bagPage = 0;
}
static void actPickItem(int idx) {
  for (int i = 0; i < 3; i++) if (tMySlot[i] < 0) { tMySlot[i] = (int8_t)idx; tMyItems[i] = game.bag[idx]; break; }
  bagPicking = false; myOfferChanged();
}
static void actPickBack(int) { bagPicking = false; }
static void actApprove(int) {
  if (tState != T_OPEN || tradeReady()) return;
  if (myApprovalValid()) { tMyApproved = false; }
  else if (roomForTrade()) { tMyApproved = true; tMyApprovedFor = tPeerVer; }
  tDirty = true; tradeSend(1);
}
static void actTradeCancel(int) { if (tState == T_COMMIT) return; tradeEnd("Trade cancelled"); tState = T_OFF; go(BAG); }
static void actTradeLeave(int) { platformRadio(false); tState = T_OFF; bagPicking = false; go(BAG); }

// apply a trade: my offered slots empty, their items go into free slots
static int applyTrade(const int8_t* slots, const Item* get, Item* got) {
  for (int i = 0; i < 3; i++) if (slots[i] >= 0) memset(&game.bag[slots[i]], 0, sizeof(Item));
  int n = 0;
  for (int i = 0; i < 3; i++) if (get[i].tier) {
    int s = bagFreeSlot(game); if (s < 0) break;
    game.bag[s] = get[i]; if (got) got[n] = get[i]; n++;
  }
  return n;
}
static void rememberDone(uint32_t key) {
  memmove(game.trade.done + 1, game.trade.done, sizeof(uint32_t) * 3); game.trade.done[0] = key;
}
// both ready: check the bag, save the trade as pending, and say "commit"
static void tradeCommit() {
  for (int i = 0; i < 3; i++)   // my offer must still be in the bag, as offered
    if (tMySlot[i] >= 0 && memcmp(&game.bag[tMySlot[i]], &tMyItems[i], sizeof(Item)) != 0) { tradeEnd("Your bag changed"); return; }
  if (!roomForTrade()) { tradeEnd("Your bag is full"); return; }
  TradeRec& r = game.trade;
  r.key = (tId ^ tPeer) | 1;
  memcpy(r.mac, tPeerMac, 6);
  memcpy(r.slots, tMySlot, sizeof r.slots);
  memcpy(r.give, tMyItems, sizeof r.give); memcpy(r.get, tPeerOffer, sizeof r.get);
  memcpy(r.name, tPeerName, sizeof r.name);
  platformSaveGame(game);
  tState = T_COMMIT; tCommitAt = lastNow; tDirty = true;
  if (overlay == ITEM && itemFrom == FROM_TRADE) overlay = NONE;
  bagPicking = false;
  tradeSend(1);
}
// the other board committed too: swap, and keep saying "swapped" for a while
static void tradeSwap() {
  tGotCount = applyTrade(game.trade.slots, game.trade.get, tGot);
  rememberDone(game.trade.key); game.trade.key = 0;
  platformSaveGame(game);
  tState = T_DONE; tDoneAt = lastNow; tDirty = true;
  tradeSend(1);
}
// an unfinished trade, settled when the two boards meet again
static void finishPending(bool doIt) {
  TradeRec& r = game.trade;
  char line[28];
  if (doIt) {
    Item got[3]; int n = applyTrade(r.slots, r.get, got);
    rememberDone(r.key);
    snprintf(line, sizeof line, "%d item%s from %s", n, n == 1 ? "" : "s", r.name);
    pushBanner("TRADE FINISHED", line, C_GOLD);
  } else {
    snprintf(line, sizeof line, "with %s: items back", r.name);
    pushBanner("TRADE CANCELLED", line, C_MUTED);
  }
  r.key = 0; memset(r.slots, -1, sizeof r.slots);
  platformSaveGame(game);
  tDirty = true;
}

static bool tradeTick(uint32_t now) {
  if (tState != T_SEARCH && tState != T_OPEN && tState != T_COMMIT && tState != T_DONE) return false;
  if (now - tLastSent >= TRADE_SEND_MS) tradeSend(tState == T_SEARCH ? 0 : 1);
  if (tState == T_DONE) {   // keep telling the other board for a while, then the radio goes off
    if (now - tDoneAt > TRADE_DONE_MS) { platformRadio(false); tState = T_ENDED; tEndText = nullptr; }
    return false;
  }
  if (tState == T_COMMIT) {
    if (tPeerCommit) { tradeSwap(); return true; }
    if (now - tCommitAt > TRADE_COMMIT_MS) {   // never heard the other commit: stays pending until they meet again
      platformRadio(false); tState = T_ENDED; tEndText = "Trade not finished"; tPendingEnd = true; tDirty = true;
      return true;
    }
    return now / 1000 != tLastSecond ? (tLastSecond = now / 1000, true) : false;
  }
  if ((int32_t)(now - tEndsAt) > 0) { tradeEnd(tState == T_SEARCH ? "No one found nearby" : "Time's up"); return true; }
  if (tState == T_OPEN && now - tLastHeard > TRADE_LOST_MS) { tradeEnd("Lost the other board"); return true; }
  if (tradeReady() && tPeerReady) { tradeCommit(); return true; }
  bool redraw = tDirty || now / 1000 != tLastSecond;   // the countdown
  tLastSecond = now / 1000; tDirty = false;
  return redraw;
}

static void tradeReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != (int)sizeof(TradeMsg)) return;
  TradeMsg m; memcpy(&m, data, sizeof m);
  if (m.magic[0] != 'P' || m.magic[1] != 'R' || m.ver != 2 || m.id == tId) return;
  // an unfinished trade with this board, from an earlier meeting: settle it now
  if (game.trade.key && memcmp(mac, game.trade.mac, 6) == 0 && (tState == T_SEARCH || tState == T_OPEN) && m.kind != 2) {
    bool theyFinished = false;
    for (int i = 0; i < 4; i++) if (m.doneKeys[i] == game.trade.key) theyFinished = true;
    finishPending(theyFinished);
  }
  if (tState == T_SEARCH) {
    if (m.kind == 2 || (m.peer != 0 && m.peer != tId)) return;   // leaving, or already trading with someone else
    tPeer = m.id; memcpy(tPeerMac, mac, 6);
    strncpy(tPeerName, m.name, HERO_MAX_NAME); tPeerName[HERO_MAX_NAME] = 0; tPeerLevel = m.level;
    tState = T_OPEN; tEndsAt = lastNow + TRADE_WINDOW_MS;   // the 60 s window starts when the two boards meet
  }
  if ((tState != T_OPEN && tState != T_COMMIT && tState != T_DONE) || m.id != tPeer) return;
  if (m.peer != 0 && m.peer != tId) return;
  tLastHeard = lastNow;
  if (tState == T_COMMIT) { if (m.commit || m.swapped) tPeerCommit = true; return; }   // nothing else changes now
  if (m.kind == 2) { if (tState == T_OPEN) tradeEnd("The other hero left"); return; }
  if (tState == T_DONE) return;
  if (m.offerVer != tPeerVer) {   // their offer changed: my approval no longer counts
    tPeerVer = m.offerVer; memcpy(tPeerOffer, m.offer, sizeof tPeerOffer);
    tMyApproved = false; tDirty = true;
    if (overlay == ITEM && itemFrom == FROM_TRADE) overlay = NONE;
  }
  bool a = m.approved, r = m.ready;
  if (a != tPeerApproved || r != tPeerReady || m.approvedFor != tPeerApprovedFor) tDirty = true;
  tPeerApproved = a; tPeerReady = r; tPeerApprovedFor = m.approvedFor;
  if ((m.commit || m.swapped) && tradeReady()) { tPeerReady = true; tPeerCommit = true; }
}

static void tradeSlot(float x, float y, const Item& it, bool mine, int i) {
  Rgb rc = it.tier ? rarityRgb(it.tier) : C_LINE;
  roundBox(x, y, 80, 80, 12, it.tier ? C_PANEL2 : C_PANEL, 1, it.tier ? mix(rc, C_LINE, 0.45f) : C_LINE, 2);
  if (it.tier) {
    itemIcon(it, x + 40, y + 38, 72, C_PANEL2, false);
    char tn[4]; snprintf(tn, sizeof tn, "%d", it.tier);
    text(FONT_PXB16, tn, (int)(x + 72), (int)(y + 74), rc, RIGHT);
  } else if (mine) { thickLine(x + 40, y + 28, x + 40, y + 52, 4, C_DIM); thickLine(x + 28, y + 40, x + 52, y + 40, 4, C_DIM); }   // +
  if (mine) hit(x, y, 80, 80, actTradeSlot, i);
  else if (it.tier) hit(x, y, 80, 80, actOpenTradeItem, i);
}
static void approvalChip(float right, float baseline, bool on) {
  const char* t = on ? "APPROVED" : "NOT YET";
  int w = textWidth(FONT_PXB16, t) + 20;
  roundBox(right - w, baseline - 18, w, 26, 13, on ? 0x2E7D4F : C_PANEL, 1, on ? 0x7BD88F : C_LINE, 2);
  text(FONT_PXB16, t, (int)(right - w / 2), (int)baseline, on ? 0xE6FFEE : C_MUTED);
}
static void drawTrade(uint32_t now) {
  drawBackground(fb);
  if (bagPicking && tState == T_OPEN) { drawBag(); return; }
  text(FONT_PX24, "TRADE", 14, 46, C_INK, LEFT);
  if (tState == T_SEARCH) {
    float k = fmodf(now / 1400.f, 1.f);   // radar pulse
    for (int i = 0; i < 3; i++) { float kk = fmodf(k + i / 3.f, 1.f); ring(184, 190, 20 + 90 * kk, 3, C_GOLD, 1 - kk); }
    disc(184, 190, 14, C_GOLD);
    text(FONT_PX16, "Looking for a hero nearby", SCREEN_W / 2, 318, C_INK);
    text(FONT_PX16, "Open Trade on the other board", SCREEN_W / 2, 340 - 4, C_MUTED);
    button(10, NAV_Y, 348, NAV_H, "Cancel", GHOST, actTradeCancel);
    return;
  }
  if (tState == T_DONE || (tState == T_ENDED && !tEndText)) {
    text(FONT_PX24, "TRADE DONE", SCREEN_W / 2, 110, C_GOLD);
    text(FONT_PX16, tGotCount ? "You got" : "You gave your items away", SCREEN_W / 2, 150, C_MUTED);
    for (int i = 0; i < tGotCount; i++) {
      float x = SCREEN_W / 2 - (tGotCount * 88 - 8) / 2.f + i * 88;
      tradeSlot(x, 172, tGot[i], false, -1);
    }
    hitCount = 0;   // the received items are only shown here
    button(10, NAV_Y, 348, NAV_H, "OK", PRIMARY, actTradeLeave);
    return;
  }
  if (tState == T_ENDED && tPendingEnd && game.trade.key) {   // lost each other while finishing
    text(FONT_PX24, "NOT FINISHED", SCREEN_W / 2, 150, C_GOLD);
    text(FONT_PX16, "The boards lost each other", SCREEN_W / 2, 186, C_INK);
    char line[40]; snprintf(line, sizeof line, "Open Trade next to %s", game.trade.name);
    text(FONT_PX16, line, SCREEN_W / 2, 222, C_MUTED);
    text(FONT_PX16, "to finish it. Your offered", SCREEN_W / 2, 246, C_MUTED);
    text(FONT_PX16, "items stay locked until then.", SCREEN_W / 2, 270, C_MUTED);
    button(10, NAV_Y, 348, NAV_H, "OK", GHOST, actTradeLeave);
    return;
  }
  if (tState == T_ENDED) {
    text(FONT_PX24, tEndText ? tEndText : "", SCREEN_W / 2, 200, C_DANGER);
    text(FONT_PX16, "Nothing was traded", SCREEN_W / 2, 236, C_MUTED);
    button(10, NAV_Y, 348, NAV_H, "OK", GHOST, actTradeLeave);
    return;
  }
  // the open table
  int left = (int)((tEndsAt - now) / 1000) + 1; if ((int32_t)(tEndsAt - now) < 0) left = 0;
  char t[40]; snprintf(t, sizeof t, "0:%02d", left > 59 ? 59 : left);
  text(FONT_PX24, t, 354, 46, left <= 10 ? C_DANGER : C_GOLD, RIGHT);
  text(FONT_PX16, "YOU GIVE", 14, 82, C_MUTED, LEFT);
  approvalChip(354, 82, myApprovalValid() || tState == T_COMMIT);
  for (int i = 0; i < 3; i++) tradeSlot(12 + i * 88, 94, tMyItems[i], true, i);
  snprintf(t, sizeof t, "%s GIVES", tPeerName); for (char* q = t; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  text(FONT_PX16, t, 14, 206, C_MUTED, LEFT);
  int nw = textWidth(FONT_PX16, t);
  snprintf(t, sizeof t, "LV %d", tPeerLevel);
  text(FONT_PX16, t, 14 + nw + 10, 206, C_GOLD, LEFT);
  approvalChip(354, 206, peerApprovalValid() || tState == T_COMMIT);
  for (int i = 0; i < 3; i++) tradeSlot(12 + i * 88, 218, tPeerOffer[i], false, i);
  const char* note;
  Rgb nc = C_MUTED;
  if (tState == T_COMMIT) { note = "Finishing the trade..."; nc = C_GOLD; }
  else if (tradeReady()) { note = "Trading..."; nc = C_GOLD; }
  else if (!roomForTrade()) { note = "Your bag is full"; nc = C_DANGER; }
  else if (myApprovalValid()) note = "Waiting for the other hero";
  else note = "Both approve to trade";
  text(FONT_PX16, note, SCREEN_W / 2, 330, nc);
  if (tState == T_COMMIT) { hitCount = 0; return; }   // nothing to tap while it finishes
  button(10, NAV_Y, 120, NAV_H, "Cancel", GHOST, actTradeCancel);
  if (myApprovalValid()) button(140, NAV_Y, 218, NAV_H, "Approved", GHOST, actApprove);
  else button(140, NAV_Y, 218, NAV_H, "Approve", roomForTrade() ? PRIMARY : DISABLED, actApprove);
}

// ---------------------------------------------------------------- always-on screen
// Black screen (AMOLED pixels off) with the hero's name, today's steps and the footprints. The block moves
// to one of nine spots every minute so no pixel stays lit in the same place for hours (burn-in).
static void drawAmbient(uint32_t now) {
  if (now - ambientMovedAt > 60000) { ambientMovedAt = now; ambientSpot = (ambientSpot + 4) % 9; }
  int dx = (ambientSpot % 3 - 1) * 18, dy = (ambientSpot / 3 - 1) * 18;
  fillRect(0, 0, SCREEN_W, SCREEN_H, 0x000000);
  if (hero.name[0] && screen != WELCOME && screen != STEP) {
    char up[HERO_MAX_NAME + 1]; strcpy(up, hero.name); for (char* q = up; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    text(FONT_PX24, up, SCREEN_W / 2 + dx, 170 + dy, C_MUTED);
  }
  char num[16]; fmtThousands(stepsToday, num);
  text(FONT_PXB48, num, SCREEN_W / 2 + dx, 246 + dy, C_GOLD);
  feet(SCREEN_W / 2 + dx - FEET_W * 4 / 2.f, 268 + dy, 4, C_MUTED);
}

// ---------------------------------------------------------------- settings (BOOT button)
static void closeOverlay(int) { overlay = NONE; }
static void backToSettings(int) { overlay = SETTINGS; }
static void actErase(int) {
  overlay = NONE; platformEraseHero();
  gameReset(game); bannerCount = 0; recentCount = 0; craftPhase = 0;
  memset(&hero, 0, sizeof hero); hero.body = (uint8_t)bodyIndexMale(); hero.skin = 1; hero.hair = (uint8_t)hairIndex("Spiky"); hero.prof = -1;
  go(WELCOME);
}
static void actAlwaysOn(int) { settings.alwaysOn ^= 1; platformSaveSettings(settings); }
static void actTimeout(int) { settings.timeout = (settings.timeout + 1) % 3; platformSaveSettings(settings); }
static void actBrightness(int) { settings.brightness = (settings.brightness + 1) % 3; platformSaveSettings(settings); platformApplyBrightness(BRIGHTNESS_LEVELS[settings.brightness]); }
static void actStartOver(int) { overlay = RESET; }
static void actUpsideDown(int) { settings.upsideDown ^= 1; platformSaveSettings(settings); }
static int settingsPage = 0;
static void actSettingsPage(int p) { settingsPage = p; }
static void actSettingsSwipe(int d) { settingsPage = d > 0 ? 1 : 0; }
static void actDone(int) { overlay = NONE; settingsPage = 0; }

static void settingRow(float y, const char* label, const char* sub, const char* value, int toggle, Action fn, Rgb labelColor = C_INK) {
  roundBox(10, y, 348, 62, 14, C_PANEL, 1, C_LINE, 2);
  text(FONT_PX24, label, 26, (int)(sub ? y + 30 : y + 39), labelColor, LEFT);
  if (sub) text(FONT_PX16, sub, 26, (int)(y + 51), C_MUTED, LEFT);
  if (value) text(FONT_PXB24, value, 340, (int)(y + 39), C_GOLD, RIGHT);
  if (toggle >= 0) {
    roundBox(288, y + 16, 54, 30, 15, toggle ? C_GOLD : C_LINE, 1);
    disc(toggle ? 327 : 303, y + 31, 11, toggle ? C_GOLD_INK : C_MUTED);
  }
  hit(10, y, 348, 62, fn);
}
// battery: pixel battery icon and the percentage (red under 20 %), or "USB" with no battery
static void drawBattery(float right, float baseline) {
  char t[12];
  if (batteryPct < 0) snprintf(t, sizeof t, "USB");
  else snprintf(t, sizeof t, "%d%%", batteryPct);
  Rgb col = batteryPct >= 0 && batteryPct < 20 && !batteryCharging ? C_DANGER : batteryCharging ? 0x7BD88F : C_GOLD;
  int tw = textWidth(FONT_PX24, t);
  text(FONT_PX24, t, (int)right, (int)baseline, col, RIGHT);
  float bx = right - tw - 46, by = baseline - 16;                 // 34 x 18 body + 4 px nub
  fillRect((int)bx, (int)by, 34, 2, col); fillRect((int)bx, (int)by + 16, 34, 2, col);
  fillRect((int)bx, (int)by, 2, 18, col); fillRect((int)bx + 32, (int)by, 2, 18, col);
  fillRect((int)bx + 34, (int)by + 5, 4, 8, col);
  int fill = batteryPct < 0 ? 26 : (26 * batteryPct + 50) / 100;
  if (fill > 0) fillRect((int)bx + 4, (int)by + 4, fill, 10, col);
  if (batteryCharging) {   // pixel-art lightning bolt to the left of the battery
    static const char* const BOLT[7] = { "...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..." };
    int px = 3, x0 = (int)bx - 5 * px - 8, y0 = (int)by + 9 - 7 * px / 2;
    for (int r = 0; r < 7; r++) for (int k = 0; k < 5; k++) if (BOLT[r][k] == '#') fillRect(x0 + k * px, y0 + r * px, px, px, col);
  }
}

// ---------------------------------------------------------------- touch test (Settings, page 2)
// Calibration: dots appear one at a time all over the screen (edges and corners too) and you tap each one.
// If a dot can't be hit, tap MISSED in the middle. At the end a report lists where each tap landed compared
// with its dot, to photograph and send for calibration. BOOT goes back to Settings.
static const int CAL_X[5] = { 18, 101, 184, 267, 350 }, CAL_Y[5] = { 18, 120, 224, 328, 430 };
static const int CAL_N = 24;                 // 5 x 5 grid without the middle (that is the MISSED button)
static uint8_t calOrder[CAL_N];
static int calStep = 0;
static int16_t calDx[CAL_N], calDy[CAL_N];
static bool calMiss[CAL_N];
static bool calDown = false; static int calDownX = 0, calDownY = 0;
static const char* boardName = "";
static const int MISS_X = 124, MISS_Y = 196, MISS_W = 120, MISS_H = 56;
static void calTarget(int k, int* x, int* y) { int i = calOrder[k]; if (i >= 12) i++; *x = CAL_X[i % 5]; *y = CAL_Y[i / 5]; }
static void actTouchTest(int) {
  overlay = TOUCHTEST; calStep = 0; calDown = false;
  for (int i = 0; i < CAL_N; i++) calOrder[i] = (uint8_t)((i * 7 + 3) % CAL_N);   // a fixed jumbled order
}
static void drawTouchTest() {
  fillRect(0, 0, SCREEN_W, SCREEN_H, 0x000000);
  char t[48];
  if (calStep < CAL_N) {
    roundBox(MISS_X, MISS_Y, MISS_W, MISS_H, 12, C_PANEL2, 1, C_LINE, 2);
    text(FONT_PXB16, "MISSED", SCREEN_W / 2, MISS_Y + 34, C_INK);
    snprintf(t, sizeof t, "TAP THE DOT  %d/%d", calStep + 1, CAL_N);
    text(FONT_PX16, t, SCREEN_W / 2, 176, C_MUTED);
    text(FONT_PX16, "can't hit it? tap missed", SCREEN_W / 2, 284, C_DIM);
    int x, y; calTarget(calStep, &x, &y);
    ring(x, y, 13, 2, C_GOLD); disc(x, y, 4, C_GOLD);
    if (calDown) { thickLine(calDownX - 10, calDownY, calDownX + 10, calDownY, 2, C_MUTED); thickLine(calDownX, calDownY - 10, calDownX, calDownY + 10, 2, C_MUTED); }
    return;
  }
  // report: where each tap landed against its dot (+x right, +y down), in pixels
  int n = 0, miss = 0; long sx = 0, sy = 0;
  for (int i = 0; i < CAL_N; i++) { if (calMiss[i]) miss++; else { n++; sx += calDx[i]; sy += calDy[i]; } }
  snprintf(t, sizeof t, "TOUCH REPORT %s", boardName);
  text(FONT_PXB16, t, SCREEN_W / 2, 22, C_GOLD);
  snprintf(t, sizeof t, "avg %+ld %+ld  missed %d", n ? sx / n : 0, n ? sy / n : 0, miss);
  text(FONT_PX16, t, SCREEN_W / 2, 44, C_INK);
  for (int k = 0; k < CAL_N; k++) {   // sorted by position: row by row
    int i = k; int gi = i >= 12 ? i + 1 : i; int x = CAL_X[gi % 5], y = CAL_Y[gi / 5];
    int step = 0; for (int s = 0; s < CAL_N; s++) if (calOrder[s] == i) step = s;
    if (calMiss[step]) snprintf(t, sizeof t, "%3d,%3d  MISS", x, y);
    else snprintf(t, sizeof t, "%3d,%3d %+3d%+3d", x, y, calDx[step], calDy[step]);
    int col = k / 12, row = k % 12;
    text(FONT_PX16, t, 8 + col * 182, 74 + row * 28, calMiss[step] ? C_DANGER : C_MUTED, LEFT);
  }
  text(FONT_PX16, "photo this. BOOT goes back", SCREEN_W / 2, 438, C_DIM);
}
static void calRecord(int x, int y) {
  bool onMiss = x >= MISS_X && x < MISS_X + MISS_W && y >= MISS_Y && y < MISS_Y + MISS_H;
  int tx, ty; calTarget(calStep, &tx, &ty);
  calMiss[calStep] = onMiss;
  calDx[calStep] = (int16_t)(x - tx); calDy[calStep] = (int16_t)(y - ty);
  calStep++;
}
void appSetBoardName(const char* name) { boardName = name; }
bool appTouchRaw(int x, int y, bool down) {
  if (overlay != TOUCHTEST || calStep >= CAL_N) return false;
  if (down && !calDown) { calDown = true; calDownX = x; calDownY = y; return true; }   // where the finger went down
  if (!down && calDown) { calDown = false; calRecord(calDownX, calDownY); return true; }
  return false;
}

// ---------------------------------------------------------------- software update (Settings, page 2)
// The sketch connects to a saved WiFi network, asks GitHub for the latest release and downloads it.
// A network is added from the phone: the board opens its own WiFi with a small page to pick one.
static int updState = U_IDLE, updProg = -1;
static char updL1[40], updL2[64];
static bool updBusy() { return updState == U_BUSY || updState == U_DOWNLOAD || updState == U_DONE; }
static void actUpdate(int) {
  if (tState == T_SEARCH || tState == T_OPEN) tradeEnd("Update started");
  overlay = UPDATE;
  appUpdateStatus(U_BUSY, "Checking...", "", -1);
  platformUpdate(UA_CHECK);
}
static void actUpdCheck(int) { appUpdateStatus(U_BUSY, "Checking...", "", -1); platformUpdate(UA_CHECK); }
static void actUpdInstall(int) { appUpdateStatus(U_DOWNLOAD, "Downloading", "Keep the board close to WiFi", 0); platformUpdate(UA_INSTALL); }
static void actUpdSetup(int) { appUpdateStatus(U_BUSY, "Starting WiFi...", "", -1); platformUpdate(UA_SETUP); }
static void actUpdStop(int) { platformUpdate(UA_STOP); appUpdateStatus(U_IDLE, "Ready", "Tap Check to look for updates", -1); }
static void actUpdBack(int) { platformUpdate(UA_STOP); overlay = SETTINGS; settingsPage = 1; }
static void drawUpdate() {
  drawBackground(fb);
  text(FONT_PX24, "UPDATE", 14, 46, C_INK, LEFT);
  char v[24]; snprintf(v, sizeof v, "Version %d", FW_VERSION);
  text(FONT_PX16, v, 354, 44, C_MUTED, RIGHT);
  const int cx = SCREEN_W / 2;
  roundBox(10, 72, 348, 200, 16, C_PANEL, 1, C_LINE, 2);
  if (updState == U_SETUP) {
    text(FONT_PX16, "On your phone, join WiFi", cx, 110, C_MUTED);
    text(FONT_PXB24, updL1, cx, 148, C_GOLD);
    text(FONT_PX16, "A page opens. If not, go to", cx, 190, C_MUTED);
    text(FONT_PXB24, "pocket.local", cx, 226, C_INK);
    text(FONT_PX16, "or 192.168.4.1", cx, 254, C_DIM);
    button(10, NAV_Y, 348, NAV_H, "Cancel", GHOST, actUpdStop);
    return;
  }
  Rgb c = updState == U_AVAILABLE || updState == U_DONE ? C_GOLD : updState == U_ERROR || updState == U_NO_WIFI ? C_DANGER : C_INK;
  bool bar = updProg >= 0;
  int y1 = bar ? 140 : 162;
  text(textWidth(FONT_PXB24, updL1) > 320 ? FONT_PXB16 : FONT_PXB24, updL1, cx, y1, c);
  text(FONT_PX16, updL2, cx, y1 + 34, C_MUTED);
  if (bar) {
    int p = updProg > 100 ? 100 : updProg;
    roundBox(40, 206, 288, 22, 11, C_PANEL2, 1, C_LINE, 2);
    if (p > 0) roundBox(40, 206, 22 + 266 * p / 100, 22, 11, C_GOLD, 1);
    char t[8]; snprintf(t, sizeof t, "%d%%", p);
    text(FONT_PXB16, t, cx, 254, C_INK);
  }
  if (updBusy()) return;   // nothing to tap while it works
  settingRow(282, "WiFi", updState == U_NO_WIFI ? "Add a network first" : "Add a network", nullptr, -1, actUpdSetup);
  button(10, NAV_Y, 120, NAV_H, "Back", GHOST, actUpdBack);
  if (updState == U_AVAILABLE) button(140, NAV_Y, 218, NAV_H, "Install", PRIMARY, actUpdInstall);
  else button(140, NAV_Y, 218, NAV_H, "Check", PRIMARY, actUpdCheck);
}
void appUpdateStatus(int state, const char* line1, const char* line2, int progress) {
  updState = state; updProg = progress;
  snprintf(updL1, sizeof updL1, "%s", line1 ? line1 : "");
  snprintf(updL2, sizeof updL2, "%s", line2 ? line2 : "");
}
bool appUpdateOpen() { return overlay == UPDATE; }

// ---------------------------------------------------------------- battery log (Settings, page 1)
// The level over the last hours, coloured by what the screen was doing, and how fast it drops in each mode.
static void actBattery(int) { overlay = BATTERY; }
static void actBatteryBack(int) { overlay = SETTINGS; settingsPage = 0; }
static const Rgb BM_COL[5] = { C_GOLD, 0xB4ABD2, 0x5A5378, 0x7BD88F, C_DANGER };
static void drawBatteryLog() {
  drawBackground(fb);
  text(FONT_PX24, "BATTERY", 14, 46, C_INK, LEFT);
  drawBattery(354, 46);
  static BatSample log[160];
  int n = platformBatteryLog(log, 160);
  // chart: the last 16 hours (96 samples), one column per sample
  const int cx0 = 14, cy0 = 66, cw = 340, ch = 120, N = 96;
  roundBox(cx0 - 4, cy0 - 6, cw + 8, ch + 12, 10, C_PANEL, 1, C_LINE, 2);
  int first = n > N ? n - N : 0;
  float colW = (float)cw / N;
  for (int i = first; i < n; i++) {
    const BatSample& b = log[i];
    float x = cx0 + (i - first) * colW, h = ch * (b.pct10 > 1000 ? 1000 : b.pct10) / 1000.f;
    if (b.mode == BM_RESTART) { fillRect((int)x, cy0, 1, ch, C_DANGER, 0.5f); continue; }
    fillRect((int)x, (int)(cy0 + ch - h), (int)(colW + 0.99f), (int)h, BM_COL[b.mode < 5 ? b.mode : 0], 0.9f);
  }
  if (n < 2) text(FONT_PX16, "Measuring... (every 10 min)", SCREEN_W / 2, cy0 + ch / 2 + 6, C_MUTED);
  text(FONT_PX16, "16 h ago", cx0, cy0 + ch + 22, C_DIM, LEFT);
  text(FONT_PX16, "now", cx0 + cw, cy0 + ch + 22, C_DIM, RIGHT);
  // drop per hour in each mode, from every 10-minute step spent in that mode
  static const char* const NAMES[3] = { "Screen on", "Always-on", "Screen off" };
  float drop[3] = { 0, 0, 0 }; int steps[3] = { 0, 0, 0 }; long sleep[3] = { 0, 0, 0 };
  for (int i = 1; i < n; i++) {
    int m = log[i].mode;
    if (m > BM_OFF || log[i - 1].mode == BM_CHARGING || log[i - 1].mode == BM_RESTART) continue;
    drop[m] += (log[i - 1].pct10 - (float)log[i].pct10) / 10.f; steps[m]++; sleep[m] += log[i].sleepPct;
  }
  char t[48];
  for (int m = 0; m < 3; m++) {
    float y = 228 + m * 40;
    disc(24, y - 6, 6, BM_COL[m]);
    text(FONT_PX16, NAMES[m], 38, (int)y, C_INK, LEFT);
    if (steps[m] < 2) { text(FONT_PX16, "no data yet", 354, (int)y, C_DIM, RIGHT); continue; }
    float perH = drop[m] / (steps[m] * BAT_LOG_MINUTES / 60.f);
    if (perH < 0) perH = 0;
    if (perH > 0.05f) snprintf(t, sizeof t, "-%.1f%%/h (%dh)", perH, (int)(100 / perH));
    else snprintf(t, sizeof t, "-0%%/h");
    text(FONT_PXB16, t, 354, (int)y, C_GOLD, RIGHT);
    snprintf(t, sizeof t, "asleep %ld%% of the time", sleep[m] / steps[m]);
    text(FONT_PX16, t, 38, (int)y + 18, C_MUTED, LEFT);
  }
  button(10, NAV_Y, 348, NAV_H, "Back", GHOST, actBatteryBack);
}

// Two pages: screen options first; orientation and Start over on the second
static void drawSettings() {
  drawBackground(fb);
  text(FONT_PX24, "SETTINGS", 14, 46, C_INK, LEFT);
  swipeFn = actSettingsSwipe;
  drawBattery(354, 46);
  if (settingsPage == 0) {
    settingRow(72, "Always-on", "Steps when idle", nullptr, settings.alwaysOn, actAlwaysOn);
    char t[8]; snprintf(t, sizeof t, "%u s", TIMEOUT_SECONDS[settings.timeout]);
    settingRow(142, "Timeout", "Before it dims", t, -1, actTimeout);
    settingRow(212, "Brightness", nullptr, BRIGHTNESS_NAMES[settings.brightness], -1, actBrightness);
    settingRow(282, "Battery", "Use per hour", nullptr, -1, actBattery);
    button(10, NAV_Y, 120, NAV_H, "More", GHOST, actSettingsPage, 1);
  } else {
    settingRow(72, "Upside down", "Turn the screen", nullptr, settings.upsideDown, actUpsideDown);
    settingRow(142, "Touch test", "Tap dots to calibrate", nullptr, -1, actTouchTest);
    char v[16]; snprintf(v, sizeof v, "v%d", FW_VERSION);
    settingRow(212, "Update", "Get the newest version", v, -1, actUpdate);
    settingRow(282, "Start over", "Delete this hero", nullptr, -1, actStartOver, C_DANGER);
    button(10, NAV_Y, 120, NAV_H, "Back", GHOST, actSettingsPage, 0);
  }
  // page dots above the buttons
  for (int i = 0; i < 2; i++) disc(SCREEN_W / 2 - 7 + i * 14, NAV_Y - 16, i == settingsPage ? 5 : 3.5f, i == settingsPage ? C_GOLD : C_DIM);
  button(140, NAV_Y, 218, NAV_H, "Done", PRIMARY, actDone);
}
static void drawReset() {
  fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG, 0.78f);
  roundBox(24, 100, 320, 236, 16, C_PANEL, 1, C_LINE, 2);
  text(FONT_PX24, "START OVER?", SCREEN_W / 2, 154, C_GOLD);
  char line[48];
  if (hero.name[0] && (screen == READY || screen == HOME)) snprintf(line, sizeof line, "This deletes %s for good.", hero.name);
  else snprintf(line, sizeof line, "This clears the hero setup.");
  text(FONT_PX16, line, SCREEN_W / 2, 196, C_INK);
  text(FONT_PX16, "Gear and progress go too.", SCREEN_W / 2, 222, C_MUTED);
  button(36, 246, 140, 66, "Keep", GHOST, backToSettings);
  roundBox(192, 246, 140, 66, 14, C_DANGER, 1);
  text(FONT_PXB24, "Delete", 262, 286, 0x2A0E0C);
  hit(192, 246, 140, 66, actErase);
}

// ---------------------------------------------------------------- public
void appSplash(uint16_t* f) {
  gfxTarget(f);
  fillRect(0, 0, SCREEN_W, SCREEN_H, 0x000000);
  uiDraw(UI_SPLASH, uiX(UI_SPLASH), uiY(UI_SPLASH));
  uiFree(UI_SPLASH);
  char v[16]; snprintf(v, sizeof v, "VERSION %d", FW_VERSION);
  text(FONT_PX16, v, SCREEN_W / 2, 426, C_DIM);
}
void appBegin(uint16_t* f, float* w, const Hero* saved, const Settings& s, const Game* savedGame) {
  fb = f; work = w; gfxTarget(fb);
  settings = s;
  if (settings.timeout > 2) settings.timeout = 1;
  if (settings.brightness > 2) settings.brightness = 2;
  if (settings.upsideDown > 1) settings.upsideDown = 0;
  memset(&hero, 0, sizeof hero);
  hero.body = (uint8_t)bodyIndexMale(); hero.skin = 1; hero.hair = (uint8_t)hairIndex("Spiky"); hero.prof = -1;
  if (saved) {
    hero = *saved; hero.name[HERO_MAX_NAME] = 0; screen = HOME;
    if (hero.prof < 0 || hero.prof >= PROFESSION_COUNT) hero.prof = 0;   // heroes saved as Jeweler become Blacksmiths
  } else screen = WELCOME;
  if (saved && savedGame && gameValid(*savedGame)) game = *savedGame; else gameReset(game);
  shownAtPending = true;
}

bool appSwipe(int dir) {
  if (ambient || !swipeFn || overlay == ITEM || overlay == RESET || overlay == TOUCHTEST || overlay == UPDATE || overlay == BATTERY) return false;
  swipeFn(dir > 0 ? 1 : -1);
  return true;
}

bool appTap(int x, int y) {
  if (overlay == TOUCHTEST) return false;   // the test only shows where the finger is
  if (ambient) return false;
  for (int i = hitCount - 1; i >= 0; i--) {
    const Hit& h = hits[i];
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) { h.fn(h.arg); return true; }
  }
  return false;
}

bool appTick(uint32_t now) {
  lastNow = now;
  if (screen == TRADE || tState != T_OFF) { bool r = tradeTick(now); if (screen == TRADE && !ambient && overlay == NONE && (r || tState == T_SEARCH)) return true; }
  if (ambient || overlay != NONE) return false;
  if (screen == CRAFT && amountNoteAt && now - amountNoteAt >= 1800) { amountNoteAt = 0; return true; }   // the note goes away
  if (screen == CRAFT && craftPhase == 1) {
    if (now - craftAt >= CRAFT_MS) craftPhase = 2;
    return true;
  }
  if (screen == HOME && bannerCount && bannerAt && now - bannerAt > 2800) {
    memmove(banners, banners + 1, sizeof(Banner) * (bannerCount - 1)); bannerCount--; bannerAt = 0;
    return true;
  }
  return false;
}

void appDraw(uint32_t now) {
  lastNow = now;
  if (shownAtPending) { shownAt = now; shownAtPending = false; }
  hitCount = 0; swipeFn = nullptr;
  if (lowBattery) {   // shown for a few seconds before the board switches itself off
    fillRect(0, 0, SCREEN_W, SCREEN_H, 0x000000);
    text(FONT_PXB24, "BATTERY LOW", SCREEN_W / 2, 200, C_DANGER);
    text(FONT_PX16, "Charge me to keep playing", SCREEN_W / 2, 240, C_MUTED);
    text(FONT_PX16, "Your hero is saved", SCREEN_W / 2, 270, C_MUTED);
    return;
  }
  if (ambient) { drawAmbient(now); return; }
  if (overlay == SETTINGS) { drawSettings(); return; }
  if (overlay == TOUCHTEST) { drawTouchTest(); return; }
  if (overlay == UPDATE) { drawUpdate(); return; }
  if (overlay == BATTERY) { drawBatteryLog(); return; }
  switch (screen) {
    case WELCOME: drawWelcome(); break;
    case STEP: drawStep(now); break;
    case READY: drawReady(now); break;
    case HOME: drawHome(); break;
    case CRAFT: drawCraft(now); break;
    case EXPLORE: drawExplore(); break;
    case BAG: drawBag(); break;
    case GEAR: drawGear(); break;
    case TRADE: drawTrade(now); break;
  }
  if (overlay == ITEM) { hitCount = 0; drawItemCard(); }
  if (overlay == RESET) { hitCount = 0; drawReset(); }
}

void appSettingsButton() {
  if (overlay == TOUCHTEST) { overlay = SETTINGS; settingsPage = 1; return; }   // back to the page it came from
  if (overlay == UPDATE) { if (!updBusy()) actUpdBack(0); return; }
  if (overlay == BATTERY) { actBatteryBack(0); return; }
  overlay = overlay == SETTINGS || overlay == RESET ? NONE : SETTINGS; settingsPage = 0;
}
const Settings& appSettings() { return settings; }
void appSetSteps(uint32_t today, uint32_t day) {
  stepsToday = today; curDay = day;
  if (!heroExists()) return;
  uint32_t dayBefore = game.day;
  FindEvent ev[64]; int lv = 0;
  int n = gameWalk(game, today, day, ev, 64, &lv);
  int keep = n < 64 ? n : 64;
  for (int i = 0; i < keep; i++) {
    if (recentCount == 3) { memmove(recent, recent + 1, sizeof(FindEvent) * 2); recentCount = 2; }
    recent[recentCount++] = ev[i];
    if (ev[i].kind == F_ITEM) { char nm[28]; itemName(ev[i].item, nm, sizeof nm); pushBanner("FOUND AN ITEM", nm, rarityRgb(ev[i].item.tier), &ev[i].item); }
  }
  levelBanner(lv);
  if (n || game.day != dayBefore) platformSaveGame(game);
}
void appShowLowBattery() { lowBattery = true; }
void appSetBattery(int percent, bool charging, bool usb) { batteryPct = percent; batteryCharging = charging; usbIn = usb; }
void appSetClock(bool valid, int hour, int minute) { clockValid = valid; clockHour = hour; clockMinute = minute; }
void appAmbient(bool on) {
  ambient = on; if (on) ambientMovedAt = lastNow;
  if (on && overlay == UPDATE && !updBusy()) appUpdateStatus(U_IDLE, "Ready", "Tap Check to look for updates", -1);
  if (on && (tState == T_SEARCH || tState == T_OPEN)) tradeEnd("The screen went off");
}
void appScreenOff() {
  if (overlay == UPDATE && !updBusy()) appUpdateStatus(U_IDLE, "Ready", "Tap Check to look for updates", -1);   // WiFi went off
  if (tState == T_SEARCH || tState == T_OPEN) tradeEnd("The screen went off"); }
bool appKeepAwake() { return overlay == UPDATE || (screen == TRADE && (tState == T_SEARCH || tState == T_OPEN || tState == T_COMMIT || tState == T_DONE)); }
bool appRadioReceive(const uint8_t* mac, const uint8_t* data, int len) {
  bool before = tDirty; tradeReceive(mac, data, len);
  bool changed = tDirty && !before;
  return changed && screen == TRADE;
}
bool appIsAmbient() { return ambient; }
