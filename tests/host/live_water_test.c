#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){sparkles_state s={.time=3,.quiet=1};uint16_t*a=malloc(SPARKLES_PIXELS*2),*b=malloc(SPARKLES_PIXELS*2);assert(a&&b);
 assert(sparkles_render_direct(&s,a,SPARKLES_PIXELS));for(int i=0;i<SP_MASK_PIXELS;i++)assert(sp_mask[i]==0);
 s.time=4;assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));s.time=4.07f;assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));assert(memcmp(a,b,SPARKLES_PIXELS*2));
 s.time=3;s.quiet=0;assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));unsigned energy=0;for(int i=0;i<SP_MASK_PIXELS;i++)energy+=sp_mask[i];assert(energy>0);
 s.quiet=1;s.strength=1;assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));energy=0;for(int i=0;i<SP_MASK_PIXELS;i++)energy+=sp_mask[i];assert(energy>0);
 free(a);free(b);puts("Blue water remains animated; ambient and touch sparkles independently gated: PASS");}
