#include "../firmware/PocketRPG/app.h"
#include "../firmware/PocketRPG/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void platformSaveHero(const Hero&){} void platformEraseHero(){} void platformSaveSettings(const Settings&){}
void platformSaveGame(const Game&){} void platformApplyBrightness(uint8_t){} void platformRadio(bool){}
void platformRadioSend(const uint8_t*, const uint8_t*, int){} uint32_t platformRandom(uint32_t n){ return rand()%n; }
int platformBatteryLog(BatSample*, int){ return 0; }
void platformUpdate(int a){ fprintf(stderr,"platformUpdate %d\n",a); }
static uint16_t fb[SCREEN_W*SCREEN_H];
static void dump(const char* n){ FILE* f=fopen(n,"wb"); fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){uint16_t c=fb[i];unsigned char p[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(p,1,3,f);} fclose(f);}
static int k=0; static void shot(){ char n[16]; appDraw(1000); snprintf(n,16,"u%d.ppm",k++); dump(n); }
int main(){
  float* work=(float*)malloc(sizeof(float)*RENDER_WORK_FLOATS);
  Hero h; memset(&h,0,sizeof h); h.body=1; h.skin=1; h.hair=3; strcpy(h.name,"Arlo");
  Game g; gameReset(g); Settings s={1,1,1,0};
  appBegin(fb,work,&h,s,&g);
  appSettingsButton(); appDraw(1000); appSwipe(1); shot();          // settings page 2
  appTap(180,240); shot();                                           // tap Update -> checking
  appUpdateStatus(U_AVAILABLE,"Version 2 is ready","New capes and swords",-1); shot();
  appUpdateStatus(U_DOWNLOAD,"Downloading","Keep the board close to WiFi",42); shot();
  appUpdateStatus(U_SETUP,"PocketRPG-AB12","",-1); shot();
  appUpdateStatus(U_NO_WIFI,"No WiFi saved","Tap WiFi to add one",-1); shot();
  appUpdateStatus(U_LATEST,"You're up to date","Version 1 is the newest",-1); shot();
}
