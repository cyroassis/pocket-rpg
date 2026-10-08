#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t platformRandom(uint32_t n){ return rand()%n; }
static uint16_t fb[SCREEN_W*SCREEN_H];
int main(){
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  int tiers[5]={1,3,8,13,18};
  for(int i=0;i<5;i++){
    Look l; memset(&l,0,sizeof l); l.body=0; l.skin=i%6; l.eye=i; l.hair=5+i; l.hairColor=i*2;
    l.armor=i? 0:-1; l.armorTier=tiers[i]; l.weapon=i? (i%3):-1; l.weaponTier=tiers[i]; l.cape=i>1?0:-1; l.capeTier=tiers[i]; l.element=i%4;
    renderCharacter(l,fb,work);
    char n[16]; snprintf(n,16,"gl%d.ppm",i); FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
    for(int k=0;k<SCREEN_W*SCREEN_H;k++){uint16_t c=fb[k];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);
  }
}
