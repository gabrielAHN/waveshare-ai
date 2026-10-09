#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static unsigned mask_energy(void){unsigned e=0;for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];return e;}
int main(void){home_ui s={.page=SPARKLES,.live_now_us=100};s.input.scene.time=3;
 uint16_t*p=malloc(SPARKLES_PIXELS*2);assert(p);
 /* No feed: no automatic sparkles. */
 assert(home_render(&s,p,SPARKLES_PIXELS));assert(mask_energy()==0);
 /* A fresh feed adds floating session markers; ambient sparkles remain. */
 s.live=(live_state){.count=2,.ids={11,22},.valid=true,.received_us=100,.flags=LIVE_FLAG_MEASURED};assert(home_live_points(&s)==2);assert(home_render(&s,p,SPARKLES_PIXELS));assert(mask_energy()>0);
 {home_ui r={.page=SPARKLES};r.live.count=LIVE_MAX;home_point a[LIVE_MAX],b[LIVE_MAX];
  for(unsigned i=0;i<LIVE_MAX;i++)r.live.ids[i]=(uint64_t)i+1;
  r.input.scene.time=1;home_live_glow_layout(&r,a);r.input.scene.time=2;home_live_glow_layout(&r,b);
  for(unsigned i=0;i<LIVE_MAX;i++){assert(a[i].x>=0&&a[i].x<368&&a[i].y>=0&&a[i].y<448);assert(a[i].x!=b[i].x||a[i].y!=b[i].y);}}
 /* Stale feed fails closed for both markers and automatic sparkles. */
 home_ui before=s;s.live_now_us=100+LIVE_TTL_US;assert(!home_live_points(&s));assert(!home_visual_equal(&s,&before));assert(home_render(&s,p,SPARKLES_PIXELS));assert(mask_energy()==0);
 /* Range of use: each session marker grows with the live token level (0..5), so the Home tiles
  * show how hard the working sessions are running, not only that they exist. */
 {home_ui h={.page=HOME,.live_now_us=100,.session_off=false};h.input.scene.time=3;
  unsigned lit[2]={0,0};
  for(int k=0;k<2;k++){
   h.live=(live_state){.count=1,.ids={11},.valid=true,.received_us=100,.flags=LIVE_FLAG_MEASURED,.level=(uint8_t)(k?5:0)};
   for(int i=0;i<SPARKLES_PIXELS;i++)p[i]=0;home_live_overlay(&h,p);
   for(int i=0;i<SPARKLES_PIXELS;i++)lit[k]+=p[i]!=0;
  }
  printf("session marker px level0=%u level5=%u\n",lit[0],lit[1]);assert(lit[0]>=150&&lit[1]>=lit[0]*2);
  home_ui a=h,b=h;a.live.level=1;b.live.level=4;assert(!home_visual_equal(&a,&b));}
 /* User rule (2026-09-29): Session sparkle OFF = the Sparkles animation plays by itself (not linked to
  * Hermes), ON = it follows the Hermes session. Off must animate even with no feed at all. */
 {home_ui o={.page=SPARKLES,.live_now_us=100,.session_off=true};o.input.scene.time=3;
  assert(home_render(&o,p,SPARKLES_PIXELS));unsigned off_empty=mask_energy();
  o.live=(live_state){.count=2,.ids={11,22},.valid=true,.received_us=100,.flags=LIVE_FLAG_MEASURED,.level=5};
  assert(home_render(&o,p,SPARKLES_PIXELS));unsigned off_busy=mask_energy();
  printf("session off: ambient energy no feed=%u busy feed=%u\n",off_empty,off_busy);
  assert(off_empty>0&&off_busy==off_empty);      /* animates, independent of Hermes */
  assert(!home_live_points(&o));                  /* no session markers when off */
  home_ui on=o;on.session_off=false;on.live=(live_state){0};
  assert(home_render(&on,p,SPARKLES_PIXELS));assert(mask_energy()==0);  /* on + no session = calm */
 }
 s.page=HOME;s.live_now_us=200;assert(home_live_points(&s)==2);before=s;s.input.scene.time+=.1f;assert(!home_visual_equal(&s,&before));s.page=SETTINGS;assert(!home_live_points(&s));free(p);puts("Automatic sparkles and session markers fail closed: PASS");}
