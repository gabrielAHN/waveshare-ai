/* Session glows (user rule 2026-09-29): "showing dots outside of the sparkle tile it should only be in
 * the tile, and ... have it move around per session like the sparkle or glow animation for the
 * provider color". Home: glows only inside the Sparkles tile's image; another tile showing = nothing
 * drawn (and no repaint). Each session wanders on its own path, a soft glow in its provider colour. */
#include <assert.h>
#include <stdio.h>
#include "home_render.h"

#define SENTINEL 0x1234
static uint16_t p[SPARKLES_PIXELS];
static void fill(void){for(int i=0;i<SPARKLES_PIXELS;i++)p[i]=SENTINEL;}
static unsigned changed(void){unsigned n=0;for(int i=0;i<SPARKLES_PIXELS;i++)n+=p[i]!=SENTINEL;return n;}
static home_ui busy(int tile,int drag,float t,uint8_t level){
 home_ui s={.page=HOME,.live_now_us=100,.tile=tile,.drag_offset=drag};s.input.scene.time=t;
 s.live=(live_state){.count=4,.ids={11,22,33,44},.providers={LIVE_PROVIDER_ANTHROPIC,LIVE_PROVIDER_OPENAI_CODEX,
  LIVE_PROVIDER_OPENROUTER,LIVE_PROVIDER_ANTHROPIC},.valid=true,.received_us=100,.flags=LIVE_FLAG_MEASURED,.level=level};
 return s;
}
/* Home: every changed pixel lies inside the Sparkles image (its rounded-square alpha), wherever the
 * tile is during a swipe, at every level and over many frames. */
static void confined_to_tile(void){
 const int drags[]={0,-60,-200,-300,40};
 unsigned total=0;
 for(unsigned d=0;d<sizeof drags/sizeof*drags;d++)for(int f=0;f<60;f++)for(uint8_t lv=0;lv<LIVE_LEVELS;lv+=5){
  home_ui s=busy(0,drags[d],f*.37f,lv);fill();home_live_overlay(&s,p);
  int ox=drags[d]*4/5+88,oy=108;  /* the tile's art moves 0.8x of its paper (parallax, home_ui.h) */
  for(int y=0;y<448;y++)for(int x=0;x<368;x++){
   if(p[y*368+x]==SENTINEL)continue;total++;
   int ix=x-ox,iy=y-oy;
   assert(ix>=0&&ix<SPARKLE_IMAGE_SIZE&&iy>=0&&iy<SPARKLE_IMAGE_SIZE);
   assert(sparkle_image_alpha[iy*SPARKLE_IMAGE_SIZE+ix]>0);
  }
 }
 assert(total>0);
 /* Ask/Sensor/Settings tile showing (no swipe): no glow anywhere, and no forced repaint. */
 for(int t=1;t<HOME_TILES;t++){
  home_ui a=busy(t,0,1.f,5),b=busy(t,0,1.5f,5);fill();home_live_overlay(&a,p);assert(changed()==0);
  assert(home_visual_equal(&a,&b));
 }
 /* Sparkles tile showing: the glows animate, so the page repaints. */
 {home_ui a=busy(0,0,1.f,3),b=busy(0,0,1.5f,3);assert(!home_visual_equal(&a,&b));}
}
/* Each session wanders on its own path around the tile (not a fixed dot with a 3 px wobble). */
static void wanders_per_session(void){
 home_ui s=busy(0,0,0,2);
 float lo[4][2],hi[4][2];home_point pt[LIVE_MAX];
 for(int i=0;i<4;i++){lo[i][0]=lo[i][1]=1e9f;hi[i][0]=hi[i][1]=-1e9f;}
 for(int f=0;f<400;f++){
  s.input.scene.time=f*.1f;home_live_glow_layout(&s,pt);
  for(int i=0;i<4;i++){
   lo[i][0]=fminf(lo[i][0],pt[i].x);hi[i][0]=fmaxf(hi[i][0],pt[i].x);
   lo[i][1]=fminf(lo[i][1],pt[i].y);hi[i][1]=fmaxf(hi[i][1],pt[i].y);
   assert(pt[i].x>=88&&pt[i].x<88+SPARKLE_IMAGE_SIZE&&pt[i].y>=108&&pt[i].y<108+SPARKLE_IMAGE_SIZE);
  }
 }
 for(int i=0;i<4;i++){
  printf("session %d roams %.0f x %.0f px\n",i,hi[i][0]-lo[i][0],hi[i][1]-lo[i][1]);
  assert(hi[i][0]-lo[i][0]>=80&&hi[i][1]-lo[i][1]>=80);   /* covers most of the 192 px tile image */
 }
 /* Different sessions are on different paths at the same moment; the same roster is deterministic. */
 s.input.scene.time=7.3f;home_live_glow_layout(&s,pt);home_point again[LIVE_MAX];home_live_glow_layout(&s,again);
 assert(!memcmp(pt,again,4*sizeof*pt));
 for(int i=0;i<4;i++)for(int j=0;j<i;j++)assert(fabsf(pt[i].x-pt[j].x)+fabsf(pt[i].y-pt[j].y)>1.f);
}
/* Soft glow in the provider colour: many graded pixels (not a flat disc), and the lit pixels' hue
 * follows the provider (Anthropic coral: red-dominant; Codex green-dominant; OpenRouter blue > green). */
static void provider_glow(void){
 const uint8_t prov[]={LIVE_PROVIDER_ANTHROPIC,LIVE_PROVIDER_OPENAI_CODEX,LIVE_PROVIDER_OPENROUTER};
 for(unsigned k=0;k<3;k++){
  home_ui s={.page=SPARKLES,.live_now_us=100};s.input.scene.time=2.f;
  s.live=(live_state){.count=1,.ids={77},.providers={prov[k]},.valid=true,.received_us=100,.flags=LIVE_FLAG_MEASURED,.level=3};
  memset(p,0,sizeof p);home_live_overlay(&s,p);
  double r=0,g=0,b=0;unsigned lit=0,shades=0;static uint8_t seen[65536];memset(seen,0,sizeof seen);
  for(int i=0;i<SPARKLES_PIXELS;i++)if(p[i]){lit++;r+=(p[i]>>11)*8;g+=((p[i]>>5)&63)*4;b+=(p[i]&31)*8;if(!seen[p[i]]){seen[p[i]]=1;shades++;}}
  printf("provider %u: lit %u shades %u avg rgb %.0f %.0f %.0f\n",prov[k],lit,shades,r/lit,g/lit,b/lit);
  assert(lit>=300&&shades>=12);
  if(k==0)assert(r>g&&r>b);
  if(k==1)assert(g>r&&g>b);
  if(k==2)assert(b>g&&r>g);
 }
}
int main(void){
 confined_to_tile();wanders_per_session();provider_glow();
 puts("Session glows: confined to the Sparkles tile, wander per session, provider-colour soft glow: PASS");
}
