#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static uint16_t out[SPARKLES_PIXELS],idle[SPARKLES_PIXELS];
static unsigned energy(void){unsigned n=0;for(int i=0;i<SP_MASK_PIXELS;i++)n+=sp_mask[i];return n;}
int main(void){
 home_ui s={0};s.page=SPARKLES;s.input.scene.time=4;s.live_now_us=2;
 home_render(&s,idle,SPARKLES_PIXELS);assert(energy()==0);
 s.live=(live_state){.valid=true,.count=1,.received_us=1,.flags=0,.level=0};s.live.ids[0]=1;
 /* A real working session whose completed-call usage is pending is not idle.
  * Unknown provider remains neutral; baseline activity is not invented throughput. */
 assert(home_live_points(&s)==1);assert(home_live_density(&s)==1);
 home_render(&s,out,SPARKLES_PIXELS);assert(energy()>0);
 s.live.flags=LIVE_FLAG_DEGRADED;s.live.level=5;assert(home_live_density(&s)==6);
 s.live.flags=LIVE_FLAG_MEASURED;s.live.level=2;assert(home_live_density(&s)==3); /* token level drives it */
 s.live.count=0;home_render(&s,out,SPARKLES_PIXELS);assert(energy()==0);assert(home_live_points(&s)==0);
 s.live.count=1;s.live_now_us=LIVE_TTL_US+1;home_render(&s,out,SPARKLES_PIXELS);assert(energy()==0);
 s.live_now_us=2;s.session_off=true;home_render(&s,out,SPARKLES_PIXELS);assert(energy()>0);  /* off: own animation */
 assert(home_live_points(&s)==0&&home_live_density(&s)==0);  /* ...with no Hermes markers or level */
 home_sample(&s,1000000,false,0,0);home_sample(&s,1010000,true,180,200);
 home_sample(&s,1110000,true,180,200);home_render(&s,out,SPARKLES_PIXELS);assert(energy()>0);
 unsigned touch=energy();s.live.count=0;home_render(&s,out,SPARKLES_PIXELS);assert(energy()==touch);
 puts("pending real working activity rendered; no idle/offline/toggle-off glints: PASS");
}
