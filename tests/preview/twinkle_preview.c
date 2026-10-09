/* HOST SIMULATION ONLY: native same-core output, no device or physical contact.
 * Proves ambient twinkle motion with NO touch and NO live session (idle-a vs
 * idle-b differ), plus a touch bloom and its release. Writes PPM; a companion
 * shell step converts to PNG and reports per-frame ambient sparkle-mask energy
 * and pixel-difference between the two idle instants. */
#include <stdio.h>
#include "direct_input.h"
static uint16_t pixels[SPARKLES_PIXELS];
static unsigned local_energy(int cx,int cy,int r){
 unsigned e=0;for(int y=cy-r;y<cy+r;y++)for(int x=cx-r;x<cx+r;x++)
  if(x>=0&&x<SPARKLES_W&&y>=0&&y<SPARKLES_H&&(x-cx)*(x-cx)+(y-cy)*(y-cy)<r*r)e+=sp_mask_at(x,y);
 return e;
}
static unsigned save(const char *path,const sparkles_state*s){
 assert(sparkles_render_direct(s,pixels,SPARKLES_PIXELS));
 unsigned energy=0;for(int i=0;i<SP_MASK_PIXELS;i++)energy+=sp_mask[i];
 FILE*f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n368 448\n255\n");
 for(int i=0;i<SPARKLES_PIXELS;i++){unsigned p=pixels[i];fputc(((p>>11)&31)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
 fclose(f);return energy;
}
int main(void){
 /* Idle: no touch ever. Advance the interaction clock with contact=0 so the
  * scene time moves exactly like the device idle loop (10ms poll cadence). */
 direct_input idle={0};
 for(int i=1;i<=400;i++)direct_sample(&idle,i*10000,0,0,0); /* t ~ 4.00s, strength 0 */
 unsigned ea=save("evidence/twinkle-idle-a.ppm",&idle.scene);
 for(int i=401;i<=560;i++)direct_sample(&idle,i*10000,0,0,0); /* +1.60s later, still no touch */
 unsigned eb=save("evidence/twinkle-idle-b.ppm",&idle.scene);
 /* Reload idle-a to diff against idle-b (render overwrites the shared mask). */
 uint16_t a[SPARKLES_PIXELS];sparkles_state ta=idle.scene;ta.time=4.0f;ta.strength=0;
 sparkles_render_direct(&ta,a,SPARKLES_PIXELS);
 sparkles_state tb=idle.scene;tb.time=5.6f;tb.strength=0;
 sparkles_render_direct(&tb,pixels,SPARKLES_PIXELS);
 unsigned idle_diff=0;for(int i=0;i<SPARKLES_PIXELS;i++)idle_diff+=a[i]!=pixels[i];
 /* Touch: press-and-hold at a point; capture mid-bloom. Measure LOCAL energy
  * in a window around the finger (global energy is dominated by the ambient
  * field, so it is not a locality test). */
 int TX=250,TY=300,TR=55;
 sparkles_state idle_at_touch=idle.scene;idle_at_touch.strength=0;
 sparkles_render_direct(&idle_at_touch,pixels,SPARKLES_PIXELS);
 unsigned local_idle=local_energy(TX,TY,TR);
 direct_input touch=idle;
 for(int i=561;i<=610;i++)direct_sample(&touch,i*10000,1,TX,TY); /* ~0.5s hold */
 unsigned et=save("evidence/twinkle-touch.ppm",&touch.scene);
 unsigned local_touch=local_energy(TX,TY,TR);
 /* Release: lift and let the local bloom decay while ambient continues. */
 for(int i=611;i<=680;i++)direct_sample(&touch,i*10000,0,0,0); /* ~0.7s after up */
 unsigned er=save("evidence/twinkle-release.ppm",&touch.scene);
 unsigned local_release=local_energy(TX,TY,TR);
 printf("IDLE_A_ENERGY=%u IDLE_B_ENERGY=%u IDLE_DIFF_PIXELS=%u TOUCH_ENERGY=%u RELEASE_ENERGY=%u\n",ea,eb,idle_diff,et,er);
 printf("LOCAL_IDLE=%u LOCAL_TOUCH=%u LOCAL_RELEASE=%u\n",local_idle,local_touch,local_release);
 printf("ambient_alive=%d idle_animates=%d touch_adds_locally=%d release_decays_locally=%d\n",
        ea>0&&eb>0, idle_diff>200, local_touch>local_idle+10000, local_release<local_touch);
 return 0;
}
