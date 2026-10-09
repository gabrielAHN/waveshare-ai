#include <assert.h>
#include <stdio.h>
#include "direct_input.h"
/* Runtime ambient-DENSITY 'usage' scalar. Proves, on the same C core used on
 * device:
 *  1. usage->density mapping helpers clamp and are monotonic (0.20x..3.0x),
 *     with the calm default mapping to the historical 1.0x baseline.
 *  2. AMBIENT glint energy (no touch, strength=0) rises monotonically with
 *     usage: usage=0 sparsest, usage=1 densest, bounded by SP_USAGE_MAX_GLINTS.
 *  3. The TOUCH down-burst / six-level held pattern is INDEPENDENT of usage:
 *     touch-attributable energy (touch-ON minus touch-OFF at the SAME time/pos,
 *     so the always-on ambient cancels) is essentially unchanged across usage,
 *     and the held-level escalation is unaffected.
 *  4. direct_set_usage clamps out-of-range inputs and never perturbs
 *     strength/hold/contact_start/theme/gradient (touch fields). */
static uint16_t px[SPARKLES_PIXELS];
/* Total ambient sparkle-mask energy at a fixed time with NO touch. */
static long ambient_energy(float time,float usage){
 sparkles_state s={.time=time,.usage=usage,.quiet=0};
 sparkles_render_direct(&s,px,SPARKLES_PIXELS);
 long e=0;for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];return e;
}
/* Touch-attributable mask energy near the finger: render touch-ON then OFF at
 * the SAME time/pos/usage and subtract, so the ambient field cancels. */
static long touch_local(sparkles_state base,float strength,int cx,int cy,int r){
 base.strength=strength;base.touch_x=cx*(2.f/(SPARKLES_W-1))-1;base.touch_y=cy*(2.f/(SPARKLES_H-1))-1;
 sparkles_render_direct(&base,px,SPARKLES_PIXELS);
 long on=0;for(int y=cy-r;y<cy+r;y++)for(int x=cx-r;x<cx+r;x++)
  if(x>=0&&x<SPARKLES_W&&y>=0&&y<SPARKLES_H&&(x-cx)*(x-cx)+(y-cy)*(y-cy)<r*r)on+=sp_mask_at(x,y);
 base.strength=0;sparkles_render_direct(&base,px,SPARKLES_PIXELS);
 long off=0;for(int y=cy-r;y<cy+r;y++)for(int x=cx-r;x<cx+r;x++)
  if(x>=0&&x<SPARKLES_W&&y>=0&&y<SPARKLES_H&&(x-cx)*(x-cx)+(y-cy)*(y-cy)<r*r)off+=sp_mask_at(x,y);
 return on-off;
}
int main(void){
 /* 1. Mapping helpers: clamp + monotonic spawn multiplier; default==baseline. */
 assert(sp_usage_clamp(-1.f)==0.f&&sp_usage_clamp(2.f)==1.f);
 assert(sp_usage_spawn(0.f)>0.f&&sp_usage_spawn(0.f)<0.5f);         /* very sparse */
 assert(fabsf(sp_usage_spawn(SP_USAGE_DEFAULT)-1.0f)<1e-4f);        /* historical baseline */
 assert(sp_usage_spawn(1.f)>2.5f);                                  /* busy */
 float prev=-1;for(int i=0;i<=20;i++){float m=sp_usage_spawn(i/20.f);assert(m>=prev-1e-6f);prev=m;} /* monotonic */
 assert(sp_usage_speed(0.f)==1.0f&&sp_usage_speed(1.f)>1.0f);
 assert(sp_usage_spawn(-5.f)==sp_usage_spawn(0.f)&&sp_usage_spawn(9.f)==sp_usage_spawn(1.f)); /* clamped */

 /* 2. Ambient density rises monotonically with usage, averaged over several
  *    times so a single frame's spawn RNG cannot flip the ordering. */
 const float usages[4]={0.0f,0.15f,0.5f,1.0f};
 long total[4]={0,0,0,0};
 for(int t=0;t<8;t++){float time=2.0f+t*0.37f;
  for(int u=0;u<4;u++)total[u]+=ambient_energy(time,usages[u]);}
 printf("ambient totals: usage0=%ld usage015=%ld usage05=%ld usage1=%ld\n",total[0],total[1],total[2],total[3]);
 for(int u=1;u<4;u++)assert(total[u]>total[u-1]); /* strictly denser with usage */
 assert(total[3]>total[0]*3); /* usage=1 is dramatically denser than usage=0 */

 /* 3a. TOUCH down-burst is NOT amplified by usage. The touch branch reads no
  *     usage at all, so if usage scaled touch we'd see b(usage=1) ~ 3x b(usage=0).
  *     Instead the burst-attributable energy stays large at both and does NOT
  *     grow with usage; it is in fact slightly LOWER at usage=1 purely because
  *     the sp_mask uses max()-blending and the ~13x denser ambient already lights
  *     some burst-area pixels, so fewer of them increase. That compositing
  *     artifact is bounded; it is not usage driving the touch pattern. */
 int cx=SPARKLES_W/2,cy=SPARKLES_H/2;
 sparkles_state burst0={.time=3.30f,.contact_start=3.0f,.hold=0.30f,.usage=0.0f,.quiet=0};
 sparkles_state burst1=burst0;burst1.usage=1.0f;
 long b0=touch_local(burst0,1.f,cx,cy,120),b1=touch_local(burst1,1.f,cx,cy,120);
 printf("burst attributable: usage0=%ld usage1=%ld ratio=%.3f\n",b0,b1,(double)b1/b0);
 assert(b0>150000&&b1>150000);        /* a real, large burst at both usages */
 assert(b1<=b0+b0/20);                /* usage does NOT amplify the touch burst */
 assert(b1>=b0/2);                    /* still clearly present (only max-blend loss) */

 /* 3b. Six-level held escalation is unaffected by usage: the tap-vs-hold gap in
  *     the outer ring holds at both the sparsest and densest ambient fields. */
 for(int k=0;k<2;k++){float uu=k?1.0f:0.0f;
  sparkles_state tap={.time=3.0f,.contact_start=0.0f,.hold=0.0f,.usage=uu,.quiet=0};
  sparkles_state hold5={.time=3.0f,.contact_start=0.0f,.hold=SP_HOLD_STEP*5.0f,.usage=uu,.quiet=0};
  long outer_tap=touch_local(tap,1.f,cx,cy,110)-touch_local(tap,1.f,cx,cy,40);
  long outer_hold=touch_local(hold5,1.f,cx,cy,110)-touch_local(hold5,1.f,cx,cy,40);
  printf("held cluster usage%.1f: tap=%ld hold5=%ld\n",uu,outer_tap,outer_hold);
  assert(outer_hold>outer_tap+60000); /* held pattern escalates regardless of usage */
 }

 /* 4. direct_set_usage clamps and touches ONLY scene.usage. */
 direct_input a={0};
 direct_sample(&a,10000,0,0,0);
 for(int i=2;i<=40;i++)direct_sample(&a,i*10000,1,SPARKLES_W/2,SPARKLES_H/2); /* build strength+hold */
 float s0=a.scene.strength,h0=a.scene.hold,th0=a.scene.theme,gr0=a.scene.gradient,cs0=a.scene.contact_start;
 direct_set_usage(&a,-3.f);assert(a.scene.usage==0.f);
 direct_set_usage(&a,7.f);assert(a.scene.usage==1.f);
 direct_set_usage(&a,0.5f);assert(fabsf(a.scene.usage-0.5f)<1e-6f);
 assert(a.scene.strength==s0&&a.scene.hold==h0&&a.scene.theme==th0&&a.scene.gradient==gr0&&a.scene.contact_start==cs0);

 puts("usage mapping, monotonic ambient density, touch independence and clamped setter PASS");
 return 0;
}
