#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static uint16_t out[SPARKLES_PIXELS];
static unsigned energy(void){unsigned n=0;for(int i=0;i<SP_MASK_PIXELS;i++)n+=sp_mask[i];return n;}
int main(void){
 home_ui s={0};s.page=SPARKLES;s.input.scene.time=4;
 home_render(&s,out,SPARKLES_PIXELS);assert(energy()==0);
 /* Quiet only gates automatic activity, never physical touch. */
 s.input.scene.quiet=1;s.input.scene.strength=1;s.input.scene.hold=1;s.input.scene.contact_start=3;
 sparkles_render_direct(&s.input.scene,out,SPARKLES_PIXELS);assert(energy()>0);
 s.input.scene.strength=0;
 s.live=(live_state){.valid=true,.count=1,.received_us=1,.level=5,.flags=LIVE_FLAG_MEASURED};s.live.ids[0]=1;s.live_now_us=2;
 /* density follows the active sessions' TOKEN level (1 + level 5 = 6), not the flags */
 assert(home_live_density(&s)==6);
 s.live.flags|=LIVE_FLAG_DEGRADED;assert(home_live_density(&s)==6);
 s.live.flags=0;s.live.level=0;assert(home_live_density(&s)==1);  /* active, usage pending: calm glint */
 s.live.flags=LIVE_FLAG_MEASURED;s.live.count=0;assert(home_live_density(&s)==0);
 s.live.count=1;s.live_now_us=LIVE_TTL_US+1;home_render(&s,out,SPARKLES_PIXELS);assert(energy()==0);
 s.live_now_us=2;s.session_off=true;home_render(&s,out,SPARKLES_PIXELS);assert(energy()>0);  /* off: own animation, not Hermes */
 puts("ambient fails closed, touch independent, fresh working-session-count density: PASS");
}
