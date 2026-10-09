#include <assert.h>
#include <stdio.h>
#include "home_render.h"
/* 'Session sparkle' toggle: a large full-width Settings row with an ON/OFF switch, render change,
 * persistence request flag. */
static int64_t t=0;
static void tap(home_ui*s,int x,int y){home_sample(s,t+=10000,true,x,y);home_sample(s,t+=10000,false,x,y);}
static uint16_t a[SPARKLES_PIXELS],b[SPARKLES_PIXELS];
int main(void){
 /* Status screen: on Wi-Fi, enrolled, with no pending phone gate. */
 home_ui s={.page=SETTINGS,.connected=true,.saved=true,.settings_tab=SETTINGS_SOUND};s.pair.state=PAIR_ENROLLED_UNPAIRED;
 assert(!s.session_off&&!s.session_save); /* zero-init = default ON */
 unsigned char w[18]={'W','L','S','4',1,0,3,1,9,0,0,0,0,0,0,0,0,1};
 assert(live_decode(&s.live,w,sizeof w,100));s.live_now_us=200;
 assert(home_settings_screen(&s)==SS_STATUS);
 settings_buttons bt=home_settings_buttons(&s,0);int k=home_settings_find(&bt,SA_SPARKLE);assert(k>=0);
 const settings_target T=bt.t[k];assert(T.toggle&&T.on&&!strcmp(T.label,"On"));
 /* 1. Tap the large row (>= 72 px, full width) -> OFF, clears roster/level, requests save. */
 assert(T.h>=72&&T.w>=300);
 tap(&s,180,T.y+T.h/2);
 assert(s.session_off&&s.session_save&&!s.live.valid&&s.live.count==0&&home_live_density(&s)==0);
 s.session_save=false;
 /* top and bottom edges of the target hit; outside does not toggle */
 {settings_buttons nb=home_settings_buttons(&s,0);assert(nb.t[k].action==SA_SPARKLE&&!nb.t[k].on&&!strcmp(nb.t[k].label,"Off"));}
 tap(&s,T.x+1,T.y+1);assert(!s.session_off&&s.session_save);s.session_save=false;
 tap(&s,T.x+T.w-2,T.y+T.h-1);assert(s.session_off);s.session_save=false;
 tap(&s,180,T.y+T.h+4);assert(s.session_off&&!s.session_save);
 tap(&s,10,T.y+20);assert(s.session_off&&!s.session_save);
 s.page=SPARKLES;tap(&s,180,T.y+20);assert(s.session_off&&!s.session_save);s.page=SETTINGS;
 /* the bottom-edge UP Home pull is Home, not a button */
 home_sample(&s,t+=10000,true,180,440);home_sample(&s,t+=10000,true,184,224);home_sample(&s,t+=10000,false,184,224);
 assert(s.page==HOME&&s.session_off);s.page=SETTINGS;
 /* 2. home_set_session is idempotent; OFF while a late live sample arrives. */
 assert(!home_set_session(&s,false));assert(home_set_session(&s,true)&&!s.session_off&&s.session_save);
 assert(live_decode(&s.live,w,sizeof w,300));s.live_now_us=400;assert(home_live_density(&s)==4); /* 1 active session, level 3 */
 /* 3. Render: ON and OFF differ only inside the button. */
 home_ui on=s,off=s;off.session_off=true;
 assert(!home_visual_equal(&on,&off));
 assert(home_render(&on,a,SPARKLES_PIXELS)&&home_render(&off,b,SPARKLES_PIXELS));
 int diff=0,outside=0;for(int y=0;y<448;y++)for(int x=0;x<368;x++)if(a[y*368+x]!=b[y*368+x]){diff++;if(y<T.y||y>=T.y+T.h)outside++;}
 assert(diff>200&&outside==0);
 puts("session toggle: default ON, large full-width switch row, clears live state, persistence flag, render invalidation PASS");
 return 0;
}
