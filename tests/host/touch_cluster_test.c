#include <assert.h>
#include <stdio.h>
#include "direct_input.h"
/* Organic touch sparkle cluster: bounds, cap, determinism, NON-ring structure,
 * gradual down flurry, six-level escalation and in-place release fade.
 * The structure metrics are validated against a synthetic evenly spaced ring
 * (the previous geometry) so they demonstrably detect ring/spiral layouts. */
static uint16_t px[SPARKLES_PIXELS];
#define TX 184.f
#define TY 224.f
static int cluster_at(float contact,float age,float hold,sp_cluster_glint*g){
 sparkles_state s={.time=contact+age,.contact_start=contact,.hold=hold,.strength=1};
 return sp_touch_cluster(&s,TX,TY,g,SP_HOLD_MAX);
}
static int cmpf(const void*a,const void*b){float x=*(const float*)a,y=*(const float*)b;return (x>y)-(x<y);}
/* Coefficient of variation of the angular gaps between visible glints. An
 * evenly spaced ring/spiral arm gives ~0; independent uniform angles ~1. */
static float angular_gap_cv(const float*ang,int n){
 float a[SP_HOLD_MAX*2];memcpy(a,ang,n*sizeof *a);qsort(a,n,sizeof *a,cmpf);
 float gaps[SP_HOLD_MAX*2],mean=0,var=0;
 for(int i=0;i<n;i++){gaps[i]=(i+1<n?a[i+1]:a[0]+6.2831853f)-a[i];mean+=gaps[i];}
 mean/=n;for(int i=0;i<n;i++)var+=(gaps[i]-mean)*(gaps[i]-mean);
 return sqrtf(var/n)/mean;
}
/* Largest fraction of glints sharing (within +-tol px) one radius. A set of
 * concentric rings concentrates every glint on a handful of radii. */
static float max_radius_share(const float*r,int n,float tol){
 int best=0;for(int i=0;i<n;i++){int c=0;for(int j=0;j<n;j++)if(fabsf(r[j]-r[i])<=tol)c++;if(c>best)best=c;}
 return (float)best/n;
}
static long mask_total(void){long e=0;for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];return e;}
static long attributable(sparkles_state b){
 sparkles_state off=b;off.strength=0;off.trail_count=0;
 sparkles_render_direct(&b,px,SPARKLES_PIXELS);long on=mask_total();
 sparkles_render_direct(&off,px,SPARKLES_PIXELS);return on-mask_total();
}
int main(void){
 sp_cluster_glint g[SP_HOLD_MAX],h[SP_HOLD_MAX];
 /* 0. Metric sanity: the former geometry (evenly spaced ring segments on a few
  *    radii) is flagged; the metrics are not vacuous. */
 {float ang[30],rad[30];int n=0;
  for(int r=0;r<3;r++)for(int k=0;k<10;k++){ang[n]=fmodf(.3f+k*6.2831853f/10+r*.6f,6.2831853f);rad[n++]=18.f+r*22.f;}
  float ring_cv=0;for(int r=0;r<3;r++)ring_cv+=angular_gap_cv(ang+r*10,10)/3;
  assert(ring_cv<.05f);assert(max_radius_share(rad,n,1.5f)>=.33f);
  printf("reference ring: per-ring gap CV=%.3f radius share=%.2f (flagged)\n",ring_cv,max_radius_share(rad,n,1.5f));}

 /* 1. API bounds, cap and determinism. */
 assert(sp_touch_cluster(NULL,TX,TY,g,SP_HOLD_MAX)==0);
 {sparkles_state s={.time=5,.contact_start=4,.hold=3,.strength=1};
  assert(sp_touch_cluster(&s,TX,TY,NULL,5)==0&&sp_touch_cluster(&s,TX,TY,g,0)==0);
  assert(sp_touch_cluster(&s,TX,TY,g,5)<=5);}
 int maxn=0;
 for(int e=0;e<40;e++)for(int f=0;f<60;f++){
  float contact=1.3f+e*2.71f,age=f*.05f,hold=age;
  int n=cluster_at(contact,age,hold,g);assert(n>=0&&n<=SP_HOLD_MAX);if(n>maxn)maxn=n;
  int m=cluster_at(contact,age,hold,h);assert(m==n&&!memcmp(g,h,n*sizeof *g)); /* reproducible */
  float sigma=sp_touch_spread(5);
  for(int i=0;i<n;i++){
   assert(g[i].alpha>=0&&g[i].alpha<=1.01f&&g[i].scale>0&&g[i].scale<=1.6f);
   assert(g[i].species>=0&&g[i].species<=2);
   float dx=g[i].x-TX,dy=g[i].y-TY;assert(sqrtf(dx*dx+dy*dy)<=sigma*.85f*3.f+1);
  }
 }
 printf("cap: max glints/frame=%d (limit %d)\n",maxn,SP_HOLD_MAX);
 assert(maxn<=SP_HOLD_MAX&&maxn>=20);
 {sparkles_state s={.time=9,.contact_start=0,.hold=99,.strength=1,.touch_x=.9f,.touch_y=-.95f};
  /* renderer clips near-edge clusters without faults (ASan) */
  assert(sparkles_render_direct(&s,px,SPARKLES_PIXELS));}

 /* 2. No ring / spiral structure at the down flurry and hold levels 0,2,5. */
 const float probes[4][2]={{.30f,.30f},{3.f,SP_HOLD_STEP*.5f},{3.f,SP_HOLD_STEP*2.5f},{3.f,SP_HOLD_STEP*5.5f}};
 const char*names[4]={"down-flurry","hold-level-0","hold-level-2","hold-level-5"};
 for(int p=0;p<4;p++){
  float cv_sum=0,share_sum=0;int frames=0,pool=0;static float radii[40*SP_HOLD_MAX];
  int bins[8]={0};float sigma=sp_touch_spread(p==0?0:(float)sp_hold_level(probes[p][1],NULL));
  for(int e=0;e<40;e++){
   float contact=2.f+e*1.93f,age=probes[p][0]+(p?e*.137f:0);
   int n=cluster_at(contact,age,probes[p][1],g);if(n<6)continue;
   float ang[SP_HOLD_MAX],rad[SP_HOLD_MAX];
   for(int i=0;i<n;i++){float dx=g[i].x-TX,dy=g[i].y-TY;rad[i]=sqrtf(dx*dx+dy*dy);ang[i]=atan2f(dy,dx)+3.1415927f;
    radii[pool++]=rad[i];int b=(int)(rad[i]/(sigma*.85f)*2);if(b<8)bins[b]++;}
   cv_sum+=angular_gap_cv(ang,n);share_sum+=max_radius_share(rad,n,1.5f);++frames;
  }
  assert(frames>=30);
  float cv=cv_sum/frames,share=share_sum/frames;
  /* distinct radii across the pooled set (1px buckets) */
  int distinct=0;static unsigned char seen[400];memset(seen,0,sizeof seen);
  for(int i=0;i<pool;i++){int r=(int)(radii[i]+.5f);if(r<400&&!seen[r]){seen[r]=1;distinct++;}}
  /* radial density per unit area falls off outward (gaussian-ish cluster) */
  float inner=bins[0]/(1.f),outer=(bins[3]+bins[4])/((25.f-9.f)); /* area ~ r^2 bands */
  printf("%s: frames=%d glints=%d gapCV=%.3f radiusShare=%.3f distinctRadii=%d innerDensity=%.1f outerDensity=%.2f\n",
   names[p],frames,pool,cv,share,distinct,inner,outer);
  assert(cv>.55f);          /* angles irregular, not evenly spaced (ring~0)      */
  assert(share<.30f);       /* radii not quantized onto a few ring values        */
  assert(distinct>=20);     /* broad continuous spread of radii                  */
  assert(inner>outer*2.f);  /* denser at the finger, falling off outward         */
 }

 /* 3. Down: a flurry that FADES IN (nothing at contact, dim first sample,
  *    many sparkles by ~0.3s). Averaged over contact epochs. */
 {float a0=0,a2=0,a30=0;int n30=0;
  for(int e=0;e<20;e++){float c=3.f+e*1.11f;int n;
   n=cluster_at(c,0,0,g);for(int i=0;i<n;i++)a0+=g[i].alpha;
   n=cluster_at(c,.02f,.02f,g);for(int i=0;i<n;i++)a2+=g[i].alpha;
   n=cluster_at(c,.30f,.30f,g);n30+=n;for(int i=0;i<n;i++)a30+=g[i].alpha;}
  printf("flurry alpha sum: t0=%.2f t20ms=%.2f t300ms=%.2f glints@300ms=%.1f\n",a0/20,a2/20,a30/20,n30/20.);
  assert(a0<1e-4f&&a2<a30*.15f&&n30/20.f>=14);}

 /* 4. Six hold levels escalate: more, brighter, larger, wider. */
 {float pn=-1,pa=-1,psc=-1,pr=-1;long pe=-1;int rose_e=0;
  for(int lv=0;lv<SP_HOLD_LEVELS;lv++){
   float n=0,a=0,sc=0,r=0;int cnt=0;long energy=0;
   for(int e=0;e<30;e++){float c=1.f+e*.91f;int k=cluster_at(c,3.f+e*.173f,SP_HOLD_STEP*(lv+.5f),g);n+=k;
    for(int i=0;i<k;i++){a+=g[i].alpha;sc+=g[i].scale;float dx=g[i].x-TX,dy=g[i].y-TY;r+=sqrtf(dx*dx+dy*dy);cnt++;}
    if(e<8){sparkles_state s={.time=c+3.f+e*.173f,.contact_start=c,.hold=SP_HOLD_STEP*(lv+.5f),.strength=1};energy+=attributable(s);}}
   n/=30;a/=30;sc/=cnt;r/=cnt;
   printf("level %d: glints=%.1f alphaSum=%.2f meanScale=%.3f meanRadius=%.1f energy=%ld\n",lv,n,a,sc,r,energy/8);
   assert(n>pn&&a>pa&&sc>psc&&r>pr);if(energy>pe)rose_e++;
   pn=n;pa=a;psc=sc;pr=r;pe=energy;
  }
  assert(rose_e==SP_HOLD_LEVELS);}

 /* 5. Release: exponential fade IN PLACE at the retained contact. */
 {direct_input d={0};int t=1;unsigned fx=90,fy=300;
  direct_sample(&d,t++*10000,0,0,0);
  for(int i=0;i<150;i++)direct_sample(&d,t++*10000,1,fx,fy); /* 1.5s hold */
  float rx=d.scene.touch_x,ry=d.scene.touch_y;long prev=attributable(d.scene),first=prev;int dec=0;
  for(int k=0;k<6;k++){
   for(int i=0;i<40;i++)direct_sample(&d,t++*10000,0,0,0);
   assert(d.scene.touch_x==rx&&d.scene.touch_y==ry); /* contact retained */
   float tx=(d.scene.touch_x+1)*.5f*(SPARKLES_W-1),ty=(d.scene.touch_y+1)*.5f*(SPARKLES_H-1);
   assert(fabsf(tx-fx)<1.5f&&fabsf(ty-fy)<1.5f);
   long e=attributable(d.scene);if(e<=prev)dec++;prev=e;
  }
  printf("release: held energy=%ld after 2.4s=%ld\n",first,prev);
  assert(dec>=5&&prev<first/20);
  for(int i=0;i<200;i++)direct_sample(&d,t++*10000,0,0,0);
  assert(d.scene.strength<.002f&&d.scene.hold==0&&attributable(d.scene)==0);}
 puts("touch cluster: bounds/cap/determinism, non-ring structure, fade-in flurry, 6-level escalation, in-place release PASS");
 return 0;
}
