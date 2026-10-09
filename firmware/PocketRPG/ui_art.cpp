#include "ui_art.h"
#include "ui_gfx.h"
#include "inflate.h"
#include <stdlib.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

static uint8_t* pixels[UI_COUNT];   // unpacked on first use and kept (about 180 KB, in PSRAM on the board)

static const uint8_t* px(int id) {
  if (id < 0 || id >= UI_COUNT) return nullptr;
  if (pixels[id]) return pixels[id];
  const UiSpriteDef& d = UI_SPRITES[id];
#ifdef ARDUINO
  uint8_t* p = (uint8_t*)heap_caps_malloc(d.rawLen, MALLOC_CAP_SPIRAM);
#else
  uint8_t* p = (uint8_t*)malloc(d.rawLen);
#endif
  if (!p) return nullptr;
  if (inflateRaw(d.z, d.zLen, p, d.rawLen) != d.rawLen) { free(p); return nullptr; }
  return pixels[id] = p;
}

int uiW(int id) { return UI_SPRITES[id].w; }
int uiH(int id) { return UI_SPRITES[id].h; }
int uiX(int id) { return UI_SPRITES[id].x; }
int uiY(int id) { return UI_SPRITES[id].y; }

// columns sx0..sx0+n-1 of the piece, drawn starting at screen column dx
static void cols(int id, int sx0, int n, int dx, int dy) {
  const uint8_t* p = px(id); if (!p) return;
  const UiSpriteDef& d = UI_SPRITES[id];
  blitPremul(p + sx0 * 4, d.w * 4, n, d.h, dx, dy);
}
void uiDraw(int id, int x, int y) { cols(id, 0, UI_SPRITES[id].w, x, y); }
void uiDrawWide(int id, int x, int y, int w) {
  int sw = UI_SPRITES[id].w;
  if (w <= sw) { uiDraw(id, x, y); return; }
  int half = sw / 2;
  cols(id, 0, half, x, y);
  for (int i = 0; i < w - sw; i++) cols(id, half, 1, x + half + i, y);
  cols(id, half, sw - half, x + half + (w - sw), y);
}
void uiDrawCols(int id, int x, int y, int w) { for (int i = 0; i < w; i++) cols(id, 0, 1, x + i, y); }
