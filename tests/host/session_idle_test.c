#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static unsigned mask_energy(void){unsigned e=0;for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];return e;}
int main(void){
 home_ui s={.page=SPARKLES};uint16_t*a=malloc(SPARKLES_PIXELS*2),*b=malloc(SPARKLES_PIXELS*2);assert(a&&b);
 /* No working session: water may animate but automatic sparkles are absent. */
 s.input.scene.time=1;assert(home_render(&s,a,SPARKLES_PIXELS));assert(mask_energy()==0);
 s.input.scene.time=15;assert(home_render(&s,b,SPARKLES_PIXELS));assert(mask_energy()==0);
 /* Water caching may retain a keyframe; no ambient activity is inferred from it. */
 /* Water alone may differ while the sparkle mask stays empty. */
 s.input.scene.time=15.5f;assert(home_render(&s,a,SPARKLES_PIXELS));assert(mask_energy()==0);
 assert(memcmp(a,b,SPARKLES_PIXELS*2));
 /* Floating session markers stay honestly gated on a real live feed. */
 assert(home_live_points(&s)==0);
 s.page=HOME;s.tile=0;s.input.scene.time=1;assert(home_render(&s,a,SPARKLES_PIXELS));s.input.scene.time=20;assert(home_render(&s,b,SPARKLES_PIXELS));assert(!memcmp(a,b,SPARKLES_PIXELS*2));
 unsigned changes=0;for(int y=104;y<258;y++)for(int x=55;x<312;x++)changes+=a[y*368+x]!=a[104*368+55];assert(changes>1000); /* authentic Material Symbol replaces the scene thumbnail */
 free(a);free(b);puts("Water without automatic glints at idle; markers feed-gated; Home is a still graphic: PASS");}
