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

# ---------------------------------------------------------------- craft screen (full-screen mockup, black background)
# The whole mockup is the background; the parts that change are wiped and drawn by the game. The two arrows
# beside the item are cut out as their own pieces (only the Blacksmith picks a weapon kind).
C = load("craft.png")
H_, W_ = C.shape[:2]
gold = (C[..., 0] > 150) & (C[..., 1] > 100) & (C[..., 2] < 110)
# the round frame around the item: its gold ring, inside the box between the arrows
ys, xs = np.where(gold[312:665, 378:745]); cy0, cx0 = 312 + (ys.min() + ys.max()) / 2, 378 + (xs.min() + xs.max()) / 2
# inner radius: walk up-right from the centre (the sample sword lies the other way) to the first gold pixel
r_in = 0
for r in range(20, 200):
    x, y = int(cx0 + r * 0.7071), int(cy0 - r * 0.7071)
    if gold[y, x]: r_in = r; break
# the glow behind it: median brightness by distance from the centre, to paint over removed parts
yy, xx = np.mgrid[0:H_, 0:W_]
dist = np.sqrt((yy - cy0) ** 2 + (xx - cx0) ** 2).astype(int)
prof = np.zeros((dist.max() + 1, 4), np.float32)
bgish = lum(C) < 40   # only the background itself (not the frames and text) decides the glow
for r in range(0, dist.max() + 1, 1):
    m = (dist == r) & bgish
    if m.any(): prof[r] = np.median(C[m], axis=0)
    else: prof[r] = (0, 0, 0, 255)
def erase(x0, y0, x1, y1):
    C[y0:y1, x0:x1] = prof[dist[y0:y1, x0:x1]]
def fill_rows(x0, y0, x1, y1, sample):
    """Each row gets the colour the same row has beside the text (sample: x ranges clear of the text)."""
    for y in range(y0, y1):
        pick = np.concatenate([C[y, a:b] for a, b in sample])
        C[y, x0:x1] = np.median(pick, axis=0)
arrow_l = C[421:582, 227:342].copy(); arrow_r = C[421:582, 784:900].copy()
erase(227, 421, 342, 582); erase(784, 421, 900, 582)
erase(90, 245, 560, 302)        # "FORGE WEAPONS"
erase(820, 80, 1070, 305)       # the corner: shards and the weapon kind
erase(320, 980, 820, 1045)      # "LUCK X1 + 2 XP"
# inside the round frame
inside = dist <= r_in - 4
for y in range(int(cy0 - r_in), int(cy0 + r_in) + 1):
    m = inside[y]
    if not m.any(): continue
    row = C[y, m]; d = row[lum(row) < 45]
    if len(d): C[y, m] = np.median(d, axis=0)
fill_rows(240, 715, 900, 920, [(240, 395), (740, 900)])        # the number and the word in the shards panel
fill_rows(135, 1120, 415, 1220, [(100, 132), (418, 450)])       # BACK
fill_rows(620, 1120, 980, 1225, [(570, 615), (985, 1030)])      # CRAFT (gold)
add("CRAFT_BG", C, 0, 0)
add("CRAFT_ARROW_L", arrow_l, 227, 421)
add("CRAFT_ARROW_R", arrow_r, 784, 900 - 900 + 421)
craft_geo = dict(cx=cx0 * SCALE, cy=cy0 * SCALE, r=r_in * SCALE)
print("craft frame:", {k: round(v, 1) for k, v in craft_geo.items()})

# ---------------------------------------------------------------- icons (one picture each, cropped and shrunk to a fixed height)
def icon(name, file, height):
    a = np.array(Image.open(os.path.join(SRC, "icons", file)).convert("RGBA")).astype(np.float32)
    ys, xs = np.where(a[..., 3] > 20)
    a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    pieces.append((name, shrink(a, height / a.shape[0]), 0, 0))
icon("SHARD", "icon_shard.png", 30)

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
    fh.write(f"#define UI_CRAFT_CX {craft_geo['cx']:.1f}f\n#define UI_CRAFT_CY {craft_geo['cy']:.1f}f\n#define UI_CRAFT_R {craft_geo['r']:.1f}f\n")
print("ui_data.h:", len(pieces), "pieces,", total // 1024, "KB")
for n, s, x, y in pieces: print(f"  {n:12s} {s.shape[1]:3d}x{s.shape[0]:3d} at {x},{y}")

# preview: every piece over black, at 2x (the craft screen on its own)
def preview(names, out):
  cv = Image.new("RGBA", (368, 448), (0, 0, 0, 255))
  for n, s, x, y in pieces:
    if n not in names: continue
    un = s.astype(np.float32); a = un[..., 3:4]
    un[..., :3] = np.where(a > 0, un[..., :3] * 255 / np.maximum(a, 1), 0)
    cv.alpha_composite(Image.fromarray(un.clip(0, 255).astype(np.uint8), "RGBA"), (x, y))
  cv.resize((736, 896), Image.NEAREST).save(os.path.join(HERE, out))
preview([n for n, *_ in pieces if not n.startswith(("CRAFT", "SPLASH"))], "ui_preview.png")
preview(["CRAFT_BG", "CRAFT_ARROW_L", "CRAFT_ARROW_R"], "ui_preview_craft.png")
