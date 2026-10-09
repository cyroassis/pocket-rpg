#include "../firmware/PocketRPG/app.h"
#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void platformSaveHero(const Hero&){} void platformEraseHero(){} void platformSaveSettings(const Settings&){}
void platformSaveGame(const Game&){} void platformApplyBrightness(uint8_t){} void platformRadio(bool){}
void platformRadioSend(const uint8_t*, const uint8_t*, int){} uint32_t platformRandom(uint32_t n){ return rand()%n; }
int platformBatteryLog(BatSample*, int){ return 0; }
int platformRadioChannel(){ return 1; }
void platformUpdate(int){}
static uint16_t fb[SCREEN_W*SCREEN_H];
static void dump(const char* n){ FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){uint16_t c=fb[i];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);}
int main(int argc, char** argv){
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  const char* names[3]={"Arlo","Bartholome","Kai"};
  for(int v=0; v<3; v++){
    Hero h; memset(&h,0,sizeof h); h.body=v==1?0:1; h.skin=1; h.hair=3; h.prof=v; strcpy(h.name,names[v]);
    Game g; gameReset(g); g.day=20261007; g.xp=v==0?560:v==1?40:9000;
    g.gear[IT_ARMOR]={IT_ARMOR,4,3}; g.gear[IT_WEAPON]={IT_WEAPON,3,(uint8_t)(1|1<<4)};
    Settings s={1,1,1,0};
    appBegin(fb,work,&h,s,&g);
    appSetSteps(0,20261007); appSetSteps(v==2?12840:4120,20261007);
    appDraw(1000); appTap(50,150); appDraw(1100); if(v==0){appTap(100,150); appDraw(1200);} if(v==2) g.mats=3; char n[16]; snprintf(n,16,"c%d.ppm",v); dump(n);
  }
}
