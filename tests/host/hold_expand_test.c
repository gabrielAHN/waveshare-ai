#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"
/* The sparkle generation AREA grows the longer a finger stays down: while held in one spot the
 * cluster and the trail births spread wider, and moving across keeps the grown area (it does not
 * shrink back to a fingertip at every re-anchor). A fresh press starts small again. */
static int cmpf(const void*a,const void*b){float x=*(const float*)a,y=*(const float*)b;return x<y?-1:x>y;}
/* 90th-percentile distance of visible cluster glints from the anchor. */
static float cluster_reach(const home_ui*s){
 int total=0;float d[SP_HOLD_MAX*40];
 for(int k=0;k<40;k++){ /* average over time so random slots do not decide the result */
  sparkles_state c=s->input.scene;c.time+=k*.05f;
  float ax=(c.anchor_x+1)*.5f*(SPARKLES_W-1),ay=(c.anchor_y+1)*.5f*(SPARKLES_H-1);
  sp_cluster_glint g[SP_HOLD_MAX];int n=sp_touch_cluster(&c,ax,ay,g,SP_HOLD_MAX);
  for(int i=0;i<n;i++)if(g[i].alpha>.03f)d[total++]=hypotf(g[i].x-ax,g[i].y-ay);
 }
 assert(total>20);qsort(d,total,sizeof *d,cmpf);return d[total*9/10];
}
/* 90th-percentile distance of the newest trail births from a point. */
static float trail_reach(const home_ui*s,float x,float y,unsigned newest){
 const sparkles_state*c=&s->input.scene;float d[SP_TRAIL_MAX];unsigned n=0;
 for(unsigned k=1;k<=newest&&k<=c->trail_count;k++){
  const sp_trail_particle*t=&c->trail[(c->trail_next+SP_TRAIL_MAX-k)%SP_TRAIL_MAX];d[n++]=hypotf(t->x-x,t->y-y);
 }
 assert(n>=16);qsort(d,n,sizeof *d,cmpf);return d[n*9/10];
}
int main(void){
 home_ui s={0};s.page=SPARKLES;s.session_off=false;/* calm: touch only */int64_t t=1000000;
 home_sample(&s,t,false,0,0);
 for(int i=0;i<30;i++)home_sample(&s,t+=10000,true,184,224);  /* 0.3 s */
 float early=cluster_reach(&s);
 for(int i=0;i<270;i++)home_sample(&s,t+=10000,true,184,224); /* 3.0 s held, same spot */
 float late=cluster_reach(&s),late_trail=trail_reach(&s,184,224,24);
 printf("held cluster p90: 0.3s=%.1f px 3.0s=%.1f px; trail p90 at 3.0s=%.1f px\n",early,late,late_trail);
 assert(late>=early*1.8f);      /* same-spot hold visibly widens the generation area */
 assert(late_trail>=40.f);      /* births spread well beyond the old fixed 24 px disk */
 assert(late<=150.f);           /* still a finger-local cluster, not the whole screen */
 /* Move across after the long hold: re-anchors (fresh births at the new spot) but keeps the area. */
 for(int i=1;i<=12;i++)home_sample(&s,t+=10000,true,184-i*10,224);
 float ax=(s.input.scene.anchor_x+1)*.5f*(SPARKLES_W-1);assert(ax<130); /* re-anchored */
 for(int i=0;i<10;i++)home_sample(&s,t+=10000,true,64,224);
 float moved=cluster_reach(&s);
 printf("after move: cluster p90=%.1f px\n",moved);
 assert(moved>=late*.8f);
 /* Moving keeps expanding too: a 3 s sweep ends wider than a 0.3 s touch. */
 home_sample(&s,t+=2000000,false,0,0);home_sample(&s,t+=3000000,false,0,0);
 for(int i=0;i<300;i++)home_sample(&s,t+=10000,true,40+(i%200),120+(i/2));
 float sweep=cluster_reach(&s);printf("3 s sweep: cluster p90=%.1f px\n",sweep);
 assert(sweep>=early*1.8f);
 /* A fresh press starts small again. */
 home_sample(&s,t+=10000,false,0,0);home_sample(&s,t+=3000000,false,0,0);
 for(int i=0;i<30;i++)home_sample(&s,t+=10000,true,184,224);
 float fresh=cluster_reach(&s);printf("fresh press: cluster p90=%.1f px\n",fresh);
 assert(fresh<=early*1.2f);
 puts("hold/move expands the sparkle generation area; fresh press starts small: PASS");
 return 0;
}
