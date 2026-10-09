#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
static uint16_t pixels[SPARKLES_PIXELS];
static unsigned char mask[SP_MASK_PIXELS];
int main(void){
 home_ui s={0};s.page=SPARKLES;s.session_off=false;/* calm: touch only */
 home_sample(&s,1000000,false,0,0);
 for(int i=1;i<=90;i++)home_sample(&s,1000000+i*10000,true,80,200);
 assert(s.input.scene.trail_count>=8);
 unsigned species=0;
 for(unsigned i=0;i<s.input.scene.trail_count;i++)species|=1u<<(sp_hash(i+7919)%3);
 assert(species==7); /* stationary holds generate all three rendered shapes */
 home_render(&s,pixels,SPARKLES_PIXELS);memcpy(mask,sp_mask,sizeof mask);
 /* At identical time, moving only the current contact cannot move any born sparkle. */
 s.input.scene.touch_x=.8f;s.input.scene.touch_y=.6f;
 home_render(&s,pixels,SPARKLES_PIXELS);
 assert(!memcmp(mask,sp_mask,sizeof mask));
 float old_x=s.input.scene.trail[0].x,old_y=s.input.scene.trail[0].y;
 home_sample(&s,1910000,true,300,200);
 assert(s.input.scene.trail[0].x==old_x&&s.input.scene.trail[0].y==old_y); /* old births stay in world coordinates */
 home_render(&s,pixels,SPARKLES_PIXELS);
 unsigned old=0,new=0;for(int y=150;y<250;y++)for(int x=0;x<368;x++){
  if(x<120)old+=sp_mask_at(x,y);if(x>260)new+=sp_mask_at(x,y);
 }
 assert(old&&new);
 home_sample(&s,1920000,false,0,0);
 home_sample(&s,4000000,false,0,0);home_render(&s,pixels,SPARKLES_PIXELS);
 for(int i=0;i<SP_MASK_PIXELS;i++)assert(!sp_mask[i]);
 puts("stationary mixed shapes, current-contact independence, moving birth history, release decay: PASS");
}
