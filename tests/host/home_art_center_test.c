#include <assert.h>
#include "home_render.h"
static uint16_t p[SPARKLES_PIXELS];
int main(void){home_ui s=(home_ui){.connected=true,.saved=true,.pair={.state=PAIR_ENROLLED_UNPAIRED,.live_http=200,.live_ok=true},.phone={.st={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED}},
 .phone_ha={.st={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED}}};  /* the plain (enabled) tile art (Sensor: its own sign-in); no-Wi-Fi / loading tiles are grey */
 for(int tile=0;tile<HOME_TILES;tile++){s.tile=tile;assert(home_render(&s,p,SPARKLES_PIXELS));
  for(int y=388;y<420;y++)for(int x=24;x<344;x++)assert(p[y*368+x]==p[y*368+24]);
 }
 s.tile=1;home_render(&s,p,SPARKLES_PIXELS);
 /* The Ask tile's art is one Kotaro, lopsided by his tail (more ink on the head side): centre him
  * between his ink mass and his outline -- each within 2.5 px of the tile centre 183.5. */
 double mass=0,moment=0;int lo=368,hi=-1;
 for(int y=90;y<320;y++)for(int x=24;x<344;x++)if(p[y*368+x]!=p[0]){
  mass++;moment+=x;if(x<lo)lo=x;if(x>hi)hi=x;
 }
 assert(mass>0&&fabs(moment/mass-183.5)<2.5&&abs(lo+hi-367)<=5);
 return 0;}
