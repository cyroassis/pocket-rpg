#!/usr/bin/env python3
"""Packs shell.html + web.js + pocket.wasm (base64) into one page: pocket-rpg.html."""
import base64, os
H = os.path.dirname(os.path.abspath(__file__))
r = lambda p: open(os.path.join(H, p)).read()
wasm = base64.b64encode(open(os.path.join(H, "pocket.wasm"), "rb").read()).decode()
out = r("shell.html").replace("__JS__", r("web.js"), 1).replace('"__WASM__"', '"' + wasm + '"', 1)
open(os.path.join(H, "pocket-rpg.html"), "w").write(out)
print("pocket-rpg.html", len(out) // 1024, "KB")
