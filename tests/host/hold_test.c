#include <assert.h>
#include <stdio.h>
#include "direct_input.h"
/* Touch-and-hold: big down-burst, a sustained six-level calm->busy pattern that
 * is distinct from a quick tap and from ambient, and smooth release decay.
 * All measurements difference touch-ON against touch-OFF at the SAME scene time
 * and finger position, so the always-on ambient field cancels and only the
 * touch/hold contribution to the glint mask is measured. */
static uint16_t px[SPARKLES_PIXELS];
static long annulus(int cx,int cy,int r0,int r1){
 long e=0;for(int y=cy-r1;y<=cy+r1;y++)for(int x=cx-r1;x<=cx+r1;x++){
  if(x<0||x>=SPARKLES_W||y<0||y>=SPARKLES_H)continue;
  int d2=(x-cx)*(x-cx)+(y-cy)*(y-cy);
  if(d2>=r0*r0&&d2<r1*r1)e+=sp_mask_at(x,y);
 }
 return e;
}
/* Touch-attributable mask energy in an annulus: render with strength then with
 * strength 0 at the same time/pos and subtract, so ambient cancels out. */
static long touch_annulus(sparkles_state base,float strength,int cx,int cy,int r0,int r1){
 base.strength=strength;base.touch_x=cx*(2.f/(SPARKLES_W-1))-1;base.touch_y=cy*(2.f/(SPARKLES_H-1))-1;
 sparkles_render_direct(&base,px,SPARKLES_PIXELS);long on=annulus(cx,cy,r0,r1);
 base.strength=0;sparkles_render_direct(&base,px,SPARKLES_PIXELS);long off=annulus(cx,cy,r0,r1);
 return on-off;
}
int main(void){
 /* 1. Six discrete levels, calm(0)->busy(5), correctly clamped, fraction sane. */
 float f;
 assert(sp_hold_level(0,&f)==0&&f<1e-6f);
 assert(sp_hold_level(SP_HOLD_STEP*0.5f,&f)==0&&f>.4f&&f<.6f);
 assert(sp_hold_level(SP_HOLD_STEP*1.0f,&f)==1);
 assert(sp_hold_level(SP_HOLD_STEP*5.0f,&f)==5);
 assert(sp_hold_level(SP_HOLD_STEP*99.f,&f)==5); /* clamped at busiest level */
 assert(SP_HOLD_LEVELS==6);

 /* 2. Hold accumulator: grows only while down; a fresh down-edge restarts it;
  *    a full release resets it so the next press starts calm. */
 direct_input a={0};direct_sample(&a,10000,0,0,0);
 for(int i=2;i<=60;i++)direct_sample(&a,i*10000,1,SPARKLES_W/2,SPARKLES_H/2); /* ~0.58s hold */
 assert(a.scene.hold>.5f);float held=a.scene.hold;
 direct_sample(&a,610000,1,SPARKLES_W/2,SPARKLES_H/2);assert(a.scene.hold>held); /* keeps growing */
 /* release long enough (~3.4s) for strength to fully decay, then hold resets to 0 */
 for(int i=62;i<=400;i++)direct_sample(&a,i*10000,0,0,0);
 assert(a.scene.hold<1e-6f);
 /* fresh press: down-edge zeroes hold, then it climbs from calm again */
 direct_sample(&a,4010000,1,SPARKLES_W/2,SPARKLES_H/2);assert(a.scene.hold<.02f);

 /* 3. Down-burst: right after contact the local mask erupts far more than a
  *    fully-decayed idle instant (big bloom). Isolate the touch contribution. */
 sparkles_state s={.time=3.0f};
 int cx=SPARKLES_W/2,cy=SPARKLES_H/2;
 sparkles_state burst=s;burst.contact_start=3.0f;burst.time=3.30f;burst.hold=0.30f; /* flurry peak (~0.3s) */
 long burst_e=touch_annulus(burst,1,cx,cy,0,120);
 assert(burst_e>150000); /* clearly a large burst near the finger */

 /* 4. Sustained pattern is level-driven and DISTINCT from a quick tap.
  *    Compare a settled tap (level 0) against a long hold (level 5) in the
  *    OUTER annulus the extra rings reach into; burst is over (large age). */
 sparkles_state tap=s;tap.contact_start=0.0f;tap.time=3.0f;tap.hold=0.0f;        /* calm */
 sparkles_state hold5=s;hold5.contact_start=0.0f;hold5.time=3.0f;hold5.hold=SP_HOLD_STEP*5.0f; /* busy */
 long outer_tap=touch_annulus(tap,1,cx,cy,40,110);
 long outer_hold5=touch_annulus(hold5,1,cx,cy,40,110);
 printf("outer annulus energy: tap(level0)=%ld hold(level5)=%ld\n",outer_tap,outer_hold5);
 assert(outer_hold5>outer_tap+80000); /* busy hold fills the ring field; a tap does not */

 /* 5. Monotone-ish escalation across the six levels in the outer band. */
 long prev=-1;int rose=0;
 for(int lv=0;lv<SP_HOLD_LEVELS;lv++){
  sparkles_state h=s;h.contact_start=0.0f;h.time=3.0f;h.hold=SP_HOLD_STEP*(lv+0.5f);
  long e=touch_annulus(h,1,cx,cy,40,120);
  printf("level %d outer energy=%ld\n",lv,e);
  if(e>prev+2000)rose++;prev=e;
 }
 assert(rose>=4); /* busier levels reach measurably further out */

 /* 6. Release decays smoothly: same hold pattern at reduced strength has less
  *    local energy, and near-zero strength returns to ambient (~0 attributable). */
 sparkles_state rel=s;rel.contact_start=0.0f;rel.time=3.0f;rel.hold=SP_HOLD_STEP*5.0f;
 long full=touch_annulus(rel,1.0f,cx,cy,0,120);
 long mid=touch_annulus(rel,0.3f,cx,cy,0,120);
 long gone=touch_annulus(rel,0.001f,cx,cy,0,120);
 printf("release: full=%ld mid=%ld gone=%ld\n",full,mid,gone);
 assert(mid<full&&gone<mid&&gone<20000);

 puts("six-level hold: accumulator, down-burst, tap-vs-hold distinction, escalation and release decay PASS");
 return 0;
}
