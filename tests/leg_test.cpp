#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t fb[SCREEN_W*SCREEN_H];
uint32_t platformRandom(uint32_t n){ return rand()%n; }
int main(){
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  static unsigned char img[100][3*80][3];
  for(int v=0;v<3;v++){
    Look l; memset(&l,0,sizeof l); l.body=BODY; l.skin=1; l.hair=3; l.armor=v==2?0:-1; l.armorTier=12; l.weapon=v==0?-1:0; l.weaponTier=12; l.cape=-1; l.capeTier=1;
    renderCharacterLayers(l, work);
    for(int i=0;i<SCREEN_W*SCREEN_H;i++) fb[i]=0x18C5;
    blitCharacter(work, fb, 0,0,SCREEN_W,SCREEN_H, 0,0,SCREEN_W,SCREEN_H, 0,SCREEN_H, 0, 0);
    for(int y=0;y<100;y++) for(int x=0;x<80;x++){ uint16_t c=fb[(y+260)*SCREEN_W + x+80]; img[y][v*80+x][0]=(c>>11)<<3; img[y][v*80+x][1]=((c>>5)&63)<<2; img[y][v*80+x][2]=(c&31)<<3; }
  }
  FILE* f=fopen("/tmp/legs.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",240,100); fwrite(img,1,sizeof img,f); fclose(f);
}
