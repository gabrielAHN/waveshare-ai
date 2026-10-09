#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
static uint16_t pixels[SPARKLES_PIXELS];
static unsigned energy(int x0,int y0,int x1,int y1){
 unsigned e=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)e+=sp_mask_at(x,y);return e;
}
/* Hold keeps the animated sparkle cluster alive at the press point (even after
 * trail births exist); moving re-generates the cluster at the new spot instead of
 * carrying the old one; release fades in place. */
int main(void){
 home_ui s={0};s.page=SPARKLES;s.session_off=false;  /* on + no session feed = calm field: touch only */
 home_sample(&s,1000000,false,0,0);
 for(int i=1;i<=250;i++)home_sample(&s,1000000+i*10000,true,120,200); /* 2.5 s hold */
 assert(s.input.scene.trail_count>0);
 /* Cluster anchored at the press. */
 assert(fabsf((s.input.scene.anchor_x+1)*.5f*367-120)<1&&fabsf((s.input.scene.anchor_y+1)*.5f*447-200)<1);
 float t0=s.input.scene.contact_start;
 /* Hold keeps animating: the cluster renders with trails present and differs frame to frame. */
 home_render(&s,pixels,SPARKLES_PIXELS);unsigned a=energy(60,140,180,260);
 static unsigned char first[SP_MASK_PIXELS];memcpy(first,sp_mask,sizeof first);
 for(int i=251;i<=270;i++)home_sample(&s,1000000+i*10000,true,120,200);
 home_render(&s,pixels,SPARKLES_PIXELS);unsigned b=energy(60,140,180,260);
 assert(a>0&&b>0&&memcmp(first,sp_mask,sizeof first)); /* alive and changing */
 /* Cluster glints exist beyond the 24 px trail disk radius: proves the hold cluster draws. */
 unsigned trail_free=0;
 {sparkles_state c=s.input.scene;sp_cluster_glint g[SP_HOLD_MAX];
  int n=sp_touch_cluster(&c,120,200,g,SP_HOLD_MAX);for(int i=0;i<n;i++)if(g[i].alpha>.05f)++trail_free;}
 assert(trail_free>=8);
 assert(s.input.scene.hold>1.5f); /* escalation keeps climbing while stationary */
 /* Small jitter does not restart the hold. */
 home_sample(&s,3710000,true,126,204);assert(s.input.scene.contact_start==t0);
 /* Move: cluster re-anchors at the new point with a fresh flurry, hold restarts. */
 for(int i=1;i<=20;i++)home_sample(&s,3710000+i*10000,true,120+i*10,200);
 assert(s.input.scene.contact_start>t0);
 float ax=(s.input.scene.anchor_x+1)*.5f*367;assert(ax>250);
 home_render(&s,pixels,SPARKLES_PIXELS);
 assert(energy(270,140,368,260)>0);
 /* Changing only touch_x never moves anything drawn (anchor owns the cluster). */
 memcpy(first,sp_mask,sizeof first);s.input.scene.touch_x=-.9f;home_render(&s,pixels,SPARKLES_PIXELS);
 assert(!memcmp(first,sp_mask,sizeof first));
 /* Release: fades in place, then fully gone. */
 home_sample(&s,3920000,false,0,0);home_sample(&s,6500000,false,0,0);home_render(&s,pixels,SPARKLES_PIXELS);
 for(int i=0;i<SP_MASK_PIXELS;i++)assert(!sp_mask[i]);
 puts("hold keeps anchored animated cluster, move regenerates at new point, no carried cloud, release clears: PASS");
 return 0;
}
