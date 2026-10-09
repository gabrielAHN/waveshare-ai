/* Sparkles swipe styles (HOST SIMULATION ONLY): a quick left/right flick on the Sparkles page toggles
 * Sea <-> Sunset (wrapping). Sea (style 0, zero-init) is the original renderer; Sunset is a dusk sky
 * (violet top -> coral -> golden horizon) with glowing paper lanterns that float UP instead of
 * twinkling in place; touch uses the same cluster/hold/trail logic, its points drifting upward.
 *   build/host-tests/sparkle_style_test [OUT_DIR]   also writes one PPM per style (+ crossfade, label). */
#include <assert.h>
#include <stdio.h>
#include "home_render.h"
static int64_t now;
static void sample(home_ui*s,bool down,int x,int y){home_sample(s,now+=10000,down,x,y);}
static void flick(home_ui*s,int dir,int steps){ /* dir -1 = finger moves left */
 for(int k=0;k<=steps;k++)sample(s,true,184-dir*75+dir*150*k/steps,220);
 sample(s,false,184+dir*75,220);
}
static uint16_t px[SPARKLES_PIXELS];
static void rgb(uint16_t p,int*r,int*g,int*b){*r=((p>>11)&31)*255/31;*g=((p>>5)&63)*255/63;*b=(p&31)*255/31;}
static void mean_rows(int y0,int y1,int*r,int*g,int*b){long R=0,G=0,B=0,n=0;
 for(int y=y0;y<y1;y++)for(int x=8;x<SPARKLES_W-8;x++){int a,c,d;rgb(px[y*SPARKLES_W+x],&a,&c,&d);R+=a;G+=c;B+=d;n++;}
 *r=(int)(R/n);*g=(int)(G/n);*b=(int)(B/n);}
static void mean(int*r,int*g,int*b){mean_rows(0,SPARKLES_H,r,g,b);}
static void save(const char*dir,const char*name){
 if(!dir)return;char path[256];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);FILE*f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n368 448\n255\n");for(int i=0;i<SPARKLES_PIXELS;i++){int r,g,b;rgb(px[i],&r,&g,&b);fputc(r,f);fputc(g,f);fputc(b,f);}fclose(f);
}
/* Render one style, settled (after the crossfade), busy ambient field (density step 4). */
static void render_style(int style,float t){
 sparkles_state s={.time=t,.density=4,.usage=SP_USAGE_DEFAULT,.style=style,.style_from=style};
 sp_direct_water_time=-1;assert(sparkles_render_direct(&s,px,SPARKLES_PIXELS));
}
/* Lantern pixels: warm (red high, blue low) and clearly brighter than the sky row they sit on. */
static int lanterns(void){int n=0;for(int y=8;y<SPARKLES_H-8;y++){
  int sr,sg,sb;sp_style_rgb(SP_SUNSET,0,y,&sr,&sg,&sb);int base=sr+sg+sb;
  for(int x=8;x<SPARKLES_W-8;x++){int r,g,b;rgb(px[y*SPARKLES_W+x],&r,&g,&b);
   n+=r>=230&&g>=130&&r+g+b>base+90&&b<r-40;}}return n;}
/* Centroid row of the lit mask cells (mask space). */
static double mask_cy(void){double s=0,w=0;for(int y=0;y<SP_MH;y++)for(int x=0;x<SP_MW;x++){unsigned a=sp_mask[y*SP_MW+x];if(a>60){s+=y*(double)a;w+=a;}}return w>0?s/w:-1;}
int main(int argc,char**argv){
 const char*dir=argc>1?argv[1]:NULL;
 assert(SP_STYLES==2&&SP_SUNSET==1);
 /* --- gestures: 2 styles, both directions wrap --- */
 home_ui s={0};s.page=SPARKLES;s.session_off=false;/* calm: touch only */
 assert(s.input.scene.style==0);
 flick(&s,-1,8);assert(s.page==SPARKLES&&s.input.scene.style==1&&s.input.scene.style_from==0);
 flick(&s,-1,8);assert(s.input.scene.style==0&&s.input.scene.style_from==1);   /* wraps forward */
 flick(&s,1,8);assert(s.input.scene.style==1);                                  /* right = previous, wraps */
 flick(&s,1,8);assert(s.input.scene.style==0);
 flick(&s,1,8);assert(s.input.scene.style==1);
 /* not a flick: slow horizontal drag (drawing), vertical drags, short moves */
 flick(&s,-1,60);assert(s.input.scene.style==1);  /* 600 ms: a drag, not a flick */
 for(int k=0;k<=8;k++)sample(&s,true,184,120+20*k);sample(&s,false,184,280);assert(s.input.scene.style==1);
 for(int k=0;k<=8;k++)sample(&s,true,184+5*k,220);sample(&s,false,224,220);assert(s.input.scene.style==1);
 /* the Home pull still goes Home and never changes the style */
 for(int k=0;k<=8;k++)sample(&s,true,100+2*k,440-27*k);sample(&s,false,0,0);assert(s.page==HOME&&s.input.scene.style==1);
 /* on Home a flick changes the TILE, not the style */
 now+=400000;sample(&s,false,0,0);int tile=s.tile;flick(&s,-1,8);assert(s.page==HOME&&s.tile!=tile&&s.input.scene.style==1);
 /* the flick itself still sparkles (it is a touch on the Sparkles page) */
 s.page=SPARKLES;float st=s.input.scene.strength;flick(&s,-1,8);assert(s.input.scene.strength>st||s.input.scene.strength>.5f);
 assert(s.input.scene.style==1);  /* back on the page it opened on Sea (SPEC3: sparkle_default_test), the flick picked Sunset */
 /* --- label: name + 2 dots for SPARKLE_LABEL_S after a swipe --- */
 assert(home_sparkle_label_on(&s));
 assert(!strcmp(sparkle_style_name(0),"Sea")&&!strcmp(sparkle_style_name(1),"Sunset")&&!strcmp(sparkle_style_name(7),"Sunset"));
 home_ui q=s;q.input.scene.time=s.input.scene.style_at+SPARKLE_LABEL_S+.01f;assert(!home_sparkle_label_on(&q));
 home_ui z={0};z.page=SPARKLES;assert(!home_sparkle_label_on(&z));   /* boot: no label */
 /* --- rendering --- */
 static const char*names[SP_STYLES]={"0-sea","1-sunset"};
 for(int k=0;k<SP_STYLES;k++){render_style(k,7.3f);int r,g,b;mean(&r,&g,&b);save(dir,names[k]);
  printf("style %s mean=%d,%d,%d\n",sparkle_style_name(k),r,g,b);}
 /* Sea is still the original: identical to a state that never had a style */
 {sparkles_state a={.time=4.1f,.density=3},b=a;uint16_t*o=malloc(SPARKLES_PIXELS*2);assert(o);
  sp_direct_water_time=-1;sparkles_render_direct(&a,px,SPARKLES_PIXELS);memcpy(o,px,sizeof px);
  b.style=0;b.style_from=0;b.style_at=3.f;sp_direct_water_time=-1;sparkles_render_direct(&b,px,SPARKLES_PIXELS);
  assert(!memcmp(o,px,sizeof px));free(o);}
 /* Sunset sky: violet/dusk at the top, coral in the middle, golden at the horizon; warm overall */
 {render_style(1,7.3f);int tr,tg,tb,mr,mg,mb,br,bg,bb;
  mean_rows(12,60,&tr,&tg,&tb);mean_rows(220,260,&mr,&mg,&mb);mean_rows(390,436,&br,&bg,&bb);
  printf("sunset top=%d,%d,%d mid=%d,%d,%d horizon=%d,%d,%d\n",tr,tg,tb,mr,mg,mb,br,bg,bb);
  assert(tb>tg&&tr>tg&&tr+tg+tb<360);                  /* dusk violet: blue & red over green, dark */
  assert(mr>200&&mr>mg+60&&mr>mb+60);                  /* coral: strongly red */
  assert(br>235&&bg>150&&bg>bb+50);                    /* golden: red+green high, blue low */
  assert(tr+tg+tb<mr+mg+mb&&mr+mg+mb<br+bg+bb);}        /* brightening toward the horizon */
 /* lanterns exist, in warm lantern colours */
 {render_style(1,7.3f);int n=lanterns();printf("lantern pixels=%d\n",n);assert(n>400);}
 /* ... and they are Sunset's OWN points (round lanterns, rising), not the Sea stars recoloured */
 {render_style(0,7.3f);uint8_t*m0=malloc(SP_MASK_PIXELS);assert(m0);memcpy(m0,sp_mask,SP_MASK_PIXELS);
  render_style(1,7.3f);int lit=0,diff=0;
  for(int i=0;i<SP_MASK_PIXELS;i++){if(m0[i]>60||sp_mask[i]>60){lit++;diff+=abs((int)m0[i]-(int)sp_mask[i])>40;}}
  printf("mask sea vs sunset lit=%d differ=%d\n",lit,diff);assert(lit>1000&&diff*10>lit*6);free(m0);}
 /* FLOAT: the lantern mask 0.12 s later best matches the earlier one shifted UP */
 {uint8_t*m0=malloc(SP_MASK_PIXELS);assert(m0);render_style(1,7.3f);memcpy(m0,sp_mask,SP_MASK_PIXELS);render_style(1,7.42f);
  int bx=0,by=0;double best=-1;
  for(int dy=-5;dy<=5;dy++)for(int dx=-5;dx<=5;dx++){double c=0;
   for(int y=6;y<SP_MH-6;y++)for(int x=6;x<SP_MW-6;x++)c+=(double)m0[y*SP_MW+x]*sp_mask[(y+dy)*SP_MW+x+dx];
   if(c>best){best=c;bx=dx;by=dy;}}
  printf("lantern travel best shift=%d,%d (mask px)\n",bx,by);assert(by<0);free(m0);}
 /* one lantern: rises ~96 px over its life, sways, fades in and out (no pop), smaller as it drifts */
 {float x0,y0,s0,a0,x1,y1,s1,a1,xm,ym,sm,am,xe,ye,se,ae;
  sp_lantern(77,0.f,100,200,&x0,&y0,&s0,&a0);sp_lantern(77,.1f,100,200,&x1,&y1,&s1,&a1);
  sp_lantern(77,.5f,100,200,&xm,&ym,&sm,&am);sp_lantern(77,1.f,100,200,&xe,&ye,&se,&ae);
  assert(a0<.01f&&ae<.01f&&am>.7f&&a1<am);            /* gentle fade in/out, bright mid-life */
  assert(ye<y0-90&&ym<y0-40);                          /* rising */
  assert(se<s0&&fabsf(xm-100)<=9.01f);}                /* shrinking, bounded sway */
 /* the lantern sprite: brightest at the centre, taller than wide (a paper lantern), soft halo */
 {memset(sp_mask,0,SP_MASK_PIXELS);sp_glint(184,224,25,1,SP_LANTERN,0,1,1);
  int c=sp_mask[112*SP_MW+92],side=sp_mask[112*SP_MW+92+7],up=sp_mask[(112-7)*SP_MW+92],halo=sp_mask[112*SP_MW+92+12];
  printf("lantern centre=%d side(14px)=%d up(14px)=%d halo(24px)=%d\n",c,side,up,halo);
  assert(c>240&&up>side&&halo>10&&halo<side);}
 /* TOUCH uses the same logic (cluster count/positions identical for both styles) ... */
 {sparkles_state t={.time=7.3f,.quiet=1,.strength=1,.contact_start=6.9f,.hold=.9f,.reach=.9f,.style=1,.style_from=1};
  sp_cluster_glint a[SP_HOLD_MAX],b[SP_HOLD_MAX];int na=sp_touch_cluster(&t,184,224,a,SP_HOLD_MAX);
  t.style=t.style_from=0;int nb=sp_touch_cluster(&t,184,224,b,SP_HOLD_MAX);
  assert(na==nb&&na>4);for(int i=0;i<na;i++)assert(a[i].x==b[i].x&&a[i].y==b[i].y&&a[i].life>=0&&a[i].life<=1);}
 /* ... but under Sunset the touch points float UP from the finger (lit mass above the contact) */
 {sparkles_state c={.time=7.3f,.quiet=1,.style=1,.style_from=1,.strength=1,.contact_start=6.6f,.hold=.9f,.reach=.9f};
  sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);double cy1=mask_cy();save(dir,"3-sunset-touch");
  int warm=lanterns();
  c.style=c.style_from=0;sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);double cy0=mask_cy();
  printf("touch centroid row sea=%.1f sunset=%.1f (mask px; contact row %d) warm=%d\n",cy0,cy1,SP_MH/2,warm);
  assert(cy1>0&&cy0>0&&cy1<cy0-2&&warm>40);}
 /* the trail's points drift up too */
 {float x=100,y=200;sp_float_up(5,.8f,&x,&y);assert(y<200-50&&fabsf(x-100)<=10.01f);}
 /* no active session (quiet): background colour + touch only, no ambient lanterns */
 {sparkles_state c={.time=7.3f,.quiet=1,.style=1,.style_from=1};sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);
  printf("quiet lantern pixels=%d\n",lanterns());assert(lanterns()<20);save(dir,"4-sunset-quiet");}
 /* crossfade: halfway through the fade the middle of the sky sits between Sea and Sunset */
 {render_style(0,10.f);int sr,sg,sb;mean_rows(200,280,&sr,&sg,&sb);render_style(1,10.f);int ur,ug,ub;mean_rows(200,280,&ur,&ug,&ub);
  sparkles_state c={.time=10.f,.density=4,.style=1,.style_from=0,.style_at=10.f-SP_STYLE_FADE/2};
  sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);int r,g,b;mean_rows(200,280,&r,&g,&b);save(dir,"2-sea-to-sunset-mid");
  printf("crossfade mid rows sea=%d,%d,%d mid=%d,%d,%d sunset=%d,%d,%d\n",sr,sg,sb,r,g,b,ur,ug,ub);
  assert(b<sb-20&&b>ub+20&&r>sr+15&&r<ur);
  assert(sp_style_mix(&c,10.f-SP_STYLE_FADE/2)==0&&sp_style_mix(&c,10.f+SP_STYLE_FADE)==256);}
 /* label pixels over the page right after the swipe */
 {home_ui v={0};v.page=SPARKLES;v.session_off=false;v.input.scene.time=5;home_sparkle_style(&v,1);v.input.scene.time=5.2f;
  assert(home_render(&v,px,SPARKLES_PIXELS));save(dir,"5-sunset-label");
  int white=0;for(int y=44;y<104;y++)for(int x=100;x<268;x++){int r,g,b;rgb(px[y*368+x],&r,&g,&b);white+=r>240&&g>240&&b>240;}
  assert(white>150);}
 /* FULL SCREEN: the animation reaches every edge (no pale printed-paper frame around it): the outer
  * 2 px ring has the same colour as the ring just inside it (8..12 px), for both styles. */
 for(int st=0;st<SP_STYLES;st++){sparkles_state c={.time=6.f,.quiet=1,.style=st,.style_from=st};
  sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);
  long o[3]={0},in[3]={0},no=0,ni=0;
  for(int y=0;y<SPARKLES_H;y++)for(int x=0;x<SPARKLES_W;x++){
   int e=x;if(SPARKLES_W-1-x<e)e=SPARKLES_W-1-x;if(y<e)e=y;if(SPARKLES_H-1-y<e)e=SPARKLES_H-1-y;
   if(e>=2&&(e<8||e>=12))continue;int r,g,b;rgb(px[y*SPARKLES_W+x],&r,&g,&b);
   if(e<2){o[0]+=r;o[1]+=g;o[2]+=b;no++;}else{in[0]+=r;in[1]+=g;in[2]+=b;ni++;}}
  int worst=0;for(int k=0;k<3;k++){int d=(int)labs(o[k]/no-in[k]/ni);if(d>worst)worst=d;}
  printf("style %d edge vs inside max channel diff=%d\n",st,worst);assert(worst<=10);}
 /* BIGGER LANTERNS: one lantern at full size lights a wide warm area (>= 2600 panel px above
  * half glow; the first Sunset lantern lit ~900) and its bright lit-paper body is >= 20 px tall. */
 {memset(sp_mask,0,SP_MASK_PIXELS);sp_glint(184,224,25,1,SP_LANTERN,0,1,1);
  int lit=0,tall=0;for(int y=0;y<SP_MH;y++)for(int x=0;x<SP_MW;x++)lit+=sp_mask[y*SP_MW+x]>128;
  for(int y=0;y<SP_MH;y++)tall+=sp_mask[y*SP_MW+92]>200;
  printf("lantern lit panel px=%d body height=%d px\n",lit*4,tall*2);assert(lit*4>=2600&&tall*2>=20);}
 /* HOLD: under Sunset the finger holds a lit lantern whose glow grows the longer you hold */
 {sparkles_state c={.time=7.3f,.quiet=1,.style=1,.style_from=1,.strength=1,.contact_start=7.2f,.hold=.05f,.reach=.05f};
  sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);
  int a0=0;for(int y=0;y<SP_MH;y++)for(int x=0;x<SP_MW;x++)a0+=sp_mask[y*SP_MW+x]>128;
  c.contact_start=5.0f;c.hold=2.3f;c.reach=2.3f;sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);
  int a1=0;for(int y=0;y<SP_MH;y++)for(int x=0;x<SP_MW;x++)a1+=sp_mask[y*SP_MW+x]>128;
  int core=sp_mask[112*SP_MW+92];save(dir,"6-sunset-hold");
  printf("hold glow early=%d long=%d core=%d\n",a0*4,a1*4,core);assert(core>200&&a1>a0*2);}
 /* MOVE: a finger drag under Sunset leaves a wake of lanterns that rise above the path */
 {home_ui v={0};v.page=SPARKLES;v.session_off=false;now=0;sample(&v,false,0,0);home_sparkle_style(&v,1);  /* opened on Sea, then Sunset */
  for(int k=0;k<=60;k++)sample(&v,true,60+4*k,300);   /* 600 ms, 240 px to the right along y=300 */
  sparkles_state c=v.input.scene;c.style_from=1;c.quiet=1;sp_direct_water_time=-1;sparkles_render_direct(&c,px,SPARKLES_PIXELS);
  save(dir,"7-sunset-move");
  double cy=mask_cy();int lit=0;for(int y=0;y<SP_MH;y++)for(int x=0;x<SP_MW;x++)lit+=sp_mask[y*SP_MW+x]>128;
  printf("move trail lit panel px=%d centroid row=%.1f (path row %d)\n",lit*4,cy,300/2);
  assert(lit*4>2000&&lit*4<60000&&cy<150);}
 puts("sparkle swipe styles: flick toggles Sea/Sunset, sunset sky, floating lanterns, touch floats up, crossfade, label: PASS");
 return 0;
}
