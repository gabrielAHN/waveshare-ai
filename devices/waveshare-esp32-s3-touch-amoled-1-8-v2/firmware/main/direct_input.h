#pragma once
#include "sparkles.h"
#include "adapter.h"
/* Shared only under the owner's mutex. Poller alone writes; renderer copies.
 * No render/DMA resources in the input clock. Missed brief contacts leave a tail. */
typedef struct {
 sparkles_state scene;
 int64_t stamp_us,max_interval_us;
 unsigned samples,errors,edges;
 bool down;
 unsigned last_x,last_y;
 float trail_x,trail_y,trail_time;
 unsigned trail_births;
} direct_input;
#define DIRECT_REANCHOR_PX 28.f
/* CST816S at 100 Hz: mid-press the controller now and then answers one poll with no point, and it
 * reports no point at all on release. Unfiltered, a dropout ended the press: a slow page scroll became
 * two short drags plus a stray tap, and a button press re-armed. Up to TOUCH_HOLD_POLLS missing polls
 * keep the press at its last point (20 ms at 100 Hz, below any real lift-and-tap). */
#define TOUCH_HOLD_POLLS 2
typedef struct { bool down; int misses, x, y; } touch_debounce;
static inline bool touch_debounce_step(touch_debounce *d, bool contact, int x, int y, int *out_x, int *out_y) {
 if (contact) { d->down = true; d->misses = 0; d->x = x; d->y = y; }
 else if (d->down && ++d->misses > TOUCH_HOLD_POLLS) d->down = false;
 *out_x = d->x; *out_y = d->y;
 return d->down;
}
static inline void direct_trail_add(direct_input*s,float x,float y){
 sparkles_state*c=&s->scene;
 /* Independent birth-local scatter; no later frame uses contact coordinates.
  * Uniform disk area avoids a ring, square lattice or centerline stamp train. */
 unsigned seed=sp_hash(++s->trail_births+7919);
 float angle=(seed&65535u)*(6.2831853f/65536.f);
 float radius=sp_touch_disk(c->reach)*sqrtf((sp_hash(seed)&65535u)/65535.f);
 x=sp_clamp(x+cosf(angle)*radius,0,SPARKLES_W-1);
 y=sp_clamp(y+sinf(angle)*radius,0,SPARKLES_H-1);
 c->trail[c->trail_next]=(sp_trail_particle){x,y,c->time};
 c->trail_next=(c->trail_next+1)%SP_TRAIL_MAX;
 if(c->trail_count<SP_TRAIL_MAX)++c->trail_count;
}
static inline void direct_trail(direct_input*s,bool down,unsigned x,unsigned y){
 if(!down)return;
 float dx=x-s->trail_x,dy=y-s->trail_y,d=sqrtf(dx*dx+dy*dy);
 if(!s->down){direct_trail_add(s,x,y);}
 else if(d>=8.f){
  /* At most 58 samples for a panel diagonal: bounded even after a missed poll. */
  int n=(int)ceilf(d/10.f);if(n>SP_TRAIL_MAX)n=SP_TRAIL_MAX;
  for(int i=1;i<=n;i++)direct_trail_add(s,s->trail_x+dx*i/n,s->trail_y+dy*i/n);
 }else if(s->scene.time-s->trail_time>=.06f+.07f*(sp_hash(s->trail_births+101u)&65535u)/65535.f){direct_trail_add(s,x,y);}
 else return;
 s->trail_x=x;s->trail_y=y;s->trail_time=s->scene.time;
}
static inline void direct_sample(direct_input *s,int64_t now,bool contact,unsigned x,unsigned y){
 if(now<s->stamp_us)return;
 int64_t elapsed=now-s->stamp_us;
 if(s->samples&&elapsed>s->max_interval_us)s->max_interval_us=elapsed;
 bool down=contact&&x<SPARKLES_W&&y<SPARKLES_H;
 if(down!=s->down)++s->edges;
 float nx=x*(2.f/(SPARKLES_W-1))-1,ny=y*(2.f/(SPARKLES_H-1))-1;
 if(down&&!s->down){s->scene.contact_start=s->scene.time;s->scene.hold=0;s->scene.reach=0;s->scene.anchor_x=nx;s->scene.anchor_y=ny;}
 float dt=elapsed/1000000.f,prior=s->scene.strength;
 if(down&&s->down){
  s->scene.theme=sp_clamp(s->scene.theme+((int)s->last_y-(int)y)*(2.f/SPARKLES_H),-1,1);
  s->scene.gradient=sp_clamp(s->scene.gradient+((int)x-(int)s->last_x)*(2.f/SPARKLES_W),-1,1);
 }
 sparkles_advance(&s->scene,dt,x*(2.f/(SPARKLES_W-1))-1,y*(2.f/(SPARKLES_H-1))-1,down);
 /* ~0.2s rise, existing ~0.36s release. Native only: do not flash an entire
  * cluster at full strength on the first touch sample. */
 if(down)s->scene.strength=prior+(1-prior)*(1-expf(-dt*5));
 /* Area clock: runs for the whole press, held or moving; frozen on release, reset on next press. */
 if(down&&s->down)s->scene.reach+=dt;
 /* Moving: once the finger leaves the anchor radius, re-anchor there with a fresh
  * flurry and calm hold level; the previous cluster is simply not drawn any more
  * (its trail births stay where they were born and fade). Stationary hold keeps
  * the same anchor, so the cluster keeps animating and escalating in place. */
 if(down&&s->down){
  float ax=(s->scene.anchor_x+1)*.5f*(SPARKLES_W-1),ay=(s->scene.anchor_y+1)*.5f*(SPARKLES_H-1);
  float ddx=x-ax,ddy=y-ay;
  if(ddx*ddx+ddy*ddy>=DIRECT_REANCHOR_PX*DIRECT_REANCHOR_PX){
   s->scene.anchor_x=nx;s->scene.anchor_y=ny;s->scene.contact_start=s->scene.time;s->scene.hold=0;
  }
 }
 direct_trail(s,down,x,y);
 if(down){s->last_x=x;s->last_y=y;}
 s->stamp_us=now;s->down=down;++s->samples;
}
/* Runtime AMBIENT-DENSITY 'usage' setter [0..1], RAM only. Independent of the
 * touch clock: callable at any time under the same owner snapshot mutex that
 * guards the scene. It only writes scene.usage, which scales the ambient field
 * in sparkles.h; strength/hold/contact_start/theme/gradient are untouched, so
 * the touch burst/held pattern is unaffected. */
static inline void direct_set_usage(direct_input *s,float usage){
 s->scene.usage=sp_usage_clamp(usage);
}
