/* Plugin switches (the named device's firmware/main/plugins.h): the Home tile list holds exactly the enabled plugins in
 * the fixed order Sparkles, Ask, Sensor, then Settings (always last), with no gaps; swipes walk every
 * tile and stop at both ends; a tap opens the tile's page; pages of plugins that are not built are
 * never shown; Settings only swipes through its enabled pages. tests/run_host_tests.sh builds this
 * suite for every -DWAVESHARE_AI_PLUGIN_SPARKLES/AI/HOME_ASSISTANT=0|1 combination (PLUGIN_MATRIX). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
static uint16_t p[SPARKLES_PIXELS+2];
static int64_t t;
static void sample(home_ui*s,bool down,int x,int y){home_sample(s,t+=10000,down,x,y);}
static void settle(home_ui*s){for(int k=0;k<40;k++)home_sample(s,t+=10000,false,0,0);}
static void tap(home_ui*s,int x,int y){sample(s,true,x,y);sample(s,true,x,y);sample(s,false,x,y);settle(s);}
static void swipe(home_ui*s,int x0,int x1,int y){for(int k=0;k<=8;k++)sample(s,true,x0+(x1-x0)*k/8,y);sample(s,false,x1,y);settle(s);}
static void home_edge(home_ui*s){for(int k=0;k<=8;k++)sample(s,true,184,440-27*k);sample(s,false,0,0);settle(s);}  /* bottom-UP Home pull */
/* A board on Wi-Fi, paired and signed in: every built tile is usable (no_wifi_gate_test covers offline). */
static home_ui ready(void){
 home_ui s;memset(&s,0,sizeof s);s.page=HOME;s.connected=s.saved=true;
 s.phone.st.valid=true;s.phone.st.state=PH_AUTHORIZED;s.phone.st.flags=PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 s.phone_ha.st=s.phone.st;  /* the Sensor tile's own (Home Assistant) sign-in */
#endif
 s.pair.state=PAIR_ENROLLED_UNPAIRED;s.pair.live_http=200;
 return s;
}
/* Light + Orange Home tiles (SPEC3 Contract S): tile i's paper is white mixed toward the accent
 * (238,112,28) at 16 / 24 / 32 / 40 % in carousel order (independent oracle of theme.h theme_tile_paper). */
static uint16_t tile_paper(int tile){static const int pct[4]={16,24,32,40};int p=pct[tile%4];
 return sp_pack_lcd(255-((255-238)*p+50)/100,255-((255-112)*p+50)/100,255-((255-28)*p+50)/100);}
int main(void){
 const int S=WAVESHARE_AI_PLUGIN_SPARKLES,A=WAVESHARE_AI_PLUGIN_AI,H=WAVESHARE_AI_PLUGIN_HOME_ASSISTANT;
 /* 1. The tile list: enabled plugins only, fixed order, Settings last. */
 home_page want[4];int n=0;
 if(S)want[n++]=SPARKLES;
 if(A)want[n++]=HELPER;
 if(H)want[n++]=SENSORS;
 want[n++]=SETTINGS;
 assert(HOME_TILES==n&&HOME_TILE_SETTINGS==n-1);
 for(int i=0;i<n;i++)assert(home_tiles[i].page==want[i]&&home_tile_index(want[i])==i);
 assert(home_tile_index(SPARKLES)==(S?0:-1));
 assert((home_tile_index(HELPER)>=0)==A&&(home_tile_index(SENSORS)>=0)==H);
 if(S&&A&&H){assert(HOME_TILES==4&&HOME_TILE_SETTINGS==3&&home_tile_index(HELPER)==1&&home_tile_index(SENSORS)==2);}
 assert(home_page_enabled(HOME)&&home_page_enabled(SETTINGS));
 assert(home_page_enabled(SPARKLES)==S&&home_page_enabled(HELPER)==A&&home_page_enabled(SENSORS)==H);

 /* 2. Every tile renders edge to edge in its own paper: no gap (the Home background never shows at rest). */
 home_ui s=ready();
 const uint16_t bg=sp_pack_lcd(181,207,213);
 for(int k=0;k<HOME_TILES;k++){
  s.tile=k;p[0]=p[SPARKLES_PIXELS+1]=0x9876;
  assert(home_render(&s,p+1,SPARKLES_PIXELS));
  assert(p[0]==0x9876&&p[SPARKLES_PIXELS+1]==0x9876);
  assert(p[1+200*368+18]==tile_paper(k)&&p[1+200*368+18]!=bg);
  assert(p[1+20*368+350]==tile_paper(k));
 }
 s.tile=0;

 /* 3. Swipes walk every tile in order and stop at both ends (rubber band only). */
 for(int k=1;k<HOME_TILES+2;k++){swipe(&s,300,120,200);assert(s.page==HOME&&s.tile==(k<HOME_TILES?k:HOME_TILES-1)&&s.drag_offset==0);}
 assert(s.tile==HOME_TILE_SETTINGS);
 for(int k=HOME_TILES-2;k>=-2;k--){swipe(&s,90,270,200);assert(s.page==HOME&&s.tile==(k>0?k:0)&&s.drag_offset==0);}

 /* 4. A tap opens exactly that tile's page, it renders, and the Home pull returns Home. */
 for(int k=0;k<HOME_TILES;k++){
  s.page=HOME;s.tile=k;s.drag_offset=0;s.home_slide_from=0;
  tap(&s,184,220);
  assert(s.page==home_tiles[k].page);
  assert(home_render(&s,p+1,SPARKLES_PIXELS));
  home_edge(&s);assert(s.page==HOME);
 }

 /* 5. A page whose plugin is not built (USB WLV1, stale state) falls back to Home on the next sample;
  *    built pages stay put. */
 static const home_page every[]={SPARKLES,HELPER,SENSORS};
 for(unsigned k=0;k<sizeof every/sizeof every[0];k++){
  home_ui u={0};u.page=every[k];home_sample(&u,10000,false,0,0);
  assert(u.page==(home_page_enabled(every[k])?every[k]:HOME));
 }
 { home_ui u={0};u.tile=7;home_sample(&u,10000,false,0,0);assert(u.tile==HOME_TILES-1); }

 /* 6. Live session glows exist only with the Sparkles plugin (Home and page). */
 {
  home_ui u={0};u.live.valid=true;u.live.count=2;u.live.ids[0]=11;u.live.ids[1]=22;u.live.received_us=1;u.live_now_us=2;
  u.page=HOME;u.tile=0;
  assert((home_live_points(&u)>0)==(S&&live_fresh(&u.live,u.live_now_us)));
  assert(home_live_tile_visible(&u)==(bool)S);
 }

 /* 7. Settings swipes visit only enabled pages; Wi-Fi first, then Display (core), Sound, Battery (core),
  *    then one tab per provider built (Hermes, Home Assistant). */
 {
  home_ui u=ready();u.page=SETTINGS;u.settings_tab=SETTINGS_WIFI;
  int visited=1,expect=1+1+(A||S)+1+WAVESHARE_AI_PROVIDER_HERMES+WAVESHARE_AI_PROVIDER_HOME_ASSISTANT;
  for(int k=0;k<5;k++){
   settings_page before=u.settings_tab;
   for(int j=0;j<=8;j++)home_sample(&u,t+=10000,true,300-24*j,200);
   home_sample(&u,t+=10000,false,108,200);
   assert(home_settings_tab_enabled(u.settings_tab));
   if(u.settings_tab!=before)visited++;
   assert(home_render(&u,p+1,SPARKLES_PIXELS));
  }
  assert(visited==expect);
  /* A disabled tab is never kept (e.g. restored state). */
  u.settings_tab=SETTINGS_SOUND;home_sample(&u,t+=10000,false,0,0);assert(home_settings_tab_enabled(u.settings_tab));
  u.settings_tab=SETTINGS_HERMES;home_sample(&u,t+=10000,false,0,0);assert(home_settings_tab_enabled(u.settings_tab));
  assert(u.settings_tab==(WAVESHARE_AI_PROVIDER_HERMES?SETTINGS_HERMES:SETTINGS_WIFI));
  u.settings_tab=SETTINGS_HOME_ASSISTANT;home_sample(&u,t+=10000,false,0,0);assert(home_settings_tab_enabled(u.settings_tab));
  assert(u.settings_tab==(WAVESHARE_AI_PROVIDER_HOME_ASSISTANT?SETTINGS_HOME_ASSISTANT:SETTINGS_WIFI));
  /* Sound page rows: completion sound (AI) and session sparkle (Sparkles) only. */
  if(A||S){
   u.settings_tab=SETTINGS_SOUND;settings_buttons b=home_settings_buttons(&u,0);
   assert(b.count==A+S);
   assert(home_settings_find(&b,SA_SOUND)>=0==(bool)A&&home_settings_find(&b,SA_SPARKLE)>=0==(bool)S);
   assert(b.t[0].y==SET_ROW1_Y);
  }
 }
 /* 8. A shared sign-in (flag 8, SPEC3 Contract S) folds the account tabs into one only when BOTH sign-ins
  *    are built (Ask and Sensor); every other build keeps its own tabs (Sparkles + Sensor: the Hermes tab
  *    has no sign-in, so the Home Assistant tab stays). */
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 {
  home_ui u=ready();u.page=SETTINGS;u.phone_ha.st.flags|=PHONE_FLAG_SHARED;
  bool fold=A&&H;
  assert(home_signin_shared(&u)==fold&&home_settings_tab_reachable(&u,SETTINGS_HOME_ASSISTANT)==!fold);
  assert(home_settings_tabs(&u)==1+1+(A||S)+1+WAVESHARE_AI_PROVIDER_HERMES+WAVESHARE_AI_PROVIDER_HOME_ASSISTANT-(fold?1:0));
  u.settings_tab=SETTINGS_HOME_ASSISTANT;home_sample(&u,t+=10000,false,0,0);
  assert(u.settings_tab==(fold?SETTINGS_HERMES:SETTINGS_HOME_ASSISTANT));
 }
#endif
 printf("plugin tiles sparkles=%d ai=%d home_assistant=%d (providers hermes=%d home_assistant=%d): %d tiles, order + swipes + taps + fallback + settings: PASS\n",
  S,A,H,WAVESHARE_AI_PROVIDER_HERMES,WAVESHARE_AI_PROVIDER_HOME_ASSISTANT,HOME_TILES);
 return 0;
}
