#include "../firmware/PocketRPG/app.h"
#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void platformSaveHero(const Hero&){} void platformEraseHero(){} void platformSaveSettings(const Settings&){}
void platformSaveGame(const Game&){} void platformApplyBrightness(uint8_t){} void platformRadio(bool){}
void platformRadioSend(const uint8_t*, const uint8_t*, int){} uint32_t platformRandom(uint32_t n){ return rand()%n; }
void platformUpdate(int){}
static uint16_t fb[SCREEN_W*SCREEN_H];
static void dump(const char* n){ FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){uint16_t c=fb[i];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);}
int main(){
  srand(3);
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  Hero h; memset(&h,0,sizeof h); h.body=1; h.skin=1; h.hair=3; h.prof=0; strcpy(h.name,"Arlo");
  Game g; gameReset(g); g.day=20261007; g.xp=900; g.mats=10000;
  g.bag[0]={IT_WEAPON,3,(uint8_t)(1|1<<4)}; g.bag[1]={IT_CAPE,12,3};
  Settings s={1,1,1,0};
  appBegin(fb,work,&h,s,&g); appSetSteps(0,20261007);
  uint32_t t=1000;
  appDraw(t); appTap(50,150); appDraw(t+=100); appTap(259,380); for(int i=0;i<30;i++){ appTick(t+=100);} appDraw(t); dump("k0.ppm");
  appTap(80,370); appDraw(t+=100); appTap(87,380); appDraw(t+=100); appTap(50,380); appDraw(t+=100);   // ok, back home, bag
  appTap(52,118); appDraw(t+=100); dump("k1.ppm");
  appTap(60,370); appDraw(t+=100); appTap(140,118); appDraw(t+=100); appTap(183,370); appDraw(t+=100); dump("k2.ppm");
}
