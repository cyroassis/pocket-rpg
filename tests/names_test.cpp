#include "../firmware/PocketRPG/names.h"
#include "../firmware/PocketRPG/app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t platformRandom(uint32_t n){ return rand()%n; }
int main(){ srand(3); char seen[20000][12]; int u=0; int two=0;
  for(int i=0;i<20000;i++){ char n[11]; rollName(n,11,nullptr); int d=0; for(int k=0;k<u;k++) if(!strcmp(seen[k],n)){d=1;break;} if(!d) strcpy(seen[u++],n); if(i<60) printf("%s%s",n,i%12==11?"\n":", "); }
  printf("\nunique in 20000 rolls: %d\n",u);
  const char* t[]={"Fuckra","Cassia","Shitel","Arlo","Nigel","Titania"}; for(auto x:t) printf("%s blocked=%d\n",x,nameBlocked(x));
}
