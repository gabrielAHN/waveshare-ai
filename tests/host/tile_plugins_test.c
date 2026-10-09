/* Home tiles are plugins (the named device's firmware/main/tile_plugins.h): one table row per tile says what it needs.
 * User rules 2026-10-02: "refactor tile for plugin for each tile someone can add or turn off based on
 * Hermes connection or not but all need WiFi setup via flash" and "sparkle needs a loading on WiFi and
 * Hermes connection like the other tiles". RED first.
 *   TILE_STANDALONE       never gated (Settings)
 *   TILE_HERMES_OPTIONAL  works on its own; shows its Wi-Fi/Hermes link state but stays usable (Sparkles:
 *                         the water runs offline, session glows need the host)
 *   TILE_HERMES_REQUIRED  grey and closed until Wi-Fi + host + phone sign-in say yes (Ask, Sensor) */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
static int tile_of(home_page page){for(int i=0;i<HOME_TILES;i++)if(home_tiles[i].page==page)return i;return -1;}
/* Light + Orange Home tiles (SPEC3 Contract S): tile i's paper is white mixed toward the accent
 * (238,112,28) at 16 / 24 / 32 / 40 % in carousel order (independent oracle of theme.h theme_tile_paper). */
static uint16_t tile_paper(int tile){static const int pct[4]={16,24,32,40};int p=pct[tile%4];
 return sp_pack_lcd(255-((255-238)*p+50)/100,255-((255-112)*p+50)/100,255-((255-28)*p+50)/100);}
static home_ui board(void){
 home_ui s;memset(&s,0,sizeof s);s.page=HOME;s.saved=s.connected=true;
 s.pair.state=PAIR_ENROLLED_UNPAIRED;s.input.stamp_us=s.live_now_us=100LL*1000*1000;
 return s;
}
static void signed_in(home_ui*s){s->phone.st.valid=true;s->phone.st.state=PH_AUTHORIZED;s->phone.st.flags=PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 s->phone_ha.st=s->phone.st;  /* the Sensor tile's own (Home Assistant) sign-in */
#endif
}
/* One live poll result every 1.5 s of board time (the worker's cadence), starting at s->live_now_us. */
static int64_t clock_us=100LL*1000*1000;
static void live(home_ui*s,int http,int times){for(int k=0;k<times;k++){clock_us+=1500000;pair_live_result(&s->pair,http,clock_us);}}

/* 1. The table: every built plugin once, Settings last, each row complete. */
static void table(void){
 assert(HOME_TILES>=1&&home_tiles[HOME_TILES-1].page==SETTINGS&&home_tiles[HOME_TILES-1].need==TILE_STANDALONE);
 for(int i=0;i<HOME_TILES;i++){
  const tile_plugin*t=&home_tiles[i];
  assert(t->name&&t->name[0]&&strlen(t->name)<=10);          /* fits the tile in the big font */
  assert(home_page_enabled(t->page)&&home_tile_index(t->page)==i);
  assert(t->need!=TILE_HERMES_REQUIRED||t->gate!=TILE_GATE_NONE);   /* required = a sign-in gate */
  for(int j=0;j<i;j++)assert(home_tiles[j].page!=t->page&&home_tiles[j].icon!=t->icon);
 }
 int sp=tile_of(SPARKLES),ask=tile_of(HELPER),sen=tile_of(SENSORS);
 if(WAVESHARE_AI_PLUGIN_SPARKLES)assert(sp>=0&&home_tiles[sp].need==TILE_HERMES_OPTIONAL&&home_tiles[sp].gate==TILE_GATE_NONE);
 if(WAVESHARE_AI_PLUGIN_AI)assert(ask>=0&&home_tiles[ask].need==TILE_HERMES_REQUIRED&&home_tiles[ask].gate==TILE_GATE_SIGNIN);
 if(WAVESHARE_AI_PLUGIN_HOME_ASSISTANT)assert(sen>=0&&home_tiles[sen].need==TILE_HERMES_REQUIRED&&home_tiles[sen].gate==TILE_GATE_HOME_ASSISTANT);
 /* Providers: Sparkles and Ask are Hermes tiles, Sensor is the Home Assistant tile, Settings is core. */
 if(sp>=0)assert(home_tiles[sp].provider==TILE_PROVIDER_HERMES&&home_tile_tab(sp)==SETTINGS_HERMES);
 if(ask>=0)assert(home_tiles[ask].provider==TILE_PROVIDER_HERMES&&home_tile_tab(ask)==SETTINGS_HERMES);
 if(sen>=0)assert(home_tiles[sen].provider==TILE_PROVIDER_HOME_ASSISTANT&&home_tile_tab(sen)==SETTINGS_HOME_ASSISTANT);
 assert(home_tiles[HOME_TILES-1].provider==TILE_PROVIDER_NONE&&home_tile_tab(HOME_TILES-1)==-1);
 /* A Sensor-only build has no Hermes provider: its unpaired tile must still direct the user to the bridge. */
 if(sen>=0){home_ui u=board();u.pair.state=PAIR_NO_BRIDGE;tile_status h=home_tile_status(&u,sen);
  assert(h.state==TILE_OFF&&h.blocks&&!strcmp(h.label,"Set up bridge"));}
 /* Settings never shows a link state. */
 home_ui n=board();n.connected=n.saved=false;tile_status st=home_tile_status(&n,HOME_TILES-1);
 assert(st.state==TILE_ON&&!st.blocks&&!st.label[0]);
}
/* 2. The live feed result behind the Sparkles link state: "unavailable" is TIME since the first failed
 *    poll (an outage poll takes its 2 s timeout + the 1.5 s pause, so a poll count would be ~70 s). */
static void live_counter(void){
 const int64_t S=1000*1000,t=500*S;
 pair_view v;memset(&v,0,sizeof v);
 pair_live_result(&v,0,t);pair_live_result(&v,503,t+3*S);assert(v.live_fails==2&&v.live_http==503&&!v.live_down);
 pair_live_result(&v,200,t+6*S);assert(v.live_fails==0&&v.live_http==200&&v.live_ok&&!v.live_down);
 /* slow failures (3.5 s apart, like a real outage): down once 30 s have passed since the first one */
 int64_t now=t+10*S;pair_live_result(&v,0,now);
 while(now-(t+10*S)<LIVE_GIVE_UP_US-4*S){now+=3500000;pair_live_result(&v,0,now);assert(!v.live_down);}
 now+=5*S;pair_live_result(&v,0,now);assert(v.live_down);
 /* one answer clears it at once */
 pair_live_result(&v,200,now+S);assert(!v.live_down&&v.live_ok&&!v.live_fails);
 /* fast failures alone never count as "down" before the time has passed */
 pair_view f;memset(&f,0,sizeof f);for(int k=0;k<200;k++)pair_live_result(&f,0,t+k*1000);assert(!f.live_down&&f.live_fails>=200);
 for(int k=0;k<1000;k++){pair_live_result(&f,0,t+k*S);}
 assert(f.live_fails==255&&f.live_down);   /* saturates */
 pair_live_forget(&f);assert(!f.live_ok&&!f.live_down&&!f.live_fails);
 assert(LIVE_GIVE_UP_US>=25*S&&LIVE_GIVE_UP_US<=35*S);
}
/* 3. Sparkles: Wi-Fi and Hermes link states like the other tiles, but never closed. */
static void sparkles_states(void){
 int sp=tile_of(SPARKLES);
 /* Joining a saved network: loading. */
 home_ui j=board();j.connected=false;tile_status st=home_tile_status(&j,sp);
 assert(st.state==TILE_LOADING&&!strcmp(st.label,"Joining Wi-Fi")&&!st.blocks);
 home_ui jh=board();jh.connected=jh.saved=false;jh.hotspot_saved=true;assert(home_tile_status(&jh,sp).state==TILE_LOADING);
 /* Nothing saved: "No Wi-Fi", still opens (the water runs offline). */
 home_ui n=board();n.connected=n.saved=false;st=home_tile_status(&n,sp);
 assert(st.state==TILE_OFF&&!strcmp(st.label,"No Wi-Fi")&&!st.blocks);
 n.tile=sp;home_tap(&n,184,224);assert(n.page==SPARKLES);
 /* Wi-Fi up, no host linked: provider-neutral bridge setup. */
 home_ui u=board();u.pair.state=PAIR_NO_BRIDGE;st=home_tile_status(&u,sp);
 assert(st.state==TILE_OFF&&!strcmp(st.label,"Set up bridge")&&!st.blocks);
 /* Linked, first live answer on its way: loading "Connecting to Hermes". */
 home_ui c=board();st=home_tile_status(&c,sp);
 assert(st.state==TILE_LOADING&&!strcmp(st.label,"Connecting to Hermes"));
 live(&c,0,(int)(LIVE_GIVE_UP_US/1500000));assert(home_tile_status(&c,sp).state==TILE_LOADING);  /* still trying */
 live(&c,0,2);st=home_tile_status(&c,sp);
 assert(st.state==TILE_OFF&&!strcmp(st.label,"Hermes unavailable")&&!st.blocks);
 c.tile=sp;home_tap(&c,184,224);assert(c.page==SPARKLES);
 /* A live answer: on, no label. Sparkles never needs the phone sign-in (the feed is board-token only). */
 home_ui a=board();live(&a,200,1);st=home_tile_status(&a,sp);assert(st.state==TILE_ON&&!st.label[0]);
 a.phone.st.valid=true;a.phone.st.state=PH_NONE;a.phone.st.flags=PHONE_FLAG_REQUIRED;assert(home_tile_status(&a,sp).state==TILE_ON);
 /* "Session sparkle" off in Settings: the tile is the plain animation, no link state at all. */
 home_ui o=board();o.connected=o.saved=false;o.session_off=true;st=home_tile_status(&o,sp);
 assert(st.state==TILE_ON&&!st.label[0]);
}
/* 4. Ask/Sensor (required) use their own provider status, independently of the live feed. */
static void required_states(void){
 int ask=tile_of(HELPER);
 home_ui a=board();signed_in(&a);live(&a,0,40);
 assert(home_tile_status(&a,ask).state==TILE_ON);
 home_ui o=board();o.phone.st.valid=true;o.phone.st.state=PH_NONE;o.phone.st.flags=PHONE_FLAG_REQUIRED;
 tile_status st=home_tile_status(&o,ask);assert(st.state==TILE_OFF&&st.blocks&&!strcmp(st.label,"Sign in"));
 home_ui j=board();j.connected=false;st=home_tile_status(&j,ask);assert(st.state==TILE_LOADING&&st.blocks);
}
/* 5. Drawing: Sparkles keeps its own paper with a status pill + spinner while loading; no pill when on. */
static uint16_t A[SPARKLES_PIXELS],B[SPARKLES_PIXELS];
static int count(const uint16_t*p,int x0,int y0,int x1,int y1,uint16_t c){int n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)n+=p[y*368+x]==c;return n;}
static void render(void){
 int sp=tile_of(SPARKLES);
 home_ui l=board();l.tile=sp;home_render(&l,A,SPARKLES_PIXELS);
 assert(A[200*368+18]==tile_paper(sp));                       /* not greyed out: its own shade */
 assert(count(A,32,388,340,418,HOME_TILE_OFF_PILL)>300);          /* the status pill */
 assert(count(A,32,388,340,418,HH_ORANGE)>0);                     /* the spinner head */
 home_ui l2=l;l2.input.scene.time+=0.5f;assert(!home_visual_equal(&l,&l2));   /* spinner turns */
 home_ui on=board();live(&on,200,1);on.tile=sp;home_render(&on,B,SPARKLES_PIXELS);
 assert(count(B,32,388,340,418,HOME_TILE_OFF_PILL)==0);
 /* A state change repaints even when nothing else moved. */
 home_ui off=on;live(&off,0,30);assert(!home_visual_equal(&on,&off));
 home_ui on2=on;on2.input.scene.time+=0.5f;
 if(!home_live_points(&on))assert(home_visual_equal(&on,&on2));   /* settled tile: no repaint */
}
int main(void){
 table();live_counter();
 if(WAVESHARE_AI_PLUGIN_SPARKLES){sparkles_states();render();}
 if(WAVESHARE_AI_PLUGIN_AI)required_states();
 printf("tile_plugins: %d tiles; Sparkles link states (Wi-Fi, Hermes) without closing; PASS\n",HOME_TILES);
}
