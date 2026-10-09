#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "sparkles.h"
int main(void) {
 sparkles_state s={0};unsigned max=0;
 for(int y=0;y<64;y++)for(int x=0;x<64;x++){
  sparkles_ray r=sparkles_trace(&s,x/63.f*2-1,y/63.f*2-1);
  assert(isfinite(r.distance));assert(r.steps<=SPARKLES_MAX_STEPS);assert(r.distance>0&&r.distance<1200);if(r.steps>max)max=r.steps;
 }
 printf("finite perspective intersections max refinements %u\n",max);
 static uint16_t a[SPARKLES_PIXELS],b[SPARKLES_PIXELS];assert(sparkles_render_direct(&s,a,SPARKLES_PIXELS));
 sparkles_advance(&s,.3f,.8f,-.8f,1);s.anchor_x=s.touch_x;s.anchor_y=s.touch_y;assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));
 unsigned changed=0;for(int i=0;i<SPARKLES_PIXELS;i++){changed+=a[i]!=b[i];}assert(changed>100);
 printf("touch/sun changes %u pixels\n",changed);
 for(int i=0;i<200;i++){sparkles_advance(&s,.1f,0,0,0);}assert(s.strength<.001f);
 s=(sparkles_state){.time=.5f};assert(sparkles_render_direct(&s,b,SPARKLES_PIXELS));changed=0;for(int i=0;i<SPARKLES_PIXELS;i++){changed+=a[i]!=b[i];}assert(changed>1000);
 printf("0.5s changes %u pixels; release PASS\n",changed);
 return 0;
}
