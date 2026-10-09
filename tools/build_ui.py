#!/usr/bin/env python3
"""
Turns the UI art (art/ui/*.png, drawn at about 3x the screen size) into firmware/PocketRPG/ui_data.h.

Each piece is cut from the full-screen mockups, its sample text is wiped (the game writes its own text),
it is shrunk to screen size and stored as premultiplied RGBA, compressed with LZMA (as planes, colours in 5-6-5 bits).
Panels that hold text of any length are drawn stretched: their middle column repeats (see uiDrawWide).
Preview of every piece: tools/ui_preview.png
"""
import os, lzma
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "art", "ui")
OUT = os.path.join(HERE, "..", "firmware", "PocketRPG", "ui_data.h")
SCALE = 368 / 1136          # the mockups are 1136 px wide for the 368 px screen

def load(name):
    return np.array(Image.open(os.path.join(SRC, name)).convert("RGBA")).astype(np.float32)

def lum(p): return 0.3 * p[..., 0] + 0.59 * p[..., 1] + 0.11 * p[..., 2]

def inner_rect(a, dark=60):
    """The panel's inside: scanning in from each edge along the middle row/column, past the rim to the first dark pixel."""
    h, w = a.shape[:2]
    L = lum(a)
    def scan(line):
        seen_rim = False; run = 0
        for i, v in enumerate(line):
            run = run + 1 if v > 110 else 0
            if run >= 3: seen_rim = True   # the gold rim: a few bright pixels in a row (the soft halo outside is thinner)
            elif seen_rim and v < dark: return i
        return 0
    mid_y, mid_x = h // 2, w // 2
    x0 = scan(L[mid_y]); x1 = w - scan(L[mid_y][::-1])
    y0 = scan(L[:, mid_x]); y1 = h - scan(L[:, mid_x][::-1])
    return x0, y0, x1, y1

def wipe(a, inset=4, keep=None):
    """Paints over the inside with each row's own fill colour (the panels have a soft top-to-bottom shade)."""
    x0, y0, x1, y1 = inner_rect(a)
    x0 += inset; y0 += inset; x1 -= inset; y1 -= inset
    for y in range(y0, y1):
        row = a[y, x0:x1]
        d = row[lum(row) < 45]
        col = np.median(d, axis=0) if len(d) else row[0]
        if keep:
            kx0, ky0, kx1, ky1 = keep
            for x in range(x0, x1):
                if not (kx0 <= x < kx1 and ky0 <= y < ky1): a[y, x] = col
        else:
            a[y, x0:x1] = col
    return a

def shrink(a, scale=SCALE):
    im = Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGBA")
    w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    # premultiply before scaling, so transparent pixels don't bleed their colour into the edges
    p = a.copy(); p[..., :3] *= p[..., 3:4] / 255
    chans = [Image.fromarray(p[..., i].clip(0, 255).astype(np.uint8)).resize((w, h), Image.BOX) for i in range(4)]
    return np.stack([np.array(c) for c in chans], -1).astype(np.uint8)   # premultiplied RGBA

A = load("home_a.png"); B = load("home_b.png")
cut = lambda img, x0, y0, x1, y1: img[y0:y1, x0:x1].copy()

pieces = []   # (name, premultiplied rgba, screen x, screen y)
def add(name, a, x, y):
    s = shrink(a)
    pieces.append((name, s, round(x * SCALE), round(y * SCALE)))
    return s

# name panel: wiped, drawn stretched to fit the name; the footprint icon is kept on its own
name = cut(A, 21, 22, 370, 298)
feet = cut(A, 21 + 56, 22 + 176, 21 + 112, 22 + 236)
add("NAME", wipe(name), 21, 22)
add("FEET", feet, 21 + 56, 22 + 176)
# action buttons, with their labels (they never change)
add("BTN_CRAFT", cut(A, 21, 327, 328, 665), 21, 327)
add("BTN_EXPLORE", cut(A, 21, 669, 328, 1006), 21, 669)
add("BTN_BAG", cut(A, 21, 1007, 328, 1346), 21, 1007)
# banner: wiped, drawn stretched to fit its text, right edge fixed
add("BANNER", wipe(cut(A, 633, 1191, 1115, 1361)), 633, 1191)

# XP bar (second mockup): the empty bar, and one column of the gold fill to repeat
bar = cut(B, 837, 61, 1086, 140)
L = lum(bar)
on = (bar[..., 3] > 128).any(1)
r0 = int(np.argmax(on)); r1 = r0
while r1 < len(on) and on[r1]: r1 += 1          # the bar only (the word LEVEL starts below a gap)
bar = bar[r0:r1]
rows = [r0]
by0 = 61 + rows[0]
# inside of the bar: the gold part (left) and the dark part (right) on the middle row
mid = bar.shape[0] // 2
line = lum(bar[mid])
gold = np.where((bar[mid, :, 0] > 180) & (bar[mid, :, 2] < 120))[0]
gx0, gx1 = gold[0], gold[-1]
ys = np.where((bar[:, gx0 + 4, 0] > 150) & (bar[:, gx0 + 4, 2] < 140))[0]
fy0, fy1 = ys[0], ys[-1] + 1
fill = bar[fy0:fy1, gx0 + (gx1 - gx0) // 2: gx0 + (gx1 - gx0) // 2 + 3].copy()
dark_col = bar[fy0:fy1, gx1 + 20:gx1 + 21].copy()
bar[fy0:fy1, gx0:gx1 + 20] = np.repeat(dark_col, gx1 + 20 - gx0, axis=1)
add("XPBAR", bar, 837, by0)
add("XPFILL", fill, 837 + gx0, by0 + fy0)

# splash screen (shown while the board starts): only the part that isn't black, drawn over black
S = load("splash.png")
ys, xs = np.where(S[..., :3].max(axis=2) > 6)
sx0, sx1, sy0, sy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
add("SPLASH", cut(S, sx0, sy0, sx1, sy1), sx0, sy0)

# ---------------------------------------------------------------- write
with open(OUT, "w") as fh:
    fh.write("// Made by tools/build_ui.py from art/ui/. Do not edit.\n#pragma once\n#include <stdint.h>\n\n")
    fh.write("struct UiSpriteDef { uint16_t w, h; int16_t x, y; uint32_t rawLen, zLen; const uint8_t* z; };\n")
    fh.write("enum UiSpriteId { " + ", ".join("UI_" + n for n, *_ in pieces) + ", UI_COUNT };\n\n")
    total = 0
    for n, s, x, y in pieces:
        # planes (alpha, then red, green, blue cut to the screen's 5-6-5 bits), then LZMA
        planes = [s[..., 3], s[..., 0] & 0xF8, s[..., 1] & 0xFC, s[..., 2] & 0xF8]
        z = lzma.compress(b"".join(np.ascontiguousarray(p).tobytes() for p in planes), format=lzma.FORMAT_RAW,
                          filters=[{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME, "lc": 3, "lp": 0, "pb": 2}])
        total += len(z)
        fh.write(f"static const uint8_t UI_Z_{n}[{len(z)}] = {{")
        fh.write(",".join(str(b) for b in z))
        fh.write("};\n")
    fh.write("\nstatic const UiSpriteDef UI_SPRITES[UI_COUNT] = {\n")
    for n, s, x, y in pieces:
        fh.write(f"  {{ {s.shape[1]}, {s.shape[0]}, {x}, {y}, {s.size}, sizeof UI_Z_{n}, UI_Z_{n} }},   // {n}\n")
    fh.write("};\n")
print("ui_data.h:", len(pieces), "pieces,", total // 1024, "KB")
for n, s, x, y in pieces: print(f"  {n:12s} {s.shape[1]:3d}x{s.shape[0]:3d} at {x},{y}")

# preview: every piece over black, at 2x
cv = Image.new("RGBA", (368, 448), (0, 0, 0, 255))
for n, s, x, y in pieces:
    un = s.astype(np.float32); a = un[..., 3:4]
    un[..., :3] = np.where(a > 0, un[..., :3] * 255 / np.maximum(a, 1), 0)
    cv.alpha_composite(Image.fromarray(un.clip(0, 255).astype(np.uint8), "RGBA"), (x, y))
cv.resize((736, 896), Image.NEAREST).save(os.path.join(HERE, "ui_preview.png"))
