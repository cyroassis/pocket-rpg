// Pocket RPG character renderer: draws the layered character into a 368x448 RGB565 framebuffer,
// with the same recoloring rules as the Character Creator page. No hardware code in here, so it
// also compiles on a PC for testing.
#pragma once
#include <stdint.h>

#define SCREEN_W 368
#define SCREEN_H 448

struct LayerDef {
  const char* name;
  const uint8_t* rle;       // raw-deflate packed; unpacked: records of 5 bytes: count, group<<4|opacity, value (3 bytes)
  uint32_t rleLen;          // packed size
  uint8_t mid3, mid4;       // middle brightness of group A / group B
  const int16_t* handRows;  // bodies: weapon-hand x range per row (-1,-1 = none)
  int16_t wrist;            // bodies: wrist row
  uint32_t skinBase;        // bodies: median skin color (0xRRGGBB)
  int16_t handTop;          // weapons: first row of the drawn hand
  float sockCx, sockCy;     // weapons: gem socket center
  int16_t sockD;            // weapons: gem socket diameter (0 = none)
  uint32_t rawLen;          // unpacked size
};

struct Look {
  uint8_t body;        // index into BODIES
  uint8_t skin;        // 0..5 (1 = the art as drawn)
  uint8_t eye;         // 0..8
  int8_t hair;         // index into HAIRS, -1 = bald
  uint8_t hairColor;   // 0..8
  int8_t armor;        // index into ARMORS by band (0 leather .. 3 titanium), -1 = none
  uint8_t armorTier;   // 1..20
  int8_t weapon;       // weapon kind (0 sword, 1 axe; the drawing follows the tier band), -1 = none
  uint8_t weaponTier;  // 1..20
  uint8_t element;     // 0 neutral, 1 fire, 2 nature, 3 water
  int8_t cape;         // 0 = wear a cape (the drawing follows the tier band), -1 = none
  uint8_t capeTier;    // 1..20
  uint8_t noBody;      // 1 = draw only the gear (item pictures)
};

// Names for the on-screen labels
extern const char* const SKIN_NAMES[6];
extern const char* const EYE_NAMES[9];
extern const char* const HAIR_COLOR_NAMES[9];
extern const char* const ELEMENT_NAMES[4];
extern const char* const RARITY_NAMES[20];
uint32_t rarityRgb(int tier);   // 0xRRGGBB of a rarity tier (1..20)
const char* armorColorName(int tier);
const char* armorMaterialName(int tier);
const char* weaponMaterialName(int tier);
const char* capeName(int tier);
int weaponKindCount();            // kinds with art: Sword, Axe, ...
const char* weaponKindName(int kind);   // Traveler, Ranger, Knight, Royal (by tier band)
// What the art set contains (from art_data.h)
int bodyCount();
int hairCount();
const char* bodyName(int i);
const char* hairName(int i);
bool hairIsFemale(int i);   // girl hairstyle (the setup only offers the styles of the chosen body)

// Draws the character into fb (SCREEN_W*SCREEN_H RGB565, byte order as the display expects).
// work must hold RENDER_WORK_FLOATS floats (about 3.3 MB: put it in PSRAM).
#define RENDER_WORK_FLOATS (SCREEN_W * SCREEN_H * 5 + SCREEN_H)
void renderCharacter(const Look& look, uint16_t* fb, float* work);

// For the menus: render the character once (transparent background, kept in work), then draw it scaled
// into any part of the screen. mode: 0 smooth (shrinking), 1 crisp pixels (zooming in), 2 silhouette in tint.
void renderCharacterLayers(const Look& look, float* work);
void blitCharacter(const float* work, uint16_t* fb, float sx, float sy, float sw, float sh,
                   float dx, float dy, float dw, float dh, int clipY0, int clipY1, int mode, uint32_t tint);
// Item pictures: the item's own art (weapons with their hand), cropped and shrunk to THUMB x THUMB
// premultiplied RGBA, kept in a small cache. Making one reuses the work buffer, so a character kept there
// must be drawn again: workGeneration changes every time the work buffer is overwritten.
#define THUMB 112
enum ThumbType { THUMB_WEAPON = 0, THUMB_ARMOR = 1, THUMB_CAPE = 2 };
const uint8_t* itemThumb(int type, int kind, int tier, int body, int element, float* work);
void blitThumb(uint16_t* fb, const uint8_t* rgba, float cx, float cy, float size, float dim);   // dim 1 = as is
extern uint32_t workGeneration;
void drawBackground(uint16_t* fb);  // dark screen with the soft floor light
void drawBackgroundAt(uint16_t* fb, float glowX);  // same, floor light centered at glowX
uint32_t skinSwatch(int i, int body);
uint32_t eyeRgb(int i);
uint32_t hairRgb(int i);
int hairIndex(const char* name);
