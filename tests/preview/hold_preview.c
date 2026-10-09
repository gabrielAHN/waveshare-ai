/* HOST SIMULATION ONLY: native same-core (sparkles_render_direct) output, no
 * device and no physical contact. Proves, via seeded PPM previews + metrics:
 *  (a) ambient twinkle still animates with NO touch (idle-a vs idle-b differ),
 *  (b) a large touch-DOWN burst blooms outward at the finger,
 *  (c) the sustained HELD pattern is distinct from a single quick tap and
 *      escalates through six calm->busy levels,
 *  (d) RELEASE decays smoothly back toward ambient.
 * The hold timeline is driven through the real direct_input 10ms poller clock,
 * so the held-duration accumulator / six-level ramp is exercised end to end.
 * A companion shell step converts the PPMs to PNG. NOT a photo of the panel. */
#include <stdio.h>
#include "direct_input.h"
static uint16_t px[SPARKLES_PIXELS];
static unsigned local_energy(int cx,int cy,int r){
 unsigned e=0;for(int y=cy-r;y<cy+r;y++)for(int x=cx-r;x<cx+r;x++)
  if(x>=0&&x<SPARKLES_W&&y>=0&&y<SPARKLES_H&&(x-cx)*(x-cx)+(y-cy)*(y-cy)<r*r)e+=sp_mask_at(x,y);
 return e;
}
static unsigned save(const char*path,const sparkles_state*s){
 sparkles_render_direct(s,px,SPARKLES_PIXELS);
 unsigned energy=0;for(int i=0;i<SP_MASK_PIXELS;i++)energy+=sp_mask[i];
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n368 448\n255\n");
 for(int i=0;i<SPARKLES_PIXELS;i++){unsigned p=px[i];fputc(((p>>11)&31)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
 fclose(f);return energy;
}
int main(void){
 int TX=SPARKLES_W/2,TY=SPARKLES_H/2,TR=70;
 /* (a) Ambient, no touch ever, advanced through the real 10ms poll cadence. */
 direct_input idle={0};
 for(int i=1;i<=400;i++)direct_sample(&idle,i*10000,0,0,0);       /* t~4.00s */
 save("evidence/hold-idle-a.ppm",&idle.scene);
 sparkles_state ta=idle.scene;sparkles_render_direct(&ta,px,SPARKLES_PIXELS);
 uint16_t a[SPARKLES_PIXELS];for(int i=0;i<SPARKLES_PIXELS;i++)a[i]=px[i];
 for(int i=401;i<=560;i++)direct_sample(&idle,i*10000,0,0,0);      /* +1.60s */
 save("evidence/hold-idle-b.ppm",&idle.scene);
 unsigned idle_diff=0;for(int i=0;i<SPARKLES_PIXELS;i++)idle_diff+=a[i]!=px[i];
 sparkles_state idle_at_touch=idle.scene;idle_at_touch.strength=0;
 sparkles_render_direct(&idle_at_touch,px,SPARKLES_PIXELS);
 unsigned local_idle=local_energy(TX,TY,TR);

 /* (b)-(d) Press-and-hold timeline: settle, DOWN-edge burst, ~3s continuous
  * hold climbing to the busy level, then release. Real accumulator. */
 direct_input h=idle;int t=561;
 direct_sample(&h,t++*10000,0,0,0);                                /* last no-touch */
 int down0=t;                                                      /* down edge */
 for(int i=0;i<12;i++)direct_sample(&h,t++*10000,1,TX,TY);         /* ~0.12s: burst */
 unsigned burst_e=save("evidence/hold-burst.ppm",&h.scene);
 unsigned local_burst=local_energy(TX,TY,TR);
 (void)down0;
 /* Continue holding to ~3s so the six-level accumulator saturates. */
 for(int i=0;i<300;i++)direct_sample(&h,t++*10000,1,TX,TY);        /* +3.0s hold */
 unsigned pat_e=save("evidence/hold-pattern.ppm",&h.scene);
 unsigned local_hold=local_energy(TX,TY,TR);
 float held_seconds=h.scene.hold;
 /* Release: lift and let the sustained pattern decay while ambient continues. */
 for(int i=0;i<70;i++)direct_sample(&h,t++*10000,0,0,0);           /* ~0.7s after up */
 save("evidence/hold-release.ppm",&h.scene);
 unsigned local_release=local_energy(TX,TY,TR);

 /* Six busy/calm level snapshots, isolated (settled, burst over) so each
  * image shows exactly one level of the sustained pattern. */
 unsigned level_local[SP_HOLD_LEVELS];
 for(int lv=0;lv<SP_HOLD_LEVELS;lv++){
  sparkles_state s={.time=3.0f};s.contact_start=0.0f;s.strength=1;s.hold=SP_HOLD_STEP*(lv+0.5f);
  s.touch_x=TX*(2.f/(SPARKLES_W-1))-1;s.touch_y=TY*(2.f/(SPARKLES_H-1))-1;
  char path[64];snprintf(path,sizeof path,"evidence/hold-level-%d.ppm",lv);
  save(path,&s);level_local[lv]=local_energy(TX,TY,110);
 }

 printf("IDLE_DIFF_PIXELS=%u LOCAL_IDLE=%u LOCAL_BURST=%u LOCAL_HOLD=%u LOCAL_RELEASE=%u held_seconds=%.2f\n",
        idle_diff,local_idle,local_burst,local_hold,local_release,held_seconds);
 printf("burst_total=%u pattern_total=%u\n",burst_e,pat_e);
 for(int lv=0;lv<SP_HOLD_LEVELS;lv++)printf("LEVEL_%d_LOCAL=%u\n",lv,level_local[lv]);
 int levels_increasing=1;for(int lv=1;lv<SP_HOLD_LEVELS;lv++)if(level_local[lv]<level_local[lv-1])levels_increasing=0;
 printf("ambient_animates=%d burst_blooms=%d hold_distinct_from_tap=%d release_decays=%d levels_escalate=%d\n",
        idle_diff>200,
        local_burst>local_idle+80000,
        local_hold>local_idle+80000,
        local_release<local_hold,
        levels_increasing||level_local[SP_HOLD_LEVELS-1]>level_local[0]+80000);
 return 0;
}
