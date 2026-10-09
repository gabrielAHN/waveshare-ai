#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static uint16_t pixels[SPARKLES_PIXELS];
int main(void){
 home_ui s={0};s.page=SPARKLES;s.session_off=false;/* calm: touch only */
 home_sample(&s,1000000,false,0,0);
 home_sample(&s,1010000,true,40,200);
 home_sample(&s,1020000,true,320,200);
 unsigned above=0,below=0;float miny=999,maxy=-999;
 for(unsigned i=0;i<s.input.scene.trail_count;i++){
  sp_trail_particle p=s.input.scene.trail[i];
  above+=p.y<194;below+=p.y>206;
  miny=fminf(miny,p.y);maxy=fmaxf(maxy,p.y);
  assert(fabsf(p.y-200)<=26);
 }
 assert(above>=4&&below>=4&&maxy-miny>25); /* not a collinear dotted line */
 home_ui hold={0};hold.page=SPARKLES;hold.session_off=false;
 home_sample(&hold,1000000,false,0,0);
 float last=-1,min_gap=99,max_gap=0;unsigned births=0;
 for(int i=1;i<=300;i++){
  unsigned before=hold.input.scene.trail_next;
  home_sample(&hold,1000000+i*10000,true,180,200);
  if(before!=hold.input.scene.trail_next){
   float t=hold.input.scene.time;
   if(last>=0){min_gap=fminf(min_gap,t-last);max_gap=fmaxf(max_gap,t-last);}
   last=t;++births;
  }
 }
 assert(births>=20&&births<=50&&max_gap-min_gap>.025f);
 home_render(&hold,pixels,SPARKLES_PIXELS);
 unsigned energy=0;for(int i=0;i<SP_MASK_PIXELS;i++)energy+=sp_mask[i];assert(energy);
 assert(home_live_points(&hold)==0&&home_live_density(&hold)==0);
 puts("organic noncollinear moving births, bounded varied continuous hold, OFF touch independence: PASS");
}
