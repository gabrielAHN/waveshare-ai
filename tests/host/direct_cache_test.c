#include <assert.h>
#include <stdio.h>
#include "sparkles.h"
int main(void){
 uint16_t *a=malloc(SPARKLES_PIXELS*2),*b=malloc(SPARKLES_PIXELS*2);assert(a&&b);
 sparkles_state s={.time=1};assert(sparkles_render_direct(&s,a,SPARKLES_PIXELS));
 unsigned n=sp_direct_water_updates;uint8_t field[sizeof(sp_field)];memcpy(field,sp_field,sizeof(field));
 s.time=1.05f;s.strength=1;s.touch_x=.5f;s.touch_y=.1f;
 assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));assert(sp_direct_water_updates==n);
 assert(!memcmp(field,sp_field,sizeof(field)));assert(memcmp(a,b,SPARKLES_PIXELS*2));
 s.time=1.15f;assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));assert(sp_direct_water_updates==n+1);
 free(a);free(b);puts("direct water/base cache cadence and faster contact overlay passed");
}
