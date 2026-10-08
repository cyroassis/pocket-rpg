#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t platformRandom(uint32_t n){ return rand()%n; }
static uint16_t fb[SCREEN_W*SCREEN_H];
int main(){
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  int tiers[8]={2,4,8,9,13,14,18,20};
  for(int i=0;i<8;i++){
    Look l; memset(&l,0,sizeof l); l.body=1; l.skin=1; l.hair=3; l.cape=-1;
    l.armor=0; l.armorTier=tiers[i]; l.weapon=i%3; l.weaponTier=tiers[i];
    renderCharacter(l,fb,work);
    char n[16]; snprintf(n,16,"wt%d.ppm",i); FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
    for(int k=0;k<SCREEN_W*SCREEN_H;k++){uint16_t c=fb[k];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);
  }
}
