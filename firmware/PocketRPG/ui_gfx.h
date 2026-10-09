// Small anti-aliased drawing kit for the 368x448 RGB565 framebuffer: rounded boxes, circles, polygons,
// lines and 4-bit anti-aliased text. Everything draws into the buffer; the sketch pushes it to the screen.
#pragma once
#include <stdint.h>

typedef uint32_t Rgb;  // 0xRRGGBB

struct Glyph { uint32_t off; uint8_t w, h; int8_t xo, yo; uint8_t adv; };
struct Font { const uint8_t* bits; const Glyph* glyphs; int16_t ascent, descent; };

// Silkscreen pixel font: 16, 24 and 32 px regular, 16, 24 and 48 px bold
extern const Font FONT_PX16, FONT_PX24, FONT_PX32, FONT_PXB16, FONT_PXB24, FONT_PXB48;

enum Align { LEFT = 0, CENTER = 1, RIGHT = 2 };

void gfxTarget(uint16_t* fb);
uint16_t to565(Rgb c);
void blendPixel(int x, int y, Rgb c, float a);
void fillRect(int x, int y, int w, int h, Rgb c, float a = 1.f);
// Rounded box. fillA = 0 skips the fill; lw = 0 skips the outline (drawn on the inside of the edge).
void roundBox(float x, float y, float w, float h, float r, Rgb fill, float fillA, Rgb stroke = 0, float lw = 0);
void disc(float cx, float cy, float r, Rgb c, float a = 1.f);
void ring(float cx, float cy, float r, float lw, Rgb c, float a = 1.f);
void thickLine(float x0, float y0, float x1, float y1, float w, Rgb c);
void polygon(const float* xy, int n, Rgb c);  // n points, filled (even-odd), 4x4 supersampled
// Premultiplied RGBA pixels (stride in bytes), w x h, drawn with their top-left at x, y.
void blitPremul(const uint8_t* px, int stride, int w, int h, int x, int y);
// Text with a dark outline around it, for text drawn straight over the picture.
void textOutlined(const Font& f, const char* s, int x, int baseline, Rgb c, Rgb outline, int r, Align a = CENTER);
int textWidth(const Font& f, const char* s);
void text(const Font& f, const char* s, int x, int baseline, Rgb c, Align a = CENTER);
