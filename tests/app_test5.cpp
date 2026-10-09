#include "../firmware/PocketRPG/app.h"
#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void platformSaveHero(const Hero&){}
void platformEraseHero(){}
void platformSaveSettings(const Settings&){}
void platformSaveGame(const Game& g){ fprintf(stderr,"save game: xp %u mats %u finds %u bag %d\n",g.xp,g.mats,g.finds,bagCount(g)); }
void platformApplyBrightness(uint8_t){}
void platformRadio(bool){}
int platformBatteryLog(BatSample*, int){ return 0; }
void platformUpdate(int){}
void platformRadioSend(const uint8_t*, const uint8_t*, int){}
uint32_t platformRandom(uint32_t n){ return rand()%n; }
static uint16_t fb[SCREEN_W*SCREEN_H];
static void dump(const char* n){ FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){uint16_t c=fb[i];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);}
static uint32_t t=1000; static int k=0;
static void shot(){ char n[16]; appTick(t); appDraw(t); snprintf(n,16,"g%d.ppm",k++); dump(n); }
static void tap(int x,int y){ appDraw(t); appTap(x,y); t+=300; appTick(t); appDraw(t); }
int main(int argc,char**argv){
  srand(argc>1?atoi(argv[1]):7);
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  Hero h; memset(&h,0,sizeof h); h.body=1; h.skin=1; h.eye=0; h.hair=3; h.hairColor=0; h.prof=0; strcpy(h.name,"Arlo");
  Game g; gameReset(g); g.day=20261007; g.xp=560; g.mats=12;
  g.bag[0]={IT_ARMOR,2,1}; g.bag[1]={IT_WEAPON,7,4}; g.bag[2]={IT_CAPE,1,1}; g.bag[5]={IT_WEAPON,12,10};
  g.gear[IT_ARMOR]={IT_ARMOR,4,3};
  Settings s={1,1,2,0};
  appBegin(fb,work,&h,s,&g);
  appSetSteps(0,20261007);
  appSetSteps(4100,20261007);          // 20 finds
  shot();                              // g0 home with banner
  tap(54,140); tap(300,136); tap(300,136); shot();   // g1 craft (mace)
  tap(300,246); tap(300,246); shot();  // g2 craft 15
  tap(250,383); t+=700; shot();        // g3 crafting
  t+=1000; appTick(t); shot();         // g4 result
  tap(60,383); tap(60,383);            // ok, back home
  tap(54,238); shot();                 // g5 explore
  tap(184,383); tap(54,336); shot();   // g6 bag
  tap(56,190); shot();                 // g7 item card (slot 1)
  tap(174,383); shot();                // g8 salvage armed
  tap(60,383); tap(60,383);            // back, home
  tap(250,200); shot();                // g9 gear
  tap(184,190); shot();                // g10 armor card
}
