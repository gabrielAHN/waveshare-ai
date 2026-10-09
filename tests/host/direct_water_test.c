#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){
 uint32_t a=100|(150u<<10)|(200u<<20),b=200|(100u<<10)|(50u<<20);
 assert(sp_mix_base(a,b,0)==a&&sp_mix_base(a,b,256)==b);
 uint32_t m=sp_mix_base(a,b,128);assert((m&1023)==150&&((m>>10)&1023)==125&&((m>>20)&1023)==125);
 uint16_t *pixels=malloc(SPARKLES_PIXELS*2);assert(pixels);
 sparkles_state s={.time=1};sparkles_render_direct(&s,pixels,SPARKLES_PIXELS);
 uint32_t old=sp_direct_base[1000];s.time=1.15f;sparkles_render_direct(&s,pixels,SPARKLES_PIXELS);
 assert(sp_direct_previous[1000]==old);unsigned updates=sp_direct_water_updates;
 s.time=1.20f;sparkles_render_direct(&s,pixels,SPARKLES_PIXELS);assert(sp_direct_water_updates==updates);
 assert(sp_direct_mix>0&&sp_direct_mix<256);
 free(pixels);puts("water keyframe interpolation, channel independence and bounded phase passed");
}
