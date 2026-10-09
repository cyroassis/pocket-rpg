#!/usr/bin/env python3
"""Pocket RPG art converter.

Turns the 368x448 PNG layers in art/ into firmware/PocketRPG/art_data.h, using the same rules as
the Character Creator page, so the board draws exactly what the page shows.

Usage:  python3 tools/build_art.py        (needs: pip install pillow numpy)

File names (same as the creator page):
  body_male.png, body_female.png
  hair_<name>_front.png  (and hair_<name>_back.png for long styles); girl styles: hair_<name>_female_front.png
  armor_leather.png, armor_chainmail.png, armor_iron.png, armor_titanium.png
  cape_<name>_back.png   (behind the body; one per rarity band: traveler, ranger, knight, royal)
  cape_<name>_front.png  (shoulder part, drawn over the armor)
  weapon_<name>.png      (gem socket = solid pure-white circle, hand in skin tones)
  gem_fire.png, gem_nature.png, gem_water.png, gem_neutral.png

Per pixel the board keeps: group (0 clear, 1 fixed color, 2 skin, 3 group A, 4 group B, 5 gem socket),
opacity 0..15, and either the original color (groups 1-2) or a brightness 0..255 (groups 3-4).
"""
import math
import lzma
import os
import sys

import numpy as np
from PIL import Image

W, H = 368, 448
HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "art")
OUT = os.path.join(HERE, "..", "firmware", "PocketRPG", "art_data.h")


def load(path):
    im = Image.open(path).convert("RGBA")
    if im.size != (W, H):
        if abs(im.width / im.height - 2 / 3) < 0.02:   # older 288x432 art: placed like the 90% bodies
            c = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            c.alpha_composite(im.resize((259, 389), Image.LANCZOS), (54, 58))
            im = c
        else:
            im = im.resize((W, H), Image.LANCZOS)
    return np.array(im).astype(np.int32)


def bleed(a):
    """Semi-transparent edge pixels take the color of the nearest solid pixel (removes white halos)."""
    out = a.copy()
    al = a[..., 3]
    ys, xs = np.where((al > 8) & (al < 200))
    for y, x in zip(ys, xs):
        best, bd = None, 99
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                yy, xx = y + dy, x + dx
                if 0 <= yy < H and 0 <= xx < W and al[yy, xx] >= 200:
                    d = dx * dx + dy * dy
                    if d < bd:
                        bd, best = d, (yy, xx)
        if best:
            out[y, x, :3] = a[best[0], best[1], :3]
    return out


def keep_socket(k):
    """Weapons: the gem socket is the round pure-white spot (filled, about as wide as tall). Other white
    pixels (shine on the blade) go back to the metal so they take the material color."""
    from collections import deque
    m = k == 5
    seen = np.zeros_like(m)
    best, best_pts = -1, None
    for y, x in zip(*np.where(m)):
        if seen[y, x]:
            continue
        q = deque([(y, x)]); seen[y, x] = True; pts = []
        while q:
            cy, cx = q.popleft(); pts.append((cy, cx))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                yy, xx = cy + dy, cx + dx
                if 0 <= yy < H and 0 <= xx < W and m[yy, xx] and not seen[yy, xx]:
                    seen[yy, xx] = True; q.append((yy, xx))
        ys = [p[0] for p in pts]; xs = [p[1] for p in pts]
        w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        round_ = len(pts) / (w * h) >= 0.55 and max(w, h) <= 1.4 * min(w, h)
        if round_ and len(pts) > best:
            best, best_pts = len(pts), pts
    k[m] = 3
    if best_pts:
        for y, x in best_pts:
            k[y, x] = 5


def keep_hand(k):
    """Weapons: the hand is the biggest patch of skin; stray skin-colored pixels elsewhere go back to the metal."""
    from collections import deque
    m = k == 2
    seen = np.zeros_like(m)
    best_pts = []
    for y, x in zip(*np.where(m)):
        if seen[y, x]:
            continue
        q = deque([(y, x)]); seen[y, x] = True; pts = []
        while q:
            cy, cx = q.popleft(); pts.append((cy, cx))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                yy, xx = cy + dy, cx + dx
                if 0 <= yy < H and 0 <= xx < W and m[yy, xx] and not seen[yy, xx]:
                    seen[yy, xx] = True; q.append((yy, xx))
        if len(pts) > len(best_pts):
            best_pts = pts
    k[m] = 3
    for y, x in best_pts:
        k[y, x] = 2


def socket_disc(a, cx, cy):
    """The whole round hole, not just its white highlight: grow from the white spot over light pixels until the
    dark outline (at most 22 px away). Its center and size place the gem; the gem is drawn 1 px wider."""
    from collections import deque
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
    sx, sy = int(round(cx)), int(round(cy))
    seen = np.zeros((H, W), bool); seen[sy, sx] = True
    q = deque([(sy, sx)]); pts = []
    while q:
        y, x = q.popleft(); pts.append((y, x))
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            yy, xx = y + dy, x + dx
            if abs(yy - sy) > 22 or abs(xx - sx) > 22 or not (0 <= yy < H and 0 <= xx < W) or seen[yy, xx]:
                continue
            if a[yy, xx, 3] > 128 and lum[yy, xx] > 110:
                seen[yy, xx] = True; q.append((yy, xx))
    ys = np.array([p[0] for p in pts]); xs = np.array([p[1] for p in pts])
    return (float(xs.mean()), float(ys.mean()), max(8, int(round(2 * math.sqrt(len(pts) / math.pi))) + 1))


def classify(a, kind):
    a = bleed(a)
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    yy = np.arange(H)[:, None].repeat(W, 1)
    head_rows = int(H * 0.41)
    k = np.zeros((H, W), np.int32)
    done = al < 9
    def put(mask, v):
        nonlocal done
        m = mask & ~done
        k[m] = v[m] if isinstance(v, np.ndarray) else v
        done = done | m
    if kind == "body":
        put(lum < 62, 1)
    put(mn > 228, 5 if kind in ("body", "weapon") else 3)
    put((g > r + 40) & (g > b + 40), 4)
    put(mx - mn < 22, np.where((kind == "body") & (yy >= head_rows), 4, 3) if kind == "body" else 3)
    if kind in ("body", "weapon"):
        put((r > g) & (g >= b - 10) & (r - b > 25), 2)
    if kind == "armor":  # bare skin showing through armor (light skin tones only, so brown straps stay as drawn)
        put((r > 170) & (g > 110) & (r - b > 40) & (r > g) & (g >= b - 10), 2)
    put(np.ones_like(done), 3 if kind == "hair" else 6)
    k[al < 9] = 0
    if kind == "weapon":
        keep_socket(k)
        keep_hand(k)

    green = (g > r + 40) & (g > b + 40)
    val = np.where(green, g, lum)
    grp = np.zeros((H, W), np.uint8)
    fixed = np.zeros((H, W), np.uint32)
    shade = np.zeros((H, W), np.uint8)
    rgb = (r.astype(np.uint32) << 16) | (g.astype(np.uint32) << 8) | b.astype(np.uint32)
    mids = {3: 128, 4: 128}
    for kk in (3, 4):
        m = k == kk
        if m.any():
            v = np.sort(val[m])
            mids[kk] = int(round(float(v[len(v) >> 1])))
    sel_fixed = (k == 1) | (k == 6) | ((k == 5) & (kind != "weapon"))
    grp[sel_fixed] = 1
    fixed[sel_fixed] = rgb[sel_fixed]
    grp[(k == 5) & (kind == "weapon")] = 5
    grp[k == 2] = 2
    fixed[k == 2] = rgb[k == 2]
    for kk in (3, 4):
        m = k == kk
        grp[m] = kk
        shade[m] = np.clip(np.round(val[m]), 0, 255).astype(np.uint8)
    alpha = np.where(grp > 0, np.maximum(1, np.round(al / 17)), 0).astype(np.uint8)
    L = {"g": grp, "a": alpha, "f": fixed, "s": shade, "mid": mids}

    if kind == "body":
        L["hand"] = find_hand(grp)
        sk = fixed[(grp == 2) & (alpha >= 14)]
        if len(sk) > 50:
            ch = lambda s: int(np.sort((sk >> s) & 255)[len(sk) >> 1])
            L["skinBase"] = (ch(16) << 16) | (ch(8) << 8) | ch(0)
        else:
            L["skinBase"] = 0xF2C39B
    if kind == "weapon":
        ys = np.where((grp == 2).any(1))[0]
        L["handTop"] = int(ys[0]) if len(ys) else H
        ys, xs = np.where(grp == 5)
        if len(ys) > 4:
            L["socket"] = socket_disc(a, float(xs.mean()), float(ys.mean()))
    return L


def find_hand(grp):
    filled = grp != 0
    def first_run(y):
        row = filled[y, : W // 2]
        xs = np.where(row)[0]
        if not len(xs):
            return None
        x = xs[0]
        e = x
        while e < W // 2 and row[e]:
            e += 1
        return (x, e - 1)
    palm, widest = -1, 0
    for y in range(round(H * 0.62), round(H * 0.8)):
        r = first_run(y)
        if r and r[1] - r[0] + 1 > widest:
            widest, palm = r[1] - r[0] + 1, y
    wrist, best = -1, 1e9
    for y in range(round(H * 0.58), palm):
        r = first_run(y)
        if r and r[1] - r[0] + 1 <= best:
            best, wrist = r[1] - r[0] + 1, y
    mask = np.zeros((H, W), bool)
    if wrist < 0:
        return {"wrist": H, "mask": mask}
    top = max(0, wrist - 50)
    right = 0
    for y in range(top, wrist + 1):
        r = first_run(y)
        if r and r[1] > right:
            right = r[1]
    seed = first_run(wrist)
    if seed:
        stack = [((seed[0] + seed[1]) // 2, wrist)]
        while stack:
            x, y = stack.pop()
            if x < 0 or y < top or x > right + 3 or y >= H or mask[y, x] or not filled[y, x]:
                continue
            mask[y, x] = True
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return {"wrist": wrist, "mask": mask}


def seal_armor(L, body):
    """Hide slivers of leg showing past the armor's edge: body pixels below the hands that sit within 3 px
    of the armor take the color of the nearest armor pixel, so the outline closes over them."""
    g, a = L["g"], L["a"]
    arm = (g > 0) & (a >= 8)
    fix = (body["g"] > 0) & ~arm
    fix[:296, :] = False
    xs = np.arange(W)[None, :].repeat(H, 0)
    ys = np.arange(H)[:, None].repeat(W, 1)
    fix &= ~((ys < 345) & ((xs < 135) | (xs > 233)))   # keep the hands
    count = 0
    for y, x in zip(*np.where(fix)):
        best, bd = None, 99
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                d = dx * dx + dy * dy
                yy, xx = y + dy, x + dx
                if d <= 9 and d < bd and 0 <= yy < H and 0 <= xx < W and arm[yy, xx]:
                    bd, best = d, (yy, xx)
        if best:
            for k in ("g", "f", "s"):
                L[k][y, x] = L[k][best]
            L["a"][y, x] = 15
            count += 1
    return count


def rle(L):
    """Run-length records of 5 bytes: count, group<<4|opacity, then 3 value bytes."""
    g, a, f, s = L["g"].ravel(), L["a"].ravel(), L["f"].ravel(), L["s"].ravel()
    vals = np.where(g == 0, 0, np.where((g == 1) | (g == 2), f, s.astype(np.uint32)))
    info = (g.astype(np.uint32) << 4) | a
    out = bytearray()
    i, n = 0, len(g)
    key = (info << 24) | vals
    while i < n:
        j = i + 1
        while j < n and key[j] == key[i] and j - i < 255:
            j += 1
        v = int(vals[i])
        out += bytes((j - i, int(info[i]), (v >> 16) & 255, (v >> 8) & 255, v & 255))
        i = j
    return bytes(out)


def pack(raw):
    """Stored as 5 planes (all counts, all group/opacity bytes, then the 3 value bytes), colours cut to the
    screen's 5-6-5 bits, then LZMA (the board unpacks it with lzma_dec.cpp and puts the records back together)."""
    r = np.frombuffer(raw, np.uint8).reshape(-1, 5).copy()
    colour = (r[:, 1] >> 4) <= 2   # groups 1-2 keep a colour; 3-4 a brightness (kept whole)
    for i, m in ((2, 0xF8), (3, 0xFC), (4, 0xF8)):
        r[colour, i] &= m
    planar = b"".join(r[:, i].tobytes() for i in range(5))
    return lzma.compress(planar, format=lzma.FORMAT_RAW,
                         filters=[{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME, "lc": 3, "lp": 0, "pb": 2}])


def c_bytes(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def ident(s):
    return "".join(ch if ch.isalnum() else "_" for ch in s).upper()


def main():
    # girl hairstyles after the boy ones, so saved heroes keep their hair index when styles are added
    files = sorted(os.listdir(ART), key=lambda f: (f.startswith("hair_") and "_female" in f, f))
    parts, entries = [], {"body": [], "hair": [], "armor": [], "weapon": [], "cape": []}
    total = 0
    hair_female = []   # 1 = girl hairstyle (hair_<name>_female_front.png), 0 = boy hairstyle
    for fn in files:
        if not fn.endswith(".png") or fn.startswith("gem_"):
            continue
        stem = fn[:-4]
        kind = stem.split("_")[0]
        if kind not in entries:
            continue
        L = classify(load(os.path.join(ART, fn)), kind)
        if kind == "armor":
            bodyfile = "body_female.png" if stem.endswith("_female") else "body_male.png"
            if os.path.exists(os.path.join(ART, bodyfile)):
                seal_armor(L, classify(load(os.path.join(ART, bodyfile)), "body"))
        raw = rle(L)
        data = pack(raw)
        total += len(data)
        name = ident(stem)
        parts.append(c_bytes(f"RLE_{name}", data))
        extra = ""
        if kind == "body":
            rows = []
            m = L["hand"]["mask"]
            for y in range(H):
                xs = np.where(m[y])[0]
                rows += [int(xs[0]), int(xs[-1])] if len(xs) else [-1, -1]
            parts.append(f"static const int16_t HAND_{name}[{H * 2}] = {{" + ",".join(map(str, rows)) + "};")
            extra = f", HAND_{name}, {L['hand']['wrist']}, 0x{L['skinBase']:06X}"
        else:
            extra = ", nullptr, 0, 0"
        sock = L.get("socket", (0, 0, 0))
        hand_top = L.get("handTop", H)
        label = stem.split("_", 1)[1] if "_" in stem else stem
        if kind == "cape" and label.endswith("_back"):
            label = label[:-len("_back")]   # cape_<name>_back.png behind the body, cape_<name>_front.png ("<Name> Front") over the armor
        if kind == "hair":
            label = label.rsplit("_", 1)[0]
            female = label.endswith("_female")
            if female:
                label = label[:-len("_female")]
            hair_female.append(1 if female else 0)
        entries[kind].append(
            f'  {{ "{label.replace("-", " ").replace("_", " ").title()}", RLE_{name}, sizeof(RLE_{name}), {L["mid"][3]}, {L["mid"][4]}{extra}, {hand_top}, {sock[0]:.1f}f, {sock[1]:.1f}f, {sock[2]}, {len(raw)} }},')
    # gems, pre-scaled to each weapon's socket (all weapons share one size for now: the first weapon's)
    gem_parts = []
    sockets = []
    for fn in files:
        if fn.startswith("weapon_"):
            L = classify(load(os.path.join(ART, fn)), "weapon")
            if "socket" in L:
                sockets.append(L["socket"][2])
    d = 32   # gems are stored at 32 px and scaled to each weapon's own socket when drawn
    for el in ("neutral", "fire", "nature", "water"):
        p = os.path.join(ART, f"gem_{el}.png")
        if os.path.exists(p):
            g = Image.open(p).convert("RGBA").resize((d, d), Image.LANCZOS)
            gem_parts.append(c_bytes(f"GEM_{el.upper()}", np.array(g).astype(np.uint8).ravel().tolist()))
        else:
            gem_parts.append(f"static const uint8_t GEM_{el.upper()}[{d * d * 4}] = {{0}};")

    with open(OUT, "w") as fh:
        fh.write("// Generated by tools/build_art.py from tools/art/*.png. Do not edit by hand.\n#pragma once\n#include <stdint.h>\n#include \"render.h\"\n\n")
        fh.write("\n".join(parts) + "\n\n" + "\n".join(gem_parts) + "\n\n")
        for kind, var in (("body", "BODIES"), ("hair", "HAIRS"), ("armor", "ARMORS"), ("weapon", "WEAPONS"), ("cape", "CAPES")):
            fh.write(f"static const LayerDef {var}[] = {{\n" + "\n".join(entries[kind]) + "\n};\n")
            fh.write(f"static const int {var}_COUNT = {len(entries[kind])};\n")
        fh.write("static const uint8_t HAIRS_FEMALE[] = {" + ",".join(map(str, hair_female or [0])) + "};\n")
        fh.write(f"static const int GEM_SIZE = {d};\nstatic const uint8_t* const GEMS[4] = {{ GEM_NEUTRAL, GEM_FIRE, GEM_NATURE, GEM_WATER }};\n")
    print(f"wrote {OUT}: {sum(len(v) for v in entries.values())} layers, {total / 1024:.0f} KB of compressed layer data, gem size {d}px")


if __name__ == "__main__":
    sys.exit(main())
