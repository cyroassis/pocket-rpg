#include "../firmware/PocketRPG/ui_art.h"
#include "../firmware/PocketRPG/ui_gfx.h"
#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
static uint16_t fb[SCREEN_W*SCREEN_H];
int main(){
  gfxTarget(fb); fillRect(0,0,SCREEN_W,SCREEN_H,0x07060E);
  uiDrawBox(UI_K_TITLE, 8, 8, 150, 50);
  uiDrawBox(UI_K_PANEL, 8, 66, 352, 110);
  uiDrawBox(UI_K_PANEL_S, 8, 184, 170, 92);
  uiDrawBox(UI_K_PANEL_S, 190, 184, 170, 92);
  uiDrawBox(UI_K_BTN_DARK, 8, 290, 110, 56); uiDrawBox(UI_K_BTN_GOLD, 126, 290, 234, 56);
  uiDrawBox(UI_K_BTN_RED, 8, 354, 170, 56); uiDrawBox(UI_K_BTN_OFF, 190, 354, 170, 56);
  text(FONT_PXB24, "BACK", 63, 327, 0xFFF4DA); text(FONT_PXB24, "CRAFT", 243, 327, 0x1A1206);
  FILE* f=fopen("/tmp/kit.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){uint16_t c=fb[i];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);
}
