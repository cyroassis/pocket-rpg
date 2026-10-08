#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t platformRandom(uint32_t n){ return rand()%n; }
static uint16_t fb[SCREEN_W*SCREEN_H];
int main(){
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  int tiers[4]={4,9,13,18};
  for(int i=0;i<4;i++){
    Look l; memset(&l,0,sizeof l); l.body=i%2; l.skin=1+i; l.eye=i; l.hair=i%2? 7:3; l.hairColor=i*2;
    l.armor=0; l.armorTier=tiers[i]; l.weapon=0; l.weaponTier=tiers[i]; l.cape=0; l.capeTier=tiers[i];
    renderCharacter(l,fb,work);
    char n[16]; snprintf(n,16,"cape%d.ppm",i); FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
    for(int k=0;k<SCREEN_W*SCREEN_H;k++){uint16_t c=fb[k];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);
  }
}
