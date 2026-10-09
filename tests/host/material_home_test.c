#include <assert.h>
#include "home_render.h"
static uint16_t p[SPARKLES_PIXELS];
int main(void){
 home_ui s={0};
 for(int tile=0;tile<HOME_TILES;tile++){
  s.tile=tile;assert(home_render(&s,p,SPARKLES_PIXELS));
  uint16_t bg=p[0];int minx=368,maxx=-1,miny=448,maxy=-1;
  for(int y=60;y<320;y++)for(int x=0;x<368;x++)if(p[y*368+x]!=bg){
   if(x<minx)minx=x;
   if(x>maxx)maxx=x;
   if(y<miny)miny=y;
   if(y>maxy)maxy=y;
  }
  assert(maxx>=minx&&minx+maxx>=366&&minx+maxx<=368);
  assert(minx>=88&&maxx<280&&miny>=108&&maxy<300);
 }
 return 0;
}
