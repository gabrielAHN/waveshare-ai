#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static uint16_t pixels[SPARKLES_PIXELS];
static unsigned band(int x0,int x1){unsigned n=0;for(int y=150;y<250;y++)for(int x=x0;x<x1;x++)n+=sp_mask_at(x,y);return n;}
int main(void){
 home_ui s={0};s.page=SPARKLES;s.session_off=false;/* calm: touch only */
 home_sample(&s,1000000,false,0,0);
 home_sample(&s,1010000,true,40,200);
 home_sample(&s,1020000,true,320,200); /* a fast motion must be resampled */
 home_sample(&s,1120000,false,0,0);
 home_render(&s,pixels,SPARKLES_PIXELS);
 assert(band(20,80)>0);assert(band(100,150)>0);assert(band(180,230)>0);assert(band(280,340)>0);
 unsigned tail=band(20,80);
 home_sample(&s,1820000,false,0,0);home_render(&s,pixels,SPARKLES_PIXELS);assert(band(20,80)<tail);
 home_sample(&s,5000000,false,0,0);home_render(&s,pixels,SPARKLES_PIXELS);assert(band(0,368)==0);
 /* New down does not interpolate across the old release. */
 home_sample(&s,5010000,true,50,200);home_sample(&s,5100000,false,0,0);home_render(&s,pixels,SPARKLES_PIXELS);assert(band(240,368)==0);
 /* Bounded stress, invalid contact and cancellation all decay. */
 for(int i=0;i<2000;i++)home_sample(&s,5110000+i*10000,true,(i*53)%368,200);
 home_sample(&s,26000000,true,-1,200);assert(!s.input.down);
 home_sample(&s,30000000,false,0,0);home_render(&s,pixels,SPARKLES_PIXELS);assert(band(0,368)==0);
 puts("resampled anchored drag trail, release, re-touch and bounded stress: PASS");
}
