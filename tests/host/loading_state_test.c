/* Loading, not "disabled", while the board is still finding out (user rule 2026-09-30):
 *   "on hermes not configured, but not wifi it should update the tiles not enabled with that message
 *    and click go to hermes signin. ... Make sure there is a loading animation for wifi and hermes
 *    logging where the device is getting the state instead of just showing disabled even though
 *    logged in".
 * RED first. */
#include <assert.h>
#include <stdio.h>
#include "home_render.h"

static int tile_of(home_page page){for(int i=0;i<HOME_TILES;i++)if(home_tiles[i].page==page)return i;return -1;}
static home_ui board(void){
 home_ui s;memset(&s,0,sizeof s);s.page=HOME;s.saved=true;s.connected=true;
 s.pair.state=PAIR_ENROLLED_UNPAIRED;s.pair.live_http=200;s.input.stamp_us=s.live_now_us=100LL*1000*1000;
 return s;
}
static void signed_in(home_ui*s){s->phone.st.valid=true;s->phone.st.state=PH_AUTHORIZED;s->phone.st.flags=PHONE_FLAG_REQUIRED;s->phone.st.received_us=s->live_now_us;
 s->phone_ha.st=s->phone.st;s->phone_ha.st.flags=PHONE_FLAG_REQUIRED;}  /* Sensor: the Home Assistant provider's own sign-in */

/* Home tiles: three states for Ask/Sensor. */
static void tile_states(void){
 int ask=tile_of(HELPER),sensor=tile_of(SENSORS);
 /* Wi-Fi saved but not up yet: loading, not "No Wi-Fi" (the board is joining). */
 home_ui j=board();j.connected=false;
 assert(home_tile_state(&j,ask)==TILE_LOADING&&!strcmp(home_tile_off_label(&j,ask),"Joining Wi-Fi"));
 /* Nothing saved and no link: really no Wi-Fi. */
 home_ui n=board();n.connected=n.saved=false;assert(home_tile_state(&n,ask)==TILE_OFF&&!strcmp(home_tile_off_label(&n,ask),"No Wi-Fi"));
 /* Wi-Fi up, sign-in status not heard from the host yet: loading. */
 home_ui c=board();assert(home_tile_state(&c,ask)==TILE_LOADING&&!strcmp(home_tile_off_label(&c,ask),"Checking sign-in"));
 /* Not linked to a host at all (Wi-Fi up): off, and a tap goes to the sign-in (Hermes) settings tab. */
 home_ui u=board();u.pair.state=PAIR_NO_BRIDGE;assert(home_tile_state(&u,ask)==TILE_OFF&&!strcmp(home_tile_off_label(&u,ask),"Set up bridge"));
 u.tile=ask;home_tap(&u,184,224);assert(u.page==SETTINGS&&u.settings_tab==SETTINGS_HERMES);
 /* Linked, host says signed out: off with "Sign in", a tap opens the sign-in QR. */
 home_ui o=board();o.phone.st.valid=true;o.phone.st.state=PH_NONE;o.phone.st.flags=PHONE_FLAG_REQUIRED;
 assert(home_tile_state(&o,ask)==TILE_OFF&&!strcmp(home_tile_off_label(&o,ask),"Sign in"));
 o.tile=ask;home_tap(&o,184,224);assert(o.page==SETTINGS&&o.settings_tab==SETTINGS_HERMES&&o.phone.want_start);
 /* Signed in: on. */
 home_ui a=board();signed_in(&a);assert(home_tile_state(&a,ask)==TILE_ON&&home_tile_state(&a,sensor)==TILE_ON);
 /* Each provider's status loads on its own: Hermes answered, Home Assistant not yet = Sensor loading. */
 home_ui h=board();signed_in(&h);h.phone_ha.st.valid=false;
 assert(home_tile_state(&h,ask)==TILE_ON&&home_tile_state(&h,sensor)==TILE_LOADING&&!strcmp(home_tile_off_label(&h,sensor),"Checking sign-in"));
 /* A loading tile is not a dead end: a tap opens the page (Ask then shows its own loading). */
 home_ui l=board();l.tile=ask;home_tap(&l,184,224);assert(l.page==HELPER);
 /* Loading tiles animate (repaint), settled ones don't. */
 home_ui l1=board(),l2=board();l1.tile=l2.tile=ask;l2.input.scene.time=l1.input.scene.time+0.5f;assert(!home_visual_equal(&l1,&l2));
 home_ui a1=board(),a2=board();signed_in(&a1);signed_in(&a2);a1.tile=a2.tile=tile_of(SETTINGS);a2.input.scene.time+=0.5f;assert(home_visual_equal(&a1,&a2));
}
/* Ask page: signed in and on Wi-Fi, but no fresh bots answer yet = loading, never "Hermes unavailable". */
static void ask_loading(void){
 home_ui s=board();signed_in(&s);s.page=HELPER;
 home_bots_sync(&s,s.live_now_us);
 assert(s.helper.block==BR_LOADING);
 assert(!strcmp(bots_reason_text(BR_LOADING),"Connecting to Hermes"));
 /* A fresh frame with the bot available: enabled. */
 s.bots.data=(bots_data){.valid=true,.count=3,.received_us=s.live_now_us};
 for(int i=0;i<3;i++){s.bots.data.b[i].available=true;s.bots.data.b[i].reset_s=BOTS_NONE32;}
 home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_NONE);
 /* The frame aging out between polls stays enabled (the bridge still enforces quota on submit). */
 home_bots_sync(&s,s.live_now_us+60LL*1000*1000);assert(s.helper.block==BR_NONE);
 /* One failed poll after a good one: still usable. Two in a row: reconnecting (loading), not off. */
 s.bots.fails=1;home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_NONE);
 s.bots.fails=2;home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_LOADING);
 /* Many failures in a row (the host is really gone): off with "Hermes unavailable". */
 s.bots.fails=BOTS_GIVE_UP_FAILS;home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_UPSTREAM);
 /* No Wi-Fi at all: "Joining Wi-Fi" style loading when a network is saved. */
 home_ui w=board();signed_in(&w);w.page=HELPER;w.connected=false;home_bots_sync(&w,w.live_now_us);assert(w.helper.block==BR_LOADING);
 /* Real reasons from the host still win. */
 s.bots.fails=0;s.bots.data.b[0].reason=BR_EXHAUSTED;home_bots_sync(&s,s.live_now_us);assert(s.helper.block==BR_EXHAUSTED);
 /* The mic is off while loading, but the page animates (Kotaro dozes) instead of freezing. */
 home_ui a=board();signed_in(&a);a.page=HELPER;home_bots_sync(&a,a.live_now_us);
 assert(a.helper.block==BR_LOADING&&!helper_can_press_mic(&a.helper));
 /* He dozes with the Ask page clock (the orbs' time): two moments differ on the bot. */
 static uint16_t p1[SPARKLES_PIXELS],p2[SPARKLES_PIXELS];
 home_render(&a,p1,SPARKLES_PIXELS);home_ui b=a;b.helper.orbs.time+=0.3f;home_render(&b,p2,SPARKLES_PIXELS);
 helper_box bb=helper_bot_box(&a.helper);unsigned diff=0;
 for(int y=bb.y0;y<bb.y1;y++)for(int x=bb.x0;x<bb.x1;x++)diff+=p1[y*368+x]!=p2[y*368+x];
 assert(diff>20);
}
/* Settings > the sign-in tab while the status is on its way: a loading screen, no "Sign in" button yet. */
static void settings_loading(void){
 home_ui s=board();s.page=SETTINGS;s.settings_tab=SETTINGS_HERMES;
 settings_buttons b=home_settings_buttons(&s,s.live_now_us);
 assert(home_settings_find(&b,SA_SIGNIN)<0&&home_settings_find(&b,SA_SIGNOUT_ASK)<0);
 assert(home_settings_loading(&s));
 signed_in(&s);assert(!home_settings_loading(&s));
 /* The Home Assistant tab loads on its own status. */
 home_ui ha=board();signed_in(&ha);ha.phone_ha.st.valid=false;ha.page=SETTINGS;ha.settings_tab=SETTINGS_HOME_ASSISTANT;
 b=home_settings_buttons(&ha,ha.live_now_us);assert(b.count==0&&home_settings_loading(&ha));
 ha.phone_ha.absent=true;assert(!home_settings_loading(&ha));   /* not set up on the bridge: a still screen */
 home_ui w=board();w.connected=false;w.page=SETTINGS;w.settings_tab=SETTINGS_WIFI;assert(home_settings_loading(&w));  /* joining */
 w.saved=false;assert(!home_settings_loading(&w));
}
int main(void){
 tile_states();ask_loading();settings_loading();
 puts("loading_state: tiles/Ask/Settings show loading while the board finds out, off only on a real answer: PASS");
}
