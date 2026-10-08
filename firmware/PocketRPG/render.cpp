// Pocket RPG character renderer. Mirrors the Character Creator page:
//  - skin: OKLab shift from the body's base tone to the chosen tone ("Light" = art as drawn)
//  - hair, eyes, rarity trim: OKLab gradient map around the layer's middle gray
//  - armor and weapon materials: keep the painted brightness, add the material's hue
#include "render.h"
#include "art_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "inflate.h"
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

// ---------------------------------------------------------------- color tables (same as the page)
struct Named { const char* name; uint32_t rgb; };
static const uint32_t SKIN_TARGET[6] = { 0xFFDCC4, 0, 0xE2A774, 0xC2875A, 0x93603C, 0x5E3A24 };  // 0 = art as drawn
const char* const SKIN_NAMES[6] = { "Fair", "Light", "Medium", "Tan", "Brown", "Deep" };
static const uint32_t EYE_RGB[9] = { 0x3B7DD8, 0x3F9A3A, 0x6B3F20, 0x34343C, 0x9AA0AA, 0x8A3FD0, 0xD8A020, 0xE8701E, 0xC8242C };
const char* const EYE_NAMES[9] = { "Blue", "Green", "Brown", "Dark gray", "Light gray", "Purple", "Golden", "Orange", "Red" };
static const uint32_t HAIR_RGB[9] = { 0x5C4433, 0xE8C15A, 0xC2512B, 0x2A2A34, 0xD6D6E2, 0x6B4FD0, 0x2AA198, 0xE46AA0, 0xB81E28 };
const char* const HAIR_COLOR_NAMES[9] = { "Brown", "Blonde", "Ginger", "Black", "Silver", "Violet", "Teal", "Pink", "Red" };
const char* const ELEMENT_NAMES[4] = { "Neutral", "Fire", "Nature", "Water" };
const char* const RARITY_NAMES[20] = { "Common", "Uncommon", "Fine", "Refined", "Rare", "Superior", "Epic", "Heroic", "Mystic", "Legendary",
  "Mythic", "Ancient", "Celestial", "Astral", "Divine", "Eternal", "Primordial", "Cosmic", "Infinite", "Absolute" };
static const uint32_t RARITY_RGB[20] = { 0x9AA0A6, 0xE8EAED, 0x4CAF50, 0x2E7D32, 0x1E88E5, 0x64B5F6, 0x8E24AA, 0xD81B60, 0xF48FB1, 0xFB8C00,
  0xE53935, 0xA1683A, 0x26C6DA, 0x3949AB, 0xFBC02D, 0xFFD54F, 0x00A86B, 0x5E35B1, 0xFF4080, 0xE0D0FF };
static const char* const ARMOR_MAT_NAME[4] = { "Leather", "Chainmail", "Iron", "Titanium" };
static const char* const WEAPON_MAT_NAME[4] = { "Wood", "Bronze", "Iron", "Titanium" };
static const char* const CAPE_NAME[4] = { "Traveler", "Ranger", "Knight", "Royal" };

// Armor color per tier: asDrawn = keep painted brightness (+lift) and add the hue; otherwise gradient map.
struct TierColor { const char* name; uint32_t rgb; bool asDrawn; float lift; };
static const TierColor ARMOR_TIERS[20] = {
  { "Brown", 0x6E4428, false, 0 }, { "Reddish brown", 0x7A3524, false, 0 }, { "Yellowish brown", 0x9C6A2A, false, 0 },
  { "Beige", 0xC2A880, false, 0 }, { "Ice white", 0xC4D2DC, false, 0 },
  { "Gray", 0x8C96A0, true, 0 }, { "Copper", 0x9C532E, false, 0 }, { "Silver", 0xC2C8D0, true, 24 }, { "Gold", 0xB48428, false, 0 },
  { "Grayish emerald", 0x5E8A78, false, 0 },
  { "Gray", 0x5F6B78, true, 0 }, { "Copper", 0xB8662A, true, 4 }, { "Silver", 0xC2C8D0, true, 24 }, { "Gold", 0xE8B020, true, 10 },
  { "Grayish emerald", 0x3E8A70, true, 4 },
  { "Bronze", 0xB8662A, true, -20 }, { "Silver", 0xC2C8D0, true, 4 }, { "Gold", 0xE8B020, true, 0 }, { "Aqua", 0x38C4C4, true, -2 },
  { "Abyss teal", 0x0C4848, true, -74 },
};
static const int GLOW_FROM = 19;
static const uint8_t SOCKET_RGB[3] = { 26, 20, 32 };

int bodyCount() { return BODIES_COUNT; }
int hairCount() { return HAIRS_COUNT; }
const char* bodyName(int i) { return BODIES[i % BODIES_COUNT].name; }
const char* hairName(int i) { return HAIRS[i % HAIRS_COUNT].name; }
bool hairIsFemale(int i) { return HAIRS_FEMALE[i % HAIRS_COUNT] != 0; }
static inline int band(int tier) { return (tier - 1) / 5; }
uint32_t rarityRgb(int t) { return RARITY_RGB[(t < 1 ? 1 : t > 20 ? 20 : t) - 1]; }
const char* armorColorName(int t) { return ARMOR_TIERS[t - 1].name; }
const char* armorMaterialName(int t) { return ARMOR_MAT_NAME[band(t)]; }
const char* weaponMaterialName(int t) { return WEAPON_MAT_NAME[band(t)]; }
const char* capeName(int t) { return CAPE_NAME[band(t)]; }

// ---------------------------------------------------------------- color math
static float toLin[256];
static uint8_t fromLin[4096];
static bool tablesReady = false;
static void initTables() {
  if (tablesReady) return;
  for (int i = 0; i < 256; i++) { float c = i / 255.f; toLin[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
  for (int i = 0; i < 4096; i++) { float c = i / 4095.f; float v = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f; int q = (int)lroundf(255 * v); fromLin[i] = q < 0 ? 0 : q > 255 ? 255 : q; }
  tablesReady = true;
}
static inline void unpack(uint32_t c, int* o) { o[0] = (c >> 16) & 255; o[1] = (c >> 8) & 255; o[2] = c & 255; }
static void toOk(const int* c, float* o) {
  float r = toLin[c[0]], g = toLin[c[1]], b = toLin[c[2]];
  float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  o[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
  o[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
  o[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}
static void fromOk(float L, float a, float b, int* o) {
  float l = L + 0.3963377774f * a + 0.2158037573f * b, m = L - 0.1055613458f * a - 0.0638541728f * b, s = L - 0.0894841775f * a - 1.2914855480f * b;
  l = l * l * l; m = m * m * m; s = s * s * s;
  float lin[3] = { 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s, -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
                   -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s };
  for (int i = 0; i < 3; i++) { float v = lin[i] < 0 ? 0 : lin[i] > 1 ? 1 : lin[i]; o[i] = fromLin[(int)lroundf(v * 4095)]; }
}
static inline float softL(float t, float d) { float room = d >= 0 ? 1 - t : t; return room <= 0 ? t : t + room * tanhf(d / room); }

static void rgbToHsl(const int* c, float* h, float* s, float* l) {
  float r = c[0] / 255.f, g = c[1] / 255.f, b = c[2] / 255.f, mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
  *h = 0; *s = 0; *l = (mx + mn) / 2;
  if (mx != mn) { float d = mx - mn; *s = *l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    *h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4; *h *= 60; }
  *s *= 100; *l *= 100;
}
static void hslToRgb(float h, float s, float l, int* o) {
  h = fmodf(fmodf(h, 360) + 360, 360); s = fmaxf(0, fminf(100, s)) / 100; l = fmaxf(0, fminf(100, l)) / 100;
  float a = s * fminf(l, 1 - l);
  const int ns[3] = { 0, 8, 4 };
  for (int i = 0; i < 3; i++) { float k = fmodf(ns[i] + h / 30, 12); float f = l - a * fmaxf(-1, fminf(k - 3, fminf(9 - k, 1))); o[i] = (int)lroundf(f * 255); }
}

// gradient map around the layer's middle brightness
static void gradLUT(uint32_t base, int mid, uint8_t lut[256][3]) {
  int bc[3]; unpack(base, bc); float t[3]; toOk(bc, t);
  float Lm = cbrtf(toLin[mid < 1 ? 1 : mid]);
  float up = powf(fminf(1, t[0] / fmaxf(1e-3f, Lm)), 0.75f), down = powf(fminf(1, (1 - t[0]) / fmaxf(1e-3f, 1 - Lm)), 0.75f);
  for (int v = 0; v < 256; v++) {
    float d0 = cbrtf(toLin[v]) - Lm, L = softL(t[0], d0 > 0 ? d0 * up : d0 * down);
    float room = L >= t[0] ? 1 - t[0] : t[0], fade = room > 0 ? 1 - 0.65f * powf(fabsf(L - t[0]) / room, 1.5f) : 1;
    int o[3]; fromOk(L, t[1] * fade, t[2] * fade, o); lut[v][0] = o[0]; lut[v][1] = o[1]; lut[v][2] = o[2];
  }
}
// tint: keep painted brightness, add hue/saturation
static void tintLUT(uint32_t base, float lift, uint8_t lut[256][3]) {
  int bc[3]; unpack(base, bc); float h, sat, ll; rgbToHsl(bc, &h, &sat, &ll);
  for (int v = 0; v < 256; v++) {
    float l0 = v / 255.f * 100, l = lift >= 0 ? l0 + lift * (1 - l0 / 100) : l0 + lift * (l0 / 100);
    l = fmaxf(0, fminf(100, l));
    float edge = fabsf(l - 50) / 50, ss = sat * (1 - 0.55f * edge * edge), hh = h + (l < 50 ? -6 : 5) * edge;
    int c[3]; hslToRgb(hh, ss, l, c);
    float y = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2], target = l / 100 * 255;
    if (y > 0.5f) { float f = target / y; for (int i = 0; i < 3; i++) { float q = c[i] * f; c[i] = (int)(q < 0 ? 0 : q > 255 ? 255 : q); } }
    lut[v][0] = c[0]; lut[v][1] = c[1]; lut[v][2] = c[2];
  }
}

struct SkinShift { bool on; float L, bL, da, db; };
static SkinShift makeSkin(uint32_t base, uint32_t target) {
  SkinShift s = { false, 0, 0, 0, 0 };
  if (!target) return s;
  int bc[3], tc[3]; unpack(base, bc); unpack(target, tc); float b[3], t[3]; toOk(bc, b); toOk(tc, t);
  s.on = true; s.L = t[0]; s.bL = b[0]; s.da = t[1] - b[1]; s.db = t[2] - b[2]; return s;
}
static void mapSkin(uint32_t f, const SkinShift& s, int* o) {
  unpack(f, o); if (!s.on) return;
  float k[3]; toOk(o, k); fromOk(softL(s.L, k[0] - s.bL), k[1] + s.da, k[2] + s.db, o);
}

// ---------------------------------------------------------------- layer drawing
struct Paint { SkinShift skin; uint8_t a[256][3]; uint8_t b[256][3]; };
static Paint paint;   // one at a time (large tables)

static float *R, *G, *B, *A;
static inline void blend(int o, int r, int g, int b, float al) {
  float inv = 1 - al; R[o] = r * al + R[o] * inv; G[o] = g * al + G[o] * inv; B[o] = b * al + B[o] * inv; A[o] = al + A[o] * inv;
}

// Layers are stored packed in flash. Unpacked copies are kept in RAM (PSRAM on the board) while they fit the budget.
struct Unpacked { const LayerDef* L; uint8_t* data; };
static Unpacked unpacked[32];
static int unpackedCount = 0;
static uint32_t unpackedBytes = 0;
static const uint32_t UNPACK_BUDGET = 1200 * 1024;
static uint8_t* bigAlloc(uint32_t n) {
#ifdef ARDUINO
  return (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
#else
  return (uint8_t*)malloc(n);
#endif
}
static const uint8_t* layerBytes(const LayerDef& L) {
  for (int i = 0; i < unpackedCount; i++) if (unpacked[i].L == &L) return unpacked[i].data;
  if (unpackedBytes + L.rawLen > UNPACK_BUDGET || unpackedCount == 32) {   // full: start over
    for (int i = 0; i < unpackedCount; i++) free(unpacked[i].data);
    unpackedCount = 0; unpackedBytes = 0;
  }
  uint8_t* d = bigAlloc(L.rawLen);
  if (!d) return nullptr;
  if (inflateRaw(L.rle, L.rleLen, d, L.rawLen) != L.rawLen) { free(d); return nullptr; }
  unpacked[unpackedCount++] = { &L, d }; unpackedBytes += L.rawLen;
  return d;
}

static void drawLayer(const LayerDef& L, int handCut, bool hideHand) {
  int k = 0;
  const uint8_t* p = layerBytes(L); if (!p) return;
  const uint8_t* end = p + L.rawLen;
  while (p < end) {
    int count = p[0], info = p[1]; uint32_t v = ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4]; p += 5;
    int grp = info >> 4; float al = (info & 15) / 15.f;
    if (grp == 0 || al <= 0) { k += count; continue; }
    int c[3];
    if (grp == 1) unpack(v, c);
    else if (grp == 2) mapSkin(v, paint.skin, c);
    else if (grp == 5) { c[0] = SOCKET_RGB[0]; c[1] = SOCKET_RGB[1]; c[2] = SOCKET_RGB[2]; }
    else { const uint8_t* q = grp == 3 ? paint.a[v & 255] : paint.b[v & 255]; c[0] = q[0]; c[1] = q[1]; c[2] = q[2]; }
    for (int i = 0; i < count; i++, k++) {
      if (hideHand && L.handRows) { int y = k / SCREEN_W, x = k % SCREEN_W; if (y >= handCut && L.handRows[y * 2] >= 0 && x >= L.handRows[y * 2] && x <= L.handRows[y * 2 + 1]) continue; }
      blend(k, c[0], c[1], c[2], al);
    }
  }
}

static void drawGem(const LayerDef& L, int element) {
  if (!L.sockD) return;
  // the gem image (GEM_SIZE square) is scaled to this weapon's socket, sampling the source area of each pixel
  const uint8_t* g = GEMS[element & 3];
  const int D = L.sockD;
  int gx = (int)lroundf(L.sockCx + 0.5f - D / 2.f), gy = (int)lroundf(L.sockCy + 0.5f - D / 2.f);   // pixel centers sit at +0.5
  const float s = (float)GEM_SIZE / D;
  for (int j = 0; j < D; j++) for (int i = 0; i < D; i++) {
    int x = gx + i, y = gy + j; if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) continue;
    int u0 = (int)(i * s), u1 = (int)((i + 1) * s), v0 = (int)(j * s), v1 = (int)((j + 1) * s);
    if (u1 <= u0) u1 = u0 + 1; if (v1 <= v0) v1 = v0 + 1;
    float r = 0, gg = 0, b = 0, a = 0; int n = 0;
    for (int v = v0; v < v1 && v < GEM_SIZE; v++) for (int u = u0; u < u1 && u < GEM_SIZE; u++) {
      const uint8_t* q = g + (v * GEM_SIZE + u) * 4; float qa = q[3] / 255.f;
      r += q[0] * qa; gg += q[1] * qa; b += q[2] * qa; a += qa; n++;
    }
    if (a <= 0.01f || !n) continue;
    blend(y * SCREEN_W + x, r / a, gg / a, b / a, a / n);
  }
}

// glow of one item (tiers 19-20): blurred silhouette in the rarity color, under the item
static void drawGlow(const LayerDef& L, uint32_t color, float* tmp) {
  const int N = SCREEN_W * SCREEN_H;
  memset(tmp, 0, sizeof(float) * N);
  int k = 0; const uint8_t* p = layerBytes(L); if (!p) return;
  const uint8_t* end = p + L.rawLen;
  while (p < end) { int count = p[0], info = p[1]; p += 5; float al = (info >> 4) ? (info & 15) / 15.f : 0;
    for (int i = 0; i < count; i++, k++) tmp[k] = al > 0.4f ? 1.f : 0.f; }
  float* line = tmp + N;  // scratch line: needs SCREEN_H floats after the image (SCREEN_H >= SCREEN_W)
  for (int pass = 0; pass < 3; pass++) {   // 3 box blurs of radius 4 ~ a soft gaussian
    for (int y = 0; y < SCREEN_H; y++) {
      float* r = tmp + y * SCREEN_W; float acc = 0;
      for (int x = -4; x <= 4; x++) acc += r[x < 0 ? 0 : x];
      for (int x = 0; x < SCREEN_W; x++) { line[x] = acc / 9; int a = x + 5, b = x - 4; acc += r[a >= SCREEN_W ? SCREEN_W - 1 : a] - r[b < 0 ? 0 : b]; }
      memcpy(r, line, sizeof(float) * SCREEN_W);
    }
    for (int x = 0; x < SCREEN_W; x++) {
      float acc = 0;
      for (int y = -4; y <= 4; y++) acc += tmp[(y < 0 ? 0 : y) * SCREEN_W + x];
      for (int y = 0; y < SCREEN_H; y++) { line[y] = acc / 9; int a = y + 5, b = y - 4;
        acc += tmp[(a >= SCREEN_H ? SCREEN_H - 1 : a) * SCREEN_W + x] - tmp[(b < 0 ? 0 : b) * SCREEN_W + x]; }
      for (int y = 0; y < SCREEN_H; y++) tmp[y * SCREEN_W + x] = line[y];
    }
  }
  int c[3]; unpack(color, c);
  for (int i = 0; i < N; i++) { float al = fminf(1, tmp[i] * 1.4f) * 0.75f; if (al > 0.01f) blend(i, c[0], c[1], c[2], al); }
}

static bool sameName(const char* a, const char* b) {
  while (*a && *b && tolower(*a) == tolower(*b)) { a++; b++; }
  return !*a && !*b;
}
// Armor drawing for a tier and body: "<Material> <Body>" (e.g. "Leather Female") when that art exists,
// otherwise the plain "<Material>" drawing.
static const LayerDef* armorFor(int tier, int bodyIdx) {
  const char* mat = ARMOR_MAT_NAME[band(tier)];
  char want[40]; snprintf(want, sizeof want, "%s %s", mat, BODIES[bodyIdx % BODIES_COUNT].name);
  for (int i = 0; i < ARMORS_COUNT; i++) if (sameName(ARMORS[i].name, want)) return &ARMORS[i];
  for (int i = 0; i < ARMORS_COUNT; i++) if (sameName(ARMORS[i].name, mat)) return &ARMORS[i];
  return ARMORS_COUNT ? &ARMORS[0] : nullptr;
}

// Weapon kinds, in the order saved items use (new kinds go at the end). Art: weapon_<material>_<kind>.png
static const char* const WEAPON_KINDS[] = { "Sword", "Axe", "Mace" };
int weaponKindCount() { return (int)(sizeof(WEAPON_KINDS) / sizeof(WEAPON_KINDS[0])); }
const char* weaponKindName(int k) { return WEAPON_KINDS[k >= 0 && k < weaponKindCount() ? k : 0]; }
// Weapon drawing for a tier: "<Material> <Kind>" (Wood 1-5, Bronze 6-10, Iron 11-15, Titanium 16-20)
static const LayerDef* weaponFor(int tier, int kind) {
  char want[40]; snprintf(want, sizeof want, "%s %s", WEAPON_MAT_NAME[band(tier)], weaponKindName(kind));
  for (int i = 0; i < WEAPONS_COUNT; i++) if (sameName(WEAPONS[i].name, want)) return &WEAPONS[i];
  return WEAPONS_COUNT ? &WEAPONS[0] : nullptr;
}

// Cape drawing for a tier: one per band (Traveler 1-5, Ranger 6-10, Knight 11-15, Royal 16-20)
// front = the shoulder part drawn over the armor ("<Name> Front"), when that drawing exists
static const LayerDef* capeFor(int tier, bool front) {
  char want[32]; snprintf(want, sizeof want, front ? "%s Front" : "%s", CAPE_NAME[band(tier)]);
  for (int i = 0; i < CAPES_COUNT; i++) if (sameName(CAPES[i].name, want)) return &CAPES[i];
  return front || !CAPES_COUNT ? nullptr : &CAPES[0];
}

// Character only, on a transparent background: premultiplied R,G,B (0..255 times alpha) and A in work.
uint32_t workGeneration = 0;
void renderCharacterLayers(const Look& look, float* work) {
  initTables();
  workGeneration++;
  const int N = SCREEN_W * SCREEN_H;
  R = work; G = work + N; B = work + 2 * N; A = work + 3 * N;
  memset(work, 0, sizeof(float) * 4 * N);
  const LayerDef& body = BODIES[look.body % BODIES_COUNT];
  const LayerDef* weapon = (look.weapon >= 0 && WEAPONS_COUNT) ? weaponFor(look.weaponTier, look.weapon) : nullptr;
  const LayerDef* armor = (look.armor >= 0) ? armorFor(look.armorTier, look.body) : nullptr;
  const LayerDef* hair = (look.hair >= 0 && HAIRS_COUNT) ? &HAIRS[look.hair % HAIRS_COUNT] : nullptr;
  int handCut = body.wrist;
  if (weapon && weapon->handTop < SCREEN_H && weapon->handTop + 6 < handCut) handCut = weapon->handTop + 6;
  paint.skin = makeSkin(body.skinBase, SKIN_TARGET[look.skin % 6]);
  float* scratch = work + 4 * N;  // glow scratch: N + SCREEN_H floats

  // cape, behind everything: the fabric takes the rarity color
  const LayerDef* cape = (look.cape >= 0) ? capeFor(look.capeTier, false) : nullptr;
  const LayerDef* capeFront = (look.cape >= 0) ? capeFor(look.capeTier, true) : nullptr;
  if (cape) {
    int t = look.capeTier;
    gradLUT(RARITY_RGB[t - 1], cape->mid3, paint.a); memcpy(paint.b, paint.a, sizeof(paint.a));
    if (t >= GLOW_FROM) drawGlow(*cape, RARITY_RGB[t - 1], scratch);
    drawLayer(*cape, SCREEN_H, false);
  }
  // body (eyes = group A, underwear = group B)
  gradLUT(EYE_RGB[look.eye % 9], body.mid3, paint.a);
  gradLUT(0xA8A8A8, body.mid4, paint.b);
  if (!look.noBody) drawLayer(body, handCut, weapon != nullptr);
  // armor
  if (armor) {
    const TierColor& tc = ARMOR_TIERS[look.armorTier - 1];
    if (tc.asDrawn) tintLUT(tc.rgb, tc.lift, paint.a); else gradLUT(tc.rgb, armor->mid3, paint.a);
    gradLUT(RARITY_RGB[look.armorTier - 1], armor->mid4, paint.b);
    if (look.armorTier >= GLOW_FROM) drawGlow(*armor, RARITY_RGB[look.armorTier - 1], scratch);
    drawLayer(*armor, SCREEN_H, false);
  }
  // cape front (shoulders and clasp), over the armor
  if (capeFront) {
    int t = look.capeTier;
    gradLUT(RARITY_RGB[t - 1], capeFront->mid3, paint.a); memcpy(paint.b, paint.a, sizeof(paint.a));
    drawLayer(*capeFront, SCREEN_H, false);
  }
  // hair
  if (hair) { gradLUT(HAIR_RGB[look.hairColor % 9], hair->mid3, paint.a); memcpy(paint.b, paint.a, sizeof(paint.a)); drawLayer(*hair, SCREEN_H, false); }
  // weapon + gem
  if (weapon) {
    int t = look.weaponTier;
    const TierColor& tc = ARMOR_TIERS[t - 1];   // same metal color per tier as the armor
    if (tc.asDrawn) tintLUT(tc.rgb, tc.lift, paint.a); else gradLUT(tc.rgb, weapon->mid3, paint.a);
    gradLUT(RARITY_RGB[t - 1], weapon->mid4, paint.b);
    if (t >= GLOW_FROM) drawGlow(*weapon, RARITY_RGB[t - 1], scratch);
    drawLayer(*weapon, SCREEN_H, false);
    drawGem(*weapon, look.element);
  }
}

static inline void put565(uint16_t* p, float r, float g, float b) {
  int ri = (int)r, gi = (int)g, bi = (int)b;
  ri = ri < 0 ? 0 : ri > 255 ? 255 : ri; gi = gi < 0 ? 0 : gi > 255 ? 255 : gi; bi = bi < 0 ? 0 : bi > 255 ? 255 : bi;
  *p = (uint16_t)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
}
static inline void get565(uint16_t v, float* o) {
  int r = (v >> 8) & 0xF8, g = (v >> 3) & 0xFC, b = (v << 3) & 0xF8;
  o[0] = r | (r >> 5); o[1] = g | (g >> 6); o[2] = b | (b >> 5);
}

// two cached backgrounds: floor light centered, and moved right (home screen)
static uint16_t* bgCacheBuf[2] = { nullptr, nullptr };
static float bgCacheX[2] = { -1, -1 };
void drawBackground(uint16_t* fb) { drawBackgroundAt(fb, SCREEN_W / 2.f); }
void drawBackgroundAt(uint16_t* fb, float glowX) {
  for (int i = 0; i < 2; i++) if (bgCacheBuf[i] && bgCacheX[i] == glowX) { memcpy(fb, bgCacheBuf[i], sizeof(uint16_t) * SCREEN_W * SCREEN_H); return; }
  for (int y = 0; y < SCREEN_H; y++) for (int x = 0; x < SCREEN_W; x++) {
    float dx = x - glowX, dy = y - (SCREEN_H - 28.f), d = sqrtf(dx * dx + dy * dy);
    float gl = d < 140 ? 0.35f * (1 - d / 140) : 0;
    put565(fb + y * SCREEN_W + x, 14 * (1 - gl) + 120 * gl, 13 * (1 - gl) + 110 * gl, 19 * (1 - gl) + 160 * gl);
  }
  int slot = bgCacheBuf[0] && bgCacheX[0] != glowX ? 1 : 0;
  if (!bgCacheBuf[slot]) bgCacheBuf[slot] = (uint16_t*)malloc(sizeof(uint16_t) * SCREEN_W * SCREEN_H);   // big, so it lands in PSRAM
  if (bgCacheBuf[slot]) { memcpy(bgCacheBuf[slot], fb, sizeof(uint16_t) * SCREEN_W * SCREEN_H); bgCacheX[slot] = glowX; }
}

void blitCharacter(const float* work, uint16_t* fb, float sx, float sy, float sw, float sh,
                   float dx, float dy, float dw, float dh, int clipY0, int clipY1, int mode, uint32_t tint) {
  const int N = SCREEN_W * SCREEN_H;
  const float *Rs = work, *Gs = work + N, *Bs = work + 2 * N, *As = work + 3 * N;
  float kx = sw / dw, ky = sh / dh;
  int x0 = (int)floorf(dx), x1 = (int)ceilf(dx + dw), y0 = (int)floorf(dy), y1 = (int)ceilf(dy + dh);
  if (y0 < clipY0) y0 = clipY0; if (y1 > clipY1) y1 = clipY1;
  if (x0 < 0) x0 = 0; if (x1 > SCREEN_W) x1 = SCREEN_W; if (y0 < 0) y0 = 0; if (y1 > SCREEN_H) y1 = SCREEN_H;
  float tr = (tint >> 16) & 255, tg = (tint >> 8) & 255, tb = tint & 255;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    float u = sx + (x + 0.5f - dx) * kx, v = sy + (y + 0.5f - dy) * ky;
    float s[4] = { 0, 0, 0, 0 };
    if (mode == 1) {  // nearest: crisp pixels when zoomed in
      int iu = (int)u, iv = (int)v; if (iu < 0 || iv < 0 || iu >= SCREEN_W || iv >= SCREEN_H) continue;
      int k = iv * SCREEN_W + iu; s[0] = Rs[k]; s[1] = Gs[k]; s[2] = Bs[k]; s[3] = As[k];
    } else {  // smooth: average of 4 bilinear taps across the pixel's footprint
      for (int t = 0; t < 4; t++) {
        float uu = u + ((t & 1) ? 0.25f : -0.25f) * kx - 0.5f, vv = v + ((t & 2) ? 0.25f : -0.25f) * ky - 0.5f;
        int iu = (int)floorf(uu), iv = (int)floorf(vv); float fu = uu - iu, fv = vv - iv;
        for (int q = 0; q < 4; q++) {
          int xx = iu + (q & 1), yy = iv + (q >> 1);
          if (xx < 0 || yy < 0 || xx >= SCREEN_W || yy >= SCREEN_H) continue;
          float w = ((q & 1) ? fu : 1 - fu) * ((q >> 1) ? fv : 1 - fv) * 0.25f;
          int k = yy * SCREEN_W + xx; s[0] += Rs[k] * w; s[1] += Gs[k] * w; s[2] += Bs[k] * w; s[3] += As[k] * w;
        }
      }
    }
    if (s[3] <= 0.002f) continue;
    if (mode == 2) { s[0] = tr * s[3]; s[1] = tg * s[3]; s[2] = tb * s[3]; }  // silhouette
    uint16_t* p = fb + y * SCREEN_W + x; float d[3]; get565(*p, d);
    float inv = 1 - s[3];
    put565(p, s[0] + d[0] * inv, s[1] + d[1] * inv, s[2] + d[2] * inv);
  }
}

void renderCharacter(const Look& look, uint16_t* fb, float* work) {
  renderCharacterLayers(look, work);
  drawBackground(fb);
  blitCharacter(work, fb, 0, 0, SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H, 0, SCREEN_H, 1, 0);
}

uint32_t skinSwatch(int i, int body) { return SKIN_TARGET[i % 6] ? SKIN_TARGET[i % 6] : BODIES[body % BODIES_COUNT].skinBase; }
uint32_t eyeRgb(int i) { return EYE_RGB[i % 9]; }
uint32_t hairRgb(int i) { return HAIR_RGB[i % 9]; }
int hairIndex(const char* name) {
  for (int i = 0; i < HAIRS_COUNT; i++) if (strcmp(HAIRS[i].name, name) == 0) return i;
  return 0;
}

// ---------------------------------------------------------------- item pictures
struct Thumb { uint32_t key, used; uint8_t* px; };
static Thumb thumbs[24];
static uint32_t thumbClock = 0;
const uint8_t* itemThumb(int type, int kind, int tier, int body, int element, float* work) {
  if (tier < 1) tier = 1; if (tier > 20) tier = 20;
  uint32_t key = (uint32_t)type << 24 | (uint32_t)(kind & 15) << 16 | (uint32_t)tier << 8 | (uint32_t)(body & 15) << 4 | (element & 15);
  for (auto& t : thumbs) if (t.px && t.key == key) { t.used = ++thumbClock; return t.px; }
  Thumb* slot = &thumbs[0];   // a free slot, or the one unused the longest
  for (auto& t : thumbs) { if (!t.px) { slot = &t; break; } if (t.used < slot->used) slot = &t; }
  if (!slot->px) slot->px = bigAlloc(THUMB * THUMB * 4);
  if (!slot->px) return nullptr;
  Look l; memset(&l, 0, sizeof l);
  l.body = (uint8_t)body; l.skin = 1; l.hair = -1; l.noBody = 1; l.element = (uint8_t)element;
  l.armor = -1; l.weapon = -1; l.cape = -1; l.armorTier = l.weaponTier = l.capeTier = (uint8_t)tier;
  if (type == THUMB_WEAPON) l.weapon = (int8_t)kind; else if (type == THUMB_ARMOR) l.armor = 0; else l.cape = 0;
  renderCharacterLayers(l, work);
  // crop to the drawn pixels, then shrink (box filter) to fit THUMB x THUMB, centered
  int x0 = SCREEN_W, y0 = SCREEN_H, x1 = -1, y1 = -1;
  for (int y = 0; y < SCREEN_H; y++) for (int x = 0; x < SCREEN_W; x++)
    if (A[y * SCREEN_W + x] > 0.03f) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
  memset(slot->px, 0, THUMB * THUMB * 4);
  slot->key = key; slot->used = ++thumbClock;
  if (x1 < 0) return slot->px;
  float bw = (float)(x1 - x0 + 1), bh = (float)(y1 - y0 + 1), s = fminf((THUMB - 2) / bw, (THUMB - 2) / bh);
  if (s > 1) s = 1;
  float ox = (THUMB - bw * s) / 2, oy = (THUMB - bh * s) / 2;
  for (int v = 0; v < THUMB; v++) for (int u = 0; u < THUMB; u++) {
    float sx0 = x0 + (u - ox) / s, sy0 = y0 + (v - oy) / s, sx1 = sx0 + 1 / s, sy1 = sy0 + 1 / s;
    int ix0 = (int)floorf(sx0), iy0 = (int)floorf(sy0), ix1 = (int)ceilf(sx1), iy1 = (int)ceilf(sy1);
    if (ix1 <= x0 || iy1 <= y0 || ix0 > x1 || iy0 > y1) continue;
    float r = 0, g = 0, b = 0, a = 0; int n = 0;
    for (int y = iy0; y < iy1; y++) for (int x = ix0; x < ix1; x++) {
      n++;
      if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) continue;
      int i = y * SCREEN_W + x; r += R[i]; g += G[i]; b += B[i]; a += A[i];
    }
    if (!n || a <= 0) continue;
    uint8_t* p = slot->px + (v * THUMB + u) * 4;
    float k = 1.f / n;
    p[0] = (uint8_t)fminf(255, r * k); p[1] = (uint8_t)fminf(255, g * k); p[2] = (uint8_t)fminf(255, b * k); p[3] = (uint8_t)fminf(255, a * k * 255);
  }
  return slot->px;
}

void blitThumb(uint16_t* fb, const uint8_t* px, float cx, float cy, float size, float dim) {
  if (!px) return;
  int D = (int)lroundf(size), dx = (int)lroundf(cx - size / 2), dy = (int)lroundf(cy - size / 2);
  float st = (float)THUMB / D;
  for (int j = 0; j < D; j++) {
    int y = dy + j; if (y < 0 || y >= SCREEN_H) continue;
    for (int i = 0; i < D; i++) {
      int x = dx + i; if (x < 0 || x >= SCREEN_W) continue;
      // sample: box average when shrinking, nearest when growing
      int u0 = (int)(i * st), v0 = (int)(j * st), u1 = (int)((i + 1) * st), v1 = (int)((j + 1) * st);
      if (u1 <= u0) u1 = u0 + 1; if (v1 <= v0) v1 = v0 + 1;
      float r = 0, g = 0, b = 0, a = 0; int n = 0;
      for (int v = v0; v < v1 && v < THUMB; v++) for (int u = u0; u < u1 && u < THUMB; u++) {
        const uint8_t* p = px + (v * THUMB + u) * 4; r += p[0]; g += p[1]; b += p[2]; a += p[3]; n++;
      }
      if (!n || a <= 0) continue;
      float k = 1.f / n; a = a * k / 255.f; r *= k * dim; g *= k * dim; b *= k * dim;
      float d[3]; get565(fb[y * SCREEN_W + x], d);
      put565(&fb[y * SCREEN_W + x], r + d[0] * (1 - a), g + d[1] * (1 - a), b + d[2] * (1 - a));
    }
  }
}
