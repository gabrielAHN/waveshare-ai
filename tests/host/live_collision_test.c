#include <assert.h>
#include <stdio.h>
#include "home_render.h"
/* Session glows with many-session rosters: deterministic, in bounds, never writing outside the frame,
 * gated off on Settings and when the feed is stale. The old fixed 22 px dot grid is gone: the glows
 * wander inside the Sparkles tile on Home, or the whole Sparkles page (tests/host/live_glow_test.c). */
static home_ui roster_ui(unsigned kind,int page){
 home_ui s={.page=page,.live_now_us=100};s.input.scene.time=3.f;
 s.live=(live_state){.count=LIVE_MAX,.valid=true,.received_us=100,.flags=LIVE_FLAG_MEASURED,.level=2};
 for(unsigned i=0;i<LIVE_MAX;i++){
  if(kind==0)s.live.ids[i]=(uint64_t)i+1;
  else if(kind==1)s.live.ids[i]=((uint64_t)i+1)<<32;
  else s.live.ids[i]=UINT64_MAX-LIVE_MAX+1+i;
  s.live.providers[i]=(uint8_t)(i%4);
 }
 return s;
}
static void full_roster(unsigned kind){
 for(int pg=0;pg<2;pg++){
  int page=pg?SPARKLES:HOME;
  home_ui s=roster_ui(kind,page);
  home_point a[LIVE_MAX],b[LIVE_MAX];
  for(unsigned frame=0;frame<256;frame++){
   s.input.scene.time=frame*.73f;home_live_glow_layout(&s,a);
   home_ui c=s;c.live.received_us=999;home_live_glow_layout(&c,b);
   assert(!memcmp(a,b,sizeof a));            /* packet timing never moves a glow */
   for(unsigned i=0;i<LIVE_MAX;i++){
    if(page==HOME)assert(a[i].x>=88&&a[i].x<88+SPARKLE_IMAGE_SIZE&&a[i].y>=108&&a[i].y<108+SPARKLE_IMAGE_SIZE);
    else assert(a[i].x>=0&&a[i].x<368&&a[i].y>=0&&a[i].y<448);
   }
  }
  /* Canaries either side of the frame: the full roster never writes outside it. */
  uint16_t *allocation=calloc(SPARKLES_PIXELS+2,sizeof *allocation);assert(allocation);
  uint16_t *p=allocation+1;allocation[0]=allocation[SPARKLES_PIXELS+1]=0xbeef;
  home_live_overlay(&s,p);unsigned lit=0;
  for(int i=0;i<SPARKLES_PIXELS;i++)lit+=p[i]!=0;
  assert(lit>0);
  s.page=SETTINGS;memset(p,0,SPARKLES_PIXELS*sizeof *p);home_live_overlay(&s,p);
  for(int i=0;i<SPARKLES_PIXELS;i++)assert(p[i]==0);
  s.page=page;s.live_now_us=100+LIVE_TTL_US;home_live_overlay(&s,p);
  for(int i=0;i<SPARKLES_PIXELS;i++)assert(p[i]==0);
  assert(allocation[0]==0xbeef&&allocation[SPARKLES_PIXELS+1]==0xbeef);free(allocation);
 }
}
static void provider_colors_follow_records(void){
 home_ui s={.page=SPARKLES,.live_now_us=100};s.input.scene.time=1.f;
 s.live=(live_state){.count=4,.ids={1,2,3,4},.providers={LIVE_PROVIDER_ANTHROPIC,
  LIVE_PROVIDER_OPENAI_CODEX,LIVE_PROVIDER_OPENROUTER,LIVE_PROVIDER_UNKNOWN},.valid=true,.received_us=100};
 home_point points[LIVE_MAX];home_live_glow_layout(&s,points);
 uint16_t *p=calloc(SPARKLES_PIXELS,sizeof *p);assert(p);
 for(unsigned i=0;i<4;i++){
  /* One session at a time: the glow's centre pixel carries its provider's (lightened) colour. */
  live_state one=s.live;one.count=1;one.ids[0]=s.live.ids[i];one.providers[0]=s.live.providers[i];
  home_ui o=s;o.live=one;memset(p,0,SPARKLES_PIXELS*sizeof *p);home_live_overlay(&o,p);
  int x=(int)points[i].x,y=(int)points[i].y;
  uint16_t c=home_live_provider_color(s.live.providers[i]),got=p[y*368+x];
  int dr=abs((int)(got>>11)-(int)(c>>11)),dg=abs((int)((got>>5)&63)-(int)((c>>5)&63));
  assert(got!=0&&dr<=24&&dg<=48);
 }
 free(p);
}
int main(void){
 assert(home_live_provider_color(0)==sp_pack_lcd(128,128,128));
 assert(home_live_provider_color(1)!=home_live_provider_color(2));
 assert(home_live_provider_color(2)!=home_live_provider_color(3));
 provider_colors_follow_records();for(unsigned kind=0;kind<3;kind++)full_roster(kind);
 puts("Live glows: 3 full 128-ID rosters x 256 frames (Home + Sparkles), bounds, determinism, canaries, stale/settings gating, provider colours: PASS");
}
