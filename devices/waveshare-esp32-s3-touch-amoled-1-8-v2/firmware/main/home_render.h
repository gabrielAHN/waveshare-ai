#pragma once
#include <stdio.h>
#include "home_ui.h"
#include "home_font.h"
#include "tile_art.h"
#include "material_symbols.h"
#include "sparkle_image.h"
#include "bot_art.h"
#include "battery_estimate.h"
#include "theme.h"  /* colour roles: Light / Dark + accent (Settings > Display) */
#define HC_BG 0xe77b
#define HC_TEXT theme_c(TC_HOME_INK)  /* light: 0x322a */
#define HC_MUTED 0x5b91
/* Hot everywhere: full-screen fills, tiles, every text pixel (scale x scale squares). A row outside the
 * rounded corners is one straight span; inside them, the row's covered span is solved once. */
static inline void home_rect(uint16_t*p,int x,int y,int w,int h,int radius,uint16_t color){
 int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>368?368:x+w,y1=y+h>448?448:y+h;
 if(x0>=x1)return;
 for(int yy=y0;yy<y1;yy++){
  int dy=yy<y+radius?y+radius-yy:(yy>=y+h-radius?yy-(y+h-radius-1):0);
  int a=x0,b=x1;
  if(radius>0&&dy>0){
   int r2=radius*radius-dy*dy;if(r2<0)continue;
   int dx=0;while((dx+1)*(dx+1)<=r2)dx++;          /* largest dx with dx^2+dy^2 <= r^2 */
   int left=x+radius-dx,right=x+w-radius-1+dx;      /* covered columns [left, right] */
   if(left>a)a=left;
   if(right+1<b)b=right+1;
  }  /* dy == 0 (between the corners): every column's dx <= radius, so the full span */
  uint16_t*row=p+yy*368;
  for(int xx=a;xx<b;xx++)row[xx]=color;
 }
}
static inline void home_text(uint16_t*p,int x,int y,const char*text,int scale,uint16_t color){
 for(;*text;text++,x+=9*scale){unsigned c=(unsigned char)*text;if(c<32||c>126)c='?';
  for(int yy=0;yy<17;yy++)for(int xx=0;xx<9;xx++)if(home_font[c-32][yy]&(1<<xx))home_rect(p,x+xx*scale,y+yy*scale,scale,scale,0,color);
 }
}
static inline void home_center(uint16_t*p,int x,int y,int w,const char*t,int scale,uint16_t color){home_text(p,x+(w-(int)strlen(t)*9*scale)/2,y,t,scale,color);}
static inline void home_art(uint16_t*p,int x,int y,int w,int h,const uint16_t*art,int radius){
 for(int yy=0;yy<h;yy++){
  int py=y+yy;if(py<0||py>=448)continue;
  int dy=yy<radius?radius-yy:yy>=h-radius?yy-(h-radius-1):0,inset=0;
  while(inset<radius&&(radius-inset)*(radius-inset)+dy*dy>radius*radius)inset++;
  int left=x+inset,right=x+w-inset;if(left<0)left=0;if(right>368)right=368;
  if(right>left)memcpy(p+py*368+left,art+yy*w+left-x,(size_t)(right-left)*sizeof *p);
 }
}
typedef struct {float x,y;} home_point;
static inline unsigned home_live_points(const home_ui*s){return WAVESHARE_AI_PLUGIN_SPARKLES&&(s->page==HOME||s->page==SPARKLES)&&!s->session_off&&live_fresh(&s->live,s->live_now_us)?s->live.count:0;}
/* Session sparkle (user, 2026-10-07: "A glow for every open session, brighter and busier while one is
 * working"). Every OPEN Hermes session gets a glow (home_live_points; the bridge lists them all in
 * WLS4). The ambient field follows the WORKING ones: their smoothed token throughput is level 0..5
 * (DensityTracker); density = 1 + level while one works, so an active session always glints and more
 * tokens/min => denser, brighter sparkle. Nobody working or a stale feed => 0 (calm, idle glows stay).
 * The Settings toggle OFF unlinks Sparkles from Hermes (its own calm animation, no glows). */
static inline bool home_live_usage_pending(const home_ui*s){(void)s;return false;}
static inline int home_live_density(const home_ui*s){
 if(s->session_off||!live_active(&s->live,s->live_now_us))return 0;
 unsigned level=s->live.level>LIVE_LEVELS-1?LIVE_LEVELS-1:s->live.level;
 return 1+(int)level;
}
/* Mix all 64 ID bits before assigning slots; never XOR-fold the halves. */
static inline uint64_t home_live_seed(uint64_t id){
 id=(id^(id>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 id=(id^(id>>27))*UINT64_C(0x94d049bb133111eb);
 return id^(id>>31);
}
static inline uint16_t home_live_provider_color(uint8_t provider){
 switch(provider){
  case LIVE_PROVIDER_ANTHROPIC:return sp_pack_lcd(217,119,87);
  case LIVE_PROVIDER_OPENAI_CODEX:return sp_pack_lcd(28,145,105);
  case LIVE_PROVIDER_OPENROUTER:return sp_pack_lcd(126,92,190);
  default:return sp_pack_lcd(128,128,128);
 }
}
/* Session glows (user rule 2026-09-29: "dots outside of the sparkle tile ... should only be in the
 * tile, and ... move around per session like the sparkle or glow animation for the provider color").
 * Home: the glows live only inside the Sparkles tile's image (clipped to its rounded-square alpha)
 * and move with the tile during a swipe. The Sparkles page: the whole screen. Each working session
 * wanders its own smooth path (two sine terms per axis, frequencies and phases from its id), a soft
 * glow in its provider colour whose size follows the RANGE OF USE (the live token level 0..5, bridge
 * DensityTracker, 60 s window) and whose core twinkles with a small four-point star. */
static inline bool home_live_tile_visible(const home_ui*s){
 int tile=home_tile_index(SPARKLES);if(tile<0)return false;
 int x=(tile-s->tile)*368+s->drag_offset;return s->page!=HOME||(x>-368&&x<368);
}
static inline void home_live_glow_area(const home_ui*s,int*x0,int*y0,int*w,int*h){
 int tile=home_tile_index(SPARKLES);if(tile<0)tile=0;
 if(s->page==HOME){*x0=home_tile_art_x((tile-s->tile)*368+s->drag_offset)+88;*y0=108;*w=*h=SPARKLE_IMAGE_SIZE;}  /* the art's parallax */
 else{*x0=0;*y0=0;*w=368;*h=448;}
}
typedef struct {float fx,fy,px,py;} home_glow_path;
static inline home_glow_path home_live_path(uint64_t id){
 uint64_t seed=home_live_seed(id);
 /* 0.28..0.62 rad/s per axis: each glow crosses the tile every ~5-11 s, each on its own path */
 return (home_glow_path){.28f+.34f*sp_random((uint32_t)seed),.28f+.34f*sp_random((uint32_t)(seed>>21)),
  6.2831853f*sp_random((uint32_t)(seed>>32)),6.2831853f*sp_random((uint32_t)(seed>>43))};
}
static inline void home_live_glow_layout(const home_ui*s,home_point points[LIVE_MAX]){
 unsigned count=s->live.count>LIVE_MAX?LIVE_MAX:s->live.count;
 int x0,y0,w,h;home_live_glow_area(s,&x0,&y0,&w,&h);
 float margin=s->page==HOME?20.f:26.f,ax=w*.5f-margin,ay=h*.5f-margin,t=s->input.scene.time;
 for(unsigned i=0;i<count;i++){
  home_glow_path g=home_live_path(s->live.ids[i]);
  float ux=.74f*sinf(t*g.fx+g.px)+.26f*sinf(t*g.fx*2.3f+g.py*1.7f);
  float uy=.74f*sinf(t*g.fy+g.py)+.26f*sinf(t*g.fy*1.9f+g.px*1.3f);
  points[i]=(home_point){x0+w*.5f+ux*ax,y0+h*.5f+uy*ay};
 }
}
static inline uint16_t home_live_light(uint16_t c){return sp_blend565(c,sp_pack_lcd(255,255,246),110);}
/* One glow: alpha peak*(1-d^2/R^2)^2 (no sqrt, no hard edge), clipped to the tile image on Home. */
static inline void home_live_glow(uint16_t*p,const home_ui*s,int cx,int cy,int R,uint16_t col,unsigned peak){
 int x0,y0,w,h;home_live_glow_area(s,&x0,&y0,&w,&h);bool clip=s->page==HOME;
 int R2=R*R;
 int ya=cy-R<y0?y0:cy-R,yb=cy+R>y0+h?y0+h:cy+R,xa=cx-R<x0?x0:cx-R,xb=cx+R>x0+w?x0+w:cx+R;
 if(ya<0)ya=0;
 if(yb>448)yb=448;
 if(xa<0)xa=0;
 if(xb>368)xb=368;
 for(int y=ya;y<yb;y++){int dy=y-cy;uint16_t*row=p+y*368;
  const uint8_t*img=clip?sparkle_image_alpha+(y-y0)*SPARKLE_IMAGE_SIZE-x0:NULL;
  for(int x=xa;x<xb;x++){int dx=x-cx,d2=dx*dx+dy*dy;if(d2>=R2)continue;
   unsigned f=(unsigned)((R2-d2)*255/R2),a=peak*f*f/(255u*255u);
   if(clip){unsigned ia=img[x];if(!ia)continue;a=a*ia/255;}
   if(!a)continue;
   row[x]=sp_blend565(row[x],col,a>255?255:a);
  }}
}
/* Twinkle: a thin four-point star through the centre, brightest at its middle. */
static inline void home_live_star(uint16_t*p,const home_ui*s,int cx,int cy,int len,uint16_t col,unsigned peak){
 int x0,y0,w,h;home_live_glow_area(s,&x0,&y0,&w,&h);bool clip=s->page==HOME;
 for(int k=-len;k<=len;k++){unsigned a=peak*(unsigned)(len-abs(k))/(unsigned)len;if(!a)continue;
  for(int axis=0;axis<2;axis++){int x=axis?cx:cx+k,y=axis?cy+k:cy;
   if(x<x0||x>=x0+w||y<y0||y>=y0+h||x<0||x>=368||y<0||y>=448)continue;
   if(clip){unsigned ia=sparkle_image_alpha[(y-y0)*SPARKLE_IMAGE_SIZE+(x-x0)];if(!ia)continue;a=a*ia/255;}
   p[y*368+x]=sp_blend565(p[y*368+x],col,a);
  }}
}
static inline void home_live_overlay(const home_ui*s,uint16_t*p){
 unsigned count=home_live_points(s);if(!count||!home_live_tile_visible(s))return;
 if(count>LIVE_MAX)count=LIVE_MAX;
 home_point points[LIVE_MAX];home_live_glow_layout(s,points);
 int level=s->live.level>LIVE_LEVELS-1?LIVE_LEVELS-1:s->live.level;
 int R=14+4*level;if(count>8)R=R*2/3;              /* many sessions: smaller glows, same total cost */
 float t=s->input.scene.time;
 for(unsigned i=0;i<count;i++){
  home_glow_path g=home_live_path(s->live.ids[i]);
  float tw=.5f+.5f*sinf(t*(2.2f+g.fx*9.f)+g.px*3.f);   /* per-session twinkle, 0..1 */
  uint16_t col=home_live_provider_color(s->live.providers[i]);
  int cx=(int)points[i].x,cy=(int)points[i].y;
  if(s->live.idle[i]){  /* open but idle: a smaller, dimmer glow that slowly breathes, no star */
   int r=count>8?10:15;float br=.5f+.5f*sinf(t*.9f+g.px);
   home_live_glow(p,s,cx,cy,r+r/2,col,(unsigned)(110+40*br));
   home_live_glow(p,s,cx,cy,r*2/3,col,(unsigned)(170+40*br));
   home_live_glow(p,s,cx,cy,r/3,home_live_light(col),(unsigned)(110+60*br));
   continue;
  }
  /* working: wide colour halo, then a smaller brighter body, then a light breathing heart: a glow,
   * not a disc, sized by the token level, with a twinkling star */
  home_live_glow(p,s,cx,cy,R+R/2,col,(unsigned)(120+40*tw));
  home_live_glow(p,s,cx,cy,R*2/3,col,255);
  home_live_glow(p,s,cx,cy,R/3+(int)(3*tw),home_live_light(col),(unsigned)(150+100*tw));
  if(tw>.5f)home_live_star(p,s,cx,cy,R+R/4,home_live_light(col),(unsigned)(255*(tw-.5f)/.5f));
 }
}
/* Sparkles swipe style: its name and one position dot per style for SPARKLE_LABEL_S after a swipe. */
#define SPARKLE_LABEL_S 1.3f
static inline bool home_sparkle_label_on(const home_ui*s){
 const sparkles_state*c=&s->input.scene;
 return s->page==SPARKLES&&(c->style!=c->style_from||c->style_at>0)&&c->time-c->style_at>=0&&c->time-c->style_at<SPARKLE_LABEL_S;
}
static inline void home_sparkle_label(const home_ui*s,uint16_t*p){
 if(!home_sparkle_label_on(s))return;
 const char*name=sparkle_style_name(s->input.scene.style);
 uint16_t ink=sp_pack_lcd(255,255,255),shade=sp_pack_lcd(18,40,54);  /* dark sea blue */
 home_center(p,2,50,368,name,2,shade);home_center(p,0,48,368,name,2,ink);
 int x0=184-(SP_STYLES*20-10)/2;
 for(int i=0;i<SP_STYLES;i++){
  bool on=i==s->input.scene.style;int r=on?6:4,cx=x0+i*20+5;
  home_rect(p,cx-r-1,95-r-1,2*r+2,2*r+2,r+1,shade);home_rect(p,cx-r,95-r,2*r,2*r,r,on?ink:sp_pack_lcd(150,187,201));
 }
}
/* ---- Voice page (glowing orbs + hold-to-talk chat) ---- */
/* Colours are theme roles (theme.h); the comments give the Light values (Light + Orange = the original). */
#define HH_TEXT theme_c(TC_INK)            /* dark warm brown ink (74,38,16) */
#define HH_MUTED theme_c(TC_MUTED)         /* (150,86,44) */
#define HH_SHADOW theme_c(TC_SHADOW)       /* light halo keeps ink readable on orange (Dark: a black outline) */
#define HH_USER theme_c(TC_USER)           /* (240,104,24) */
#define HH_WHITE sp_pack_lcd(255,255,255)  /* plain white (not themed) */
#define HH_ORANGE theme_c(TA_FILL)         /* the accent (238,112,28) */
#define HH_PEACH theme_c(TA_TINT)          /* the accent's soft tint (255,196,140) */
#define HH_ERROR theme_c(TC_ERROR)         /* (196,52,24) */
#define HH_BOT theme_c(TC_GLASS)           /* glass tint (255,255,255) */
#define HH_WARM theme_c(TA_FILL)
static inline void helper_dim(uint16_t*p,int y0,int y1){
 if(y0<0)y0=0;
 if(y1>448)y1=448;
 for(int i=y0*368;i<y1*368;i++)p[i]=(uint16_t)((p[i]>>1)&0x7BEF);
}
/* Translucent rounded panel: halves the orbs underneath, then tints (cheap RGB565 blend). */
/* square_bl: keep the bottom-left corner square (the reply bubble's tail) in the SAME glass fill. */
static inline void helper_glass_shape(uint16_t*p,int x,int y,int w,int h,int radius,uint16_t tint,bool square_bl){
 int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>368?368:x+w,y1=y+h>448?448:y+h;
 bool dark=theme_dark();  /* Dark: denser glass (theme_glass_px) so light ink reads over the brightest orb */
 uint16_t t=theme_glass_tint(tint,dark);
 for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
  int dx=xx<x+radius?x+radius-xx:(xx>=x+w-radius?xx-(x+w-radius-1):0);
  int dy=yy<y+radius?y+radius-yy:(yy>=y+h-radius?yy-(y+h-radius-1):0);
  if(square_bl&&xx<x+radius&&yy>=y+h-radius)dx=dy=0;
  if(dx*dx+dy*dy>radius*radius)continue;
  uint16_t*q=p+yy*368+xx;*q=theme_glass_px(*q,t,dark);
 }
}
static inline void helper_glass(uint16_t*p,int x,int y,int w,int h,int radius,uint16_t tint){helper_glass_shape(p,x,y,w,h,radius,tint,false);}
static inline void helper_text(uint16_t*p,int x,int y,const char*t,int scale,uint16_t color){
 if(theme_dark()){
  /* Dark: a 1 px black outline all round, so light text reads over the glowing orbs too. */
  uint16_t o=HH_SHADOW;
  home_text(p,x-1,y,t,scale,o);home_text(p,x+1,y,t,scale,o);home_text(p,x,y-1,t,scale,o);home_text(p,x,y+1,t,scale,o);
  home_text(p,x,y,t,scale,color);return;
 }
 home_text(p,x+1,y+1,t,scale,HH_SHADOW);home_text(p,x,y,t,scale,color);
}
/* Text inside a glass bubble: the glass already gives the contrast in Dark (no outline needed). */
static inline void helper_glass_text(uint16_t*p,int x,int y,const char*t,int scale,uint16_t color){
 if(theme_dark()){home_text(p,x,y,t,scale,color);return;}
 helper_text(p,x,y,t,scale,color);
}
static inline float helper_ease(float t){return ui_ease_cubic_out(t,0.f,1.f,1.f);}
static inline const char*helper_status(const helper_view*h){
 switch(h->state){
  case HV_LISTENING:return "Listening";
  case HV_TRANSCRIBING:return h->note[0]?h->note:"Sending...";
  case HV_RUNNING:return h->note[0]?h->note:"Tap to stop";  /* Hermes' live phrase ("Searching the web") */
  case HV_STOPPING:return "Stopping...";
  case HV_DONE:return "";
  case HV_INTERRUPTED:return "Stopped";
  case HV_ERROR:return h->note[0]?h->note:"Something went wrong";
  default:return h->note;  /* idle: the bot itself (the welcome layout adds "Hold to talk") */
 }
}
/* Loading spinner (user rule 2026-09-30: loading, not "disabled", while the board finds out):
 * 8 dots on a ring of radius r, a bright head and a fading tail travelling round at ~1.2 turns/s. */
static inline void home_spinner(uint16_t*p,int cx,int cy,int r,float time,uint16_t head,uint16_t base){
 float turn=time*1.2f;int lead=((int)(turn*8.f))%8;if(lead<0)lead+=8;
 int dot=r>=24?5:r>=12?3:2;
 for(int i=0;i<8;i++){
  float a=i*0.7853982f;int x=cx+(int)(r*sinf(a)),y=cy-(int)(r*cosf(a));
  int behind=(lead-i+8)%8;unsigned mix=behind==0?256:behind==1?170:behind==2?90:0;
  uint16_t c=mix?sp_blend565(base,head,mix>255?255:mix):base;
  home_rect(p,x-dot,y-dot,dot*2,dot*2,dot,c);
 }
}
/* Three bouncing "thinking" dots. */
static inline void helper_dots(uint16_t*p,int x,int y,float time){
 for(int i=0;i<3;i++){
  float s=sinf(time*7.f-i*0.9f);int lift=s>0?(int)(s*6):0;
  home_rect(p,x+i*16,y-lift,10,10,5,s>0.3f?HH_WARM:HH_PEACH);
 }
}
/* The bot graphic (bot_art.h) is the hold-to-talk control: hold it to talk, tap it to stop. */
#define HH_OFF sp_pack_lcd(214,208,200)
#define HH_OFF_INK theme_c(TC_OFF)  /* (160,150,140) */
/* The selected bot cannot take a command (no quota / sign-in / unavailable provider): greyed, no halo. */
static inline bool helper_mic_disabled(const helper_view*h){return h->block&&helper_can_record(h);}
/* Kotaro's mood for a page state (also for the inputs before his last mood switch: helper_kotaro). */
static inline bot_mood helper_bot_mood_of(helper_state st,unsigned block,bool id){
 bool can=st==HV_IDLE||st==HV_DONE||st==HV_ERROR||st==HV_INTERRUPTED;
 if(block==BR_LOADING&&can)return BOT_LOADING;  /* still finding out: he dozes */
 if(block&&can)return BOT_SLEEP;
 switch(st){
  case HV_LISTENING:return BOT_LISTEN;
  case HV_TRANSCRIBING:return id?BOT_WORK:BOT_THINK;
  case HV_RUNNING:return BOT_WORK;
  case HV_STOPPING:return BOT_STOPPING;
  case HV_DONE:return BOT_HAPPY;
  case HV_ERROR:return BOT_SAD;
  case HV_INTERRUPTED:return BOT_STOPPED;
  default:return BOT_IDLE;
 }
}
static inline bot_mood helper_bot_mood(const helper_view*h){return helper_bot_mood_of(h->state,h->block,h->id[0]!=0);}
/* The Ask page's Kotaro: his mood and clocks, plus his life between commands (helper_ui.h helper_kotaro:
 * easing from the previous mood, routines, nap, celebrate, tap / hold / send reactions). */
static inline bot_pose helper_bot_pose(const helper_view*h,int look){
 bot_pose b;memset(&b,0,sizeof b);
 helper_bot_place(h,&b.cx,&b.cy,&b.u);
 b.look=(bot_look)look;b.mood=helper_bot_mood(h);b.t=h->orbs.time;b.mood_t=h->mood_state==h->state?h->mood_t:0;
 float lv=h->orbs.level>.01f?h->orbs.level:h->level_milli/1000.f;
 b.level=h->state==HV_LISTENING?lv:0;
 b.talking=h->state==HV_DONE&&helper_revealing(h);
 b.stop_badge=helper_stop_visible(h);
 b.grey=b.mood==BOT_SLEEP;b.shadow=!h->chat;b.compact=h->chat;
 const helper_kotaro*k=&h->kot;
 if(k->key_state==(unsigned char)h->state&&k->key_block==h->block&&k->key_id==(h->id[0]!=0)){  /* the poller has seen this mood */
  b.from=helper_bot_mood_of((helper_state)k->from_state,k->from_block,k->from_id);
  b.blend=b.from!=b.mood&&k->switch_s<BOT_BLEND_S;b.from_t=k->from_t;b.blend_t=k->switch_s;
 }
 b.routine=k->routine;b.routine_s=k->routine_s;b.idle_s=k->idle_s;b.wake_s=k->wake_s;b.wake_from=k->wake_from;
 b.cheer_s=k->cheer_s;b.tap_s=k->tap_s;b.lean=k->lean;b.nod_s=k->nod_s;b.breath_s=k->breath_s;
 return b;
}
static inline void helper_draw_bot(uint16_t*p,const helper_view*h,int look){bot_pose b=helper_bot_pose(h,look);bot_art_draw(p,&b);}
/* One status line, at most `cols` glyphs (a longer one ends in "..."). */
static inline const char*helper_fit(const char*t,char*out,size_t cap,int cols){
 size_t n=strlen(t),max=(size_t)cols<cap-1?(size_t)cols:cap-1;
 if(n<=max){memcpy(out,t,n+1);return out;}
 memcpy(out,t,max-3);memcpy(out+max-3,"...",4);return out;
}
/* Chat: the whole conversation of this bot's Hermes session, oldest at the top. Your words in
 * right-aligned orange bubbles, replies in left-aligned glass bubbles; the newest sits just above the
 * button. h->scroll (px) scrolls back through older turns, h->pull lifts the newest (swipe up = new
 * chat). Bubbles paint only between `clip` and HELPER_CHAT_CLIP_BOTTOM. */
/* Chat layout zones (never shared):  bubbles [HELPER_CHAT_CLIP .. HELPER_CHAT_CLIP_BOTTOM)
 *   status/notice row [HELPER_STATUS_Y .. +14]   button row: mic/stop with its halo/orbit and, while
 *   listening, the level bars on BOTH SIDES of the button (never above it, where the chat is). */
#define HELPER_CHAT_CLIP_BOTTOM 300
#define HELPER_GAP 12
static int helper_scroll_extent;  /* last paint: px of conversation above the window (owner copies it back) */
static char helper_lines[HELPER_WRAP_MAX][HELPER_COLS+1];
static inline int helper_role_cols(char role){return role=='U'?HELPER_USER_COLS:HELPER_BOT_COLS;}
static inline int helper_lines_width(int n){int w=0;for(int i=0;i<n;i++){int l=(int)strlen(helper_lines[i]);if(l>w)w=l;}return w;}
static inline int helper_bubble_h(int n,bool dots){return n?n*HELPER_LINE+20:(dots?38:0);}
static inline void helper_bubble(uint16_t*p,char role,int n,int y,int hh,float e,int clip,bool dots,bool caret,float time){
 const int clip1=HELPER_CHAT_CLIP_BOTTOM;
 int slide=(int)((1-e)*40);y+=(int)((1-e)*10);
 int w=n?helper_lines_width(n)*9+28:84;
 int x=role=='U'?368-16-w+slide:16-slide;
 int top=y<clip?clip:y,bot=y+hh>clip1?clip1:y+hh;
 if(bot<=top)return;
 if(role=='U'){
  int sb=y+hh+3>clip1?clip1:y+hh+3;
  if(sb>top+3)home_rect(p,x+2,top+3,w,sb-top-3,14,theme_c(TC_USER_SHADOW));
  home_rect(p,x,top,w,bot-top,14,HH_USER);
  if(y+hh-8>=clip&&y+hh<=clip1)home_rect(p,x+w-18,y+hh-8,18,8,0,HH_USER); /* tail corner */
  if(dots&&y+9>=clip&&y+25<=clip1)for(int i=0;i<3;i++){float sn=sinf(time*7.f-i*0.9f);int lift=sn>0?(int)(sn*6):0;home_rect(p,x+22+i*16,y+15-lift,10,10,5,sn>0.3f?theme_c(TC_USER_INK):theme_c(TC_USER_DOT));}
  for(int i=0;i<n;i++){int ty=y+10+i*HELPER_LINE;if(ty>=clip&&ty+14<=clip1)home_text(p,x+14,ty,helper_lines[i],1,theme_c(TC_USER_INK));}
  return;
 }
 helper_glass_shape(p,x,top,w,bot-top,14,HH_BOT,y+hh<=clip1);  /* tail = square corner, same glass */
 int bar0=top+10>y+10?top+10:y+10,bar1=(y+hh-10<bot?y+hh-10:bot);
 if(bar1>bar0)home_rect(p,x,bar0,3,bar1-bar0,1,role=='E'?HH_ERROR:HH_WARM);
 if(dots&&y+9>=clip&&y+25<=clip1)helper_dots(p,x+22,y+15,time);
 for(int i=0;i<n;i++){int ty=y+10+i*HELPER_LINE;if(ty>=clip&&ty+14<=clip1)helper_glass_text(p,x+14,ty,helper_lines[i],1,role=='E'?HH_ERROR:HH_TEXT);}
 if(caret&&n&&((int)(time*4))%2==0){int cxp=x+14+(int)strlen(helper_lines[n-1])*9+2,cyp=y+10+(n-1)*HELPER_LINE;if(cyp>=clip&&cyp+15<=clip1)home_rect(p,cxp,cyp+2,2,13,0,HH_WARM);}
}
/* The items of the chat, oldest first: log records, then the live exchange. Returns the count.
 * For live items the text may be a partial (typewriter) copy in `shown`. */
typedef struct{char role;const char*text;bool dots,caret,live_user,live_bot;} helper_item;
#define HELPER_ITEMS 48  /* newest records shown; older ones stay in the Hermes session */
static inline int helper_items(const helper_view*h,helper_item*it,int cap,char*shown){
 int n=0,records=0,skip;
 for(size_t at=0;at<h->log.used;){size_t len=strnlen(h->log.data+at,h->log.used-at);if(len>1)records++;at+=len+1;}
 skip=records>cap-2?records-(cap-2):0;
 for(size_t at=0;at<h->log.used&&n<cap-2;){
  const char*rec=h->log.data+at;size_t len=strnlen(rec,h->log.used-at);
  if(len>1){if(skip)skip--;else it[n++]=(helper_item){rec[0],rec+1,false,false,false,false};}
  at+=len+1;
 }
 bool pending=!h->transcript[0]&&(h->state==HV_LISTENING||h->state==HV_TRANSCRIBING);
 if(h->transcript[0]||pending)it[n++]=(helper_item){'U',h->transcript,pending,false,true,false};
 size_t len=strlen(h->reply),vis=(size_t)h->reveal;if(vis>len)vis=len;
 memcpy(shown,h->reply,vis);shown[vis]=0;
 bool thinking=helper_bot_visible(h)&&!vis;
 if(h->state==HV_ERROR&&!vis&&h->transcript[0])it[n++]=(helper_item){'E',h->note[0]?h->note:"Something went wrong",false,false,false,true};
 else if(vis||thinking)it[n++]=(helper_item){'B',shown,thinking,vis&&vis<len,false,true};
 return n;
}
static inline void helper_chat(uint16_t*p,const helper_view*h,int clip){
 static helper_item it[HELPER_ITEMS];static char shown[VOICE_TEXT_MAX+1];
 static int heights[HELPER_ITEMS];
 int n=helper_items(h,it,(int)(sizeof it/sizeof it[0]),shown),total=0;
 for(int i=0;i<n;i++){
  int lines=it[i].text[0]?helper_wrap(it[i].text,helper_role_cols(it[i].role),HELPER_WRAP_MAX,helper_lines):0;
  heights[i]=helper_bubble_h(lines,it[i].dots);
  total+=heights[i]+(i?HELPER_GAP:0);
 }
 int window=HELPER_BUBBLE_BOTTOM-clip;
 helper_scroll_extent=total>window?total-window:0;
 int scroll=h->scroll>helper_scroll_extent?helper_scroll_extent:h->scroll;
 int y=HELPER_BUBBLE_BOTTOM-total+scroll-h->pull;
 for(int i=0;i<n;i++){
  int hh=heights[i];
  if(hh&&y+hh>clip&&y<HELPER_CHAT_CLIP_BOTTOM){
   int lines=it[i].text[0]?helper_wrap(it[i].text,helper_role_cols(it[i].role),HELPER_WRAP_MAX,helper_lines):0;
   float e=it[i].live_user?helper_ease(h->user_in):(it[i].live_bot?helper_ease(h->bot_in):1.f);
   helper_bubble(p,it[i].role,lines,y,hh,e,clip,it[i].dots,it[i].caret,h->orbs.time);
  }
  y+=hh+HELPER_GAP;
 }
 /* Scroll bar while older messages are hidden above (or you scrolled back). */
 if(helper_scroll_extent>0){
  int track=HELPER_CHAT_CLIP_BOTTOM-clip-8,thumb=track*window/(total>0?total:1);if(thumb<24)thumb=24;
  int ty=clip+4+(track-thumb)*(helper_scroll_extent-scroll)/helper_scroll_extent;
  home_rect(p,362,ty,3,thumb,1,theme_c(TC_SCROLL));
 }
 /* Swipe-up affordance under the lifted conversation. */
 if(h->pull>8){
  const char*t=h->pull>=HELPER_NEW_PX?"Release for new chat":"Swipe up for new chat";
  int ty=HELPER_BUBBLE_BOTTOM-h->pull/2-7;if(ty<clip)ty=clip;
  helper_text(p,184-(int)strlen(t)*9/2,ty,t,1,h->pull>=HELPER_NEW_PX?theme_c(TA_TEXT):HH_MUTED);
 }
}
/* Why the mic is off: a short 2x title + a 1x detail line (welcome layout), or one line (chat).
 * Exhausted: "No quota" / "resets in 2h 10m" (detail only when known). */
typedef struct {const char*line1;char line2[28];} helper_block_text;
static inline helper_block_text helper_block_lines(const helper_view*h,const char*reset){
 helper_block_text t={"",{0}};
 if(h->block==BR_EXHAUSTED){t.line1="No quota";if(reset&&reset[0])snprintf(t.line2,sizeof t.line2,"%.27s",reset);}
 else if(h->block==BR_SIGNIN){t.line1="Sign in";snprintf(t.line2,sizeof t.line2,"on the host to use this bot");}
 else if(h->block==BR_UNLABELLED){t.line1="Not ready";snprintf(t.line2,sizeof t.line2,"Hermes unavailable");}
 else if(h->block==BR_PHONE){t.line1="Sign in";snprintf(t.line2,sizeof t.line2,"on Settings");}
 else if(h->block==BR_LOADING){t.line1="Connecting";snprintf(t.line2,sizeof t.line2,"to Hermes...");}
 return t;
}
/* Listening: the newest levels mirrored on both sides of the bot (clear of its listening pulse),
 * centred on its body: n bars per side, starting `gap` px from the centre. */
#define HELPER_WAVE_GAP_BIG ((int)(76*HELPER_BOT_SCALE)+8)
#define HELPER_WAVE_GAP_CHAT ((int)(76*HELPER_CHAT_SCALE)+6)
static inline void helper_side_wave(uint16_t*p,const helper_view*h,int cy,int gap,int n){
 const int pitch=8;
 if(n>HELPER_WAVE)n=HELPER_WAVE;
 for(int i=0;i<n;i++){
  int v=h->wave[HELPER_WAVE-1-i],hh=4+v*48/250;if(hh>52)hh=52;
  uint16_t c=i<3?HH_TEXT:HH_WARM;int y=cy-hh/2;
  home_rect(p,184+gap+i*pitch,y,5,hh,2,c);
  home_rect(p,184-gap-i*pitch-5,y,5,hh,2,c);
 }
}
static inline void helper_chat_controls(const helper_view*h,uint16_t*p,int look,const char*reset);
/* Everything above the orbs (bot, status, chat); bubbles never paint above `clip`. `look` = the
 * selected bot's graphic (bot_art.h bot_look_of), `reset` = countdown text of its blocking window. */
static inline void helper_render_overlay(const helper_view*h,uint16_t*p,int clip,int look,const char*reset){
 bool off=helper_mic_disabled(h);helper_block_text bt=helper_block_lines(h,reset);
 if(!h->chat){
  /* Welcome layout: the big bot in the middle (hold it to talk), one line underneath. */
  if(h->state==HV_LISTENING)helper_side_wave(p,h,HELPER_BOT_Y,HELPER_WAVE_GAP_BIG,6);
  helper_draw_bot(p,h,look);
  int y=HELPER_BOT_TEXT_Y;
  if(off){
   helper_text(p,184-(int)strlen(bt.line1)*9,y,bt.line1,2,HH_TEXT);
   if(bt.line2[0])helper_text(p,184-(int)strlen(bt.line2)*9/2,y+40,bt.line2,1,HH_MUTED);
   return;
  }
  const char*status=helper_status(h);
  if(h->state==HV_IDLE&&!status[0])status="Hold to talk";
  char line[40];helper_fit(status,line,sizeof line,34);
  helper_text(p,184-(int)strlen(line)*9/2,y,line,1,h->state==HV_ERROR?HH_ERROR:HH_MUTED);
  return;
 }
 helper_chat(p,h,clip);
 helper_chat_controls(h,p,look,reset);
}
/* Chat layout controls only (status row + button row); disjoint from helper_chat() by construction
 * (tests/host/chat_zones_test.c renders both separately and checks no pixel is touched by both). */
static inline void helper_chat_controls(const helper_view*h,uint16_t*p,int look,const char*reset){
 bool off=helper_mic_disabled(h);helper_block_text bt=helper_block_lines(h,reset);
 if(h->state==HV_LISTENING)helper_side_wave(p,h,HELPER_CHAT_BOT_Y,HELPER_WAVE_GAP_CHAT,HELPER_WAVE/2);
 helper_draw_bot(p,h,look);
 if(off){
  /* Chat layout: one line above the compact sleeping bot. */
  char line[48];
  if(h->block==BR_EXHAUSTED)snprintf(line,sizeof line,"No quota%s%.27s",bt.line2[0]?" - ":"",bt.line2);
  else snprintf(line,sizeof line,"%s",bots_reason_text((bots_reason)h->block));
  helper_text(p,184-(int)strlen(line)*9/2,HELPER_STATUS_Y,line,1,HH_TEXT);
 }else if(h->state!=HV_LISTENING){
  /* While working this is Hermes' live phrase (h->note), else "Tap to stop" / "Sending..." */
  const char*status=helper_status(h);
  if(h->state==HV_ERROR&&h->transcript[0])status="";  /* the error is already in the reply bubble */
  char line[40];helper_fit(status,line,sizeof line,34);
  helper_text(p,184-(int)strlen(line)*9/2,HELPER_STATUS_Y,line,1,h->state==HV_RUNNING&&h->note[0]?HH_TEXT:HH_MUTED);
 }
}
/* Bot name pill + one dot per bot at the top (fixed: it does not slide with the page). */
static inline void helper_bot_header(uint16_t*p,const helper_view*h,const char*name){
 int w=(int)strlen(name)*9+32,x=184-w/2;
 helper_glass(p,x,HELPER_PILL_Y,w,HELPER_PILL_H,HELPER_PILL_H/2,HH_BOT);
 home_rect(p,x+12,HELPER_PILL_Y+11,6,6,3,h->block&&h->block!=BR_LOADING?HH_OFF_INK:HH_WARM);
 home_text(p,x+22,HELPER_PILL_Y+5,name,1,HH_TEXT);
 int n=helper_nbots(h),total=n*8+(n-1)*8+8,dx=184-total/2;
 for(int i=0;i<n;i++){
  bool on=i==h->bot;int dw=on?16:8;
  home_rect(p,dx,HELPER_DOTS_Y,dw,8,4,on?HH_ORANGE:theme_c(TC_BOT_DOT));
  dx+=dw+8;
 }
}
/* Shift the page content horizontally by `off` px (slide transition / rubber band); the uncovered
 * strip is the orbs' paper colour. Only runs while an animation is in progress. */
#define HH_PAPER theme_c(TC_PAPER)  /* (255,252,247) */
static inline void helper_shift(uint16_t*p,int off){
 if(!off)return;
 uint16_t paper=HH_PAPER;
 if(off>=368||off<=-368){for(int i=0;i<SPARKLES_PIXELS;i++)p[i]=paper;return;}
 for(int y=0;y<448;y++){
  uint16_t*row=p+y*368;
  if(off>0){memmove(row+off,row,(size_t)(368-off)*2);for(int x=0;x<off;x++)row[x]=paper;}
  else{memmove(row,row-off,(size_t)(368+off)*2);for(int x=368+off;x<368;x++)row[x]=paper;}
 }
}
static inline void helper_render_view(const helper_view*h,uint16_t*p,const char*name,int look,const char*reset){
 SP_STAMP(0);
 orbs_theme_dark=theme_dark();  /* the orbs stay warm; Dark glows them on black paper */
 orbs_render(&h->orbs,p);
 SP_STAMP(1);
 helper_render_overlay(h,p,HELPER_CHAT_CLIP,look,reset);
 SP_STAMP(2);
 helper_shift(p,(int)h->slide);
 helper_bot_header(p,h,name);
 SP_STAMP(3);
}
static inline void helper_render(const helper_view*h,uint16_t*p){
 int i=h->bot>=0&&h->bot<HELPER_BOTS?h->bot:0;
 helper_render_view(h,p,bots_default_name[i],bot_look_of(bots_default_id[i]),"");
}
/* Ask page: selected bot's name, graphic and mic gate from the latest /v1/bots frame (compiled-in names until the first frame). */
static inline void helper_render_page(const home_ui*s,uint16_t*p){
 char reset[28];bots_reset_text(bots_reset_left(&s->bots,s->helper.bot,s->live_now_us),reset,sizeof reset);
 helper_render_view(&s->helper,p,bots_name(&s->bots,s->helper.bot),bot_look_of(bots_id(&s->bots,s->helper.bot)),reset);
}
/* Home Ask tile: just Kotaro, once, in no clothes (user rule 2026-10-06: each bot's outfit shows on
 * its Ask page, not here), sitting still in the same style as the Ask page (4 px cells); dozing in
 * colour while the tile is loading (eyes shut, a slow nod, Zz: user, 2026-10-07), greys asleep when it
 * is closed. Inside the 192 px icon box (x+88.., y 108..300). His tail makes him lopsided: on the tile
 * centre his ink mass sits 2 px left of it and his outline 2 px right. */
enum {HOME_BOT_TILE_ON,HOME_BOT_TILE_LOADING,HOME_BOT_TILE_OFF};
static inline void home_bots_tile(uint16_t*p,int x,int state,float time){
 bool off=state==HOME_BOT_TILE_OFF,loading=state==HOME_BOT_TILE_LOADING;
 bot_pose b;memset(&b,0,sizeof b);
 b.t=loading?time:.6f;b.mood_t=b.t;b.shadow=true;b.grey=off;b.mood=off?BOT_SLEEP:(loading?BOT_LOADING:BOT_IDLE);
 b.look=BOT_LOOK_PLAIN;b.u=1.1f;b.cx=(float)x+184.f;b.cy=212.f;b.gaze=0;
 bot_art_draw(p,&b);
}

/* ---- Settings: one warm screen, no text entry, large buttons, all inside the safe area ---- */
#define PHONE_QR_INK sp_pack_lcd(0,0,0)
#define PHONE_QR_PAPER sp_pack_lcd(255,255,255)
#define PHONE_BG theme_c(TC_PAGE)     /* (255,248,238); Dark: black */
#define PHONE_PILL theme_c(TC_ROW)    /* (255,228,196); Dark: dark grey rows */
/* Draw the QR symbol on a white card: the full square 4-module quiet zone plus a 4 px rounded white
 * rim outside it, so the rounding never eats into the quiet zone. */
static inline void phone_render_qr(const phone_view*ph,uint16_t*p){
 int size=phone_qr_size(ph);if(!size)return;
 int qx,qy,qs;phone_qr_geometry(ph,&qx,&qy,&qs);
 int card=(size+2*PHONE_QR_QUIET)*qs,cx=qx-PHONE_QR_QUIET*qs,cy=qy-PHONE_QR_QUIET*qs;
 home_rect(p,cx-4,cy-4,card+8,card+8,8,PHONE_QR_PAPER);
 for(int my=0;my<size;my++){
  uint16_t*row=p+(qy+my*qs)*368;
  for(int mx=0;mx<size;mx++){
   if(!phone_qr_module(ph,mx,my))continue;
   for(int yy=0;yy<qs;yy++)for(int xx=0;xx<qs;xx++)row[yy*368+qx+mx*qs+xx]=PHONE_QR_INK;
  }
 }
}
static inline const char*phone_host(const char*uri,char*out,size_t cap){
 const char*h=strncmp(uri,"https://",8)?uri:uri+8;size_t n=0;
 while(h[n]&&h[n]!='/'&&h[n]!='?'&&n+1<cap)n++;
 memcpy(out,h,n);out[n]=0;return out;
}
/* ---- Settings text layout: word-wrapped, centred, never wider than the safe width. The host tests
 * (tests/host/settings_ui_test.c) install `set_rec` to collect every drawn text box; firmware leaves it NULL. */
#define SET_TEXT_W (SET_W-2*SET_PAD)          /* 288 px: 32 small / 16 big glyphs per line */
#define SET_REC_MAX 32
typedef struct {int x,y,w,h,scale;bool truncated;char text[40];} settings_text;
typedef struct {int count;bool overflow;settings_text t[SET_REC_MAX];} settings_record;
static settings_record*set_rec;
static inline void set_note(int x,int y,const char*t,size_t n,int scale,bool truncated){
 if(!set_rec)return;
 if(set_rec->count>=SET_REC_MAX){set_rec->overflow=true;return;}
 settings_text*r=&set_rec->t[set_rec->count++];
 if(n>sizeof r->text-1){n=sizeof r->text-1;truncated=true;}
 r->x=x;r->y=y;r->w=(int)n*9*scale;r->h=17*scale;r->scale=scale;r->truncated=truncated;memcpy(r->text,t,n);r->text[n]=0;
}
static inline void set_draw(uint16_t*p,int x,int y,const char*t,size_t n,int scale,uint16_t color,bool truncated){
 char buf[48];if(n>sizeof buf-1){n=sizeof buf-1;truncated=true;}
 memcpy(buf,t,n);buf[n]=0;home_text(p,x,y,buf,scale,color);set_note(x,y,buf,n,scale,truncated);
}
/* Network names for the 9 px ASCII font: iOS writes iPhone names with typographic quotes (U+2018/2019,
 * "Sam’s iPhone"); show them as ' instead of three '?'. Any other non-ASCII character is one '?'. */
static inline const char*set_ascii_name(const char*in,char*out,size_t cap){
 size_t o=0;
 for(const unsigned char*u=(const unsigned char*)in;*u&&o+1<cap;){
  if(u[0]==0xE2&&u[1]==0x80&&(u[2]==0x98||u[2]==0x99)){out[o++]='\'';u+=3;}
  else if(u[0]>=0x80){out[o++]='?';u++;while((*u&0xC0)==0x80)u++;}
  else out[o++]=(char)*u++;
 }
 out[o]=0;return out;
}
/* Split `t` into lines of at most `cols` glyphs at spaces (a word longer than a line is hard-broken). */
static inline int set_wrap(const char*t,int cols,int max_lines,int start[],int len[],bool*cut){
 int n=0,i=0,total=(int)strlen(t);*cut=false;
 while(i<total){
  while(i<total&&t[i]==' ')i++;
  if(i>=total)break;
  if(n==max_lines){*cut=true;break;}
  int end=i+cols;
  if(end>=total)end=total;
  else{int k=end;while(k>i&&t[k]!=' ')k--;if(k>i)end=k;}
  int e=end;while(e>i&&t[e-1]==' ')e--;
  start[n]=i;len[n]=e-i;n++;i=end;
 }
 return n;
}
/* Paragraph centred in [x0, x0+w) from y (scale 2 falls back to 1 when a word would not fit).
 * Returns the next y. */
static inline int set_block(uint16_t*p,int x0,int w,int y,const char*t,int scale,int max_lines,uint16_t color){
 int st[6],ln[6];bool cut;if(max_lines>6)max_lines=6;
 int n=set_wrap(t,w/(9*scale),max_lines,st,ln,&cut);
 if(scale>1&&cut){scale=1;n=set_wrap(t,w/9,max_lines,st,ln,&cut);}
 for(int i=0;i<n;i++){int lw=ln[i]*9*scale;set_draw(p,x0+(w-lw)/2,y,t+st[i],(size_t)ln[i],scale,color,cut&&i==n-1);y+=17*scale+(scale>1?6:5);}
 return y;
}
static inline int set_para(uint16_t*p,int y,const char*t,int scale,int max_lines,uint16_t color){
 return set_block(p,184-SET_TEXT_W/2,SET_TEXT_W,y,t,scale,max_lines,color);
}
/* One target: filled rounded control, optional small caption line (+ right hint), big label, switch. */
#define SET_SWITCH_W 76
#define SET_SWITCH_H 40
/* Settings > Display: the switch slot shows the choice instead of a switch. Theme = a half light / half
 * black disc (the black half bigger in Dark); Accent = a disc of the accent in a light rim. */
static inline void set_swatch(uint16_t*p,const settings_target*t,int sx,int sy){
 int cx=sx+SET_SWITCH_W/2,cy=sy+SET_SWITCH_H/2,r=SET_SWITCH_H/2;
 if(t->swatch==SET_SWATCH_ACCENT){
  home_rect(p,cx-r,cy-r,2*r,2*r,r,theme_c(TC_KNOB));
  home_rect(p,cx-r+4,cy-r+4,2*r-8,2*r-8,r-4,HH_ORANGE);
  return;
 }
 /* Theme: a rim in the ink, then the disc split light | black at a slant that follows the mode */
 home_rect(p,cx-r,cy-r,2*r,2*r,r,HH_TEXT);
 int ri=r-3,split=t->on?-6:6;
 uint16_t light=theme_color_of(THEME_LIGHT,0,TC_PAGE),black=theme_color_of(THEME_DARK,0,TC_PAGE);
 for(int y=-ri;y<ri;y++)for(int x=-ri;x<ri;x++){
  if((2*x+1)*(2*x+1)+(2*y+1)*(2*y+1)>4*ri*ri)continue;
  p[(cy+y)*368+cx+x]=x<split+(y*2)/5?light:black;
 }
}
static inline void set_target(uint16_t*p,const settings_target*t,const char*hint){
 uint16_t fill=t->primary?HH_ORANGE:PHONE_PILL,ink=t->primary?theme_c(TA_ON):HH_TEXT,soft=t->primary?theme_c(TA_ON):HH_MUTED;
 home_rect(p,t->x,t->y,t->w,t->h,20,fill);
 int text_w=t->w-2*SET_PAD-(t->toggle||t->swatch?SET_SWITCH_W+12:0),tx=t->x+SET_PAD;
 size_t n=strlen(t->label);int scale=(int)n*18<=text_w?2:1;
 bool cut=(int)n*9*scale>text_w;if(cut)n=(size_t)(text_w/(9*scale));
 int lh=17*scale;
 if(t->caption){
  int cy=t->y+(t->h-(17+8+lh))/2;
  set_draw(p,tx,cy,t->caption,strlen(t->caption),1,soft,(int)strlen(t->caption)*9>text_w);
  if(hint){int hw=(int)strlen(hint)*9;set_draw(p,t->x+t->w-SET_PAD-hw,cy,hint,strlen(hint),1,t->primary?theme_c(TA_ON):theme_c(TA_TEXT),false);}
  set_draw(p,tx,cy+25+(scale==1?8:0),t->label,n,scale,ink,cut);
 }else{
  int w=(int)n*9*scale;
  set_draw(p,t->x+(t->w-w)/2,t->y+(t->h-lh)/2,t->label,n,scale,ink,cut);
 }
 if(t->toggle){
  int sx=t->x+t->w-SET_PAD-SET_SWITCH_W,sy=t->y+(t->h-SET_SWITCH_H)/2,r=SET_SWITCH_H/2;
  home_rect(p,sx,sy,SET_SWITCH_W,SET_SWITCH_H,r,t->on?HH_ORANGE:HH_OFF_INK);
  int kx=t->on?sx+SET_SWITCH_W-SET_SWITCH_H+4:sx+4;
  home_rect(p,kx,sy+4,SET_SWITCH_H-8,SET_SWITCH_H-8,r-4,theme_c(TC_KNOB));
 }else if(t->swatch)set_swatch(p,t,t->x+t->w-SET_PAD-SET_SWITCH_W,t->y+(t->h-SET_SWITCH_H)/2);
}
/* A status line that is not a control: small muted caption + value, no fill (reads as content). */
static inline int set_status(uint16_t*p,int y,const char*label,const char*value,uint16_t color){
 char line[80];snprintf(line,sizeof line,"%s: %s",label,value);
 return set_para(p,y,line,1,2,color);
}
/* One provider's sign-in state as a status line ("Hermes: ready", "Home Assistant: sign in to use Sensor"). */
static inline int set_signin_line(uint16_t*p,int y,const phone_view*v,bool ha){
 bool ok=ha?phone_signed_in(v):!phone_gate(v);
 const char*what=ok?"ready":(v->st.state==PH_REFUSED?"not for this account":(ha?"sign in to use Sensor":"sign in to use Ask"));
 return set_status(p,y,ha?"Home Assistant":"Hermes",what,ok?HH_MUTED:HH_ERROR)+4;
}
/* Tab dots, centred: one per reachable tab (no sign-in tab without Wi-Fi, home_settings_tabs). */
static int set_tab_count=SETTINGS_TABS;
static bool set_tab_on[SETTINGS_TABS]={true,true,true,true,true,true};
static inline void set_title(uint16_t*p,const char*title,settings_page tab){
 set_para(p,20,title,2,1,HH_TEXT);
 int n=set_tab_count,x=184-(16+(n-1)*8+(n-1)*8)/2;
 for(int k=0;k<SETTINGS_TABS;k++){if(!set_tab_on[k])continue;int w=k==(int)tab?16:8;home_rect(p,x,56,w,6,3,k==(int)tab?HH_ORANGE:theme_c(TC_DOT));x+=w+8;}
}
static inline void set_ring(uint16_t*p,int y){home_rect(p,148,y,72,72,36,theme_c(TC_DOT));home_rect(p,166,y+18,36,36,18,PHONE_BG);}
#include "power_ui.h"  /* power-off countdown overlay + the Battery hint line (needs the text helpers above) */
/* ---- Settings > Battery: the charge as a soft battery in the bots' style (soft_shapes.h), the big
 * percent and one state line (battery_estimate.h battery_status_text: the part before " - " big). ---- */
#define BATT_X0 77      /* body 77..277 + nub to 291: centred on 184 */
#define BATT_Y0 92
#define BATT_W 200
#define BATT_H 104
static inline void battery_bolt(uint16_t*p,float cx,float cy,float k,uint16_t c){
 ss_tri(p,cx+9*k,cy-33*k,cx-19*k,cy+6*k,cx+4*k,cy+3*k,c,255);
 ss_tri(p,cx-4*k,cy-3*k,cx+19*k,cy-6*k,cx-9*k,cy+33*k,c,255);
}
static inline void battery_draw(uint16_t*p,const battery_view*b){
 battery_state st=battery_state_of(b);
 float x0=BATT_X0,y0=BATT_Y0,x1=x0+BATT_W,y1=y0+BATT_H,cy=(y0+y1)*.5f;
 bool known=st!=BATT_UNKNOWN&&st!=BATT_MISSING;
 uint16_t shell=theme_c(known?TC_BATT_SHELL:TC_BATT_SHELL_OFF),deep=theme_c(known?TC_BATT_DEEP:TC_BATT_DEEP_OFF);
 /* Same build as a bot: soft ground shadow, deep bottom shade under the case colour, gloss streak. */
 ss_rrect(p,x0+16,y1+3,x1-16,y1+11,4,theme_c(TC_BATT_GROUND),90,8);
 ss_rrect(p,x1-4,cy-21,x1+14,cy+21,9,deep,255,1);                                /* nub */
 ss_rrect(p,x1-4,cy-21,x1+14,cy+18,9,shell,255,1);
 ss_rrect(p,x0,y0,x1,y1,28,deep,255,1);                                          /* case */
 ss_rrect(p,x0,y0,x1,y1-5,28,shell,255,1);
 ss_rrect(p,x0+24,y0+4,x0+64,y0+8,2,theme_c(TC_BATT_GLOSS),150,1);
 ss_rrect(p,x0+12,y0+12,x1-12,y1-14,17,theme_c(TC_BATT_WINDOW),255,1);           /* window */
 if(!known){home_center(p,(int)x0,(int)cy-25,BATT_W,"?",3,HH_OFF_INK);return;}
 int pct=b->percent<0?0:(b->percent>100?100:b->percent);
 float fx0=x0+19,fw=(x1-19-fx0)*(float)pct/100.f;
 uint16_t fill=st==BATT_ON_BATTERY&&pct<=BATTERY_LOW_PCT?HH_ERROR:(st==BATT_FULL?theme_c(TA_FULL):HH_ORANGE);
 if(fw>=3){
  ss_rrect(p,fx0,y0+19,fx0+fw,y1-21,11,sp_blend565(fill,theme_c(TC_BATT_DARK),60),255,1);    /* deep shade */
  ss_rrect(p,fx0,y0+19,fx0+fw,y1-24,11,fill,255,1);
  if(fw>24)ss_rrect(p,fx0+8,y0+25,fx0+fw-8,y0+30,2.5f,HH_WHITE,120,1);         /* gloss */
 }
 if(st==BATT_CHARGING||st==BATT_PLUGGED){
  for(int k=0;k<8;k++){float a=k*.7853982f;battery_bolt(p,184+3.f*cosf(a),cy+3.f*sinf(a),1.f,theme_c(TC_BATT_DARK));}
  battery_bolt(p,184,cy,1.f,HH_WHITE);
 }
}
static inline bool known_voltage(const battery_view*b){return b->known&&b->present&&b->vbat_mv>0&&b->vbat_mv<10000;}
static inline void battery_page(const home_ui*s,uint16_t*p){
 const battery_view*b=&s->battery;
 set_title(p,"Battery",SETTINGS_BATTERY);
 battery_draw(p,b);
 battery_state st=battery_state_of(b);
 char big[8];
 if(st==BATT_UNKNOWN||st==BATT_MISSING||b->percent<0)snprintf(big,sizeof big,"--");
 else snprintf(big,sizeof big,"%d%%",b->percent>100?100:b->percent);
 set_draw(p,184-(int)strlen(big)*18,214,big,strlen(big),4,HH_TEXT,false);
 char line[64];battery_status_text(b,line,sizeof line);
 char*dash=strstr(line," - ");const char*detail="";
 if(dash){*dash=0;detail=dash+3;}
 int y=set_para(p,296,line,2,2,HH_TEXT)+2;
 if(detail[0])y=set_para(p,y,detail,1,2,HH_MUTED)+4;
 if(known_voltage(b)){char v[24];snprintf(v,sizeof v,"%d.%02d V",b->vbat_mv/1000,(b->vbat_mv%1000)/10);set_para(p,y,v,1,1,HH_MUTED);}
 set_draw(p,184-(int)strlen(POWER_HINT_TEXT)*9/2,POWER_HINT_Y,POWER_HINT_TEXT,strlen(POWER_HINT_TEXT),1,HH_MUTED,false); /* power_ui.h */
}
static inline void settings_render(const home_ui*s,uint16_t*p){
 {uint16_t bg=PHONE_BG;for(int i=0;i<SPARKLES_PIXELS;i++)p[i]=bg;}
 /* The open provider tab's own sign-in (Hermes or Home Assistant); unused on the core tabs. */
 const phone_view*ph=home_tab_phone_c(s,s->settings_tab);
 if(!ph)ph=&s->phone;
 bool ha=s->settings_tab==SETTINGS_HOME_ASSISTANT;
 settings_screen m=home_settings_screen(s);
 settings_buttons b=home_settings_buttons(s,s->live_now_us);
 set_tab_count=home_settings_tabs(s);
 for(int k=0;k<SETTINGS_TABS;k++)set_tab_on[k]=home_settings_tab_reachable(s,k);
 const char*hint[SETTINGS_MAX_BUTTONS]={0};
 bool enrolled=s->pair.state>=PAIR_ENROLLED_UNPAIRED,offline=s->pair.live_http&&s->pair.live_http!=200;
 char wifi_name[40];
 const char*wifi=s->connected?(s->credentials.ssid[0]?set_ascii_name(s->credentials.ssid,wifi_name,sizeof wifi_name):"connected"):(s->saved?"connecting...":"not set up");
 if(s->settings_tab==SETTINGS_BATTERY){battery_page(s,p);return;}
 if(s->settings_tab==SETTINGS_WIFI){
  /* Status only (user rule 2026-10-02): home Wi-Fi first, iPhone Personal Hotspot as the fallback
   * (wifi_choice.h). Both networks come with the flash (.env -> tools/flash.sh -> USB). */
  bool hs=s->connected&&s->on_hotspot,any=s->saved||s->hotspot_saved;
  set_title(p,"Wi-Fi",SETTINGS_WIFI);
  set_para(p,86,hs?"On hotspot":s->connected?"Connected":any?"Joining Wi-Fi":"No Wi-Fi yet",2,1,HH_TEXT);
  char shown[40];
  set_para(p,132,set_ascii_name(hs&&s->hotspot_ssid[0]?s->hotspot_ssid:s->credentials.ssid[0]?s->credentials.ssid:any?"iPhone hotspot":"No saved network",shown,sizeof shown),1,2,HH_TEXT);
  if(any)set_para(p,184,s->status[0]?s->status:"",1,2,HH_MUTED);
  const char*note=
   hs?(s->saved?"Moves back to home Wi-Fi as soon as it is in range.":"Uses data from your iPhone."):
   s->hotspot_saved&&s->saved?"Falls back to your iPhone hotspot when home Wi-Fi is out of reach.":
   s->hotspot_saved?"Joins your iPhone hotspot. Home Wi-Fi comes with the next flash.":
   any?"Add an iPhone hotspot fallback in .env and flash again.":
   "Wi-Fi comes with the flash: put it in .env on the host, then run tools/flash.sh.";
  if(home_settings_loading(s))home_spinner(p,184,272,22,s->input.scene.time,HH_ORANGE,theme_c(TC_DOT));
  else set_para(p,any?238:184,note,1,4,HH_MUTED);
  return;
 }

 if(m==SS_QR&&phone_display(ph,s->live_now_us)==PH_PENDING&&ph->qr_ok){
  /* The whole screen is the QR: title + countdown, the biggest symbol that fits (4-module white quiet
   * zone), then the short code + where to type it beside a compact Back (both inside the safe area). */
  char left[16],title[40];phone_countdown(ph,s->live_now_us,left,sizeof left);
  snprintf(title,sizeof title,"Scan with your phone  %s",left);
  set_para(p,PHONE_QR_CARD_Y-24,title,1,1,HH_TEXT);
  phone_render_qr(ph,p);
  int by=b.count?b.t[0].y:PHONE_QR_ROW_Y,cw=SET_QR_CODE_W;
  int y=set_block(p,SET_X,cw,by+2,ph->st.user_code,2,1,HH_TEXT);
  char host[40],where[64];phone_host(ph->st.uri,host,sizeof host);
  snprintf(where,sizeof where,"or type it at %s",host);
  set_block(p,SET_X,cw,y-1,where,1,2,HH_MUTED);
 }else{
  settings_page shown=s->settings_tab;
  if(m==SS_NO_WIFI||m==SS_JOINING)shown=SETTINGS_WIFI;
  set_title(p,shown==SETTINGS_WIFI?"Wi-Fi":(shown==SETTINGS_SOUND?"Sound":(shown==SETTINGS_DISPLAY?"Display":home_provider_label(shown))),shown);
  switch(m){
   case SS_NO_WIFI:{
    int y=set_para(p,76,"No Wi-Fi yet",2,1,HH_TEXT)+12;
    set_para(p,y,"Wi-Fi comes with the flash: put it in .env on the host, then run tools/flash.sh.",1,4,HH_MUTED);
    break;}
   case SS_JOINING:{
    int y=set_para(p,76,"Joining Wi-Fi",2,1,HH_TEXT)+12;
    y=set_para(p,y,s->credentials.ssid[0]?s->credentials.ssid:"saved network",1,2,HH_TEXT)+6;
    set_para(p,y,s->status[0]?s->status:"Connecting...",1,2,HH_MUTED);
    break;}
   case SS_FIND:{
    if(s->pair.step==PV_SCANNING){set_ring(p,96);int y=set_para(p,188,"Looking for your bridge",2,2,HH_TEXT)+10;set_para(p,y,"waveshare-bridge must be running on the host.",1,2,HH_MUTED);}
    else if(s->pair.list.count){
     int y=set_para(p,76,"Pick your host",2,1,HH_TEXT)+2;
     if(s->pair.list.count>2){char more[32];snprintf(more,sizeof more,"%d found, showing 2",s->pair.list.count);set_para(p,y,more,1,1,HH_MUTED);}
    }else{
     int y=set_para(p,96,"Bridge not found",2,2,HH_TEXT)+10;
     if(s->pair.note[0]&&s->pair.step==PV_ERROR)set_para(p,y,s->pair.note,1,2,HH_ERROR);
     else set_para(p,y,"Start waveshare-bridge on the host, then search again.",1,3,HH_MUTED);
     set_status(p,254,"Wi-Fi",wifi,HH_MUTED);
    }
    break;}
   case SS_CODE:
    if(s->pair.code_shown){
     char code[8];pair_code_text(s->pair.code,code);
     int y=set_para(p,64,"Same code on your host?",1,1,HH_TEXT)+8;
     home_rect(p,SET_X+24,y,SET_W-48,92,22,theme_c(TC_CARD));
     set_draw(p,184-(int)strlen(code)*18,y+12,code,strlen(code),4,HH_TEXT,false);
     y+=104;
     y=set_para(p,y,"Confirm it on the host with",1,1,HH_MUTED);
     y=set_para(p,y,"waveshare-bridge enroll",1,1,theme_c(TA_TEXT))+4;
     set_para(p,y,s->pair.bridge[0]?s->pair.bridge:"your host",1,1,HH_MUTED);
    }else{set_ring(p,96);int y=set_para(p,188,"Connecting",2,1,HH_TEXT)+10;set_para(p,y,s->pair.bridge[0]?s->pair.bridge:"to your host",1,2,HH_MUTED);}
    break;
   case SS_QR:{
    phone_state d=phone_display(ph,s->live_now_us);
    const char*title="Getting a code",*l1="";
    switch(d){
     case PH_NONE:if(ph->note[0]){title="Not available";l1=ph->note;}break;
     case PH_DENIED:title="Declined";l1="Declined on the phone.";break;
     case PH_EXPIRED:title="Code expired";l1="Get a fresh code and scan again.";break;
     case PH_REFUSED:title="Not allowed";l1=ha?"This account can't use Home Assistant.":"This account can't use Hermes.";break;
     case PH_ERROR:title="Sign-in failed";l1=ph->note[0]?ph->note:"Portal unreachable";break;
     default:break;
    }
    set_ring(p,68);
    int y=set_para(p,150,title,2,1,HH_TEXT)+8;
    if(l1[0])set_para(p,y,l1,1,2,HH_MUTED);
    break;}
   case SS_STATUS:{
    /* (The Wi-Fi tab returns before this switch: see the SETTINGS_WIFI block above.) */
    if(s->settings_tab==SETTINGS_DISPLAY){
     set_para(p,292,"Dark keeps most of the screen black: easier at night, and the pixels that are off save battery.",1,4,HH_MUTED);
    }else if(s->settings_tab==SETTINGS_SOUND){
     set_para(p,292,s->connected?"Completion sound is a very soft click. Session sparkle controls Hermes activity markers.":
      "Completion sound is a very soft click. Session sparkle needs Wi-Fi.",1,4,HH_MUTED);
    }else if(ph->absent){
     /* The bridge answered 404 for this provider: it is not set up there (no buttons). */
     char where[64];snprintf(where,sizeof where,"Not set up on %s",s->pair.bridge[0]?s->pair.bridge:"this host");
     int y=set_para(p,96,where,2,3,HH_TEXT)+10;
     set_para(p,y,"Add it to providers in bridge.json on the host.",1,3,HH_MUTED);
    }else{
     bool signin=home_provider_signin(s->settings_tab);
     if(phone_signed_in(ph))hint[0]="Sign out";
     if(!b.count&&offline)set_para(p,SET_ROW1_Y+30,"host bridge offline",1,1,HH_MUTED);
     else if(!b.count&&!signin)set_para(p,SET_ROW1_Y+30,"No sign-in needed",1,1,HH_MUTED);
     else if(!b.count){home_spinner(p,72,SET_ROW1_Y+36,14,s->input.scene.time,HH_ORANGE,theme_c(TC_DOT));set_para(p,SET_ROW1_Y+30,"Checking sign-in...",1,1,HH_MUTED);}
     /* What this provider's sign-in unlocks (and whether it is shared), then the host link: plain
      * status lines, not controls. The one account tab of a shared sign-in (SPEC3 Contract S) says it
      * covers both providers, then each one's own state (Home Assistant from its own status). */
     int y=SET_ROW2_Y+8;
     bool both=home_signin_shared(s)&&s->settings_tab==SETTINGS_HERMES;
     if(both)y=set_para(p,y,WAVESHARE_AI_SHARED_COVERS,1,1,HH_MUTED)+4;
     if(ph->st.valid&&signin){
      y=set_signin_line(p,y,ph,ha);
      if(phone_shared(ph)&&!both)y=set_para(p,y,ha?WAVESHARE_AI_HOME_ASSISTANT_SHARED:WAVESHARE_AI_HERMES_SHARED,1,2,HH_MUTED)+4;
     }
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
     if(both&&s->phone_ha.st.valid&&!s->phone_ha.absent)y=set_signin_line(p,y,&s->phone_ha,true);
#endif
     if(y<SET_STATUS_Y)y=SET_STATUS_Y;
     set_status(p,y,"host",!enrolled?"not connected":(offline?"bridge offline":(s->pair.bridge[0]?s->pair.bridge:"connected")),offline?HH_ERROR:HH_MUTED);
    }
    break;}
   case SS_SIGNOUT:{
    /* A shared sign-in (flag 8) signs out both providers at once: say so. */
    int y=set_para(p,80,"Sign out?",2,1,HH_TEXT)+12;
    set_para(p,y,phone_shared(ph)||home_signin_shared(s)?WAVESHARE_AI_SHARED_SIGNOUT_NOTE:(ha?WAVESHARE_AI_HOME_ASSISTANT_SIGNOUT_NOTE:WAVESHARE_AI_HERMES_SIGNOUT_NOTE),1,5,HH_MUTED);
    break;}
  }
 }
 for(int k=0;k<b.count;k++){
  set_target(p,&b.t[k],hint[k]);
 }
}
static inline bool pair_visual_equal(const pair_view*a,const pair_view*b){
 return a->state==b->state&&a->step==b->step&&a->confirm_forget==b->confirm_forget&&a->code_shown==b->code_shown&&a->code==b->code&&a->live_http==b->live_http&&
  a->list.count==b->list.count&&!memcmp(a->list.items,b->list.items,sizeof a->list.items)&&!memcmp(a->bridge,b->bridge,sizeof a->bridge)&&!memcmp(a->base,b->base,sizeof a->base)&&
  !memcmp(a->note,b->note,sizeof a->note)&&!memcmp(a->fp,b->fp,sizeof a->fp);
}
static inline bool phone_view_visual_equal(const phone_view*x,const phone_view*y,int64_t ta,int64_t tb){
 if(x->showing!=y->showing||x->confirm_signout!=y->confirm_signout||x->qr_ok!=y->qr_ok||x->absent!=y->absent||strcmp(x->note,y->note))return false;
 if(x->qr_ok&&memcmp(x->qr,y->qr,sizeof x->qr))return false;
 if(x->st.valid!=y->st.valid||x->st.state!=y->st.state||x->st.flags!=y->st.flags||strcmp(x->st.name,y->st.name)||
    strcmp(x->st.user_code,y->st.user_code)||strcmp(x->st.uri,y->st.uri))return false;
 if(x->showing&&(phone_display(x,ta)!=phone_display(y,tb)||phone_left(x,ta)!=phone_left(y,tb)))return false;
 return true;
}
/* Every provider's sign-in view (the Settings screen and the Home tiles read them). */
static inline bool phone_visual_equal(const home_ui*a,const home_ui*b){
 if(!phone_view_visual_equal(&a->phone,&b->phone,a->live_now_us,b->live_now_us))return false;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 if(!phone_view_visual_equal(&a->phone_ha,&b->phone_ha,a->live_now_us,b->live_now_us))return false;
#endif
 return true;
}
/* A still screen needs no rerasterization or QSPI transfer. The display owner
 * calls this only against its last successfully committed, unchanged shadow. */
/* ---- Sensor page: four plain-language reading tiles, the sensor Pi dashboard's design ----
 * 2x2 rounded tiles: Air (PM1 + PM2.5 + PM10, worst wins), Temperature, Humidity, Pressure. Each says
 * what the reading means in a word (Comfy, Dry, Clean...), with the number small underneath and a band
 * gauge (the GOOD/MEDIUM/BAD ranges) with a marker at the value. The tile is tinted by the status the
 * bridge sent: GOOD soft blue, MEDIUM soft yellow, BAD soft red. No fresh verdict (stale, unknown,
 * signed out) = a neutral card with "--" and no number. Words, gauges: sensors_view.h. */
typedef theme_rgb sn_rgb;
/* Theme roles (theme.h); the Light values in the comments. Dark: black page, dark tiles, light ink. */
#define SN_PAGE_RGB theme_rgb_now(TC_SN_PAGE)    /* warm cream page (250,244,232) */
#define SN_INK_RGB theme_rgb_now(TC_SN_INK)      /* deep blue-grey ink (47,52,74) */
#define SN_MUTED_RGB theme_rgb_now(TC_SN_MUTED)  /* soft lavender-grey (132,126,146) */
#define SN_CARD_RGB theme_rgb_now(TC_SN_CARD)    /* neutral tile (no verdict) (241,233,219) */
#define SN_RULE_RGB theme_rgb_now(TC_SN_RULE)    /* neutral gauge track (222,212,196) */
#define SN_CORE_RGB theme_rgb_now(TC_SN_CORE)    /* the marker's light core (255,252,246) */
#define SN_TINT_RGB theme_rgb_now(TC_SN_TINT)    /* the white the tile tints are mixed from (255,254,250) */
static inline uint16_t sn_565(sn_rgb c){return sp_pack_lcd(c.r,c.g,c.b);}
/* a + (b - a) * pct / 100 per channel, rounded half to even (Python's round(), as the Pi does). */
static inline sn_rgb sn_mix(sn_rgb a,sn_rgb b,int pct){
 const uint8_t*pa=&a.r,*pb=&b.r;uint8_t o[3];
 for(int i=0;i<3;i++){
  int v=pa[i]*100+(pb[i]-pa[i])*pct,q=v/100,rem=v%100;
  if(rem>50||(rem==50&&(q&1)))q++;
  o[i]=(uint8_t)q;
 }
 return (sn_rgb){o[0],o[1],o[2]};
}
static inline sn_rgb sn_status_rgb(sensors_quality q){
 return q==SQ_GOOD?(sn_rgb){110,168,232}:(q==SQ_MEDIUM?(sn_rgb){242,200,76}:(sn_rgb){236,112,112});
}
#define SN_PAGE sn_565(SN_PAGE_RGB)
#define SN_INK sn_565(SN_INK_RGB)
#define SN_MUTED sn_565(SN_MUTED_RGB)
/* Tile fill: half the status colour into near-white (Dark: 34 % into near-black); neutral: the card. */
static inline sn_rgb sn_fill_rgb(sensors_quality q){return q>SQ_BAD?SN_CARD_RGB:sn_mix(SN_TINT_RGB,sn_status_rgb(q),theme_sn_fill(theme_mode_now()));}
/* Label + number: the status colour 70% toward the ink (Dark: 45 % toward the light ink); neutral:
 * muted 40% toward the ink. */
static inline sn_rgb sn_label_rgb(sensors_quality q){return q>SQ_BAD?sn_mix(SN_MUTED_RGB,SN_INK_RGB,40):sn_mix(sn_status_rgb(q),SN_INK_RGB,theme_sn_label(theme_mode_now()));}
/* One gauge band on a tile of status `tile`: the band's colour 8% toward the ink, then 15% toward the fill. */
static inline sn_rgb sn_band_rgb(sensors_quality band,sensors_quality tile){
 return sn_mix(sn_mix(sn_status_rgb(band),SN_INK_RGB,8),sn_fill_rgb(tile),15);
}
/* Layout (368x448 portrait): just the four tiles, no title (user rule 2026-10-06); a short note above
 * them only when there are no numbers (loading, signed out, offline). Everything stays >= 16 px from
 * the edges and the rounded corners and out of the bottom 28 px (the swipe-Home band): see
 * set_safe_px. */
#define SN_NOTE_Y 22
#define SN_TILE_X 20
#define SN_TILE_Y 50
#define SN_TILE_W 158
#define SN_TILE_H 172
#define SN_TILE_GAP 12
#define SN_TILE_R 14
#define SN_PAD 14
/* inside a tile: label at the top, the number on one row for every tile (so neighbours line up), the
 * word centred between them (one line, or two at SN_WORD_PITCH), the gauge at the bottom */
#define SN_LABEL_Y 16
#define SN_WORD_Y 58
#define SN_WORD2_Y 42
#define SN_WORD_PITCH 31
#define SN_DETAIL_Y 118
#define SN_BAR_H 10
#define SN_BAR_BOTTOM 18 /* bar bottom above the tile bottom */
#define SN_DOT_R 8.f
#define SN_CORE_R 3.9f
#define SN_SPAN (SN_TILE_W-2*SN_PAD)
static inline void sensors_tile_box(int i,int*x,int*y){*x=SN_TILE_X+(i&1)*(SN_TILE_W+SN_TILE_GAP);*y=SN_TILE_Y+(i>>1)*(SN_TILE_H+SN_TILE_GAP);}
/* The word, never truncated: one line at scale 2 when it fits the tile, else split at a space into two
 * scale-2 lines ("Very" / "high"), else scale 1. */
typedef struct {int lines,scale;char line[2][24];} sensors_word_fit;
static inline sensors_word_fit sensors_fit_word(const char*w,int width){
 sensors_word_fit f;memset(&f,0,sizeof f);
 int n=(int)strlen(w);if(n>23)n=23;  /* the words are <= 9 chars (sensors_view.h) */
 f.lines=1;f.scale=2;memcpy(f.line[0],w,(size_t)n);
 if(n*18+1<=width)return f;  /* +1: the bold pass */
 for(int k=n-1;k>0;k--){
  if(w[k]!=' '||k*18+1>width||(n-k-1)*18+1>width)continue;
  f.lines=2;memset(f.line,0,sizeof f.line);memcpy(f.line[0],w,(size_t)k);memcpy(f.line[1],w+k+1,(size_t)(n-k-1));
  return f;
 }
 f.scale=1;return f;
}
/* The number line: 9x17 font plus the two drawn glyphs (sensors_view.h SENSORS_DOT / SENSORS_DEGREE). */
static inline void sensors_detail_draw(uint16_t*p,int x,int y,const char*t,uint16_t c){
 char one[2]={0,0};
 for(;*t;t++,x+=9){
  if(*t==SENSORS_DOT){home_rect(p,x+3,y+8,3,3,1,c);continue;}            /* centred on the x-height */
  if(*t==SENSORS_DEGREE){                                                /* 4x4 ring at the cap top */
   home_rect(p,x+3,y+3,2,1,0,c);home_rect(p,x+3,y+6,2,1,0,c);home_rect(p,x+2,y+4,1,2,0,c);home_rect(p,x+5,y+4,1,2,0,c);continue;
  }
  one[0]=*t;home_text(p,x,y,one,1,c);
 }
}
/* Rounded (pill) band bar: the GOOD/MEDIUM/BAD ranges in their colours, anti-aliased ends. */
static inline void sensors_gauge_draw(uint16_t*p,int x,int y,const sensors_tile*t,uint16_t fill){
 const sensors_gauge_spec*g=sensors_gauge(t->row);
 uint16_t col[SN_SPAN],rule=sn_565(SN_RULE_RGB);
 for(int i=0;i<SN_SPAN;i++)col[i]=rule;
 bool health=t->q<=SQ_BAD;
 int start=0;
 for(int k=0;k<g->n;k++){
  int end=sensors_gauge_px(t->row,g->edge[k],SN_SPAN);
  uint16_t c=sn_565(health?sn_band_rgb((sensors_quality)g->band[k],t->q):SN_RULE_RGB);
  for(int i=start;i<end&&i<SN_SPAN;i++)col[i]=c;
  start=end;
 }
 const float r=SN_BAR_H*.5f,cy=r;
 for(int yy=0;yy<SN_BAR_H;yy++){
  uint16_t*row=p+(y+yy)*368+x;float dy=fabsf(yy+.5f-cy);
  for(int i=0;i<SN_SPAN;i++){
   float px=i+.5f,dx=px<r?r-px:(px>SN_SPAN-r?px-(SN_SPAN-r):0.f),cov=.5f-(sqrtf(dx*dx+dy*dy)-r);
   if(cov<=0.f)continue;
   row[i]=cov>=1.f?col[i]:sp_blend565(fill,col[i],(unsigned)(cov*255.f+.5f));
  }
 }
 if(!health)return;
 int mx=sensors_gauge_px(t->row,t->x10,SN_SPAN);
 if(mx<3)mx=3;
 if(mx>SN_SPAN-3)mx=SN_SPAN-3;
 ss_disc(p,(float)(x+mx)+.5f,(float)y+cy,SN_DOT_R,SN_INK,255,1.f);
 ss_disc(p,(float)(x+mx)+.5f,(float)y+cy,SN_CORE_R,sn_565(SN_CORE_RGB),255,1.f);
}
static inline void sensors_tile_draw(uint16_t*p,int x,int y,const sensors_tile*t){
 uint16_t fill=sn_565(sn_fill_rgb(t->q)),label=sn_565(sn_label_rgb(t->q));
 ss_rrect(p,(float)x,(float)y,(float)(x+SN_TILE_W),(float)(y+SN_TILE_H),SN_TILE_R,fill,255,1.f);
 home_text(p,x+SN_PAD,y+SN_LABEL_Y,t->label,1,label);
 sensors_word_fit w=sensors_fit_word(t->word,SN_SPAN);
 int wy=y+(w.lines>1?SN_WORD2_Y:SN_WORD_Y);
 for(int k=0;k<w.lines;k++){  /* drawn twice, 1 px apart: a heavier word, like the Pi's rounded bold face */
  home_text(p,x+SN_PAD,wy+k*SN_WORD_PITCH,w.line[k],w.scale,SN_INK);home_text(p,x+SN_PAD+1,wy+k*SN_WORD_PITCH,w.line[k],w.scale,SN_INK);
 }
 if(t->detail[0])sensors_detail_draw(p,x+SN_PAD,y+SN_DETAIL_Y,t->detail,label);
 sensors_gauge_draw(p,x+SN_PAD,y+SN_TILE_H-SN_BAR_BOTTOM-SN_BAR_H,t,fill);
}
#define HOME_TILE_OFF_PAPER theme_c(TC_TILE_OFF_PAPER)  /* (214,214,210) */
#define HOME_TILE_OFF_INK theme_c(TC_TILE_OFF_INK)      /* (112,112,108) */
#define HOME_TILE_OFF_PILL theme_c(TC_TILE_OFF_PILL)    /* (236,236,232) */
static inline void sensors_render(const home_ui*s,uint16_t*p){
 uint16_t paper=SN_PAGE;
 for(int i=0;i<SPARKLES_PIXELS;i++)p[i]=paper;
 /* Not signed in to the Home Assistant provider (or refused there): no numbers, like the bridge's 409.
  * The bridge has no Home Assistant provider at all (404): "Set up on the host", no numbers. */
 sensors_view gated=s->sensors;
 if(home_ha_absent(s)){memset(&gated.data,0,sizeof gated.data);gated.data.valid=true;gated.data.state=SS_SETUP;gated.http=0;}
 else if(home_ha_off(s))gated.http=409;
 int64_t now=s->input.stamp_us;const sensors_view*v=&gated;
 const char*note=sensors_note(v,now);
 if(note[0])home_center(p,0,SN_NOTE_Y,368,note,1,sn_565(sn_mix(SN_MUTED_RGB,SN_INK_RGB,40)));
 for(int i=0;i<SENSORS_TILES;i++){
  int x,y;sensors_tile_box(i,&x,&y);
  sensors_tile t=sensors_tile_at(v,i,now);
  sensors_tile_draw(p,x,y,&t);
 }
}
/* A provider tab's "Checking sign-in..." spinner turns (settings_render draws it under the same test). */
static inline bool home_settings_spins(const home_ui*s){
 const phone_view*v=home_tab_phone_c(s,s->settings_tab);
 return v&&!v->absent&&home_provider_signin(s->settings_tab)&&home_settings_screen(s)==SS_STATUS&&
  !home_settings_buttons(s,s->live_now_us).count&&!(s->pair.live_http&&s->pair.live_http!=200);
}
/* What a Home tile shows of a sign-in view. */
static inline bool phone_tile_equal(const phone_view*x,const phone_view*y){
 return x->st.valid==y->st.valid&&x->st.state==y->st.state&&x->st.flags==y->st.flags&&x->absent==y->absent;
}
static inline unsigned home_cache_mix(unsigned h,unsigned v){return (h^v)*16777619u;}
/* The held Home cache is semantic, not merely geometric: connectivity and provider state can change
 * the selected tile while the finger is still. Include both the rendered status and its owning inputs. */
static inline unsigned home_cache_key(const home_ui*s){
 unsigned h=2166136261u;
 h=home_cache_mix(h,(unsigned)s->tile);
 h=home_cache_mix(h,(unsigned)s->theme_mode);
 h=home_cache_mix(h,(unsigned)s->accent);
 h=home_cache_mix(h,(unsigned)s->connected);
 h=home_cache_mix(h,(unsigned)s->saved);
 h=home_cache_mix(h,(unsigned)s->pair.state);
 h=home_cache_mix(h,(unsigned)s->pair.live_ok);
 h=home_cache_mix(h,(unsigned)s->pair.live_http);
 h=home_cache_mix(h,(unsigned)s->phone.st.valid);
 h=home_cache_mix(h,(unsigned)s->phone.st.state);
 h=home_cache_mix(h,(unsigned)s->phone.st.flags);
 h=home_cache_mix(h,(unsigned)s->phone.absent);
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 h=home_cache_mix(h,(unsigned)s->phone_ha.st.valid);
 h=home_cache_mix(h,(unsigned)s->phone_ha.st.state);
 h=home_cache_mix(h,(unsigned)s->phone_ha.st.flags);
 h=home_cache_mix(h,(unsigned)s->phone_ha.absent);
#endif
 tile_status st=home_tile_status(s,s->tile);
 h=home_cache_mix(h,(unsigned)st.state);
 h=home_cache_mix(h,(unsigned)st.blocks);
 for(const unsigned char*p=(const unsigned char*)st.label;*p;p++)h=home_cache_mix(h,*p);
 return h;
}
/* The page layer looks the same: same tile-opening offset, same blob (or none). */
static inline bool home_layer_equal(const home_ui*a,const home_ui*b){
 bool ba=home_blob_shown(a),bb=home_blob_shown(b);
 if(ba!=bb||home_page_offset(a)!=home_page_offset(b))return false;
 return !ba||!memcmp(&a->blob,&b->blob,sizeof a->blob);
}
static inline bool home_visual_equal(const home_ui*a,const home_ui*b){
 if(!power_view_equal(&a->power,&b->power))return false;  /* power-off countdown (power_ui.h) */
 /* Page slides, the Home pull's blob and the carousel settle (home_ui.h): repaint while they move,
  * never at rest (a finger holding the blob still is no repaint). */
 if(home_motion_moving(a)||home_motion_moving(b)||!home_layer_equal(a,b))return false;
 if(a->theme_mode!=b->theme_mode||a->accent!=b->accent)return false;  /* Settings > Display: every page repaints */
 if(a->page!=b->page)return false;
 /* A held Home exit is a frozen solid shape over its once-per-motion Home cache. Page animations and
  * data behind it do not advance until travel, theme or the selected Home tile changes. */
 if(a->slide_kind==HOME_MOTION_DRAG&&b->slide_kind==HOME_MOTION_DRAG&&home_blob_shown(a)&&home_blob_shown(b))
  return a->motion_id==b->motion_id&&a->slide_page==b->slide_page&&home_cache_key(a)==home_cache_key(b);
 /* A Settings spinner turns: repaint while loading (Wi-Fi joining, sign-in status on its way). */
 if(home_settings_page(a)&&(home_settings_loading(a)||home_settings_loading(b)||home_settings_spins(a)))return false;
 if(a->page==SPARKLES)return false; /* blue water/fader stays gently alive */
 if(a->page==HOME){
  unsigned ac=home_live_points(a),bc=home_live_points(b);
  if(ac!=bc)return false;
  if(ac&&(home_live_tile_visible(a)||home_live_tile_visible(b)))return false; /* glows move in the Sparkles tile */
  tile_status ta=home_tile_status(a,a->tile),tb=home_tile_status(b,b->tile);
  if(ta.state==TILE_LOADING||tb.state==TILE_LOADING)return false; /* spinner turns */
  if(ta.state!=tb.state||ta.blocks!=tb.blocks||strcmp(ta.label,tb.label))return false;
  if(a->connected!=b->connected||a->saved!=b->saved||a->pair.state!=b->pair.state)return false;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  if(!phone_tile_equal(&a->phone_ha,&b->phone_ha))return false;
#endif
  return a->tile==b->tile&&a->drag_offset==b->drag_offset&&phone_tile_equal(&a->phone,&b->phone);
 }
 if(a->page==HELPER)return false; /* glowing orbs stay gently alive */
 /* still page: repaint on new data, a reading going stale, or a sign-in change */
 if(a->page==SENSORS)return !memcmp(&a->sensors,&b->sensors,sizeof a->sensors)&&
  sensors_fresh(&a->sensors,a->input.stamp_us)==sensors_fresh(&b->sensors,b->input.stamp_us)&&home_ha_off(a)==home_ha_off(b)&&home_ha_absent(a)==home_ha_absent(b);
 if(!home_settings_page(a))return false;
 /* Settings: everything settings_render reads (never the password: it is not drawn). */
 return b->page==a->page&&home_settings_screen(a)==home_settings_screen(b)&&a->settings_tab==b->settings_tab&&a->sound_off==b->sound_off&&a->session_off==b->session_off&&a->busy==b->busy&&
  a->saved==b->saved&&a->connected==b->connected&&!strcmp(a->credentials.ssid,b->credentials.ssid)&&
  a->hotspot_saved==b->hotspot_saved&&a->on_hotspot==b->on_hotspot&&!strcmp(a->hotspot_ssid,b->hotspot_ssid)&&!memcmp(a->status,b->status,sizeof a->status)&&
  a->signin_shared==b->signin_shared&&  /* one account tab instead of two (tab dots, its lines) */
  pair_visual_equal(&a->pair,&b->pair)&&phone_visual_equal(a,b)&&(a->settings_tab!=SETTINGS_BATTERY||battery_visual_equal(&a->battery,&b->battery));
}
static inline int home_render_page(const home_ui*s,uint16_t*p,size_t pixels){
 if(!s||!p||pixels<SPARKLES_PIXELS)return 0;
 theme_use(s->theme_mode,s->accent);  /* every colour role below follows Settings > Display */
 #if WAVESHARE_AI_PLUGIN_SPARKLES
 if(s->page==SPARKLES){
  sparkles_state scene=s->input.scene;
  scene.theme=0; /* retain the requested blue fader */
  /* Water is independent; fresh working presence animates even while usage
   * is pending. Touch remains independent of presence and the session toggle. */
  /* Session sparkle ON: the ambient field follows the Hermes session (calm when none is working).
   * OFF (user rule 2026-09-29): the animation plays by itself at the calm usage default, not linked
   * to Hermes at all. */
  scene.density=s->session_off?0:home_live_density(s);
  scene.quiet=!s->session_off&&scene.density==0;
  if(!sparkles_render_direct(&scene,p,pixels))return 0;
  home_live_overlay(s,p);
  home_sparkle_label(s,p);
  return 1;
 }
 #endif
 #if WAVESHARE_AI_PLUGIN_AI
 if(s->page==HELPER){helper_render_page(s,p);return 1;}
 #endif
 #if WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
 if(s->page==SENSORS){sensors_render(s,p);return 1;}
 #endif
 if(home_settings_page(s)){settings_render(s,p);return 1;}
 uint16_t background=theme_c(TC_HOME_BG);
 for(int i=0;i<SPARKLES_PIXELS;i++)p[i]=background;
 if(s->page==HOME){
  /* One edge-to-edge service per 368px page. Only background bleeds into
   * rounded corners; all labels/art controls stay inside the AMOLED safe area. */
  for(int i=0;i<HOME_TILES;i++){
   int x=(i-s->tile)*368+s->drag_offset;
   if(x>=368||x+368<=0)continue;
   /* One plugin row per tile (tile_plugins.h). A Hermes-required tile (Ask, Sensor) is drawn grey
    * while closed and a tap opens the fix (home_ui.h home_tap); a Hermes-optional tile (Sparkles)
    * keeps its colours and only shows its link state in the pill. */
   const tile_plugin*tp=&home_tiles[i];
   tile_status st=home_tile_status(s,i);
   bool off=st.blocks&&st.state!=TILE_ON;
   /* Light: each usable tile a different light shade of the accent, in carousel order (theme.h
    * theme_tile_paper: 16 / 24 / 32 / 40 % toward the accent). Dark: every tile on black (the panel's
    * pixels stay off); its art and label carry it. */
   uint16_t paper=off?HOME_TILE_OFF_PAPER:(theme_dark()?theme_c(TC_HOME_BG):theme_565(theme_tile_paper(theme_accent_now(),i)));
   uint16_t ink=off?HOME_TILE_OFF_INK:HC_TEXT;
   home_rect(p,x,0,368,448,0,paper);
   if(tp->icon==TILE_ICON_SPARKLES){
    /* Sparkles: a real frame from this device's sparkle renderer (tools/build_sparkle_image.c),
     * alpha-blended through its rounded-square mask; same 192px box as the Material icons. */
    for(int yy=0;yy<SPARKLE_IMAGE_SIZE;yy++)for(int xx=0;xx<SPARKLE_IMAGE_SIZE;xx++){
     int px=home_tile_art_x(x)+88+xx;if(px<0||px>=368)continue;
     unsigned a=sparkle_image_alpha[yy*SPARKLE_IMAGE_SIZE+xx];if(!a)continue;
     uint16_t fg=sparkle_image_rgb[yy*SPARKLE_IMAGE_SIZE+xx],bg=p[(108+yy)*368+px];
     unsigned r=(((fg>>11)&31)*a+((bg>>11)&31)*(255-a)+127)/255;
     unsigned g=(((fg>>5)&63)*a+((bg>>5)&63)*(255-a)+127)/255;
     unsigned b=((fg&31)*a+(bg&31)*(255-a)+127)/255;
     p[(108+yy)*368+px]=(uint16_t)((r<<11)|(g<<5)|b);
    }
   }
   /* Ask: the bots themselves, in the Ask page's style (no mic icon any more). */
   if(tp->icon==TILE_ICON_BOTS)home_bots_tile(p,home_tile_art_x(x),st.state==TILE_LOADING?HOME_BOT_TILE_LOADING:(off?HOME_BOT_TILE_OFF:HOME_BOT_TILE_ON),s->input.scene.time);
   const uint8_t*icon=tp->icon==TILE_ICON_SENSORS?material_sensors:material_settings;
   /* Actual SVG ink bounds are centered at x+183.5 by the offline converter. */
   if(tp->icon!=TILE_ICON_SPARKLES&&tp->icon!=TILE_ICON_BOTS)for(int yy=0;yy<MATERIAL_SIZE;yy++)for(int xx=0;xx<MATERIAL_SIZE;xx++){
    int px=home_tile_art_x(x)+88+xx;if(px<0||px>=368)continue;
    unsigned a=icon[yy*MATERIAL_SIZE+xx];if(!a)continue;
    uint16_t bg=p[(108+yy)*368+px];
    unsigned r=(((ink>>11)&31)*a+((bg>>11)&31)*(255-a)+127)/255;
    unsigned g=(((ink>>5)&63)*a+((bg>>5)&63)*(255-a)+127)/255;
    unsigned b=((ink&31)*a+(bg&31)*(255-a)+127)/255;
    p[(108+yy)*368+px]=(uint16_t)((r<<11)|(g<<5)|b);
   }
   home_text(p,x+32,332,tp->name,3,ink);
   if(st.state!=TILE_ON&&st.label[0]){const char*why=st.label;bool loading=st.state==TILE_LOADING;
    /* "No Wi-Fi" / "Set up Hermes" / "Sign in" / "Hermes unavailable"; while loading a small spinner
     * leads "Joining Wi-Fi" / "Checking sign-in" / "Connecting to Hermes". */
    int w=(int)strlen(why)*9+36+(loading?24:0);
    home_rect(p,x+32,388,w,30,15,HOME_TILE_OFF_PILL);
    if(loading)home_spinner(p,x+56,403,9,s->input.scene.time,HH_ORANGE,theme_c(TC_TILE_SPIN));
    home_text(p,x+50+(loading?24:0),395,why,1,HOME_TILE_OFF_INK);}
  }
 }

 home_live_overlay(s,p);
 return 1;
}
/* Every page, then the power-off countdown on top of it (power_ui.h; nothing while idle). */
static inline void power_ui_draw(const power_view*v,uint16_t*p);
static inline int home_render(const home_ui*s,uint16_t*p,size_t pixels){
 if(!home_render_page(s,p,pixels))return 0;
 power_ui_draw(&s->power,p);
 return 1;
}
/* ---- Page slides and the circular Home reveal ----
 * Tile OPEN uses the page snapshot for its translated slide. A Home pull uses that same PSRAM frame
 * as the untransformed outgoing source and the second frame as real Home: the expanding circle selects
 * Home inside and the outgoing page outside, with semantic TA_FILL only on its narrow AA edge. */
typedef struct {uint16_t*px;unsigned motion;int page;bool valid;uint16_t*home;unsigned home_motion,home_key;bool home_valid;} home_snapshot;
/* One owner-scoped frame is enough to compose the reveal when both optional snapshot buffers are
 * absent. Firmware installs PSRAM storage from direct_main; host probes use static test storage. */
static uint16_t*home_reveal_scratch;
static inline void home_reveal_set_scratch(uint16_t*p){home_reveal_scratch=p;}
#ifndef ESP_PLATFORM
static uint16_t home_reveal_host_scratch[SPARKLES_PIXELS];
static inline uint16_t*home_reveal_get_scratch(void){return home_reveal_scratch?home_reveal_scratch:home_reveal_host_scratch;}
#else
static inline uint16_t*home_reveal_get_scratch(void){return home_reveal_scratch;}
#endif
/* One bounded row pass: solve the shape edge once per row boundary and split rapidly changing top/bottom
 * rows into sub-rows. Inner spans are integer solid fills; only edge pixels blend coverage. */
#define HOME_BLOB_SUB 8
static inline float home_blob_half(const home_blob*b,float y){
 float v=(y-b->cy)/(b->h*.5f);
 if(v<0)v=-v;
 if(v>=1.f)return 0.f;
 if(b->n==2.f)return b->w*.5f*sqrtf(1.f-v*v);
 return b->w*.5f*powf(1.f-powf(v,b->n),1.f/b->n);
}
static inline bool home_blob_sane(const home_blob*b){
 return b->w>=1.f&&b->w<=4096.f&&b->h>=1.f&&b->h<=4096.f&&b->n>=1.f&&b->n<=64.f&&
        b->alpha>0.f&&b->alpha<=1.f&&b->cx>-4096.f&&b->cx<4096.f&&b->cy>-4096.f&&b->cy<4096.f;
}
static inline uint32_t home_spread565(uint16_t c){return ((uint32_t)c|((uint32_t)c<<16))&0x07E0F81Fu;}
static inline uint16_t home_pack565(uint32_t s){return (uint16_t)(s|(s>>16));}
/* bg + (fg - bg) x a/32 per channel (a 0..32). */
static inline uint16_t home_mix565(uint16_t bg,uint16_t fg,unsigned a){
 uint32_t b=home_spread565(bg),f=home_spread565(fg);
 return home_pack565((b+(((f-b)*a)>>5))&0x07E0F81Fu);
}
/* Coverage (0..1) of pixel column X: the mean over the row's sub-rows of [X, X+1) inside cx +- half. */
static inline float home_blob_cover(float cx,const float*half,int k,int X){
 float c=0,x0=(float)X-cx,x1=x0+1.f;
 for(int j=0;j<k;j++){float h=half[j],a=x0>-h?x0:-h,e=x1<h?x1:h;if(e>a)c+=e-a;}
 return c/(float)k;
}
static inline int home_clampi(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
/* Select Home through the circular aperture. The edge coverage blends the two real frames; a weak
 * semantic-accent tint is applied only to partial-coverage pixels, never to the aperture interior. */
static inline void home_reveal_draw(uint16_t*p,const uint16_t*outgoing,const uint16_t*home,
                                    const home_blob*b,uint16_t accent){
 if(!p||!outgoing)return;
 if(!home||!home_blob_sane(b)){
  if(p!=outgoing)memcpy(p,outgoing,SPARKLES_PIXELS*sizeof *p);
  return;
 }
 bool home_alias=p==home;
 if(!home_alias&&p!=outgoing)memcpy(p,outgoing,SPARKLES_PIXELS*sizeof *p);
 float hh=b->h*.5f;
 int y0=home_clampi((int)floorf(b->cy-hh),0,448),y1=home_clampi((int)ceilf(b->cy+hh),y0,448);
 if(home_alias){
  if(y0>0)memcpy(p,outgoing,(size_t)y0*368*sizeof *p);
  if(y1<448)memcpy(p+(size_t)y1*368,outgoing+(size_t)y1*368,(size_t)(448-y1)*368*sizeof *p);
 }
 float edge_top=home_blob_half(b,(float)y0);
 for(int Y=y0;Y<y1;Y++){
  uint16_t*o=p+(size_t)Y*368;
  const uint16_t*hr=home+(size_t)Y*368,*orow=outgoing+(size_t)Y*368;
  float edge_bot=home_blob_half(b,(float)(Y+1)),half[HOME_BLOB_SUB],lo,hi;
  int k=1;
  float d=edge_top>edge_bot?edge_top-edge_bot:edge_bot-edge_top;
  if(d<=1.5f&&edge_top>0&&edge_bot>0){
   half[0]=.5f*(edge_top+edge_bot);lo=edge_top<edge_bot?edge_top:edge_bot;hi=edge_top>edge_bot?edge_top:edge_bot;
  }else{
   k=HOME_BLOB_SUB;lo=1e9f;hi=0;
   for(int j=0;j<k;j++){
    half[j]=home_blob_half(b,(float)Y+((float)j+.5f)/(float)k);
    if(half[j]<lo)lo=half[j];
    if(half[j]>hi)hi=half[j];
   }
  }
  edge_top=edge_bot;
  int ea=home_clampi((int)floorf(b->cx-hi),0,368),eb=home_clampi((int)ceilf(b->cx+hi),ea,368);
  int ia=home_clampi((int)ceilf(b->cx-lo),ea,eb),ib=home_clampi((int)floorf(b->cx+lo),ea,eb);
  if(ib<=ia)ia=ib=ea;
  for(int X=ea;X<eb;X++){
   if(X==ia&&ib>ia){X=ib-1;continue;}
   unsigned a=(unsigned)(home_blob_cover(b->cx,half,k,X)*32.f+.5f);
   if(a>32)a=32;
   uint16_t base=a>=32?hr[X]:(!a?orow[X]:home_mix565(orow[X],hr[X],a));
   unsigned peak=a<=16?a:32-a;
   unsigned edge=(unsigned)((float)peak*.5f*b->alpha+.5f);
   o[X]=edge?home_mix565(base,accent,edge):base;
  }
  if(!home_alias&&ib>ia)memcpy(o+ia,hr+ia,(size_t)(ib-ia)*sizeof *p);
  if(home_alias){
   if(ea>0)memcpy(o,orow,(size_t)ea*sizeof *p);
   if(eb<368)memcpy(o+eb,orow+eb,(size_t)(368-eb)*sizeof *p);
  }
 }
}
/* Kept as the small renderer-test entry point: p is the outgoing source on entry. */
static inline void home_accent_draw(uint16_t*p,const uint16_t*home,const home_blob*b,uint16_t accent){
 home_reveal_draw(p,p,home,b,accent);
}
/* Render a page at rest without copying the ~25 KB UI. Only these three presentation fields differ,
 * and they are restored on every return so the caller's state remains byte-identical. */
static inline int home_render_still_page(home_ui*s,home_page page,uint16_t*p,size_t pixels){
 home_page keep_page=s->page;unsigned char keep_kind=s->slide_kind;int keep_y=s->page_y;
 s->page=page;s->slide_kind=HOME_MOTION_NONE;s->page_y=0;
 int ok=home_render_page(s,p,pixels);
 s->page=keep_page;s->slide_kind=keep_kind;s->page_y=keep_y;
 return ok;
}
static inline bool home_outgoing_snapshot(home_ui*s,home_snapshot*snap,
                                          const uint16_t*last,int last_page){
 if(!snap||!snap->px)return false;
 if(snap->motion==s->motion_id&&snap->page==(int)s->slide_page&&snap->valid)return true;
 snap->motion=s->motion_id;snap->page=(int)s->slide_page;snap->valid=false;
 if(last&&last_page==(int)s->slide_page){
  memcpy(snap->px,last,SPARKLES_PIXELS*sizeof *last);snap->valid=true;
 }else{
  snap->valid=home_render_still_page(s,s->slide_page,snap->px,SPARKLES_PIXELS)!=0;
 }
 return snap->valid;
}
static inline int home_compose(home_ui*s,uint16_t*p,size_t pixels,home_snapshot*snap,const uint16_t*last,int last_page){
 int y=home_page_offset(s);bool blob=home_blob_shown(s);
 if(!p||pixels<SPARKLES_PIXELS)return 0;
 if(!y&&!blob)return home_render(s,p,pixels);
 if(blob){
  const uint16_t*outgoing=NULL,*home=NULL;
  if(home_outgoing_snapshot(s,snap,last,last_page))outgoing=snap->px;
  else if(last&&last_page==(int)s->slide_page)outgoing=last;
  if(snap&&snap->home){
   if(!snap->home_valid||snap->home_motion!=s->motion_id||snap->home_key!=home_cache_key(s)){
    snap->home_motion=s->motion_id;snap->home_key=home_cache_key(s);
    snap->home_valid=home_render_still_page(s,HOME,snap->home,SPARKLES_PIXELS)!=0;
   }
   if(snap->home_valid)home=snap->home;
  }
  if(!outgoing){
   uint16_t*target=home?p:home_reveal_get_scratch();
   if(!target||!home_render_still_page(s,s->slide_page,target,pixels))return 0;
   outgoing=target;
  }
  if(!home){
   uint16_t*target=outgoing!=p?p:home_reveal_get_scratch();
   if(!target||target==outgoing||!home_render_still_page(s,HOME,target,pixels))return 0;
   home=target;
  }
  theme_use(s->theme_mode,s->accent);
  home_reveal_draw(p,outgoing,home,&s->blob,theme_c(TA_FILL));
  power_ui_draw(&s->power,p);
  return 1;
 }
 if(!snap||!snap->px)return home_render(s,p,pixels);
 if(y>448)y=448;
 if(y<-448)y=-448;
 if(snap->motion!=s->motion_id||snap->page!=(int)s->slide_page){
  snap->motion=s->motion_id;snap->page=(int)s->slide_page;snap->valid=false;
  bool copy=last&&last_page==(int)s->slide_page;
  if(s->page==s->slide_page){
   /* A pull can interrupt a tile opening and commit between owner frames: page is already Home,
    * and last_page == -1 correctly rejects the translated / transformed last frame. Reconstruct
    * the full outgoing page from this owner's copied state, never from presenter-owned buffers. */
   snap->valid=home_render_still_page(s,s->slide_page,snap->px,SPARKLES_PIXELS)!=0;
  }
  else if(copy){memcpy(snap->px,last,SPARKLES_PIXELS*sizeof *p);snap->valid=true;}
 }
 int ok=home_render_still_page(s,HOME,p,pixels);
 if(!ok)return 0;
 if(snap->valid&&y>0&&y<448)memcpy(p+(size_t)y*368,snap->px,(size_t)(448-y)*368*sizeof *p);
 if(snap->valid&&y<0&&y>-448)memcpy(p,snap->px+(size_t)(-y)*368,(size_t)(448+y)*368*sizeof *p);
 power_ui_draw(&s->power,p);
 return 1;
}
