#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
/* Constrained live-reference adaptation.
 * Perspective + 3-octave noise water, blurred blue print and transient glints.
 * Geometry on a 24x28 field; 184x224 composite replicated into 368x448.
 * Output: native LCD-order RGB565 frames, 368 pixels per row (direct owner, direct_main.c).
 * All time is seconds, never a per-frame phase. No storage or panel access. */
#define SPARKLES_W 368
#define SPARKLES_H 448
#define SPARKLES_GW 24
#define SPARKLES_GH 28
#define SPARKLES_MAX_STEPS 4
#define SPARKLES_PIXELS (SPARKLES_W*SPARKLES_H)
/* Ambient sparkle-DENSITY 'usage' scalar in [0,1] (RAM-only; no NVS/flash).
 * It scales the AMBIENT sparkle field ONLY -- spawn probability, effective
 * concurrent glints and a subtle twinkle-speed -- so higher usage reads as a
 * busier/denser calm shimmer and lower usage as a sparser, occasional glint.
 * It NEVER touches the touch down-burst / six-level held pattern, which stay
 * driven purely by strength/hold/contact_start/time. The default is a calm mid
 * value set by the owner at startup; a zero-initialised state (all host unit
 * tests) reads usage=0, i.e. the sparsest ambient field. SP_USAGE_MAX_GLINTS is
 * a hard per-frame safety cap on ambient stamps that keeps idle FPS bounded
 * (>=~8 FPS) even at usage=1. */
#define SP_USAGE_DEFAULT 0.15f
#define SP_USAGE_MAX_GLINTS 96
/* density: 0 = no live token-density level (use the usage scalar above, i.e.
 * the calm default); 1..6 = live session token-density
 * level 0..5 from the host bridge (WLS4); zero-init is calm. */
#define SP_TRAIL_MAX 64
#define SP_TRAIL_LIFE 1.15f
typedef struct {float x,y,born;} sp_trail_particle;
/* reach: seconds the finger has been down in this press (hold OR move); widens the generation area
 * and is NOT reset by re-anchoring, only by a fresh press. 0 for synthetic/test states. */
/* style: the Sparkles-page swipe style 0..SP_STYLES-1 (0 = the original sea, zero-init). style_from +
 * style_at (scene seconds) crossfade the palette for SP_STYLE_FADE after a swipe. */
typedef struct {float time,x,y,strength,touch_x,touch_y,theme,gradient,contact_start,hold,usage,anchor_x,anchor_y,reach;int quiet,density;
 sp_trail_particle trail[SP_TRAIL_MAX];unsigned trail_next,trail_count;
 int style,style_from;float style_at;
} sparkles_state;
typedef struct {float distance,x,y; unsigned steps;} sparkles_ray;
typedef struct {float x,y,z;} sp_v3;
static inline float sp_clamp(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float sp_smooth(float x){x=sp_clamp(x,0,1);return x*x*(3-2*x);}
/* usage->ambient-density mapping. usage in [0,1] (clamped). sp_usage_spawn is a
 * monotonic spawn-probability multiplier: very sparse (0.20x) at usage=0, the
 * historical field density (1.0x) at the calm default (0.15), rising to a busy
 * 3.0x at usage=1. sp_usage_speed is a mild twinkle-rate scale so a busier field
 * also shimmers a touch faster. Both affect ONLY the ambient twinkle field. */
static inline float sp_usage_clamp(float u){return sp_clamp(u,0.f,1.f);}
static inline float sp_usage_spawn(float u){
 u=sp_usage_clamp(u);
 if(u<=SP_USAGE_DEFAULT)return 0.20f+(1.0f-0.20f)*(u/SP_USAGE_DEFAULT);
 return 1.0f+(3.0f-1.0f)*((u-SP_USAGE_DEFAULT)/(1.0f-SP_USAGE_DEFAULT));
}
static inline float sp_usage_speed(float u){u=sp_usage_clamp(u);return 1.0f+0.35f*u;}
/* Six live token-density steps (session throughput level 0..5). Each step
 * raises spawn density, glint brightness (alpha gain; the eased envelope then
 * plateaus longer at full brightness), glint size/bloom and twinkle speed.
 * cap bounds per-frame ambient stamps so level 5 stays >=~8 FPS on device.
 * density 0 (no live level) keeps the usage-scalar mapping unchanged. */
#define SP_DENSITY_LEVELS 6
typedef struct {float spawn,alpha,scale,speed;int cap;} sp_density_step;
static inline sp_density_step sp_density(int density,float usage){
 static const sp_density_step steps[SP_DENSITY_LEVELS]={
  {1.00f,0.90f,0.94f,1.05f,44},  /* L0 calm: idle / <20k tok/min     */
  {1.55f,1.00f,1.00f,1.10f,50},  /* L1 <100k tok/min (bridge window) */
  {2.15f,1.08f,1.04f,1.18f,56},  /* L2 <200k tok/min                  */
  {2.80f,1.17f,1.07f,1.27f,64},  /* L3 <350k tok/min                  */
  {3.50f,1.27f,1.10f,1.37f,72},  /* L4 <600k tok/min                  */
  {4.20f,1.38f,1.14f,1.48f,80}}; /* L5 >=600k tok/min; cap*scale^2 budgeted for >=8.5 FPS on device */
 if(density>=1&&density<=SP_DENSITY_LEVELS)return steps[density-1];
 return (sp_density_step){sp_usage_spawn(usage),1.f,1.f,sp_usage_speed(usage),SP_USAGE_MAX_GLINTS};
}
static inline uint32_t sp_hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
static inline float sp_random(uint32_t x){return (sp_hash(x)&65535)*(1.f/65535.f);}
static inline sp_v3 sp_norm(sp_v3 a){float k=1.f/sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);return(sp_v3){a.x*k,a.y*k,a.z*k};}
/* Repeatable version of the reference's random 256x256 nearest noise texture. */
static uint8_t *sp_noise;
static int sp_ready;
static sp_v3 sp_rays[SPARKLES_GW*SPARKLES_GH];
static uint8_t sp_field[SPARKLES_GW*SPARKLES_GH],sp_blur[SPARKLES_GW*SPARKLES_GH];
/* Glint alpha mask at the 2x2-replicated composite resolution (184x224): every stamp and the
 * composite only ever touch even (x,y), so the compact mask carries exactly the same values while
 * moving 1/4 of the PSRAM bytes with packed half-resolution mask cells. */
#define SP_MW (SPARKLES_W/2)
#define SP_MH (SPARKLES_H/2)
#define SP_MASK_PIXELS (SP_MW*SP_MH)
static uint8_t *sp_mask;
static inline unsigned sp_mask_at(int x,int y){return ((x|y)&1)||x<0||y<0||x>=SPARKLES_W||y>=SPARKLES_H?0:sp_mask[(y>>1)*SP_MW+(x>>1)];}
static inline float sp_noise_at(int x,int y){return sp_noise[((y&255)<<8)|(x&255)]*(1.f/255.f)-.5f;}
static inline float sp_value(float x,float y){
 int ix=(int)x,iy=(int)y;if(x<ix)--ix;if(y<iy)--iy;float u=sp_smooth(x-ix),v=sp_smooth(y-iy);
 float a=sp_noise_at(ix,iy),b=sp_noise_at(ix+1,iy),c=sp_noise_at(ix,iy+1),d=sp_noise_at(ix+1,iy+1);
 return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v;
}
static inline void sp_init(void){
 if(sp_ready)return;
 sp_noise=malloc(256*256);sp_mask=malloc(SP_MASK_PIXELS);assert(sp_noise&&sp_mask);
 for(unsigned i=0;i<256*256;i++)sp_noise[i]=sp_hash(i+71)>>24;
 /* camera [-2.22,20.84,11.94], lookAt [-3.68,7.85,-4.34], fov 60.
    Preserve reference horizontal framing (616/431), fill portrait panel. */
 sp_v3 f=sp_norm((sp_v3){-1.46f,-12.99f,-16.28f});
 sp_v3 r=sp_norm((sp_v3){-f.z,0,f.x});
 sp_v3 u={r.y*f.z-r.z*f.y,r.z*f.x-r.x*f.z,r.x*f.y-r.y*f.x};
 for(int j=0;j<SPARKLES_GH;j++)for(int i=0;i<SPARKLES_GW;i++){
  float x=((i+.5f)/SPARKLES_GW*2-1)*.577350269f*(616.f/431.f),y=(1-(j+.5f)/SPARKLES_GH*2)*.577350269f;
  sp_rays[j*SPARKLES_GW+i]=sp_norm((sp_v3){f.x+r.x*x+u.x*y,f.y+r.y*x+u.y*y,f.z+r.z*x+u.z*y});
 }
 sp_ready=1;
}
static inline float sparkles_height(const sparkles_state*s,float x,float y){
 /* Reference: H=2.7, scale=.08, speed=.06, freq*=1.75, amp*=.5,
    rotation pi; each octave has its own advective speed .06*(10-i).
    Replacing 3.14 by pi removes only a 0.00159-radian rotation. */
 x*=.08f;y*=.08f;float sum=0,f=1,a=2.7f;
 for(int i=0;i<3;i++){float t=s->time*(.06f*(10-i));sum+=a*sp_value(f*(x+t),f*(y+t));x=-x;y=-y;f*=1.75f;a*=.5f;}
 return sum;
}
static inline sparkles_ray sp_trace(const sparkles_state*s,sp_v3 d){
 float t=-20.84f/d.y;
 /* Bounded fixed-point intersection refinement instead of 24+6 GPU steps. */
 for(unsigned i=0;i<SPARKLES_MAX_STEPS;i++)t=(sparkles_height(s,-2.22f+d.x*t,11.94f+d.z*t)-20.84f)/d.y;
 return(sparkles_ray){t,-2.22f+d.x*t,11.94f+d.z*t,SPARKLES_MAX_STEPS};
}
static inline sparkles_ray sparkles_trace(const sparkles_state*s,float u,float v){
 sp_init();int x=(int)sp_clamp((u+1)*.5f*(SPARKLES_GW-1),0,SPARKLES_GW-1),y=(int)sp_clamp((v+1)*.5f*(SPARKLES_GH-1),0,SPARKLES_GH-1);
 return sp_trace(s,sp_rays[y*SPARKLES_GW+x]);
}
static inline void sparkles_advance(sparkles_state*s,float dt,float x,float y,int down){
 dt=fmaxf(0,dt);s->time+=dt;
 /* Reference touch moves the sun, not ripples. Spring-like easing is bounded. */
 float k=1.f-expf(-dt*5.f);if(dt==0 && down)k=1;
 s->x+=(sp_clamp(down?x:0,-1,1)-s->x)*k;s->y+=(sp_clamp(down?y:0,-1,1)-s->y)*k;
 if(down){s->touch_x=sp_clamp(x,-1,1);s->touch_y=sp_clamp(y,-1,1);}
 /* Keep the last contact fixed while its glints fade after up/cancel. */
 s->strength=down?1:s->strength*expf(-dt*2.8f);
 /* Held-duration accumulator: grows only while the finger is down; frozen on
  * release so the sustained touch-and-hold pattern's level/phase is captured
  * and the burst then fades out via strength (existing release decay) rather
  * than continuing to escalate after lift. It restarts on a fresh down-edge
  * (reset by direct_sample below) and, as a safety net for any caller, also
  * once the release tail has fully faded so the next press always starts calm. */
 if(down)s->hold+=dt;
 else if(s->strength<.002f)s->hold=0;
}
static inline unsigned sparkles_byte(float c){return (unsigned)sp_clamp(c,0,255);}
static inline unsigned sp_lerp(unsigned a,unsigned b,unsigned f){return(a*(256-f)+b*f)>>8;}
static inline unsigned sp_field_sample(const uint8_t*f,int x,int y){
 unsigned fx=(unsigned)x*(SPARKLES_GW-1)*256/(SPARKLES_W-1),fy=(unsigned)y*(SPARKLES_GH-1)*256/(SPARKLES_H-1);
 unsigned ix=fx>>8,iy=fy>>8,jx=ix+1<SPARKLES_GW?ix+1:ix,jy=iy+1<SPARKLES_GH?iy+1:iy;
 return sp_lerp(sp_lerp(f[iy*SPARKLES_GW+ix],f[iy*SPARKLES_GW+jx],fx&255),sp_lerp(f[jy*SPARKLES_GW+ix],f[jy*SPARKLES_GW+jx],fx&255),fy&255);
}
/* Sprite bake only: rasterize one glint into a dw x dh scratch (max-blend). */
static inline void sp_glint_build(uint8_t*dst,int dw,int dh,int cx,int cy,float radius,float envelope,int species,float angle,float stretch){
 /* Only rasterize a bounded glint rectangle, not N glints at every pixel. */
 float reach=radius*(species==0?3.2f:1.25f);int rr=(int)reach+1;
 int x0=cx-rr<0?0:cx-rr,x1=cx+rr>=dw?dw-1:cx+rr;
 int y0=cy-rr<0?0:cy-rr,y1=cy+rr>=dh?dh-1:cy+rr;
 float ca=cosf(angle),sa=sinf(angle),inv=1.f/(radius*radius);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
  float dx=x-cx,dy=y-cy,d2=(dx*dx+dy*dy)*inv;
  float core=sp_clamp(1-d2*2.7f,0,1);core=core*core;
  float glow=sp_clamp(1-d2*.22f,0,1);glow=glow*glow*.38f;
  float star=0;
  if(species==0){float a=fabsf(dx*ca+dy*sa)/radius,b=fabsf(-dx*sa+dy*ca)/radius;
   star=sp_clamp(1-(a/stretch+b)*.32f-a*b*3.5f,0,1);star=star*star*.88f;}
  unsigned k=(unsigned)(sp_clamp(core+glow+star,0,1)*envelope*255);
  unsigned idx=y*dw+x;if(k>dst[idx])dst[idx]=k;
 }
}
/* Cache reference-shaped glow masks once. No per-pixel trigonometry,
   divisions or float conversions during animated sprite stamping. */
#define SP_SPRITE 160
static uint8_t *sp_sprites;
/* Per-variant bounding box of NONZERO sprite texels [x0,x1) x [y0,y1): stamping skips the
 * transparent margin (a small glint uses ~30x30 of its 160x160 cache). Zero texels can never raise
 * the max-blended mask, so the output is bit-identical. */
static int16_t sp_sprite_bb[11][4];  /* variant 10: paper lantern (Sunset style only) */
/* Per-sparkle twinkle lifecycle. u is normalized life in [0,1]: an eased
 * fade-in, a brief hold, then an eased fade-out, with a matching grow/shrink
 * scale so each glint emerges small+dim, peaks, then recedes instead of
 * popping on and off. Alpha and scale peak together. */
static inline void sp_twinkle_env(float u,float*alpha,float*scale){
 u=sp_clamp(u,0,1);
 float rise=sp_smooth(u*(1.f/.34f));
 float fall=1.f-sp_smooth((u-.5f)*(1.f/.5f));
 float a=rise*fall;
 *alpha=a;*scale=.32f+.68f*a;
}
/* scale grows/shrinks the cached feathered sprite about its center. scale==1
 * keeps the exact prior fast path (touch blooms, unit callers); other scales
 * take a bounded nearest-sample path so twinkles can breathe. radius stays for
 * caller documentation/jitter but the baked sprite carries the shape. */
static inline void sp_glint(int cx,int cy,float radius,float envelope,int species,float angle,float stretch,float scale){
 if(!sp_sprites){
  sp_sprites=malloc(11*SP_SPRITE*SP_SPRITE);uint8_t*bake=malloc(SP_SPRITE*SP_SPRITE);assert(sp_sprites&&bake);
  for(int k=0;k<11;k++){
   memset(bake,0,SP_SPRITE*SP_SPRITE);
   if(k==10){  /* paper lantern: a tall glowing body (lit paper, 26x38 px) inside a wide warm halo (~60 px) */
    for(int y=0;y<SP_SPRITE;y++)for(int x=0;x<SP_SPRITE;x++){
     float dx=x-80,dy=y-80,e=dx*dx*(1.f/(13.f*13.f))+dy*dy*(1.f/(19.f*19.f));
     float body=sp_clamp(1.3f-e,0,1);body=body>1?1:body;body=sp_smooth(body);
     float halo=.8f*expf(-(dx*dx+dy*dy*.7f)*(1.f/(2*30.f*30.f)));
     float v=body*.78f+halo;
     bake[y*SP_SPRITE+x]=(uint8_t)sp_clamp(v*255.f+.5f,0,255);}
   }else
   sp_glint_build(bake,SP_SPRITE,SP_SPRITE,80,80,k==9?11:25,1,k<8?0:1,-.448799f*(k%4)/3,k<4?1:1.6f);
   for(int y=0;y<SP_SPRITE;y++)for(int x=0;x<SP_SPRITE;x++){
    /* Stretched stars extend beyond the cache square. Feather radially to
     * transparent before its boundary, rather than clipping a visible box. */
    float dx=x-80,dy=y-80;
    float feather=1-sp_smooth((dx*dx+dy*dy-48*48)/(76.f*76-48*48));
    sp_sprites[k*SP_SPRITE*SP_SPRITE+y*SP_SPRITE+x]=(uint8_t)(bake[y*SP_SPRITE+x]*feather);
   }
   int bx0=SP_SPRITE,by0=SP_SPRITE,bx1=0,by1=0;
   for(int y=0;y<SP_SPRITE;y++)for(int x=0;x<SP_SPRITE;x++)if(sp_sprites[k*SP_SPRITE*SP_SPRITE+y*SP_SPRITE+x]){
    bx0=x<bx0?x:bx0;bx1=x+1>bx1?x+1:bx1;by0=y<by0?y:by0;by1=y+1>by1?y+1:by1;}
   if(bx1<=bx0){bx0=by0=0;bx1=by1=0;}
   sp_sprite_bb[k][0]=(int16_t)bx0;sp_sprite_bb[k][1]=(int16_t)by0;sp_sprite_bb[k][2]=(int16_t)bx1;sp_sprite_bb[k][3]=(int16_t)by1;
  }
  free(bake);
 }
 int variant=species==3?10:species==0?(int)(sp_clamp(-angle/.448799f,0,1)*3+.5f)+(stretch>1.3f?4:0):(species==1?9:8);
 unsigned alpha=(unsigned)(sp_clamp(envelope,0,1)*256);
 const uint8_t *sprite=sp_sprites+variant*SP_SPRITE*SP_SPRITE;
 (void)radius;
 float sc=sp_clamp(scale,.12f,1.6f);
 if(sc>.985f&&sc<1.015f){
  const int16_t*bb=sp_sprite_bb[variant];
  int x0=cx-80+bb[0],y0=cy-80+bb[1],x1=cx-80+bb[2],y1=cy-80+bb[3];
  x0=x0<0?0:x0;y0=y0<0?0:y0;x1=x1>SPARKLES_W?SPARKLES_W:x1;y1=y1>SPARKLES_H?SPARKLES_H:y1;
  x0=(x0+1)&~1;y0=(y0+1)&~1;
  for(int y=y0;y<y1;y+=2){
   uint8_t *dest=sp_mask+(y>>1)*SP_MW+(x0>>1);const uint8_t *src=sprite+(y-cy+80)*SP_SPRITE+x0-cx+80;
   for(int x=x0;x<x1;x+=2){unsigned k=(*src*alpha)>>8;if(k>*dest)*dest=k;src+=2;dest++;}
  }
  return;
 }
 /* Bounded scaled nearest-sample. Output offset d maps to sprite offset d/sc,
  * staying inside the feathered cache for sc in [.12,1.6]; out-of-range is
  * transparent. Even-aligned 2px stride preserves the 2x2 replication contract. */
 float inv=1.f/sc;int ext=(int)(80*sc)+1;
 int x0=cx-ext<0?0:cx-ext,y0=cy-ext<0?0:cy-ext;
 int x1=cx+ext>SPARKLES_W?SPARKLES_W:cx+ext,y1=cy+ext>SPARKLES_H?SPARKLES_H:cy+ext;
 x0=(x0+1)&~1;y0=(y0+1)&~1;
 /* Column map once per glint (one float mapping per column, not per pixel). */
 int cols=(x1-x0+1)/2;if(cols<=0)return;
 static int16_t sxs[SP_MW+1];
 const int16_t*bb=sp_sprite_bb[variant];
 int c0=cols,c1=0;
 for(int c=0;c<cols;c++){int sx=80+(int)((x0+2*c-cx)*inv);int ok=sx>=bb[0]&&sx<bb[2];sxs[c]=(int16_t)(ok?sx:-1);if(ok){if(c<c0)c0=c;c1=c+1;}}
 if(c1<=c0)return;
 for(int y=y0;y<y1;y+=2){
  int sy=80+(int)((y-cy)*inv);
  if(sy<bb[1]||sy>=bb[3])continue;
  const uint8_t *srow=sprite+sy*SP_SPRITE;uint8_t *dest=sp_mask+(y>>1)*SP_MW+(x0>>1);
  for(int c=c0;c<c1;c++){
   int sx=sxs[c];
   unsigned k=sx>=0?(srow[sx]*alpha)>>8:0;
   if(k>dest[c])dest[c]=(uint8_t)k;
  }
 }
}
#ifdef ESP_PLATFORM
static int64_t sp_stages[4];
#define SP_STAMP(i) sp_stages[i]=esp_timer_get_time()
#else
#define SP_STAMP(i) ((void)0)
#endif
static inline uint16_t sp_blend565(uint16_t base,uint16_t foreground,unsigned alpha){
 unsigned w=(alpha+4)>>3;if(w>32)w=32;
 uint32_t b=((uint32_t)base|((uint32_t)base<<16))&0x07e0f81fu;
 uint32_t f=((uint32_t)foreground|((uint32_t)foreground<<16))&0x07e0f81fu;
 uint32_t v=((b*(32-w)+f*w)>>5)&0x07e0f81fu;
 return (uint16_t)(v|(v>>16));
}
static inline int sp_grain(unsigned k){int g=(int)(sp_hash(k+51)>>27)-16;return g/4;}
static inline float sp_visual_time(float wall){return wall*.78f;}
/* Touch-and-hold escalation. The held-duration accumulator (sparkles_state.hold)
 * is mapped to six discrete calm->busy levels: the longer the finger stays down,
 * the busier the random sparkle cluster at the finger (more concurrent glints,
 * brighter, larger and spread wider). A quick tap barely accumulates hold, so it
 * stays at the calm level 0; only a real hold climbs toward the busy level 5. */
#define SP_HOLD_LEVELS 6
#define SP_HOLD_STEP 0.45f   /* seconds of continuous hold per busy level */
#define SP_HOLD_MAX 44       /* hard per-frame cap on hold-pattern glint stamps */
static inline int sp_hold_level(float hold,float *frac){
 float lv=fmaxf(hold,0)/SP_HOLD_STEP;
 int i=(int)lv;
 if(i>SP_HOLD_LEVELS-1)i=SP_HOLD_LEVELS-1;
 if(frac){float f=lv-i;*frac=f<0?0:(f>1?1:f);}
 return i;
}
/* Organic touch sparkle cluster.
 * Every glint has its OWN deterministic seed (sp_random over contact epoch +
 * slot + cycle) giving a random birth time, lifespan, species, size and a
 * position drawn from a 2D-gaussian (Rayleigh radius, uniform angle) around the
 * finger: dense near the contact, falling off outward, no ring/spiral geometry.
 * (A) DOWN: a flurry of SP_TOUCH_FLURRY sparkles, each fading in at its own
 *     random birth within ~0.16s.  (B) HELD: continuously respawning slots whose
 *     count/brightness/size/spread grow with the six hold levels.  Everything is
 *     scaled by strength, so the existing exponential release fades it in place
 *     at the retained contact. Output capped at SP_HOLD_MAX glints. */
typedef struct {float x,y,alpha,scale;int species;float life;} sp_cluster_glint;  /* life: 0..1 of its own cycle */
#define SP_TOUCH_FLURRY 18
#define SP_TOUCH_SLOTS_BASE 8
#define SP_TOUCH_SLOTS_STEP 5
static inline float sp_touch_spread(float lv){return 16.f+7.f*lv;}
/* Press-duration area level 0..5 (same 0.45 s/level pace as the hold escalation). */
static inline float sp_touch_reach_level(float reach){return sp_clamp(reach/SP_HOLD_STEP,0,SP_HOLD_LEVELS-1);}
/* Trail-birth disk radius: 24 px fingertip at press, widening to ~69 px by ~2.3 s of contact. */
static inline float sp_touch_disk(float reach){return 24.f+9.f*sp_touch_reach_level(reach);}
static inline int sp_touch_species(uint32_t h){unsigned k=(h>>9)%10;return k<5?0:(k<8?1:2);}
static inline float sp_touch_radius(uint32_t h,float sigma){
 /* Rayleigh radius: 2D gaussian scatter. Clamp u away from 0 (max ~2.5 sigma). */
 return sigma*.85f*sqrtf(-2.f*logf(fmaxf(sp_random(h),.012f)));
}
static inline int sp_touch_cluster(const sparkles_state*s,float tx,float ty,sp_cluster_glint*out,int cap){
 if(!s||!out||cap<=0)return 0;
 if(cap>SP_HOLD_MAX)cap=SP_HOLD_MAX;
 float age=fmaxf(s->time-s->contact_start,0);
 float frac;int level=sp_hold_level(s->hold,&frac);float lv=level+(level<SP_HOLD_LEVELS-1?frac:0);
 uint32_t epoch=sp_hash((uint32_t)(int32_t)(s->contact_start*1000.f)+0x51edu);
 /* Area grows with the whole press (held or moving), brightness/count with the local hold. */
 float sigma=sp_touch_spread(fmaxf(lv,sp_touch_reach_level(s->reach)));int n=0;
 /* (A) down flurry */
 if(age<1.f)for(int i=0;i<SP_TOUCH_FLURRY&&n<cap;i++){
  uint32_t h=sp_hash(epoch+(uint32_t)i*0x9e3779b9u+11u);
  float birth=sp_random(h)*.16f,life=.26f+.22f*sp_random(h+1);
  float u=(age-birth)/life;if(u<0||u>=1)continue;
  float a,sc;sp_twinkle_env(u,&a,&sc);
  float rr=sp_touch_radius(h+2,sigma),th=6.2831853f*sp_random(h+3);
  out[n++]=(sp_cluster_glint){tx+cosf(th)*rr,ty+sinf(th)*rr,a*(.70f+.30f*sp_random(h+4))*s->strength,
   sc*(.55f+.50f*sp_random(h+5)),sp_touch_species(h),u};
 }
 /* (B) held: continuous random respawn, escalating with hold level */
 float want=SP_TOUCH_SLOTS_BASE+SP_TOUCH_SLOTS_STEP*lv,gain=.70f+.07f*lv,size=.50f+.08f*lv,speed=1.f+.06f*lv;
 for(int i=0;i<SP_TOUCH_SLOTS_BASE+SP_TOUCH_SLOTS_STEP*SP_HOLD_LEVELS&&n<cap;i++){
  if(i>=want)break;
  float slot=sp_clamp(want-i,0,1);
  uint32_t h0=sp_hash(epoch^((uint32_t)i*0x85ebca6bu+17u));
  /* Stationary respawn: each slot's cycles are offset by a random phase so
   * births are spread uniformly in time; a cycle that would have begun before
   * ~0.08s after contact is skipped, so nothing appears already mid-life. */
  float period=.42f+.50f*sp_random(h0),offset=sp_random(h0+1)*period;
  float local=age*speed+offset;
  unsigned cycle=(unsigned)(local/period);float phase=local-cycle*period;
  if(local-phase-offset<.08f*speed)continue;
  uint32_t h=sp_hash(h0+cycle*0x27d4eb2du+5u);
  float dur=period*(.60f+.35f*sp_random(h));if(phase>=dur)continue;
  float a,sc;sp_twinkle_env(phase/dur,&a,&sc);
  float rr=sp_touch_radius(h+2,sigma),th=6.2831853f*sp_random(h+3);
  out[n++]=(sp_cluster_glint){tx+cosf(th)*rr,ty+sinf(th)*rr,sp_clamp(a*slot*gain*(.65f+.35f*sp_random(h+4)),0,1)*s->strength,
   sc*size*(.75f+.5f*sp_random(h+5)),sp_touch_species(h),phase/dur};
 }
 return n;
}
static inline void sp_direct_tint(int *r,int *g,int *b,float theme,float gradient,int x){
 /* Muted ice-blue <-> sea-glass/cream, not a saturated hue wheel. */
 float t=sp_clamp(theme,-1,1)*18;
 float q=sp_clamp(gradient,-1,1)*16*(x*(2.f/(SPARKLES_W-1))-1);
 *r=(int)sp_clamp(*r-t+q,0,255);
 *g=(int)sp_clamp(*g+t*.20f+q*.20f,0,255);
 *b=(int)sp_clamp(*b+t*.55f-q*.55f,0,255);
}
static inline uint16_t sp_pack_lcd(unsigned r,unsigned g,unsigned b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
/* ---- Swipe styles (Sparkles page: swipe left = next, right = previous, wrapping; home_ui.h) ----
 * 0 Sea: light blue water with white star glints.
 * 1 Sunset: a dusk sky graded from violet (top) through coral to a golden horizon (bottom), with
 *   glowing paper lanterns that drift UP and sway instead of twinkling in place. Touch is the same
 *   cluster/hold/trail logic as Sea, but each point is a lantern floating up from where it was born.
 * A lantern = a warm halo in the glow colour + a lit-paper core (core colour where the mask is
 * strong). The water field still breathes faintly under Sunset (luminance scaled by gain/8). */
#define SP_STYLES 2
#define SP_STYLE_FADE .45f
#define SP_SUNSET 1
typedef struct {short base[3],gain[3],glow[3],core[3],edge[3];} sp_palette;
static const sp_palette sp_palettes[SP_STYLES]={
 {{149,187,201},{8,5,4},{247,246,230},{247,246,230},{242,240,238}},  /* Sea (base formula is exact below) */
 {{226,112,98},{3,2,2},{255,160,64},{255,238,196},{244,232,220}}};  /* Sunset: coral mid-sky (graded below) */
/* Sunset sky stops (top -> middle -> horizon) at t = y/(H-1) = 0, .55, 1. */
static const short sp_sunset_sky[3][3]={{70,44,104},{226,112,98},{255,186,104}};
static inline int sp_style_clamp(int style){return style<0?0:(style>=SP_STYLES?SP_STYLES-1:style);}
/* Weight (0..256) of the current style against style_from, at wall-clock scene time t. */
static inline unsigned sp_style_mix(const sparkles_state*s,float t){
 if(s->style==s->style_from)return 256;
 return (unsigned)(sp_smooth((t-s->style_at)/SP_STYLE_FADE)*256);
}
static inline void sp_style_rgb(int style,int l,int y,int*r,int*g,int*b){
 if(style<=0||style>=SP_STYLES){*r=149+l;*g=187+l*2/3;*b=201+l/2;return;}
 const sp_palette*p=&sp_palettes[style];
 int t=y*1024/(SPARKLES_H-1),lo=t<563?0:1,f=lo?(t-563)*256/(1024-563):t*256/563;
 const short*a=sp_sunset_sky[lo],*c=sp_sunset_sky[lo+1];
 *r=(a[0]*(256-f)+c[0]*f)/256+l*p->gain[0]/8;*g=(a[1]*(256-f)+c[1]*f)/256+l*p->gain[1]/8;*b=(a[2]*(256-f)+c[2]*f)/256+l*p->gain[2]/8;
}
/* Water-base keyframe colour at row y (10 bits per channel, as sp_direct_base stores it). */
static inline uint32_t sp_style_base(const sparkles_state*s,int l,int y,unsigned f){
 int r,g,b;sp_style_rgb(s->style,l,y,&r,&g,&b);
 if(f<256){int r0,g0,b0;sp_style_rgb(s->style_from,l,y,&r0,&g0,&b0);
  r=(r0*(int)(256-f)+r*(int)f)>>8;g=(g0*(int)(256-f)+g*(int)f)>>8;b=(b0*(int)(256-f)+b*(int)f)>>8;}
 r=r<0?0:(r>1023?1023:r);g=g<0?0:(g>1023?1023:g);b=b<0?0:(b>1023?1023:b);
 return (unsigned)r|((unsigned)g<<10)|((unsigned)b<<20);
}
/* One palette colour (0 glow, 1 core, 2 edge) crossfaded from style_from. */
static inline uint16_t sp_style_color(const sparkles_state*s,int which,unsigned f){
 const sp_palette*pa=&sp_palettes[sp_style_clamp(s->style_from)],*pb=&sp_palettes[sp_style_clamp(s->style)];
 const short*a=which==0?pa->glow:(which==1?pa->core:pa->edge),*b=which==0?pb->glow:(which==1?pb->core:pb->edge);
 unsigned c[3];for(int i=0;i<3;i++)c[i]=(unsigned)((a[i]*(int)(256-f)+b[i]*(int)f)>>8);
 return sp_pack_lcd(c[0],c[1],c[2]);
}
/* A Sunset lantern over its life u in [0,1], born at cx,cy: it rises ~96 px, sways side to side,
 * fades in and out gently (no twinkle), flickers softly and shrinks a little as it drifts away. */
static inline void sp_lantern(uint32_t seed,float u,float cx,float cy,float*x,float*y,float*scale,float*alpha){
 u=sp_clamp(u,0,1);float r=sp_random(seed+5)*6.2831853f;
 *x=cx+9*sinf(u*4.712389f+r);*y=cy+(.35f-u)*96;
 *scale=1.08f-.34f*u;
 *alpha=sp_smooth(u*(1.f/.22f))*(1-sp_smooth((u-.66f)*(1.f/.34f)))*(.86f+.14f*sinf(u*31.f+r*3));
}
/* A touch/trail point that floats up for Sunset: the same point, moved by its own life. */
static inline void sp_float_up(uint32_t seed,float life,float*x,float*y){
 float r=sp_random(seed+9)*6.2831853f;life=sp_clamp(life,0,1);
 /* eases off the finger, then rises steadily (~72 px) with a slow sway, like a released lantern */
 *x+=10*sinf(life*4.712389f+r);*y-=72*life*(.35f+.65f*life);
}
#define SP_LANTERN 3
static inline int sp_style_species(int style,int species){return style==SP_SUNSET?SP_LANTERN:species;}
/* Native LCD-order output, SPARKLES_W pixels per row. */
#define SP_DIRECT_BASE_W (SPARKLES_W/4)
#define SP_DIRECT_BASE_H (SPARKLES_H/4)
#define SP_DIRECT_BASE_PIXELS (SP_DIRECT_BASE_W*SP_DIRECT_BASE_H)
static uint32_t *sp_direct_base,*sp_direct_previous,*sp_direct_frame;
static unsigned sp_direct_mix;
static inline uint32_t sp_mix_base(uint32_t a,uint32_t b,unsigned f){
 unsigned r=sp_lerp(a&1023,b&1023,f),g=sp_lerp((a>>10)&1023,(b>>10)&1023,f),bl=sp_lerp((a>>20)&1023,(b>>20)&1023,f);
 return r|(g<<10)|(bl<<20);
}
static float sp_direct_water_time=-1;
static unsigned sp_direct_water_updates;
/* Water/base cache: no more than 8Hz. Glints/contact composite on every frame. */
static __attribute__((noinline)) void sp_composite(const sparkles_state*s,uint16_t*out,unsigned stride,int water,int previous,unsigned style_f){
 int16_t tint[SPARKLES_W/2][3];
 int tinted=!s->style&&!s->style_from&&(s->theme!=0||s->gradient!=0);  /* drag tint: Sea only */
 if(tinted)for(int x=0;x<SPARKLES_W;x+=2){
  int r=128,g=128,b=128;sp_direct_tint(&r,&g,&b,s->theme,s->gradient,x);
  tint[x/2][0]=r-128;tint[x/2][1]=g-128;tint[x/2][2]=b-128;
 }
 /* Water is already a broad blurred field: keyframes at 92x112 avoid
  * repeating the same interpolation four times. Glints/grain remain 2px. */
 if(water)for(int y=0;y<SPARKLES_H;y+=4)for(int x=0;x<SPARKLES_W;x+=4){
  unsigned ci=(y/4)*SP_DIRECT_BASE_W+x/4;
  int l=(int)sp_field_sample(sp_blur,x,y)-115;
  uint32_t next=s->style||s->style_from?sp_style_base(s,l,y,style_f):(unsigned)(149+l)|((unsigned)(187+l*2/3)<<10)|((unsigned)(201+l/2)<<20);
  sp_direct_previous[ci]=previous?sp_direct_base[ci]:next;sp_direct_base[ci]=next;
 }
 for(int y=0;y<SPARKLES_H;y+=4)for(int x=0;x<SPARKLES_W;x+=4){
  unsigned i=(y/4)*SP_DIRECT_BASE_W+x/4;
  uint32_t v=sp_mix_base(sp_direct_previous[i],sp_direct_base[i],sp_direct_mix);
  int grain=sp_grain(y*SPARKLES_W+x);
  int r=(v&1023)+grain,g=((v>>10)&1023)+grain,b=((v>>20)&1023)+grain;
  if(tinted){r+=tint[x/2][0];g+=tint[x/2][1];b+=tint[x/2][2];}
  r=r<0?0:(r>255?255:r);g=g<0?0:(g>255?255:g);b=b<0?0:(b>255?255:b);
  sp_direct_frame[i]=sp_pack_lcd(r,g,b);
 }
 int styled=s->style||s->style_from;
 const uint16_t glint=styled?sp_style_color(s,0,style_f):sp_pack_lcd(247,246,230);
 const uint16_t core=styled?sp_style_color(s,1,style_f):glint;
 /* out rows are 4-byte aligned (stride and x even, u16 buffer from calloc): one 32-bit store
  * writes both pixels of a 2x2 block row. Plain aligned stores, never memcpy (GCC emitted a call). */
 int aligned=(((uintptr_t)out|(stride*2u))&3u)==0;
 for(int y=0;y<SPARKLES_H;y+=2){const uint8_t*mrow=sp_mask+(y>>1)*SP_MW;uint16_t*o0=out+y*stride,*o1=o0+stride;
 for(int x=0;x<SPARKLES_W;x+=2){
  uint16_t p=sp_direct_frame[(y/4)*SP_DIRECT_BASE_W+x/4];
  unsigned a=mrow[x>>1];if(a){p=sp_blend565(p,glint,a);if(styled&&a>140)p=sp_blend565(p,core,(a-140)*255/115);}  /* lantern: warm halo, lit-paper core */
  /* Full screen on the panel: no printed-paper frame. */
  if(aligned){uint32_t pp=(uint32_t)p|((uint32_t)p<<16);*(uint32_t*)(void*)(o0+x)=pp;*(uint32_t*)(void*)(o1+x)=pp;}
  else{o0[x]=o0[x+1]=o1[x]=o1[x+1]=p;}
 }}
}
static inline void sp_render_output(const sparkles_state*s,uint16_t*out,unsigned stride){
 float wall_time=s->time;
 sparkles_state animated=*s;animated.time=sp_visual_time(s->time);
 animated.contact_start=sp_visual_time(s->contact_start);s=&animated;
 sp_init();memset(sp_mask,0,SP_MASK_PIXELS);
 int previous=sp_direct_water_time>=0&&wall_time>=sp_direct_water_time;
 int water=!previous||wall_time-sp_direct_water_time>=.125f;
 if(!sp_direct_base){
  sp_direct_base=malloc(SP_DIRECT_BASE_PIXELS*sizeof(uint32_t));
  sp_direct_previous=malloc(SP_DIRECT_BASE_PIXELS*sizeof(uint32_t));
  sp_direct_frame=malloc(SP_DIRECT_BASE_PIXELS*sizeof(uint32_t));assert(sp_direct_base&&sp_direct_previous&&sp_direct_frame);
 }
 sp_direct_mix=water?0:(unsigned)(sp_smooth((wall_time-sp_direct_water_time)/.125f)*256);
 SP_STAMP(0);
 if(water){
 float az=-2.8f-.5f*s->x,el=.85f+.3f*s->y;
 sp_v3 sun={sinf(el)*cosf(az),cosf(el),sinf(el)*sinf(az)};
 for(int j=0;j<SPARKLES_GH;j++)for(int i=0;i<SPARKLES_GW;i++){
  int k=j*SPARKLES_GW+i;sp_v3 d=sp_rays[k];sparkles_ray p=sp_trace(s,d);
  float eps=.22f;
  float nx=sparkles_height(s,p.x-eps,p.y)-sparkles_height(s,p.x+eps,p.y);
  float nz=sparkles_height(s,p.x,p.y-eps)-sparkles_height(s,p.x,p.y+eps);
  sp_v3 n=sp_norm((sp_v3){nx,2*eps,nz});sp_v3 half=sp_norm((sp_v3){sun.x-d.x,sun.y-d.y,sun.z-d.z});
  float a=sp_clamp(n.x*half.x+n.y*half.y+n.z*half.z,0,1);
  /* pow(a,256), no software libm per-pixel. */
  for(int q=0;q<8;q++)a*=a;
  float crest=sp_clamp(sparkles_height(s,p.x,p.y)/2.7f+.5f,0,1);
  sp_field[k]=(uint8_t)sparkles_byte(70+crest*95+a*130);
 }
 /* Approximate the reference 100px Poisson blur with bounded 3x3 box.
    Blurring before palette/grain is essential: don't show sine contours. */
 for(int y=0;y<SPARKLES_GH;y++)for(int x=0;x<SPARKLES_GW;x++){
  unsigned sum=0,count=0;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
   int xx=x+dx,yy=y+dy;if(xx>=0&&xx<SPARKLES_GW&&yy>=0&&yy<SPARKLES_GH){sum+=sp_field[yy*SPARKLES_GW+xx];count++;}}
  sp_blur[y*SPARKLES_GW+x]=sum/count;
 }
 }
 SP_STAMP(1);
 /* Reference cycles 0.8..1.2s, shape changes each cycle; .2s speckles,
    0.8/1s stars, 1.5s circles. Reference stars are screen-anchored cells.
    Ambient twinkle now runs every frame regardless of touch or session, so
    the field always shimmers. Cycle periods are lengthened and each sparkle's
    on-window (duration) is kept below its period so a full eased fade-in /
    hold / fade-out lifecycle is visible even at the ~2-3 FPS device budget;
    at higher rates it simply reads as a denser shifting field of glints. */
 /* Sunset: the NEW style's lanterns from the swipe on (the palette crossfades underneath). */
 int style=sp_style_clamp(s->style);
 float rotation=-6.2831853f/14*(.5f+.5f*sinf(s->time*.3f));
 float stretch=1+.6f*powf(fmaxf(sinf(s->time*3.14159265f),0),2);
 if(!s->quiet){
 /* Ambient DENSITY tracks the runtime usage scalar: higher usage => more
  * frequent, denser glints; lower usage => a sparser, calmer field. This
  * scales ONLY the ambient twinkle; the touch burst/held pattern below is
  * fully independent of usage. Total ambient stamps are hard-capped at
  * SP_USAGE_MAX_GLINTS so idle FPS stays bounded even at usage=1. */
 /* A live token-density level (s->density 1..6) replaces the usage mapping
  * with one of six steps that also raise brightness/size. Cells are visited
  * in a fixed coprime-stride permutation so the cap thins the field evenly
  * rather than starving the lower rows (max-blend makes order irrelevant
  * when uncapped). */
 sp_density_step dstep=sp_density(s->density,s->usage);
 float udens=dstep.spawn,uspeed=dstep.speed;
 int ambient_stamps=0;
 for(int cell=0;cell<630;cell++){
  int perm=(cell*257)%630,gx=perm%30,gy=perm/30;
  /* Sunset lanterns are ~5x the area of a Sea glint: a third of the cap keeps the frame rate at the
   * busiest level (measured on device: 29 ms of lantern stamping per frame uncapped) and reads as a
   * sky of lanterns rather than a crowd. */
  if(ambient_stamps>=(style?(dstep.cap+2)/3:dstep.cap))goto ambient_capped;
  unsigned seed=sp_hash(gx+gy*31+719);float period=1.6f+.8f*sp_random(seed);
  float local=s->time*uspeed+sp_random(seed+1)*period,phase=local-floorf(local/period)*period;
  unsigned cycle=(unsigned)floorf(local/period),pick=sp_hash(seed+cycle*977);
  int cx=(int)((gx+.5f)*SPARKLES_W/30),cy=(int)((gy+.5f)*SPARKLES_H/21);
  float lum=sp_field_sample(sp_field,cx,cy)*(1.f/255.f);
  /* Brightness-gated sparkles, predominantly left where sun reflects, with a
     small floor so the whole field twinkles sparsely as in the reference.
     3-octave normals omit GPU high frequencies: integrate their probability
     rather than flashing arbitrary fixed stars. The usage factor udens then
     scales this spawn probability up (busy) or down (sparse). */
  float left=1.f-(float)cx/SPARKLES_W;
  /* Sunset spreads lanterns evenly (no sun side), fewer of them, each with a long rising life. */
  float chance=sp_clamp((lum-.20f)*.26f,.012f,.24f)*(style?.62f:.35f+left)*udens;
  if(sp_random(pick)>chance)continue;
  int species=(pick>>17)%3;float duration=species==0?1.2f:(species==1?.8f:1.5f);
  if(style)duration=period*.95f;
  if(phase>=duration)continue;
  float alpha_env,scale_env;sp_twinkle_env(phase/duration,&alpha_env,&scale_env);
  float radius=(species==1?11.f:25.f)*( .9f+.2f*sp_random(seed+3));
  if(style){
   /* small (species 1, farther away) and large lanterns, each floating up over its life */
   float px,py,ps,pa,size=species==1?.42f:.62f+.22f*sp_random(seed+4);
   sp_lantern(pick,phase/duration,(float)cx,(float)cy,&px,&py,&ps,&pa);
   sp_glint((int)px,(int)py,radius,pa*dstep.alpha,SP_LANTERN,rotation,1,ps*dstep.scale*size);
  }else
  sp_glint(cx,cy,radius,alpha_env*dstep.alpha,species,rotation,stretch,scale_env*dstep.scale);
  ++ambient_stamps;
 }
 ambient_capped:;
 } /* Only automatic glints are gated; touch is independent. */
 /* A contact excites a bounded cluster of the same transient star/circle
    sprites, with a ~0.36s exponential release tail.
    The cluster lives at a fixed ANCHOR (direct_input.h). Holding keeps it
    animating/escalating in place; moving past the re-anchor radius starts a fresh
    cluster at the new spot (old glints are never transported). Trail births are
    drawn in addition, below. */
 if(s->strength>.002f){
  float tx=(s->anchor_x+1)*.5f*(SPARKLES_W-1),ty=(s->anchor_y+1)*.5f*(SPARKLES_H-1);
  /* --- Touch: organic random sparkle cluster at the finger ---
   * Down: a flurry of independently timed sparkles fades in around the
   * contact. Held: continuous random respawn escalating through six hold
   * levels (more/brighter/larger/wider), no rings or spirals. Everything
   * rides s->strength so the existing release decay fades it in place.
   * Hard-capped at SP_HOLD_MAX stamps (see sp_touch_cluster). */
  sp_cluster_glint cluster[SP_HOLD_MAX];
  int count=sp_touch_cluster(s,tx,ty,cluster,SP_HOLD_MAX);
  if(style==SP_SUNSET){
   /* The finger holds a lit lantern: it follows the finger, breathes, and its light grows over
    * the six hold levels (0.6x -> 1.3x); released, it fades with strength. The cluster's small
    * lanterns keep floating up out of it. */
   float frac;int level=sp_hold_level(s->hold,&frac);float lv=level+(level<SP_HOLD_LEVELS-1?frac:0);
   float fx=(s->touch_x+1)*.5f*(SPARKLES_W-1),fy=(s->touch_y+1)*.5f*(SPARKLES_H-1);
   float breath=sinf(s->time*3.1f);
   sp_glint((int)fx,(int)(fy-4-3*breath),25,s->strength*(.82f+.12f*breath),SP_LANTERN,0,1,.6f+.14f*lv+.03f*breath);
  }
  for(int i=0;i<count;i++){
   if(cluster[i].alpha<.01f)continue;
   int sp=sp_style_species(style,cluster[i].species);
   float gx=cluster[i].x,gy=cluster[i].y;
   if(sp==SP_LANTERN)sp_float_up(sp_hash((uint32_t)i*2654435761u+(uint32_t)(gx*7+gy*13)),cluster[i].life,&gx,&gy);
   sp_glint((int)gx,(int)gy,sp==1?11.f:25.f,cluster[i].alpha,sp,rotation,stretch,cluster[i].scale*(sp==SP_LANTERN?.8f:1));
  }
 }
 /* Anchored particles use the input wall clock, not the slowed water clock.
  * No strength multiplier: releasing/moving cannot relocate or erase the trail. */
 for(unsigned i=0;i<s->trail_count&&i<SP_TRAIL_MAX;i++){
  const sp_trail_particle*t=&s->trail[i];float age=wall_time-t->born;
  if(age<0||age>=SP_TRAIL_LIFE)continue;
  float fade=1-age/SP_TRAIL_LIFE;fade=fade*fade*(3-2*fade);
  unsigned seed=sp_hash(i+7919);int species=sp_style_species(style,(int)(seed%3));
  float jx=(sp_random(seed+1)-.5f)*8,jy=(sp_random(seed+2)-.5f)*12,gx=t->x+jx,gy=t->y+jy;
  if(species==SP_LANTERN){
   /* Moving under Sunset: every third trail birth releases a lantern that lifts off the finger's
    * path, sways and fades as it rises (bigger sprite, so fewer of them: a wake, not a smear). */
   if((seed>>5)%3)continue;
   float u=age/SP_TRAIL_LIFE;sp_float_up(seed,u,&gx,&gy);
   float in=sp_smooth(u*(1.f/.12f)),glow=sp_clamp(in*(1.15f-.55f*u*u),0,1);  /* bright while it lifts, fades late */
   sp_glint((int)gx,(int)gy,18.f,glow,species,0,1,(.42f+.22f*sp_random(seed+3))*(1.1f-.3f*u));
   continue;
  }
  sp_glint((int)gx,(int)gy,species==1?11.f:18.f,fade*.88f,species,rotation,stretch,.55f+.4f*fade);
 }
 SP_STAMP(2);
 sp_composite(s,out,stride,water,previous,sp_style_mix(s,wall_time));
 if(water){sp_direct_water_time=wall_time;++sp_direct_water_updates;}
 SP_STAMP(3);
}
static inline int sparkles_render_direct(const sparkles_state*s,uint16_t*out,size_t pixels){
 if(!s||!out||pixels<SPARKLES_PIXELS)return 0;
 sp_render_output(s,out,SPARKLES_W);return 1;
}
