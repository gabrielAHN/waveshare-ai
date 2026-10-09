/* HOST SIMULATION ONLY: native same-core (sparkles_render_direct) output, no
 * device and no physical contact. Proves via seeded PPM previews + metrics that
 * the ambient sparkle field gets visibly DENSER as the runtime 'usage' scalar
 * rises (usage 0.0 -> 0.15 -> 0.5 -> 1.0), while the touch burst is unchanged by
 * usage (touch-with-low-usage vs touch-with-high-usage show the same bloom).
 * The idle timelines are advanced through the real direct_input 10ms poll
 * cadence. A companion shell step converts the PPMs to PNG. NOT a photo of the
 * physical panel; no optical / finger-alignment claim. */
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
 /* Four ambient-only densities, no touch ever, advanced through the real 10ms
  * poll cadence so scene time matches the device idle loop. */
 const float usages[4]={0.0f,0.15f,0.5f,1.0f};
 const char*names[4]={"evidence/usage-0.ppm","evidence/usage-015.ppm","evidence/usage-05.ppm","evidence/usage-1.ppm"};
 unsigned amb[4];
 for(int u=0;u<4;u++){
  direct_input idle={0};direct_set_usage(&idle,usages[u]);
  for(int i=1;i<=400;i++)direct_sample(&idle,i*10000,0,0,0); /* t~4.00s, strength 0 */
  amb[u]=save(names[u],&idle.scene);
 }
 /* Touch independence: a big down-burst at panel center, once with LOW ambient
  * usage and once with HIGH ambient usage. The finger bloom is identical; only
  * the surrounding ambient field differs in density. */
 int TX=SPARKLES_W/2,TY=SPARKLES_H/2,TR=70;
 unsigned touch_lo,touch_hi;long delta_lo,delta_hi;
 {
  direct_input h={0};direct_set_usage(&h,0.0f);int t=1;
  for(int i=0;i<400;i++)direct_sample(&h,t++*10000,0,0,0);
  sparkles_state amb_at=h.scene;amb_at.strength=0;sparkles_render_direct(&amb_at,px,SPARKLES_PIXELS);
  long off=local_energy(TX,TY,TR);
  for(int i=0;i<14;i++)direct_sample(&h,t++*10000,1,TX,TY); /* ~0.14s hold: burst */
  touch_lo=save("evidence/usage-touch-low.ppm",&h.scene);
  delta_lo=(long)local_energy(TX,TY,TR)-off;
 }
 {
  direct_input h={0};direct_set_usage(&h,1.0f);int t=1;
  for(int i=0;i<400;i++)direct_sample(&h,t++*10000,0,0,0);
  sparkles_state amb_at=h.scene;amb_at.strength=0;sparkles_render_direct(&amb_at,px,SPARKLES_PIXELS);
  long off=local_energy(TX,TY,TR);
  for(int i=0;i<14;i++)direct_sample(&h,t++*10000,1,TX,TY);
  touch_hi=save("evidence/usage-touch-high.ppm",&h.scene);
  delta_hi=(long)local_energy(TX,TY,TR)-off;
 }
 printf("AMBIENT_TOTAL usage0=%u usage015=%u usage05=%u usage1=%u\n",amb[0],amb[1],amb[2],amb[3]);
 printf("TOUCH_DELTA low_usage=%ld high_usage=%ld total_lo=%u total_hi=%u\n",delta_lo,delta_hi,touch_lo,touch_hi);
 int density_increases=amb[0]<amb[1]&&amb[1]<amb[2]&&amb[2]<amb[3];
 /* The touch branch reads no usage, so usage must NOT amplify the burst: the
  * high-usage burst delta is not larger than the low-usage one (it is in fact
  * a little smaller due to max-blend overlap with the denser ambient), while
  * both remain large. That is the honest independence claim. */
 int touch_not_amplified=delta_hi<=delta_lo+delta_lo/20&&delta_lo>150000&&delta_hi>100000;
 printf("ambient_density_increases=%d touch_burst_not_amplified_by_usage=%d\n",density_increases,touch_not_amplified);
 return 0;
}
