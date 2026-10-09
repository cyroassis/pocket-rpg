#!/bin/bash
# Compiles the board's screens to WebAssembly (pocket.wasm) with clang and a WASI sysroot, then packs
# everything into one page: pocket-rpg.html. Set WASI to the folder with wasi-sysroot-24.0 and res/.
set -e
H=$(cd "$(dirname "$0")" && pwd); W=${WASI:?set WASI to the WASI SDK folder}; S=$H/../firmware/PocketRPG
clang++ --target=wasm32-wasi --sysroot=$W/wasi-sysroot-24.0 -resource-dir $W/res -O2 -fno-exceptions -fno-rtti -nostdlib++ \
  -mexec-model=reactor -I$S $H/web.cpp $S/app.cpp $S/game.cpp $S/names.cpp $S/lzma_dec.cpp $S/render.cpp $S/ui_gfx.cpp $S/ui_art.cpp \
  -Wl,--allow-undefined -Wl,--initial-memory=16777216 -Wl,--strip-all -o $H/pocket.wasm 2>&1 | grep -v "warning\|^ *[0-9]* |\|^ *|\|note:\|generated" || true
python3 $H/pack.py
