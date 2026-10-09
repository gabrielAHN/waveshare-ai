/* HOST SIMULATION ONLY: seeded same-core (sparkles_render_direct) previews for
 * the organic touch cluster and the six session token-density levels. No
 * device, no physical contact; NOT a photo of the panel. Writes PPMs that a
 * companion step converts to PNG, plus a per-frame metrics line. Also renders
 * a crop with the touch-attributable glint CENTRES marked, so the absence of
 * any ring/spiral arrangement can be judged by eye. */
#include <stdio.h>
#include "direct_input.h"
static uint16_t px[SPARKLES_PIXELS];
static long save(const char*path,const sparkles_state*s){
 sparkles_render_direct(s,px,SPARKLES_PIXELS);long e=0;for(int i=0;i<SP_MASK_PIXELS;i++)e+=sp_mask[i];
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n368 448\n255\n");
 for(int i=0;i<SPARKLES_PIXELS;i++){unsigned p=px[i];fputc(((p>>11)&31)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
 fclose(f);return e;
}
/* 184x224 map (1 px per 2 panel px) of the glint centres: white dot, grey
 * guide circles are NOT drawn (nothing hints at a ring). */
static void centres(const char*path,const sparkles_state*s){
 static unsigned char m[224][184];memset(m,0,sizeof m);
 sp_cluster_glint g[SP_HOLD_MAX];float tx=(s->touch_x+1)*.5f*(SPARKLES_W-1),ty=(s->touch_y+1)*.5f*(SPARKLES_H-1);
 int n=sp_touch_cluster(s,tx,ty,g,SP_HOLD_MAX);
 for(int i=0;i<n;i++){int cx=(int)(g[i].x/2),cy=(int)(g[i].y/2);unsigned char v=(unsigned char)(80+175*(g[i].alpha>1?1:g[i].alpha));
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int x=cx+dx,y=cy+dy;if(x>=0&&x<184&&y>=0&&y<224&&v>m[y][x])m[y][x]=v;}}
 m[(int)(ty/2)][(int)(tx/2)]=40; /* finger position: dim marker */
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P5\n184 224\n255\n");fwrite(m,1,sizeof m,f);fclose(f);
 printf("%s glints=%d\n",path,n);
}
int main(void){
 const int TX=184,TY=224;
 direct_input d={0};int t=1;
 for(;t<=400;t++)direct_sample(&d,t*10000,0,0,0); /* ambient settle */
 d.scene.usage=SP_USAGE_DEFAULT;
 for(int i=0;i<20;i++)direct_sample(&d,t++*10000,1,TX,TY);       /* 0.20 s: down flurry */
 printf("touch-down energy=%ld\n",save("evidence/density-touch-down.ppm",&d.scene));centres("evidence/density-touch-down-centres.pgm",&d.scene);
 /* hold levels 0,2,5 through the real poller clock */
 const int levels[3]={0,2,5};int held=20;
 for(int k=0;k<3;k++){
  int target=(int)((levels[k]+.6f)*SP_HOLD_STEP*100);
  while(held<target){direct_sample(&d,t++*10000,1,TX,TY);held++;}
  char p[96];snprintf(p,sizeof p,"evidence/density-hold-level%d.ppm",levels[k]);
  printf("hold level %d (hold=%.2fs) energy=%ld\n",sp_hold_level(d.scene.hold,NULL),d.scene.hold,save(p,&d.scene));
  snprintf(p,sizeof p,"evidence/density-hold-level%d-centres.pgm",levels[k]);centres(p,&d.scene);
 }
 for(int i=0;i<60;i++)direct_sample(&d,t++*10000,0,0,0);          /* 0.6 s after release */
 printf("release+0.6s strength=%.3f energy=%ld\n",d.scene.strength,save("evidence/density-release.ppm",&d.scene));
 /* Ambient at session density levels 0..5 (scene.density = level+1), no touch */
 for(int lv=0;lv<6;lv++){
  sparkles_state s={.time=6.0f,.density=lv+1,.usage=SP_USAGE_DEFAULT};
  char p[96];snprintf(p,sizeof p,"evidence/density-ambient-level%d.ppm",lv);
  printf("ambient level %d energy=%ld\n",lv,save(p,&s));
 }
 {sparkles_state s={.time=6.0f,.density=0,.usage=SP_USAGE_DEFAULT};printf("ambient calm-default(no feed) energy=%ld\n",save("evidence/density-ambient-default.ppm",&s));}
 return 0;
}
