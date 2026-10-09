#include <assert.h>
#include <stdio.h>
#include "home_render.h"
/* Session sparkle shows the TOKEN USAGE of active (working) sessions: density = 1 + level (0..5),
 * so more tokens/min => more sparkle. No active session, a stale feed or toggle OFF => no ambient
 * sparkle at all. The session count itself does not raise it. */
static uint16_t px[SPARKLES_PIXELS];
static home_ui with_sessions(unsigned n,unsigned level,unsigned flags){
 home_ui s={0};s.page=SPARKLES;s.live_now_us=2;
 s.live=(live_state){.valid=true,.count=(uint16_t)n,.received_us=1,.level=(uint8_t)level,.flags=(uint8_t)flags};
 for(unsigned i=0;i<n;i++)s.live.ids[i]=1000+i*7;
 return s;
}
static long ambient(unsigned n,unsigned level){
 long e=0;
 for(int k=0;k<8;k++){home_ui s=with_sessions(n,level,LIVE_FLAG_MEASURED);s.input.scene.time=2.f+k*.37f;
  s.live_now_us=2;home_render(&s,px,SPARKLES_PIXELS);for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];}
 return e;
}
int main(void){
 /* density follows the token level; zero active sessions => 0 whatever the level says */
 for(unsigned level=0;level<LIVE_LEVELS;level++){
  home_ui s=with_sessions(1,level,LIVE_FLAG_MEASURED);assert(home_live_density(&s)==1+(int)level);
  home_ui none=with_sessions(0,level,LIVE_FLAG_MEASURED);assert(home_live_density(&none)==0);
 }
 /* the session COUNT does not change it: 1 or 6 active sessions at the same usage are equal */
 {home_ui a=with_sessions(1,2,LIVE_FLAG_MEASURED),b=with_sessions(6,2,LIVE_FLAG_MEASURED);
  assert(home_live_density(&a)==home_live_density(&b));}
 /* an out-of-range level never overflows the six steps */
 {home_ui s=with_sessions(1,0,0);s.live.level=200;assert(home_live_density(&s)==LIVE_LEVELS);}
 /* Visible: no active session = nothing; energy climbs with token usage. */
 long none=ambient(0,0),e[LIVE_LEVELS];
 for(unsigned l=0;l<LIVE_LEVELS;l++)e[l]=ambient(1,l);
 printf("usage sparkle energy: none=%ld L0=%ld L1=%ld L2=%ld L3=%ld L4=%ld L5=%ld\n",none,e[0],e[1],e[2],e[3],e[4],e[5]);
 assert(none==0&&e[0]>0);
 for(unsigned l=1;l<LIVE_LEVELS;l++)assert(e[l]>e[l-1]+e[l-1]/10);
 /* Gates: stale, toggle OFF. */
 {home_ui s=with_sessions(4,3,LIVE_FLAG_MEASURED);s.live_now_us=LIVE_TTL_US+2;assert(home_live_density(&s)==0);
  s.live_now_us=2;s.session_off=true;assert(home_live_density(&s)==0);}
 puts("more token usage => more sparkle; no active session / stale / off => none: PASS");
 return 0;
}
