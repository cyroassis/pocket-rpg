#include "ui_gfx.h"
#include "render.h"
#include "fonts.h"
#include <math.h>

static uint16_t* T = nullptr;
void gfxTarget(uint16_t* fb) { T = fb; }

uint16_t to565(Rgb c) { return (uint16_t)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xFF) >> 3)); }

void blendPixel(int x, int y, Rgb c, float a) {
  if (a <= 0.003f || x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  uint16_t* p = T + y * SCREEN_W + x;
  if (a >= 0.997f) { *p = to565(c); return; }
  uint16_t d = *p;
  int dr = (d >> 8) & 0xF8, dg = (d >> 3) & 0xFC, db = (d << 3) & 0xF8;
  dr |= dr >> 5; dg |= dg >> 6; db |= db >> 5;
  int r = (int)(((c >> 16) & 255) * a + dr * (1 - a));
  int g = (int)(((c >> 8) & 255) * a + dg * (1 - a));
  int b = (int)((c & 255) * a + db * (1 - a));
  *p = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void fillRect(int x, int y, int w, int h, Rgb c, float a) {
  int x1 = x + w, y1 = y + h;
  if (x < 0) x = 0; if (y < 0) y = 0; if (x1 > SCREEN_W) x1 = SCREEN_W; if (y1 > SCREEN_H) y1 = SCREEN_H;
  if (a >= 0.997f) { uint16_t v = to565(c); for (int j = y; j < y1; j++) { uint16_t* p = T + j * SCREEN_W; for (int i = x; i < x1; i++) p[i] = v; } return; }
  for (int j = y; j < y1; j++) for (int i = x; i < x1; i++) blendPixel(i, j, c, a);
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// signed distance to a rounded box centered at the origin (negative inside)
static inline float sdBox(float px, float py, float hx, float hy, float r) {
  float qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy; if (in > 0) in = 0;
  return sqrtf(ox * ox + oy * oy) + in - r;
}

void roundBox(float x, float y, float w, float h, float r, Rgb fill, float fillA, Rgb stroke, float lw) {
  float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
  if (r > hx) r = hx; if (r > hy) r = hy;
  int x0 = (int)floorf(x) - 1, y0 = (int)floorf(y) - 1, x1 = (int)ceilf(x + w) + 1, y1 = (int)ceilf(y + h) + 1;
  for (int j = y0; j < y1; j++) {
    if (j < 0 || j >= SCREEN_H) continue;
    float py = j + 0.5f - cy;
    bool solidRow = fabsf(py) < hy - r - 1;
    for (int i = x0; i < x1; i++) {
      float px = i + 0.5f - cx;
      float d = solidRow ? fabsf(px) - hx : sdBox(px, py, hx, hy, r);
      if (fillA > 0) { float cov = clamp01(0.5f - d); if (cov > 0) blendPixel(i, j, fill, cov * fillA); }
      if (lw > 0) { float e = fabsf(d + lw / 2) - lw / 2; float cov = clamp01(0.5f - e); if (cov > 0) blendPixel(i, j, stroke, cov); }
    }
  }
}

void disc(float cx, float cy, float r, Rgb c, float a) {
  int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 2), y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 2);
  for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) {
    float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d = sqrtf(dx * dx + dy * dy) - r;
    float cov = clamp01(0.5f - d); if (cov > 0) blendPixel(i, j, c, cov * a);
  }
}

void ring(float cx, float cy, float r, float lw, Rgb c, float a) {
  int x0 = (int)(cx - r - lw - 1), x1 = (int)(cx + r + lw + 2), y0 = (int)(cy - r - lw - 1), y1 = (int)(cy + r + lw + 2);
  for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) {
    float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d = fabsf(sqrtf(dx * dx + dy * dy) - r) - lw / 2;
    float cov = clamp01(0.5f - d); if (cov > 0) blendPixel(i, j, c, cov * a);
  }
}

void thickLine(float x0, float y0, float x1, float y1, float w, Rgb c) {
  float minx = fminf(x0, x1) - w, maxx = fmaxf(x0, x1) + w, miny = fminf(y0, y1) - w, maxy = fmaxf(y0, y1) + w;
  float vx = x1 - x0, vy = y1 - y0, ll = vx * vx + vy * vy; if (ll < 1e-6f) ll = 1e-6f;
  for (int j = (int)miny; j <= (int)maxy; j++) for (int i = (int)minx; i <= (int)maxx; i++) {
    float px = i + 0.5f - x0, py = j + 0.5f - y0, t = clamp01((px * vx + py * vy) / ll);
    float dx = px - vx * t, dy = py - vy * t, d = sqrtf(dx * dx + dy * dy) - w / 2;
    float cov = clamp01(0.5f - d); if (cov > 0) blendPixel(i, j, c, cov);
  }
}

static bool inside(const float* xy, int n, float px, float py) {
  bool in = false;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float xi = xy[i * 2], yi = xy[i * 2 + 1], xj = xy[j * 2], yj = xy[j * 2 + 1];
    if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) in = !in;
  }
  return in;
}
void polygon(const float* xy, int n, Rgb c) {
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < n; i++) { minx = fminf(minx, xy[i * 2]); maxx = fmaxf(maxx, xy[i * 2]); miny = fminf(miny, xy[i * 2 + 1]); maxy = fmaxf(maxy, xy[i * 2 + 1]); }
  for (int j = (int)floorf(miny); j <= (int)ceilf(maxy); j++) for (int i = (int)floorf(minx); i <= (int)ceilf(maxx); i++) {
    int hits = 0;
    for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) if (inside(xy, n, i + (sx + 0.5f) / 4, j + (sy + 0.5f) / 4)) hits++;
    if (hits) blendPixel(i, j, c, hits / 16.f);
  }
}

// ---------------------------------------------------------------- text (UTF-8: ASCII plus · and …)
static int nextChar(const char*& s) {
  uint8_t c = (uint8_t)*s;
  if (!c) return -1;
  if (c < 0x80) { s++; return c >= 32 && c < 127 ? c - 32 : 0; }
  if (c == 0xC2 && (uint8_t)s[1] == 0xB7) { s += 2; return 95; }                       // ·
  if (c == 0xE2 && (uint8_t)s[1] == 0x80 && (uint8_t)s[2] == 0xA6) { s += 3; return 96; } // …
  s++; while ((*s & 0xC0) == 0x80) s++;
  return 0;
}
int textWidth(const Font& f, const char* s) {
  int w = 0, g;
  while ((g = nextChar(s)) >= 0) w += f.glyphs[g].adv;
  return w;
}
void text(const Font& f, const char* s, int x, int baseline, Rgb c, Align a) {
  if (a != LEFT) { int w = textWidth(f, s); x -= a == CENTER ? w / 2 : w; }
  int g;
  while ((g = nextChar(s)) >= 0) {
    const Glyph& gl = f.glyphs[g];
    const uint8_t* b = f.bits + gl.off;
    int n = 0;
    for (int j = 0; j < gl.h; j++) for (int i = 0; i < gl.w; i++, n++) {
      int v = (n & 1) ? (b[n >> 1] & 15) : (b[n >> 1] >> 4);
      if (v) blendPixel(x + gl.xo + i, baseline + gl.yo + j, c, v / 15.f);
    }
    x += gl.adv;
  }
}
