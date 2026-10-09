// UI art pieces (buttons, panels, the XP bar) made by tools/build_ui.py from art/ui/. Each piece remembers
// where it sits on the screen in the mockup (uiX/uiY), so screens can place it there or anywhere else.
#pragma once
#include "ui_data.h"

int uiW(int id);
int uiH(int id);
int uiX(int id);   // position in the mockup
int uiY(int id);
void uiDraw(int id, int x, int y);
// Panels that hold text of any length: wider than the art, the middle column repeats; narrower, the middle is left out.
void uiDrawWide(int id, int x, int y, int w);
// Repeats the piece's first column across w pixels (bar fills).
void uiDrawCols(int id, int x, int y, int w);
void uiFree(int id);   // drops the unpacked copy (big pieces shown once, like the splash)
// Kit pieces (K_...): drawn at any size; the corners stay, the middle row and column repeat.
void uiDrawBox(int id, int x, int y, int w, int h);
