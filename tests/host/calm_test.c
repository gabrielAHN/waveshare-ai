#include <assert.h>
#include <stdio.h>
#include "home_render.h"
int main(void){
 assert(fabsf(sp_visual_time(10)-7.8f)<.001f);
 home_ui s={.page=SPARKLES};s.input.scene.time=3;s.input.scene.quiet=1;uint16_t *x=malloc(SPARKLES_PIXELS*2),*y=malloc(SPARKLES_PIXELS*2);
 assert(home_render(&s,x,SPARKLES_PIXELS));assert(sparkles_render_direct(&s.input.scene,y,SPARKLES_PIXELS));assert(!memcmp(x,y,SPARKLES_PIXELS*2)); /* no Home overlay */
 s.page=HOME;assert(home_render(&s,x,SPARKLES_PIXELS));
 /* Full-screen service pages supersede centered tiles. Bottom Home band stays empty. */
 for(int row=420;row<448;row++)for(int col=1;col<368;col++)assert(x[row*368+col]==x[row*368]);
 for(int tile=0;tile<HOME_TILES;tile++){s.tile=tile;assert(home_render(&s,x,SPARKLES_PIXELS));}
 free(x);free(y);puts("calm motion scale, unobstructed canvas and tile-only Home: PASS");
}
