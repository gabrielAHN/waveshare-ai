/* No Wi-Fi = nothing that needs Hermes (user rule 2026-09-30): "if no wifi do not show hermes
 * connection since require so with no wifi disable tiles and settings required for it. Also on
 * setting for wifi adjust to wifi configured if not go to hotspot barcode to device".
 * RED first. */
#include <assert.h>
#include <stdio.h>
#include "home_render.h"

static home_ui signed_in(bool wifi){
 /* saved=false: truly no Wi-Fi (a saved network that is still joining is LOADING, loading_state_test). */
 home_ui s;memset(&s,0,sizeof s);s.page=HOME;s.connected=wifi;s.saved=wifi;
 s.phone.st.valid=true;s.phone.st.state=PH_AUTHORIZED;s.phone.st.flags=PHONE_FLAG_REQUIRED;
 s.phone_ha.st.valid=true;s.phone_ha.st.state=PH_AUTHORIZED;s.phone_ha.st.flags=PHONE_FLAG_REQUIRED;  /* Sensor: its own sign-in */
 s.pair.state=PAIR_ENROLLED_UNPAIRED;s.pair.live_http=200;
 return s;
}
static int tile_of(home_page page){for(int i=0;i<HOME_TILES;i++)if(home_tiles[i].page==page)return i;return -1;}
static void tap_tile(home_ui*s,home_page page){s->page=HOME;s->tile=tile_of(page);home_tap(s,184,224);}

/* Ask and Sensor need Hermes / Home Assistant through the host: off without Wi-Fi, even signed in.
 * Sparkles and Settings stay usable. The disabled tile says why. */
static void tiles_need_wifi(void){
 home_ui on=signed_in(true),off=signed_in(false);
 assert(!home_tile_disabled(&on,tile_of(HELPER))&&!home_tile_disabled(&on,tile_of(SENSORS)));
 assert(home_tile_disabled(&off,tile_of(HELPER))&&home_tile_disabled(&off,tile_of(SENSORS)));
 assert(!home_tile_disabled(&off,tile_of(SPARKLES))&&!home_tile_disabled(&off,tile_of(SETTINGS)));
 assert(!strcmp(home_tile_off_label(&off,tile_of(HELPER)),"No Wi-Fi"));
 home_ui out=signed_in(true);out.phone.st.state=PH_NONE;   /* Wi-Fi up, signed out */
 assert(home_tile_disabled(&out,tile_of(HELPER))&&!strcmp(home_tile_off_label(&out,tile_of(HELPER)),"Sign in"));
 /* A disabled tile without Wi-Fi opens Settings on the Wi-Fi tab, never the phone sign-in QR. */
 tap_tile(&off,HELPER);
 assert(off.page==SETTINGS&&off.settings_tab==SETTINGS_WIFI&&!off.phone.want_start&&!off.phone.showing);
}
/* Settings without Wi-Fi: only the Wi-Fi, Display, Sound and Battery tabs; the provider (Hermes, Home
 * Assistant) tabs are gone and a stale provider tab / QR request moves to Wi-Fi. */
static void settings_hide_hermes(void){
 home_ui s=signed_in(false);s.page=SETTINGS;s.settings_tab=SETTINGS_HERMES;s.phone.showing=true;
 assert(home_settings_tabs(&s)==4);
 home_settings_tick(&s);
 assert(s.settings_tab==SETTINGS_WIFI&&!s.phone.showing);
 /* Swiping never reaches a provider tab. */
 for(int k=0;k<4;k++){int next=home_settings_next_tab(&s,-1);assert(next!=SETTINGS_HERMES&&next!=SETTINGS_HOME_ASSISTANT);s.settings_tab=(settings_page)next;}
 assert(s.settings_tab==SETTINGS_BATTERY);  /* Display, Sound, then straight on to Battery */
 /* The sign-in route cannot open without Wi-Fi. */
 s.page=HOME;home_open_signin(&s);assert(s.settings_tab!=SETTINGS_HERMES&&!s.phone.want_start);
 s.tile=tile_of(SENSORS);home_open_signin(&s);assert(s.settings_tab==SETTINGS_WIFI&&!s.phone_ha.want_start);
 /* With Wi-Fi all six tabs are back: Battery swipes on to Hermes, then Home Assistant. */
 home_ui w=signed_in(true);assert(home_settings_tabs(&w)==6);
 w.settings_tab=SETTINGS_BATTERY;assert(home_settings_next_tab(&w,-1)==SETTINGS_HERMES);
 w.settings_tab=SETTINGS_HERMES;assert(home_settings_next_tab(&w,-1)==SETTINGS_HOME_ASSISTANT);
}
/* Nothing saved (no home Wi-Fi, no hotspot): opening Settings goes straight to the Wi-Fi tab, which only
 * says how Wi-Fi gets there (with the flash); nothing starts on the board (wifi_flash_only_test). */
static void unconfigured_opens_wifi_tab(void){
 home_ui s=signed_in(false);s.saved=false;s.hotspot_saved=false;s.settings_tab=SETTINGS_SOUND;
 tap_tile(&s,SETTINGS);
 assert(s.page==SETTINGS&&s.settings_tab==SETTINGS_WIFI&&home_settings_buttons(&s,0).count==0);
 /* A saved network (home or hotspot): Settings opens where it was left. */
 home_ui h=signed_in(false);h.saved=true;h.settings_tab=SETTINGS_SOUND;tap_tile(&h,SETTINGS);assert(h.settings_tab==SETTINGS_SOUND);
}
/* Rendering: the Home tile says "No Wi-Fi", and no Hermes / host status is drawn in Settings. */
static void render_says_no_wifi(void){
 static uint16_t a[SPARKLES_PIXELS],b[SPARKLES_PIXELS];
 home_ui off=signed_in(false),out=signed_in(true);out.phone.st.state=PH_NONE;
 off.tile=out.tile=tile_of(HELPER);
 home_render(&off,a,SPARKLES_PIXELS);home_render(&out,b,SPARKLES_PIXELS);
 assert(memcmp(a,b,sizeof a));   /* different label text */
 home_ui s=signed_in(false);s.page=SETTINGS;s.settings_tab=SETTINGS_HERMES;home_settings_tick(&s);
 settings_buttons btn=home_settings_buttons(&s,0);
 for(int k=0;k<btn.count;k++)assert(btn.t[k].action!=SA_SIGNIN&&btn.t[k].action!=SA_SIGNOUT_ASK&&btn.t[k].action!=SA_SCAN);
 home_ui h=signed_in(false);h.page=SETTINGS;h.settings_tab=SETTINGS_HOME_ASSISTANT;h.phone_ha.showing=true;home_settings_tick(&h);
 assert(h.settings_tab==SETTINGS_WIFI&&!h.phone_ha.showing);
 btn=home_settings_buttons(&h,0);
 for(int k=0;k<btn.count;k++)assert(btn.t[k].action!=SA_SIGNIN&&btn.t[k].action!=SA_SIGNOUT_ASK&&btn.t[k].action!=SA_SCAN);
 /* Sound tab: the Session sparkle toggle (Hermes) is gone without Wi-Fi; Completion sound stays. */
 s.settings_tab=SETTINGS_SOUND;btn=home_settings_buttons(&s,0);
 assert(home_settings_find(&btn,SA_SOUND)>=0&&home_settings_find(&btn,SA_SPARKLE)<0);
 home_ui w=signed_in(true);w.page=SETTINGS;w.settings_tab=SETTINGS_SOUND;btn=home_settings_buttons(&w,0);
 assert(home_settings_find(&btn,SA_SPARKLE)>=0);
}
int main(void){
 tiles_need_wifi();settings_hide_hermes();unconfigured_opens_wifi_tab();render_says_no_wifi();
 puts("no_wifi_gate: Ask/Sensor off without Wi-Fi, Settings hides the sign-in tab, unconfigured board opens the Wi-Fi tab: PASS");
}
