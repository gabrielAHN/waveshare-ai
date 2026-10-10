#pragma once
#include "plugins.h"
#include "account.h"
#include "direct_input.h"
#include "home_credentials.h"
#include "live_state.h"
#include "helper_ui.h"
#include "pair_ui.h"
#include "bots_view.h"
#include "phone_qr.h"
#include "settings_ui.h"
#include "tile_plugins.h"
#include "sensors_view.h"
#include "battery_estimate.h"
#include "power_button.h"
#include "theme.h"
#include "ui_easing.h"
/* Home tiles (swipe order, what each needs) are the plugin table in tile_plugins.h. */
/* Pages that exist in this build. A page of a disabled plugin is never entered (USB WLV1, gestures and
 * restored state fall back to Home). */
static inline bool home_page_enabled(int page){
 switch(page){
  case HOME:case SETTINGS:return true;
  case SPARKLES:return WAVESHARE_AI_PLUGIN_SPARKLES;
  case HELPER:return WAVESHARE_AI_PLUGIN_AI;
  case SENSORS:return WAVESHARE_AI_PLUGIN_HOME_ASSISTANT;
  default:return false;
 }
}
/* Settings pages: Wi-Fi, Display (colour theme) and Battery are core; Sound holds the AI completion
 * sound and the Sparkles session toggle; each provider built adds its own sign-in tab (Contract D:
 * Hermes, Home Assistant). */
static inline bool home_settings_tab_enabled(int tab){
 switch(tab){
  case SETTINGS_WIFI:return true;
  case SETTINGS_DISPLAY:return true;
  case SETTINGS_SOUND:return WAVESHARE_AI_PLUGIN_AI||WAVESHARE_AI_PLUGIN_SPARKLES;
  case SETTINGS_BATTERY:return true;
  case SETTINGS_HERMES:return WAVESHARE_AI_PROVIDER_HERMES;
  case SETTINGS_HOME_ASSISTANT:return WAVESHARE_AI_PROVIDER_HOME_ASSISTANT;
  default:return false;
 }
}
/* Next enabled Settings page in direction dir (+1/-1); stays put at either end. */
static inline settings_page home_settings_step(settings_page tab,int dir){
 for(int next=(int)tab+dir;next>=SETTINGS_WIFI&&next<SETTINGS_TABS;next+=dir)
  if(home_settings_tab_enabled(next))return (settings_page)next;
 return tab;
}
/* Settings is horizontally swiped full-screen pages (Wi-Fi, Display, Sound, Battery, then one per provider:
 * Hermes, Home Assistant; one for both when they share a sign-in), with no text entry: Wi-Fi arrives with the flash (.env -> tools/flash.sh -> USB), the
 * bridge is found by mDNS (or handed over USB, tools/pair_usb.py), and each provider's sign-in is a
 * phone QR. Every control is a LARGE full-width target (>= SET_BTN_MIN_H tall).
 *
 * Safe-area rule (tests/host/settings_ui_test.c checks every pixel, text box and target of every state):
 * nothing is drawn closer than SET_EDGE px to a panel edge, closer than SET_EDGE px to the rounded
 * corner arc (radius SET_PANEL_R; ~40 px measured on the Waveshare 1.8" V2 dimension photo, 56 used
 * as margin), or inside the bottom SET_HOME_BAND px (the panel's rounded bottom; the lowest rows of the
 * Home pull zone, HOME_PULL_ZONE_Y below). */
typedef enum {HOME_NONE,HOME_SAVE,HOME_FORGET} home_action;
/* Motion (SPEC2 Contract M, home_sample below): the last touch samples (release speeds) and what the
 * touch poller logs: HOME_PULL start when a touch becomes the Home pull (wait_ms = how long it waited
 * to be classified), HOME_GESTURE on its release (kind home = commit / cancel = spring back, offset =
 * downward travel, drag_ms, anim_ms), HOME_PULL end when its drop or spring back has finished (ms),
 * HOME_SLIDE when a carousel settle ends. */
#define HOME_TRACK 16
typedef struct {int64_t t[HOME_TRACK];short x[HOME_TRACK],y[HOME_TRACK],v[HOME_TRACK];unsigned char n,head;} home_track;
typedef struct {bool gesture,home,slide,pull,done,done_home;int offset,speed_milli,from,to,ms,wait_ms,drag_ms,anim_ms,done_ms;} home_motion_note;
enum {HOME_MOTION_NONE,HOME_MOTION_DRAG,HOME_MOTION_BACK,HOME_MOTION_OUT,HOME_MOTION_OPEN};
/* The centered circular Home-reveal aperture; w == h and n == 2 for every active frame. */
#define HOME_BLOB_HAS_ALPHA 1
typedef struct {float cx,cy,w,h,n,alpha;} home_blob;
#define HOME_PULL_BUF 24   /* bounded samples retained for a reserved pull-zone contact */
typedef struct {
 direct_input input;
 live_state live;int64_t live_now_us;
 home_page page;
 int tile,drag_offset,start_x,start_y,last_x,last_y,max_move;
 int64_t start_us,home_slide_us;
 int home_slide_from;
 bool down,edge,consumed;
 /* The Home pull (W14, home_pull_classify): pull_wait = this touch started in the pull zone of a page
  * and remains reserved; up to HOME_PULL_BUF samples are retained in pull_t/x/y without capacity
  * ending its eligibility. edge = it is the Home pull (pull_us: since when). blob = the page's
  * shape now; blob_from = its shape at the release, blob_ax/ay = where the finger took the page,
  * blob_f0/dx0 = the travel a spring back starts from. */
 bool pull_wait;unsigned char pull_n;home_page pull_page;
 int64_t pull_t[HOME_PULL_BUF],pull_us;short pull_x[HOME_PULL_BUF],pull_y[HOME_PULL_BUF];
 home_blob blob,blob_from;short blob_ax,blob_ay;float blob_f0,blob_dx0,blob_vf,blob_vx;
 /* Motion. page_y: where the page layer is while a tile opens (448 = still below, 0 = in place; Home
  * shows above it), moved by slide_kind (the Home pull's finger / spring back / drop: the blob above;
  * a tile opening: page_y) from slide_y0 at slide_us over slide_len ms (slide_vt = start speed x
  * length, px); slide_page = the page in that layer; motion_id = new per page motion (the renderer's
  * snapshot key).
  * Carousel settle: from home_slide_from px at home_slide_us over home_slide_len ms, home_slide_vt
  * likewise; drag_base = where a caught settle was (raw px), caught = this touch caught it (never a tap). */
 int page_y,slide_y0,drag_base,home_slide_tile;
 float slide_len,slide_vt,home_slide_len,home_slide_vt;
 int64_t slide_us;
 unsigned char slide_kind;
 bool caught;
 home_page slide_page;
 unsigned motion_id;
 home_track track;
 home_motion_note note;
 home_credentials credentials;  /* from NVS / USB only; the SSID is shown, the password never */
 home_action action;           /* HOME_SAVE after a USB WSP1 frame, HOME_FORGET from WPC1 (tools/pair_usb.py) */
 bool busy,saved,connected;
 /* iPhone Personal Hotspot fallback (wifi_choice.h): its name only, never its password. */
 bool hotspot_saved,on_hotspot;char hotspot_ssid[33];
 bool auto_scan,auto_qr;       /* once per Settings visit: search for the bridge / open the phone QR */
 settings_page settings_tab;
 bool settings_gesture_cancel;
 bool sound_off,sound_save;    /* completion click preference only; playback is owned elsewhere */
 /* 'Session sparkle' Settings toggle. Stored inverted so zero-init = ON
  * (the product default). session_save asks the low-priority Wi-Fi/NVS
  * worker to persist; OFF also clears the live roster/level immediately. */
 bool session_off,session_save;
 /* Sparkles swipe style (sparkles.h SP_STYLES): a quick horizontal flick on the Sparkles page picks
  * the next (left) / previous (right) style, wrapping, until you leave the page: every entry starts on
  * style 0, Sea (SPEC3 Contract S, home_sparkle_entry); never saved. sparkle_open = the page was Sparkles
  * at the last check (an entry is its change to true). */
 bool sparkle_open;
 /* Colour theme (Settings > Display, theme.h): theme_mode 0 = light, 1 = dark; accent 0..4 (0 = Orange).
  * Zero-init = light + Orange. theme_save asks the NVS worker to persist (key "theme"). */
 uint8_t theme_mode,accent;
 bool theme_save;
 char status[48],ip[16];
 helper_view helper;
 pair_view pair;
 /* Ask-page bots (bridge /v1/bots, WBT1): per-bot availability from the official quota, written by
  * the live worker while the Ask page is visible. No quota is ever drawn; it only disables a bot's
  * mic. bots_busy = a /v1/bots TLS request is in flight (the voice upload waits for it). */
 bots_view bots;
 bool bots_busy;
 /* Phone sign-in per provider (device grant via the bridge, phone_qr.h), mirrored by the live worker:
  * phone = the Hermes provider (Ask), phone_ha = the Home Assistant provider (Sensor). */
 phone_view phone;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 phone_view phone_ha;
#endif
 bool signin_shared;  /* a status with PHONE_FLAG_SHARED (8) arrived this boot (RAM only): home_signin_shared */
 /* Sensor page (sensors_view.h): WHS1 from the bridge's /v1/home, polled only while it is open. */
 sensors_view sensors;
 /* Settings > Battery (battery_estimate.h): written every ~15 s by the battery poll task (direct_main.c). */
 battery_view battery;
 /* Power-off countdown overlay (power_button.h / power_ui.h), set by the power task (power_main.c). */
 power_view power;
} home_ui;
/* Tile status from the plugin table (tile_plugins.h). User rules 2026-09-30 / 2026-10-02: every tile
 * that talks to Hermes shows LOADING (animated: "Joining Wi-Fi", "Checking sign-in", "Connecting to
 * Hermes") while the board is still finding out, never "disabled" then; OFF only on a real answer:
 *   no Wi-Fi saved/up -> "No Wi-Fi"        (tap: Settings > Wi-Fi)
 *   no host linked      -> "Set up bridge"   (tap: its provider's Settings tab, which pairs the host)
 *   host says signed out-> "Sign in"         (tap: its provider's sign-in QR)  TILE_HERMES_REQUIRED only
 *   live feed gone     -> "Hermes unavailable" (30 s after the first failed poll)  TILE_HERMES_OPTIONAL only
 *   provider not on the bridge -> "Not set up" (its sign-in status answered 404; tap: its Settings tab)
 * `blocks` = a tap opens the fix instead of the page: only TILE_HERMES_REQUIRED tiles block. A
 * TILE_HERMES_OPTIONAL tile (Sparkles) keeps its colours and always opens; its line just says what
 * the Hermes part is doing. With "Session sparkle" off it has no Hermes part at all. */
typedef enum {TILE_ON,TILE_LOADING,TILE_OFF} tile_state;
typedef struct {tile_state state;bool blocks;const char*label;} tile_status;
/* ---- Providers (plugins.h): each one built owns a phone sign-in view and a Settings tab ---- */
static inline bool home_provider_tab(int tab){return tab==SETTINGS_HERMES||tab==SETTINGS_HOME_ASSISTANT;}
/* A provider tab's sign-in view, or NULL (not a provider tab, or that provider is not built). */
static inline phone_view*home_tab_phone(home_ui*s,int tab){
 if(tab==SETTINGS_HERMES)return WAVESHARE_AI_PROVIDER_HERMES?&s->phone:NULL;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 if(tab==SETTINGS_HOME_ASSISTANT)return &s->phone_ha;
#endif
 return NULL;
}
static inline const phone_view*home_tab_phone_c(const home_ui*s,int tab){return home_tab_phone((home_ui*)s,tab);}
/* The provider has a tile that needs its sign-in (Hermes: Ask; Home Assistant: Sensor). A Sparkles-only
 * Hermes build needs none (the live feed is board-token only). */
static inline bool home_provider_signin(int tab){
 return tab==SETTINGS_HERMES?(WAVESHARE_AI_PROVIDER_HERMES&&WAVESHARE_AI_PLUGIN_AI):(tab==SETTINGS_HOME_ASSISTANT&&WAVESHARE_AI_PLUGIN_HOME_ASSISTANT);
}
/* Its sign-in label (account.h): the tab title. */
static inline const char*home_provider_label(int tab){return tab==SETTINGS_HOME_ASSISTANT?WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME:WAVESHARE_AI_HERMES_ACCOUNT_NAME;}
static inline int home_provider_other(int tab){return tab==SETTINGS_HERMES?SETTINGS_HOME_ASSISTANT:SETTINGS_HERMES;}
/* Shared sign-in (SPEC3 Contract S): both providers are built with a sign-in (Ask and Sensor) and a status
 * with flag 8 has arrived (kept in RAM for the boot; until then the tabs stay separate, no guessing).
 * Settings then has ONE account tab, the Hermes one, for both: the Home Assistant tab is unreachable. */
static inline bool home_signin_shared(const home_ui*s){
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 if(!home_provider_signin(SETTINGS_HERMES)||!home_provider_signin(SETTINGS_HOME_ASSISTANT))return false;
 return s->signin_shared||phone_shared(&s->phone)||phone_shared(&s->phone_ha);
#else
 (void)s;return false;
#endif
}
/* The Settings tab that holds a provider's sign-in: its own tab, or the shared one. */
static inline int home_signin_tab(const home_ui*s,int tab){return tab==SETTINGS_HOME_ASSISTANT&&home_signin_shared(s)?SETTINGS_HERMES:tab;}
/* Shared: no Home Assistant tab, so nothing of its sign-in is open either (a QR or sign-out question that
 * was open when flag 8 arrived closes, a requested code is dropped: a second device flow on the shared
 * gateway would put the other provider back to pending); the shared tab offers its own. */
static inline void home_signin_fold(home_ui*s){
 if(!home_signin_shared(s))return;
 phone_view*v=home_tab_phone(s,SETTINGS_HOME_ASSISTANT);
 if(v)v->showing=v->confirm_signout=v->want_start=false;
 if(s->settings_tab==SETTINGS_HOME_ASSISTANT)s->settings_tab=SETTINGS_HERMES;
}
/* The Settings tab of a Home tile's provider, or -1 (the Settings tile). */
static inline int home_tile_tab(int tile){
 if(tile<0||tile>=HOME_TILES)return -1;
 switch(home_tiles[tile].provider){
  case TILE_PROVIDER_HERMES:return SETTINGS_HERMES;
  case TILE_PROVIDER_HOME_ASSISTANT:return SETTINGS_HOME_ASSISTANT;
  default:return -1;
 }
}
/* Open a provider's sign-in QR (a fresh code). One QR or sign-out question is open at a time. */
static inline void home_phone_start(home_ui*s,int tab){
 for(int k=SETTINGS_HERMES;k<=SETTINGS_HOME_ASSISTANT;k++){phone_view*o=home_tab_phone(s,k);if(o&&k!=tab)o->showing=o->confirm_signout=false;}
 phone_view*v=home_tab_phone(s,tab);
 if(v)phone_request_start(v);
}
/* Close every provider's QR and sign-out question (leaving Settings, losing Wi-Fi). */
static inline void home_phone_close(home_ui*s){
 for(int k=SETTINGS_HERMES;k<=SETTINGS_HOME_ASSISTANT;k++){phone_view*v=home_tab_phone(s,k);if(v)v->showing=v->confirm_signout=false;}
}
/* Sensor readings need the Home Assistant sign-in: "off" on an explicit answer (signed out, refused,
 * or not set up on the bridge); unknown status never disables anything (the bridge answers 409). */
static inline bool home_ha_absent(const home_ui*s){const phone_view*v=home_tab_phone_c(s,SETTINGS_HOME_ASSISTANT);return v&&v->absent;}
static inline bool home_ha_off(const home_ui*s){
 const phone_view*v=home_tab_phone_c(s,SETTINGS_HOME_ASSISTANT);
 return !v||v->absent||(v->st.valid&&!phone_signed_in(v));
}
/* Which pages poll a provider's sign-in status (phone_poll_due: 60 s while visible, 3 s while its QR
 * shows): Hermes on Home, Ask and Settings; Home Assistant on Home, Sensor and its own Settings tab (the
 * shared tab when the sign-in is shared: both are polled while it shows). */
static inline bool home_phone_visible(const home_ui*s,int tab){
 if(!home_tab_phone_c(s,tab))return false;
 if(s->page==HOME)return true;
 if(tab==SETTINGS_HERMES)return s->page==HELPER||s->page==SETTINGS;
 return s->page==SENSORS||((s->page==SETTINGS)&&(int)s->settings_tab==home_signin_tab(s,SETTINGS_HOME_ASSISTANT));
}
static inline tile_status home_tile_status(const home_ui*s,int tile){
 tile_status r={TILE_ON,false,""};
 if(tile<0||tile>=HOME_TILES)return r;
 const tile_plugin*t=&home_tiles[tile];
 if(t->need==TILE_STANDALONE)return r;
 if(t->need==TILE_HERMES_OPTIONAL&&s->session_off)return r;   /* Sparkles without the session link */
 r.blocks=t->need==TILE_HERMES_REQUIRED;
 if(!s->connected){
  if(s->saved||s->hotspot_saved){r.state=TILE_LOADING;r.label="Joining Wi-Fi";}
  else{r.state=TILE_OFF;r.label="No Wi-Fi";}
  return r;
 }
 if(s->pair.state<PAIR_ENROLLED_UNPAIRED){r.state=TILE_OFF;r.label="Set up bridge";return r;}
 /* The bridge does not have this tile's provider (404 from its sign-in status). */
 const phone_view*v=home_tab_phone_c(s,home_tile_tab(tile));
 if(v&&v->absent){r.state=TILE_OFF;r.label="Not set up";return r;}
 if(t->need==TILE_HERMES_OPTIONAL){
  /* The live feed only needs the board's own link to the host (no phone sign-in). */
  if(s->pair.live_ok){r.state=TILE_ON;r.blocks=false;return r;}
  if(s->pair.live_down){r.state=TILE_OFF;r.label="Hermes unavailable";}
  else{r.state=TILE_LOADING;r.label="Connecting to Hermes";}
  return r;
 }
 if(!v)v=&s->phone;
 if(!v->st.valid){r.state=TILE_LOADING;r.label="Checking sign-in";return r;}
 /* Ask: the Hermes gate (required and not signed in; grandfathered boards pass). Sensor: its own
  * provider signed in. */
 bool off=t->gate==TILE_GATE_HOME_ASSISTANT?!phone_signed_in(v):phone_gate(v);
 if(off){r.state=TILE_OFF;r.label="Sign in";}
 else{r.state=TILE_ON;r.blocks=false;}
 return r;
}
static inline tile_state home_tile_state(const home_ui*s,int tile){return home_tile_status(s,tile).state;}
/* Closed: a tap opens the fix (Wi-Fi / the phone sign-in), not the page. Only Hermes-required tiles close. */
static inline bool home_tile_disabled(const home_ui*s,int tile){tile_status t=home_tile_status(s,tile);return t.blocks&&t.state!=TILE_ON;}
static inline const char*home_tile_off_label(const home_ui*s,int tile){return home_tile_status(s,tile).label;}
static inline bool home_tiles_loading(const home_ui*s){
 for(int t=0;t<HOME_TILES;t++)if(home_tile_state(s,t)==TILE_LOADING)return true;
 return false;
}
/* Settings tabs: Wi-Fi, Display, Sound, Battery, then the provider tabs (Hermes, Home Assistant; each titled with
 * its sign-in label; ONE, the Hermes tab, when they share a sign-in) only when there is Wi-Fi to reach them. */
static inline bool home_settings_tab_reachable(const home_ui*s,int tab){
 return home_settings_tab_enabled(tab)&&(!home_provider_tab(tab)||s->connected)&&home_signin_tab(s,tab)==tab;
}
static inline int home_settings_tabs(const home_ui*s){
 int n=0;for(int k=0;k<SETTINGS_TABS;k++)if(home_settings_tab_reachable(s,k))n++;
 return n;
}
static inline int home_settings_next_tab(const home_ui*s,int dx){
 /* A sign-in QR / sign-out question keeps its page until Back (home_settings_tick pins it there). */
 const phone_view*v=home_tab_phone_c(s,s->settings_tab);
 if(v&&(v->showing||v->confirm_signout))return (int)s->settings_tab;
 int dir=dx<0?1:-1;
 for(int next=(int)s->settings_tab+dir;next>=SETTINGS_WIFI&&next<SETTINGS_TABS;next+=dir)
  if(home_settings_tab_reachable(s,next))return next;
 return (int)s->settings_tab;
}
/* Nothing saved at all (no home Wi-Fi, no iPhone hotspot): Wi-Fi has to come with the next flash. */
static inline bool home_wifi_unconfigured(const home_ui*s){return !s->saved&&!s->hotspot_saved;}
/* Only the Ask page uses bot availability, so only it polls. */
static inline bool home_bots_surface(const home_ui*s){return s->page==HELPER;}
/* Never compete with the mic: no poll while recording or while a voice command talks to the bridge. */
static inline bool home_bots_paused(const home_ui*s){return s->helper.state==HV_LISTENING||s->helper.net_busy||s->helper.holding;}
/* Refresh the selected bot's mic gate from the latest frame (called under the owner mutex). */
/* Signed in with the phone QR: opening Ask (the mic tile) shows every bot enabled at once. Only
 * explicit per-bot evidence blocks it: a reported reason (No quota, provider sign-in, plugin), a
 * FRESH frame saying the route is down, repeated poll failures, or no Wi-Fi. A merely old frame or
 * no frame yet never disables the mic (the bridge still enforces quota with 409 on submit). */
/* User rule 2026-09-30: while the board is still finding out (joining Wi-Fi, first answer on its way,
 * a dropped poll), show loading instead of "disabled". Only BOTS_GIVE_UP_FAILS polls in a row with no
 * answer (~30 s) turn into "Hermes unavailable". */
#define BOTS_SIGNED_FAILS 2
static inline bots_reason home_bots_signed(const home_ui*s,int64_t now){
 if(!s->connected)return s->saved||s->hotspot_saved?BR_LOADING:BR_UPSTREAM;
 if(s->bots.fails>=BOTS_GIVE_UP_FAILS)return BR_UPSTREAM;
 if(s->bots.fails>=BOTS_SIGNED_FAILS)return BR_LOADING;
 const bots_view*v=&s->bots;int i=s->helper.bot;
 if(!v->data.valid||!v->data.count)return BR_LOADING;   /* first answer on its way */
 if(i<0||i>=v->data.count)return BR_UPSTREAM;
 const bots_entry*e=&v->data.b[i];
 if(e->reason==BR_PHONE)return BR_NONE; /* frame predates the sign-in; a refresh is requested */
 if(e->reason!=BR_NONE)return (bots_reason)e->reason;
 if(!e->available&&now-v->data.received_us<=BOTS_EVIDENCE_US)return BR_UPSTREAM;
 return BR_NONE;
}
static inline void home_bots_sync(home_ui*s,int64_t now){
 s->helper.nbots=bots_count(&s->bots);
 if(s->helper.bot>=s->helper.nbots)s->helper.bot=s->helper.nbots-1;
 bots_reason r;
 if(phone_signed_in(&s->phone)){
  r=home_bots_signed(s,now);
  if(s->bots.data.valid&&s->helper.bot<s->bots.data.count&&s->bots.data.b[s->helper.bot].reason==BR_PHONE)s->bots.refresh=true;
 }else if(!s->connected&&(s->saved||s->hotspot_saved))r=BR_LOADING;     /* joining Wi-Fi */
 else if(s->connected&&!s->phone.st.valid&&s->pair.state>=PAIR_ENROLLED_UNPAIRED)r=BR_LOADING;  /* sign-in status on its way */
 else r=phone_gate(&s->phone)?BR_PHONE:bots_block(&s->bots,s->helper.bot,now);
 s->helper.block=(unsigned char)r;
}
/* Apply a provider's phone status (worker, under the state lock). Becoming signed in to Hermes asks for
 * a fresh /v1/bots poll so per-bot reasons from the gated frame are replaced. A shared sign-in (flag 8)
 * that signs in or out asks the other provider's view to poll again, so one QR approval / one sign-out
 * shows on both providers; flag 8 is remembered for the boot and folds the two account tabs into one at
 * once (no frame of a hidden tab). Returns phone_apply's "new QR" flag. */
static inline bool home_provider_apply(home_ui*s,int tab,const phone_status*st,bool polled){
 phone_view*v=home_tab_phone(s,tab);
 if(!v)return false;
 bool was=phone_signed_in(v),shared=phone_shared(v)||(st->valid&&(st->flags&PHONE_FLAG_SHARED));
 bool qr=phone_apply(v,st,polled);
 bool now=phone_signed_in(v);
 if(tab==SETTINGS_HERMES&&!was&&now)s->bots.refresh=true;
 if(was!=now&&shared){phone_view*o=home_tab_phone(s,home_provider_other(tab));if(o)o->refresh=true;}
 if(st->valid&&(st->flags&PHONE_FLAG_SHARED))s->signin_shared=true;
 home_signin_fold(s);
 return qr;
}
/* The Hermes provider's status (Ask). */
static inline bool home_phone_apply(home_ui*s,const phone_status*st,bool polled){return home_provider_apply(s,SETTINGS_HERMES,st,polled);}
/* Set the toggle (true = reading Hermes sessions ON). Returns whether it
 * changed. OFF drops the live snapshot so markers/level vanish at once; the
 * live worker then stops polling entirely. */
static inline bool home_set_session(home_ui*s,bool on){
 if(!s->session_off==on)return false;
 s->session_off=!on;s->session_save=true;
 if(!on)memset(&s->live,0,sizeof s->live);
 return true;
}
/* ---- Settings: screen model shared by the renderer, the touch handler and the tests ---- */
static inline bool home_settings_page(const home_ui*s){return s->page==SETTINGS;}
static inline settings_screen home_settings_screen(const home_ui*s){
 if(s->settings_tab==SETTINGS_DISPLAY||s->settings_tab==SETTINGS_SOUND||s->settings_tab==SETTINGS_BATTERY)return SS_STATUS;
 if(s->settings_tab==SETTINGS_WIFI){if(!s->connected)return s->saved?SS_JOINING:SS_NO_WIFI;return SS_STATUS;}
 if(!s->connected)return SS_STATUS;
 /* Pairing with the bridge shows on whichever provider tab is open while the board is not paired. */
 if(s->pair.step==PV_ENROLLING)return SS_CODE;
 if(s->pair.state<PAIR_ENROLLED_UNPAIRED)return SS_FIND;
 const phone_view*v=home_tab_phone_c(s,s->settings_tab);
 if(!v)return SS_STATUS;
 if(phone_signed_in(v))return v->confirm_signout?SS_SIGNOUT:SS_STATUS;
 return v->showing?SS_QR:SS_STATUS;
}
static inline const char*settings_screen_name(settings_screen m){
 static const char*n[]={"no_wifi","joining","find","code","qr","status","signout"};return m<=SS_SIGNOUT?n[m]:"?";
}
/* Still finding out (a spinner, no buttons that could be wrong yet): Wi-Fi joining a saved network,
 * or a provider tab before the host has said whether this board is signed in there. */
static inline bool home_settings_loading(const home_ui*s){
 if(!s->connected)return s->settings_tab==SETTINGS_WIFI&&(s->saved||s->hotspot_saved);
 const phone_view*v=home_tab_phone_c(s,s->settings_tab);
 return v&&home_provider_signin(s->settings_tab)&&s->pair.state>=PAIR_ENROLLED_UNPAIRED&&!v->st.valid&&!v->showing&&!v->absent;
}
/* Up to three large targets per screen, in reading order (WPC1 5/6 press the first/second). */
static inline const char*settings_action_name(int a){
 static const char*n[]={"none","scan","pick0","pick1","cancel","retry","back","signin","signout_ask","signout_yes","signout_no","sound","sparkle","theme","accent"};
 return a>=0&&a<=SA_ACCENT?n[a]:"?";
}
static inline void settings_add(settings_buttons*b,int y,int h,int action,const char*label,const char*caption,bool primary){
 if(b->count>=SETTINGS_MAX_BUTTONS)return;
 settings_target*t=&b->t[b->count++];memset(t,0,sizeof *t);
 t->x=SET_X;t->y=y;t->w=SET_W;t->h=h;t->action=action;t->primary=primary;t->caption=caption;
 size_t n=strlen(label);if(n>SET_LABEL_LEN-1)n=SET_LABEL_LEN-1;memcpy(t->label,label,n);t->label[n]=0;
}
/* Main Settings rows (fixed positions so the screen never jumps). */
#define SET_ROW1_Y 72
#define SET_ROW2_Y 180
#define SET_STATUS_Y 292
static inline settings_buttons home_settings_buttons(const home_ui*s,int64_t now){
 settings_buttons b;memset(&b,0,sizeof b);
 /* Wi-Fi is status only: the networks come with the flash (.env -> tools/flash.sh -> USB). */
 if(s->settings_tab==SETTINGS_WIFI||s->settings_tab==SETTINGS_BATTERY)return b;  /* status pages: no targets */
 switch(home_settings_screen(s)){
  case SS_FIND:
   if(s->pair.step==PV_SCANNING)break;
   if(s->pair.step==PV_LIST&&s->pair.list.count){
    for(int i=0;i<2&&i<s->pair.list.count;i++)settings_add(&b,i?SET_SLOT_B:SET_SLOT_A,SET_BTN_H,SA_PICK0+i,s->pair.list.items[i].name,"Use this host",i==0);
   }else settings_add(&b,SET_SLOT_B,SET_BTN_H,SA_SCAN,"Search again",NULL,true);
   break;
  case SS_CODE:settings_add(&b,SET_SLOT_B,SET_BTN_H,SA_CANCEL,"Cancel",NULL,false);break;
  case SS_QR:{
   const phone_view*v=home_tab_phone_c(s,s->settings_tab);
   if(!v)break;
   phone_state d=phone_display(v,now);
   bool waiting=(d==PH_PENDING&&v->qr_ok)||(d==PH_NONE&&!v->note[0]);
   if(d==PH_PENDING&&v->qr_ok){
    /* QR on screen: a compact (still >= 72x140) Back beside the user code, under the symbol. */
    settings_add(&b,PHONE_QR_ROW_Y,SET_BTN_H,SA_BACK,"Back",NULL,false);
    b.t[0].x=SET_X+SET_W-SET_QR_BACK_W;b.t[0].w=SET_QR_BACK_W;
    break;
   }
   if(!waiting)settings_add(&b,SET_SLOT_A,SET_BTN_H,SA_RETRY,"Try again",NULL,true);
   settings_add(&b,SET_SLOT_B,SET_BTN_H,SA_BACK,"Back",NULL,false);
   break;}
  case SS_STATUS:{
   /* Row 1 = the provider's sign-in. No target until the bridge has said whether this board is signed
    * in (avoids a stray "Sign in" for the first second after boot); the row then shows a status line. */
   if(s->settings_tab==SETTINGS_DISPLAY){
    /* Colour theme (theme.h): Theme toggles Light / Dark, Accent steps to the next colour (wrapping).
     * Both work without Wi-Fi; the swatch in the switch slot shows the choice. */
    settings_add(&b,SET_ROW1_Y,SET_ROW_H,SA_THEME,theme_mode_name(s->theme_mode),"Theme",false);
    b.t[b.count-1].swatch=SET_SWATCH_THEME;b.t[b.count-1].on=s->theme_mode==THEME_DARK;
    settings_add(&b,SET_ROW2_Y,SET_ROW_H,SA_ACCENT,theme_accent_name(s->accent),"Accent",false);
    b.t[b.count-1].swatch=SET_SWATCH_ACCENT;
   }else if(s->settings_tab==SETTINGS_SOUND){
#if WAVESHARE_AI_PLUGIN_AI
    settings_add(&b,SET_ROW1_Y,SET_ROW_H,SA_SOUND,s->sound_off?"Off":"On","Completion sound",false);
    b.t[b.count-1].toggle=true;b.t[b.count-1].on=!s->sound_off;
#endif
#if WAVESHARE_AI_PLUGIN_SPARKLES
    /* Session sparkle follows Hermes sessions through the host: no toggle without Wi-Fi. */
    if(s->connected){
     settings_add(&b,b.count?SET_ROW2_Y:SET_ROW1_Y,SET_ROW_H,SA_SPARKLE,s->session_off?"Off":"On","Session sparkle",false);
     b.t[b.count-1].toggle=true;b.t[b.count-1].on=!s->session_off;
    }
#endif
   }else if(home_provider_tab(s->settings_tab)){
    /* Each provider's own phone sign-in (the phone scans its QR); none when its provider is not set up
     * on the bridge, or when none of its tiles needs one. */
    const phone_view*v=home_tab_phone_c(s,s->settings_tab);
    if(!v||v->absent||!home_provider_signin(s->settings_tab))break;
    bool ha=s->settings_tab==SETTINGS_HOME_ASSISTANT;
    if(phone_signed_in(v))settings_add(&b,SET_ROW1_Y,SET_ROW_H,SA_SIGNOUT_ASK,v->st.name[0]?v->st.name:"your phone",ha?WAVESHARE_AI_HOME_ASSISTANT_SIGNED_IN:WAVESHARE_AI_HERMES_SIGNED_IN,false);
    else if(v->st.valid)settings_add(&b,SET_ROW1_Y,SET_ROW_H,SA_SIGNIN,"Scan to sign in",ha?WAVESHARE_AI_HOME_ASSISTANT_SIGNED_OUT:WAVESHARE_AI_HERMES_SIGNED_OUT,true);
   }
   break;}
  case SS_SIGNOUT:
   settings_add(&b,SET_SLOT_A,SET_BTN_H,SA_SIGNOUT_YES,"Yes, sign out",NULL,true);
   settings_add(&b,SET_SLOT_B,SET_BTN_H,SA_SIGNOUT_NO,"Keep signed in",NULL,false);break;
  default:break;
 }
 return b;
}
/* Which target a point hits (index, or -1). */
static inline int home_settings_hit(const settings_buttons*b,int x,int y){
 for(int k=0;k<b->count;k++){const settings_target*t=&b->t[k];if(x>=t->x&&x<t->x+t->w&&y>=t->y&&y<t->y+t->h)return k;}
 return -1;
}
static inline int home_settings_find(const settings_buttons*b,int action){for(int k=0;k<b->count;k++)if(b->t[k].action==action)return k;return -1;}
/* A target's action. The sign-in actions (SA_SIGNIN ... SA_SIGNOUT_NO) act on the open provider tab's
 * own sign-in view, whichever provider it is. */
static inline bool home_settings_do(home_ui*s,int action){
 phone_view*v=home_tab_phone(s,s->settings_tab);
 switch(action){
  case SA_SCAN:pair_request_scan(&s->pair);return true;
  case SA_PICK0:case SA_PICK1:return pair_request_enroll(&s->pair,action-SA_PICK0);
  case SA_CANCEL:s->pair.want_cancel=true;return true;
  case SA_RETRY:case SA_SIGNIN:if(!v)return false;home_phone_start(s,s->settings_tab);return true;
  case SA_BACK:if(!v)return false;v->showing=false;s->auto_qr=true;return true;  /* stays closed for this visit */
  case SA_SIGNOUT_ASK:if(!v)return false;v->confirm_signout=true;return true;
  case SA_SIGNOUT_YES:if(!v)return false;v->want_forget=true;v->confirm_signout=false;
   /* shared: the bridge signs out both providers; re-poll the other one at once too */
   if(home_signin_shared(s)){phone_view*o=home_tab_phone(s,home_provider_other(s->settings_tab));if(o)o->refresh=true;}
   return true;
  case SA_SIGNOUT_NO:if(!v)return false;v->confirm_signout=false;return true;
  case SA_SOUND:s->sound_off=!s->sound_off;s->sound_save=true;return true;
  case SA_SPARKLE:return home_set_session(s,s->session_off);
  case SA_THEME:s->theme_mode=s->theme_mode==THEME_DARK?THEME_LIGHT:THEME_DARK;s->accent=(uint8_t)theme_accent_clamp(s->accent);s->theme_save=true;return true;
  case SA_ACCENT:s->accent=(uint8_t)((theme_accent_clamp(s->accent)+1)%THEME_ACCENTS);s->theme_mode=(uint8_t)theme_mode_clamp(s->theme_mode);s->theme_save=true;return true;
  default:return false;
 }
}
/* Press target k (0-based) of the current Settings screen: the WPC1 USB test hook (5 = first, 6 = second). */
static inline bool home_settings_press(home_ui*s,int k){
 settings_buttons b=home_settings_buttons(s,s->live_now_us?s->live_now_us:s->input.stamp_us);
 if(k<0||k>=b.count)return false;
 return home_settings_do(s,b.t[k].action);
}
/* The provider whose QR opens by itself on a Settings visit: the first (tab order) whose status says
 * the board must sign in and is not signed in (not after a refusal: that account is known and not
 * allowed; the tab offers "Scan to sign in" instead). -1 = none; *known = a provider has answered "not
 * signed in" (the visit's decision is made; signed-in providers leave it open, as a sign-out may follow). */
static inline int home_settings_auto_qr(const home_ui*s,bool*known){
 *known=false;
 for(int k=SETTINGS_HERMES;k<=SETTINGS_HOME_ASSISTANT;k++){
  const phone_view*v=home_tab_phone_c(s,k);
  if(!v||!home_provider_signin(k)||home_signin_tab(s,k)!=k||!v->st.valid||phone_signed_in(v))continue;  /* shared: the Hermes QR only */
  *known=true;
  if(phone_gate(v)&&!v->showing&&v->st.state!=PH_REFUSED)return k;
 }
 return -1;
}
/* Every 10 ms sample: the Settings screen, and on
 * each visit search for the bridge once / open a phone QR once when the bridge requires it. */
static inline void home_settings_tick(home_ui*s){
 if(!home_settings_page(s)){s->auto_scan=s->auto_qr=false;return;}
 if(!home_settings_tab_enabled(s->settings_tab))s->settings_tab=SETTINGS_WIFI;
 if(!s->connected){
  /* No Wi-Fi: nothing that needs a provider (sign-in QR, sign-out, host search) is shown. */
  home_phone_close(s);
  if(!home_settings_tab_reachable(s,s->settings_tab))s->settings_tab=SETTINGS_WIFI;
  return;
 }
 home_signin_fold(s);  /* shared sign-in: the one account tab */
 /* An open QR / sign-out question keeps (or brings back) its own provider's tab. */
 {const phone_view*o=home_tab_phone_c(s,s->settings_tab);
  if(!o||!(o->showing||o->confirm_signout))
   for(int k=SETTINGS_HERMES;k<=SETTINGS_HOME_ASSISTANT;k++){const phone_view*v=home_tab_phone_c(s,k);if(v&&(v->showing||v->confirm_signout)){s->settings_tab=(settings_page)k;break;}}}
 if(s->pair.state<PAIR_ENROLLED_UNPAIRED){
  if(!home_provider_tab(s->settings_tab))return;
  if(!s->auto_scan&&s->pair.step==PV_IDLE)pair_request_scan(&s->pair);
  s->auto_scan=true;return;
 }
 for(int k=SETTINGS_HERMES;k<=SETTINGS_HOME_ASSISTANT;k++){
  phone_view*v=home_tab_phone(s,k);if(!v)continue;
  if(phone_signed_in(v))v->showing=false;    /* approved: the QR closes */
  else v->confirm_signout=false;
 }
 if(!s->auto_qr){
  bool known;int k=home_settings_auto_qr(s,&known);
  if(k>=0){s->settings_tab=(settings_page)k;home_phone_start(s,k);}
  if(known)s->auto_qr=true;
 }
}
static inline bool home_settings_has_keyboard(const home_ui*s){(void)s;return false;}
/* Sparkles flick: short, fast and clearly horizontal, so slow drags keep drawing sparkles. */
#define SPARKLE_FLICK_PX 80
#define SPARKLE_FLICK_US 450000
static inline const char*sparkle_style_name(int style){
 static const char*n[SP_STYLES]={"Sea","Sunset"};return n[sp_style_clamp(style)];
}
/* Switch to style (wrapping), crossfading from the current one at the scene clock. It lasts until you
 * leave the page; nothing is saved (SPEC3 Contract S). */
static inline void home_sparkle_style(home_ui*s,int style){
 sparkles_state*c=&s->input.scene;
 style=((style%SP_STYLES)+SP_STYLES)%SP_STYLES;
 c->style_from=c->style;c->style=style;c->style_at=c->time;
}
static inline void home_sparkle_swipe(home_ui*s,int dir){home_sparkle_style(s,s->input.scene.style+(dir<0?1:-1));}
/* Entering the Sparkles page by any path (its tile, USB WLV1, any page switch) starts on the default
 * style 0 (Sea): no crossfade, no style label. home_sample checks before and after every sample; the USB
 * page select (home_wifi.c) calls it as it switches the page, so even the first frame is Sea. */
static inline void home_sparkle_entry(home_ui*s){
 bool open=s->page==SPARKLES;
 if(open&&!s->sparkle_open){sparkles_state*c=&s->input.scene;c->style=c->style_from=0;c->style_at=0;}
 s->sparkle_open=open;
}
/* A closed tile opens its provider's Settings tab: that provider's QR at once when signed out there,
 * its status otherwise (signed in with Sign out; refused = "not for this account" with Scan to sign in;
 * or "Not set up on ..."). Without Wi-Fi: the Wi-Fi tab. */
static inline void home_open_settings(home_ui*s);
static inline void home_open_provider(home_ui*s,int tab){
 if(!s->connected){home_open_settings(s);s->settings_tab=SETTINGS_WIFI;return;}  /* Wi-Fi first */
 tab=home_signin_tab(s,tab);  /* shared sign-in: the one account tab (the Sensor fix too) */
 if(!home_provider_tab(tab)||!home_settings_tab_enabled(tab)){home_open_settings(s);return;}
 s->page=SETTINGS;s->settings_tab=(settings_page)tab;s->auto_qr=true;
 if(s->pair.state<PAIR_ENROLLED_UNPAIRED)return;  /* no host yet: the provider tab searches for it */
 const phone_view*v=home_tab_phone_c(s,tab);
 if(v&&!v->absent&&home_provider_signin(tab)&&!phone_signed_in(v)&&!(v->st.valid&&v->st.state==PH_REFUSED))home_phone_start(s,tab);
}
/* The fix for the current Home tile (Ask: Hermes; Sensor: Home Assistant). */
static inline void home_open_signin(home_ui*s){
 int t=s->tile<0?0:(s->tile>=HOME_TILES?HOME_TILES-1:s->tile);
 int tab=home_tile_tab(t);
 if(tab<0||!home_settings_tab_enabled(tab))tab=home_settings_tab_enabled(SETTINGS_HERMES)?SETTINGS_HERMES:SETTINGS_HOME_ASSISTANT;
 home_open_provider(s,tab);
}
/* A tapped Settings visit. An unconfigured board opens on the Wi-Fi tab, which says how to add Wi-Fi
 * (it comes with the flash); nothing starts on the board. */
static inline void home_open_settings(home_ui*s){
 s->page=SETTINGS;
 if(home_wifi_unconfigured(s))s->settings_tab=SETTINGS_WIFI;
}
/* ---- Motion (SPEC2 Contract M, W14): the Home pull, page slides and the carousel ----
 * Every position is a function of the time since its slide started, never a per-sample step: a slow
 * poll or a dropped frame changes how often things are drawn, not where they are.
 * The Home pull (W14): on any page but Home, a touch that starts
 * in the reserved bottom edge (y >= HOME_PULL_ZONE_Y) and, at any time during that contact, travels
 * >= HOME_PULL_DECIDE_PX UP with up > 1.5|dx|. Held, a circle fixed at panel centre reveals real Home
 * and expands with upward distance (home_blob_pull); released far or fast enough it finishes covering
 * the panel within 240 ms. Otherwise it contracts to zero and restores the original page within 220 ms.
 * HOME_PULL_ZONE_Y 420 is the existing rounded-panel safe strip. It reserves taps, holds, sideways
 * and downward drags without sending them to page controls. The former lower-third zone is page-owned:
 *   Ask: the welcome Kotaro's hold box ends at y 291 (HELPER_BOT_Y 208 + 68 x 1.1 + 8 slop) and the chat
 *     bubbles at 298, so holds on the big Kotaro, chat scrolling and the pull-up are above the zone; the
 *     chat layout's compact Kotaro (y 322..418, hold-to-talk and Stop), the link button (374) and the
 *     status row (306) are in it and still get taps and holds (a hold arms at the original down time).
 *   Sparkles: the lower third paints with still, sideways and upward strokes; only a stroke that starts
 *     there and heads DOWN is the pull. Settings: rows 1/2 end at y 276, SET_SLOT_B buttons (336..412)
 *     take taps. Sensor: the lower tiles (from y ~230) take taps. Higher (~250) would put the chat
 *     bubbles and the welcome Kotaro in the zone; lower (~360) leaves the finger < 88 px to travel. */
#define HOME_PULL_ZONE_Y 420
#define HOME_PULL_CENTER_Y 224
#define HOME_PULL_DECIDE_PX 12     /* net travel that promotes a reserved zone touch (= HELPER_ARM_SLOP) */
#define HOME_PULL_FLICK_SPEED .5f  /* upward speed magnitude (px/ms over HOME_SPEED_US) */
#define HOME_PULL_FLICK_PX 24
#define HOME_BLOB_N0 2.f           /* the reveal is circular from its first active frame */
#define HOME_REVEAL_RADIUS 291.f    /* panel-centre to far pixel corner, plus AA coverage */
#define HOME_REVEAL_DIAMETER (2.f*HOME_REVEAL_RADIUS)
#define HOME_EXIT_FINISH_MS 240     /* a partial flick finishes at centre within this bound */
#define HOME_SPRING_MS 220         /* spring back: 120..220 ms by the travel, slight overshoot */
#define HOME_SPRING_MIN_MS 120
#define HOME_SPRING_BACK .8f       /* back-ease constant: ~2 % overshoot */
#define HOME_SPEED_US 80000        /* release speeds: the last 80 ms of the touch */
#define HOME_OPEN_MS 220           /* a tile's page slides up over Home: 220 ms, cubic ease-out */
#define HOME_RUBBER .35f           /* carousel: past the first / last tile the strip moves 0.35x */
#define HOME_FLICK_SPEED .35f      /* a flick (px/ms over HOME_SPEED_US) ... */
#define HOME_FLICK_PX 24           /* ... this long moves one tile in its direction */
#define HOME_SETTLE_MIN_MS 120     /* the settle lasts 120..300 ms by the distance left */
#define HOME_SETTLE_MAX_MS 300
static inline int home_round(float v){return (int)(v<0?v-.5f:v+.5f);}
static inline void home_track_add(home_track*k,int64_t now,int x,int y,int v){
 k->t[k->head]=now;k->x[k->head]=(short)x;k->y[k->head]=(short)y;k->v[k->head]=(short)v;
 k->head=(unsigned char)((k->head+1)%HOME_TRACK);if(k->n<HOME_TRACK)k->n++;
}
static inline float home_track_at(const home_track*k,int i,int axis){return axis==0?k->x[i]:(axis==1?k->y[i]:k->v[i]);}
/* Speed in px/ms (axis 0 = finger x, 1 = finger y, 2 = the layer it moves) over the last HOME_SPEED_US
 * before the newest sample. The position that long before is interpolated between the samples around
 * it, so uneven sample gaps measure the same speed; a younger touch uses its first sample. */
static inline float home_track_speed(const home_track*k,int axis){
 if(k->n<2)return 0;
 int last=(k->head+HOME_TRACK-1)%HOME_TRACK,i=last;
 int64_t t1=k->t[last],want=t1-HOME_SPEED_US;
 float p1=home_track_at(k,last,axis);
 for(int c=1;c<k->n;c++){
  int j=(i+HOME_TRACK-1)%HOME_TRACK;
  if(k->t[j]<=want){
   float span=(float)(k->t[i]-k->t[j]),f=span>0?(float)(want-k->t[j])/span:1.f;
   float p0=home_track_at(k,j,axis)+(home_track_at(k,i,axis)-home_track_at(k,j,axis))*f;
   return (p1-p0)*1000.f/HOME_SPEED_US;
  }
  i=j;
 }
 int64_t dt=t1-k->t[i];
 return dt>0?(p1-home_track_at(k,i,axis))*1000.f/(float)dt:0;
}
/* A settle's distance left at u (0..1 of its length): it starts d px away moving at vt/length px/ms
 * and comes to rest at 0 (cubic Hermite). With vt = -3d this is exactly the cubic ease-out d(1-u)^3. */
static inline float home_settle_at(float d,float vt,float u){return d*(2*u*u*u-3*u*u+1)+vt*(u*u*u-2*u*u+u);}
/* Carousel strip offset from tile `tile`'s rest (px, + = toward the previous tile) for a raw finger
 * offset: 1:1 between the first and the last tile, HOME_RUBBER past either end; and back. */
static inline int home_rubber(int tile,float raw){
 float p=raw-tile*368.f,lo=-(HOME_TILES-1)*368.f;
 if(p>0)p*=HOME_RUBBER;
 else if(p<lo)p=lo+(p-lo)*HOME_RUBBER;
 return home_round(p)+tile*368;
}
static inline float home_unrubber(int tile,int offset){
 float p=offset-tile*368.f,lo=-(HOME_TILES-1)*368.f;
 if(p>0)p/=HOME_RUBBER;
 else if(p<lo)p=lo+(p-lo)/HOME_RUBBER;
 return p+tile*368.f;
}
/* Parallax: a tile's art moves 0.8x of its paper (x = the paper's offset; at rest both are at 0). */
static inline int home_tile_art_x(int x){return x*4/5;}
/* The carousel settles from drag_offset to 0, starting at the strip's speed v (px/ms). Its length grows
 * with the distance (120..300 ms); moving toward the tile it arrives at the finger's pace (a shorter
 * settle, never under 120 ms), and it never starts faster than a cubic ease-out (no overshoot). */
static inline void home_settle_start(home_ui*s,int64_t now,float v){
 int d=s->drag_offset;
 s->home_slide_from=d;s->home_slide_us=now;s->home_slide_vt=0;s->home_slide_len=0;
 if(!d)return;
 float ad=d<0?-(float)d:(float)d,ms=HOME_SETTLE_MIN_MS+(HOME_SETTLE_MAX_MS-HOME_SETTLE_MIN_MS)*ad/368.f;
 if(ms>HOME_SETTLE_MAX_MS)ms=HOME_SETTLE_MAX_MS;
 float av=v<0?-v:v;
 if(v*d<0&&3*ad<av*ms)ms=3*ad/av;   /* moving toward the tile: arrive at the finger's pace */
 if(ms<HOME_SETTLE_MIN_MS)ms=HOME_SETTLE_MIN_MS;
 float vmax=3*ad/ms;
 if(v>vmax)v=vmax;
 if(v<-vmax)v=-vmax;
 s->home_slide_len=ms;s->home_slide_vt=v*ms;
}
static inline void home_settle_tick(home_ui*s,int64_t now){
 if(!s->home_slide_from)return;
 float u=s->home_slide_len>0?(now-s->home_slide_us)/(s->home_slide_len*1000.f):1.f;
 if(u<1){s->drag_offset=home_round(home_settle_at((float)s->home_slide_from,s->home_slide_vt,u));return;}
 s->drag_offset=0;s->home_slide_from=0;
 s->note.slide=true;s->note.from=s->home_slide_tile;s->note.to=s->tile;s->note.ms=(int)s->home_slide_len;
}
/* Release on Home: a flick moves one tile its way, anything else goes to the nearest tile. */
static inline void home_carousel_release(home_ui*s,int64_t now,bool flick){
 int dx=s->last_x-s->start_x,dy=s->last_y-s->start_y,to=s->tile;
 float vx=home_track_speed(&s->track,0);
 if(flick&&(vx>=HOME_FLICK_SPEED||vx<=-HOME_FLICK_SPEED)&&abs(dx)>=HOME_FLICK_PX&&abs(dx)>abs(dy)&&vx*dx>0)to+=dx<0?1:-1;
 else to-=home_round(s->drag_offset/368.f);
 if(to<0)to=0;
 if(to>HOME_TILES-1)to=HOME_TILES-1;
 s->home_slide_tile=s->tile;s->drag_offset+=(to-s->tile)*368;s->tile=to;
 home_settle_start(s,now,home_track_speed(&s->track,2));
}
/* The page layer: start a slide of `kind` from page_y, `ms` long, vt = start speed x ms (px). */
static inline void home_slide_begin(home_ui*s,int kind,int64_t now,float ms,float vt){
 s->slide_kind=(unsigned char)kind;s->slide_us=now;s->slide_y0=s->page_y;s->slide_len=ms;s->slide_vt=vt;
}
/* A slide belongs to the page shown: out = Home showing, the others = their own page. A page switched
 * under a slide (USB, a plugin page that is not built) drops it at the next sample; until then it is
 * neither drawn nor counted as motion. */
static inline bool home_slide_valid(const home_ui*s){
 int k=s->slide_kind;
 return k!=HOME_MOTION_NONE&&(k==HOME_MOTION_OUT?s->page==HOME:s->page==s->slide_page);
}
/* The page layer's offset while a tile opens (the only translated slide). */
static inline int home_page_offset(const home_ui*s){return home_slide_valid(s)&&s->slide_kind==HOME_MOTION_OPEN?s->page_y:0;}
/* The Home pull shows the page as a blob: held, springing back or dropping. */
static inline bool home_blob_shown(const home_ui*s){
 int k=s->slide_kind;
 return home_slide_valid(s)&&(k==HOME_MOTION_DRAG||k==HOME_MOTION_BACK||k==HOME_MOTION_OUT);
}
/* Nothing of the page layer moved: a frame is the plain page (a snapshot source, direct_main.c). */
static inline bool home_layer_rest(const home_ui*s){return !home_page_offset(s)&&!home_blob_shown(s);}
/* The held pull is travel-only. A circular aperture stays at the physical panel centre and expands
 * from zero to beyond every pixel corner. Only upward distance changes it: x motion and time held do
 * not, and reversing to the same y reproduces the exact same geometry. */
static inline home_blob home_blob_pull(int ax,int ay,float f,float dx){
 (void)ax;(void)dx;
 float span=(float)(ay-HOME_PULL_CENTER_Y-HOME_PULL_DECIDE_PX);
 float u=span>0.f?(f-(float)HOME_PULL_DECIDE_PX)/span:1.f;
 if(u<0.f)u=0.f;
 if(u>1.f)u=1.f;
 float q=ui_ease_quad_in_out(u,0.f,1.f,1.f);
 home_blob b={184.f,224.f,HOME_REVEAL_DIAMETER*q,HOME_REVEAL_DIAMETER*q,2.f,q};
 if(u<=0.f)b.w=b.h=b.alpha=0.f;
 if(u>=1.f){b.w=b.h=HOME_REVEAL_DIAMETER;b.alpha=1.f;}
 return b;
}
/* Continue the same distance curve. Hermite carries the measured release velocity into the timed
 * expansion and reaches diagonal corner cover with zero terminal speed. */
static inline home_blob home_blob_finish(const home_ui*s,float t){
 float len=s->slide_len,u=len>0.f?t/len:1.f;
 if(u<0)u=0;
 if(u>1)u=1;
 float target=(float)(s->blob_ay-HOME_PULL_CENTER_Y);
 float f=target+home_settle_at(s->blob_f0-target,s->blob_vf*len,u);
 float dx=home_settle_at(s->blob_dx0,s->blob_vx*len,u);
 home_blob b=home_blob_pull(s->blob_ax,s->blob_ay,f,dx);
 /* Cubic settling can round travel/easing to its exact float endpoint one or more samples early.
  * Keep an active unfinished reveal one ULP interior; the exact terminal sample remains unchanged. */
 if(t<len&&s->blob_from.w<HOME_REVEAL_DIAMETER){
  if(b.w>=HOME_REVEAL_DIAMETER)b.w=b.h=nextafterf(HOME_REVEAL_DIAMETER,0.f);
  if(b.alpha>=1.f)b.alpha=nextafterf(1.f,0.f);
 }
 return b;
}
/* The Hermite-to-endpoint curve in its equivalent convex cubic-Bezier form. With bounded control
 * points this evaluation also avoids a tiny negative radius from cancellation near u=1. */
static inline float home_blob_settle(float from,float velocity,float target,float len,float u){
 if(u<=0.f)return from;
 if(u>=1.f)return target;
 if(from==target&&velocity==0.f)return target;
 float one=1.f-u,p1=from+velocity*len/3.f;
 return one*one*one*from+3.f*one*one*u*p1+u*u*(3.f*one+u)*target;
}
/* Return every visible field from its exact held value to the zero aperture. Field-space Hermite
 * carries the measured finger tangent through release and reaches the endpoint only at completion,
 * with zero endpoint tangent and no early outgoing-only plateau. */
static inline home_blob home_blob_back(const home_ui*s,float t){
 float len=s->slide_len,u=len>0.f?t/len:1.f;
 if(u<0)u=0;
 if(u>1)u=1;
 const float dt=.25f;
 home_blob target=home_blob_pull(s->blob_ax,s->blob_ay,(float)HOME_PULL_DECIDE_PX,0.f);
 home_blob before=home_blob_pull(s->blob_ax,s->blob_ay,s->blob_f0-s->blob_vf*dt,s->blob_dx0-s->blob_vx*dt);
 home_blob after=home_blob_pull(s->blob_ax,s->blob_ay,s->blob_f0+s->blob_vf*dt,s->blob_dx0+s->blob_vx*dt);
 home_blob out;
#define HOME_BACK_FIELD(field) out.field=home_blob_settle(s->blob_from.field,(after.field-before.field)*.5f/dt,target.field,len,u)
 HOME_BACK_FIELD(cx);HOME_BACK_FIELD(cy);HOME_BACK_FIELD(w);HOME_BACK_FIELD(h);HOME_BACK_FIELD(n);HOME_BACK_FIELD(alpha);
#undef HOME_BACK_FIELD
 return out;
}
/* A Hermite field is the cubic Bezier [from, from + velocity*len/3, target, target]. Shorten only
 * the time interval when that velocity control point would cross the target or a physical field
 * bound. The measured release tangent is unchanged; the endpoint remains exact with zero tangent. */
static inline float home_blob_back_len(const home_ui*s,float len){
 const float dt=.25f;
 home_blob target=home_blob_pull(s->blob_ax,s->blob_ay,(float)HOME_PULL_DECIDE_PX,0.f);
 home_blob before=home_blob_pull(s->blob_ax,s->blob_ay,s->blob_f0-s->blob_vf*dt,s->blob_dx0-s->blob_vx*dt);
 home_blob after=home_blob_pull(s->blob_ax,s->blob_ay,s->blob_f0+s->blob_vf*dt,s->blob_dx0+s->blob_vx*dt);
#define HOME_BACK_LIMIT(field,low,high) do { \
  float v=(after.field-before.field)*.5f/dt; \
  if(fabsf(v)>1e-6f){ \
   float delta=target.field-s->blob_from.field; \
   float room=v*delta>0.f?fabsf(delta):(v>0.f?(high)-s->blob_from.field:s->blob_from.field-(low)); \
   if(room>0.f){float safe=2.9f*room/fabsf(v);if(safe<len)len=safe;} \
  } \
 } while(0)
 HOME_BACK_LIMIT(cx,0.f,368.f);HOME_BACK_LIMIT(cy,0.f,448.f);
 HOME_BACK_LIMIT(w,0.f,HOME_REVEAL_DIAMETER);HOME_BACK_LIMIT(h,0.f,HOME_REVEAL_DIAMETER);
 HOME_BACK_LIMIT(n,2.f,HOME_BLOB_N0);HOME_BACK_LIMIT(alpha,0.f,1.f);
#undef HOME_BACK_LIMIT
 return len;
}
/* Spring back, u = 0..1 of its length: 0 -> 1 with a back ease (~2 % overshoot past the page's place). */
static inline float home_spring_at(float u){
 if(u>=1)return 1.f;
 if(u<=0)return 0.f;
 float w=u-1.f,c=HOME_SPRING_BACK;
 return 1.f+(c+1.f)*w*w*w+c*w*w;
}
static inline void home_pull_end(home_ui*s,int64_t now){
 if(s->slide_kind==HOME_MOTION_OUT||s->slide_kind==HOME_MOTION_BACK){
  s->note.done=true;s->note.done_home=s->slide_kind==HOME_MOTION_OUT;s->note.done_ms=(int)((now-s->slide_us)/1000);
 }
 s->slide_kind=HOME_MOTION_NONE;s->page_y=0;
}
static inline void home_slide_tick(home_ui*s,int64_t now){
 int k=s->slide_kind;
 if(k==HOME_MOTION_NONE)return;
 if(!home_slide_valid(s)){s->slide_kind=HOME_MOTION_NONE;s->page_y=0;return;}
 if(k==HOME_MOTION_DRAG)return;
 float ms=(now-s->slide_us)/1000.f,u=s->slide_len>0?ms/s->slide_len:1.f,y0=(float)s->slide_y0;
 if(u>=1){
  if(k==HOME_MOTION_OUT)s->blob=home_blob_pull(s->blob_ax,s->blob_ay,(float)(s->blob_ay-HOME_PULL_CENTER_Y),0.f);
  else if(k==HOME_MOTION_BACK)s->blob=home_blob_pull(s->blob_ax,s->blob_ay,(float)HOME_PULL_DECIDE_PX,0.f);
  home_pull_end(s,now);return;
 }
 if(k==HOME_MOTION_OUT)s->blob=home_blob_finish(s,ms);
 else if(k==HOME_MOTION_BACK)s->blob=home_blob_back(s,ms);
 else s->page_y=home_round(home_settle_at(y0,s->slide_vt,u));  /* open: cubic ease-out to 0 */
}
/* Something moves (repaint every frame); a finger holding the page still is not motion. */
static inline bool home_motion_moving(const home_ui*s){
 return (s->page==HOME&&s->home_slide_from)||(home_slide_valid(s)&&s->slide_kind!=HOME_MOTION_DRAG);
}
/* A touch that started in the pull zone (down samples are retained up to HOME_PULL_BUF). It is the
 * Home pull once its net travel reaches HOME_PULL_DECIDE_PX up with up > 1.5|dx| (edge, the blob
 * starts: a new motion id, so a new snapshot). Time, retained-sample capacity and earlier direction
 * do not end eligibility; an unqualified lift is consumed by the reserved strip. */
static inline bool home_pull_classify(home_ui*s,int64_t now,bool down,int x,int y){
 if(down&&s->pull_n<HOME_PULL_BUF){int k=s->pull_n++;s->pull_t[k]=now;s->pull_x[k]=(short)x;s->pull_y[k]=(short)y;}
 int dx=x-s->start_x,dy=y-s->start_y,up=-dy;
 bool same=s->page==s->pull_page;
 if(down&&same&&!s->consumed){
  if(up>=HOME_PULL_DECIDE_PX&&2*up>3*abs(dx)){
   s->pull_wait=false;s->edge=true;
   s->slide_kind=HOME_MOTION_DRAG;s->slide_page=s->page;s->motion_id++;s->page_y=0;
   s->pull_us=now;s->blob_ax=(short)s->start_x;s->blob_ay=(short)s->start_y;
   s->note.pull=true;s->note.wait_ms=(int)((now-s->start_us)/1000);
  }
  return false;
 }
 s->pull_wait=false;
 s->consumed=true;
 return false;
}
/* Release of the Home pull: at centre, or >= HOME_PULL_FLICK_SPEED px/ms up (last
 * 80 ms) after >= HOME_PULL_FLICK_PX = Home, live at once while the circular reveal finishes expanding;
 * otherwise it restores the page. A touch that broke off (consumed) always
 * springs back. The note: offset = downward travel, drag_ms, anim_ms. */
static inline void home_pull_release(home_ui*s,int64_t now){
 int up=s->start_y-s->last_y,dx=s->last_x-s->start_x;
 float v=-home_track_speed(&s->track,1);   /* upward speed magnitude, px/ms */
 bool centre=s->last_y<=HOME_PULL_CENTER_Y;
 bool go=!s->consumed&&(centre||(up>=HOME_PULL_FLICK_PX&&v>=HOME_PULL_FLICK_SPEED));
 s->note.gesture=true;s->note.home=go;s->note.offset=up;s->note.speed_milli=home_round(v*1000);
 s->note.drag_ms=(int)((now-s->pull_us)/1000);
 s->blob_from=s->blob;
 if(go){
  if(s->page==HELPER)helper_leave(&s->helper);
  s->page=HOME;home_phone_close(s);s->pair.confirm_forget=false;
  if(s->blob.w<1.f||s->blob.h<1.f){
   s->slide_kind=HOME_MOTION_NONE;s->page_y=0;s->note.anim_ms=0;
   s->note.done=true;s->note.done_home=true;s->note.done_ms=0;
   return;
  }
  if(s->blob.w>=HOME_REVEAL_DIAMETER&&s->blob.h>=HOME_REVEAL_DIAMETER){
   s->blob_f0=(float)(s->blob_ay-HOME_PULL_CENTER_Y);s->blob_dx0=0.f;
   s->blob_vf=s->blob_vx=0.f;
   home_slide_begin(s,HOME_MOTION_OUT,now,1.f,0.f);s->note.anim_ms=1;
   return;
  }
  s->blob_f0=(float)up;s->blob_dx0=(float)dx;s->blob_vf=v;s->blob_vx=home_track_speed(&s->track,0);
  float remain=(float)(s->blob_ay-HOME_PULL_CENTER_Y)-s->blob_f0;
  float ms=HOME_EXIT_FINISH_MS;
  if(v>0.f&&3.f*remain/v<ms)ms=3.f*remain/v;
  if(ms<1.f)ms=1.f;
  home_slide_begin(s,HOME_MOTION_OUT,now,ms,0);
  s->note.anim_ms=(int)ms;
  return;
 }
 s->blob_f0=up>0?(float)up:0.f;s->blob_dx0=(float)dx;
 s->blob_vf=v;s->blob_vx=home_track_speed(&s->track,0);
 if(s->blob_f0<=(float)HOME_PULL_DECIDE_PX){
  s->blob=home_blob_pull(s->blob_ax,s->blob_ay,(float)HOME_PULL_DECIDE_PX,0.f);
  s->slide_kind=HOME_MOTION_NONE;s->page_y=0;s->note.anim_ms=0;
  s->note.done=true;s->note.done_home=false;s->note.done_ms=0;return;
 }
 float d=s->blob_f0>(float)abs(dx)?s->blob_f0:(float)abs(dx);
 if(d<=0){s->slide_kind=HOME_MOTION_NONE;s->page_y=0;s->note.anim_ms=0;return;}
 float ms=HOME_SPRING_MIN_MS+(HOME_SPRING_MS-HOME_SPRING_MIN_MS)*(d>100.f?1.f:d/100.f);
 ms=home_blob_back_len(s,ms);
 home_slide_begin(s,HOME_MOTION_BACK,now,ms,0);
 s->note.anim_ms=(int)ms;
}
static inline void home_tap(home_ui*s,int x,int y){
 if(s->page==HOME&&set_safe_px(x,y)){
  int t=s->tile<0?0:(s->tile>=HOME_TILES?HOME_TILES-1:s->tile);
  /* A closed (Hermes-required) tile opens the fix; LOADING opens the page, which shows its own loading. */
  if(home_tile_disabled(s,t)&&home_tile_state(s,t)==TILE_OFF){home_open_signin(s);return;}
  if(home_tiles[t].page==SETTINGS){home_open_settings(s);return;}
  if(home_page_enabled(home_tiles[t].page))s->page=home_tiles[t].page;
  return;
 }
 if(s->page==SENSORS){s->sensors.refresh=true;return;}  /* tap = read again now */
 if(!home_settings_page(s))return;
 settings_buttons b=home_settings_buttons(s,s->input.stamp_us);
 int hit=home_settings_hit(&b,x,y);
 if(hit>=0)home_settings_do(s,b.t[hit].action);
 else{phone_view*v=home_tab_phone(s,s->settings_tab);if(v)v->confirm_signout=false;}  /* a tap beside the buttons backs out of the sign-out question */
}
static inline void home_sample(home_ui*s,int64_t now,bool contact,int x,int y){
 if(now<s->input.stamp_us)return;
 if(!home_page_enabled(s->page)){s->page=HOME;s->consumed=s->down;}  /* page of a plugin not in this build */
 if(s->tile>=HOME_TILES)s->tile=HOME_TILES-1;
 home_sparkle_entry(s);  /* a page switch outside this poller (USB) into Sparkles: Sea */
 home_settings_tick(s);
 home_page entry=s->page;
 home_slide_tick(s,now);
 home_settle_tick(s,now);
 bool down=contact&&x>=0&&y>=0&&x<368&&y<448;
 if(contact&&!down)s->consumed=true;
 bool down_edge=down&&!s->down,up_edge=!down&&s->down;
 /* The panel reports no point on release (direct_main.c passes 0,0): a release happens where the
  * finger last was, so release-time page and button hit tests use that last position. */
 if(up_edge){x=s->last_x;y=s->last_y;}
 if(down_edge){
  s->start_x=x;s->start_y=y;s->start_us=now;s->max_move=0;s->caught=false;s->drag_base=0;s->track.n=0;
  /* A touch ends a page slide at once: the finger lands on the final layout (during a drop: Home). */
  if(s->slide_kind!=HOME_MOTION_NONE){s->slide_kind=HOME_MOTION_NONE;s->page_y=0;}
  /* The Home pull (W14): a touch that starts in the pull zone of any page but Home waits, unclassified, until home_pull_classify decides: the pull (it never
   * reaches the page: no tap, paint, scroll or button) or the page's (it gets every sample, at its own
   * time and place, preserving holds, taps, swipes and scrolls). */
  s->edge=false;s->pull_n=0;s->pull_page=s->page;
  s->pull_wait=s->page!=HOME&&y>=HOME_PULL_ZONE_Y;
  /* Home still settling: the finger catches the strip where it is (a drag, never a tap). */
  if(s->page==HOME&&s->home_slide_from){s->caught=true;s->home_slide_from=0;s->drag_base=home_round(home_unrubber(s->tile,s->drag_offset));}
  s->consumed=home_settings_page(s)&&!s->pull_wait&&!set_safe_px(x,y);s->settings_gesture_cancel=false;
 }
 /* replay: the waiting samples (all down, the current one last when down) go to the page now. */
 bool replay=s->pull_wait&&home_pull_classify(s,now,down,x,y);
 bool page_touch=!s->edge&&!s->consumed&&!s->pull_wait;
 if(s->page==HELPER){
  /* The page's replay keeps the original hold timing while Home classifies the contact. */
  helper_tick(&s->helper,now);
  home_bots_sync(s,now);
  if(replay)for(int i=0;i<s->pull_n;i++)helper_touch(&s->helper,s->pull_t[i],i==0,true,false,s->pull_x[i],s->pull_y[i]);
  if(page_touch&&!(replay&&down))helper_touch(&s->helper,now,down_edge,down,up_edge,x,y);
  else if(up_edge&&!page_touch){if(s->helper.holding)helper_release(&s->helper,now);s->helper.touching=s->helper.armed=s->helper.swiping=false;}
  home_bots_sync(s,now); /* a committed swipe shows the new bot's gate in the same frame */
 }
 if(down){
  s->last_x=x;s->last_y=y;
  int dx=x-s->start_x,dy=y-s->start_y;
  int move=abs(dx)>abs(dy)?abs(dx):abs(dy);if(move>s->max_move)s->max_move=move;
  /* Home: the strip follows the finger 1:1, with rubber-band resistance past the first/last tile. */
  if(s->page==HOME&&!s->consumed)s->drag_offset=home_rubber(s->tile,(float)(s->drag_base+dx));
  /* The Home pull, held: the centered circle follows upward travel and reverses from the same geometry. */
  if(s->edge&&!s->consumed&&s->slide_kind==HOME_MOTION_DRAG&&s->page==s->slide_page)
   s->blob=home_blob_pull(s->start_x,s->start_y,dy<0?(float)-dy:0.f,(float)dx);
  if(home_settings_page(s)&&page_touch&&abs(dx)>SETTINGS_CANCEL_PX)s->settings_gesture_cancel=true;
  home_track_add(&s->track,now,x,y,s->page==HOME?s->drag_offset:s->page_y);
 }
 /* The Home pull never leaks into palette or contact strength; a Sparkles touch that waited gets its
  * waiting points now (at this sample's time: the scene clock cannot go back). */
 if(replay&&s->page==SPARKLES)for(int i=0;i<s->pull_n-(down?1:0);i++)direct_sample(&s->input,now,true,(unsigned)s->pull_x[i],(unsigned)s->pull_y[i]);
 direct_sample(&s->input,now,down&&s->page==SPARKLES&&page_touch,(unsigned)x,(unsigned)y);
 /* The Home pull decides on release, even a broken-off touch (it springs back). */
 if(up_edge&&s->edge&&s->slide_kind==HOME_MOTION_DRAG)home_pull_release(s,now);
 if(up_edge&&!s->consumed&&!s->edge){
  int dx=s->last_x-s->start_x,dy=s->last_y-s->start_y;
  bool tap=s->max_move<=SETTINGS_CANCEL_PX&&now-s->start_us<=500000;
  if(s->page==HOME){
   home_carousel_release(s,now,true);
   if(tap&&!s->caught)home_tap(s,s->start_x,s->start_y);
  }
  else if(s->page==SPARKLES&&abs(dx)>=SPARKLE_FLICK_PX&&abs(dx)>abs(dy)*2&&now-s->start_us<=SPARKLE_FLICK_US)
   home_sparkle_swipe(s,dx);
  else if(home_settings_page(s)&&s->settings_gesture_cancel){
   if(abs(dx)>=SETTINGS_SWIPE_PX&&abs(dx)>abs(dy)*2){
    s->settings_tab=(settings_page)home_settings_next_tab(s,dx);
   }
  }else if(tap)home_tap(s,s->start_x,s->start_y);
 }else if(up_edge&&s->page==HOME&&!s->home_slide_from&&s->drag_offset)home_carousel_release(s,now,false);  /* broken off: nearest tile */
 /* A tile opened from Home: its page slides up over Home. */
 if(entry==HOME&&s->page!=HOME){s->page_y=448;s->slide_page=s->page;s->motion_id++;home_slide_begin(s,HOME_MOTION_OPEN,now,HOME_OPEN_MS,-3.f*448);}
 if(s->page!=HOME){s->drag_offset=0;s->home_slide_from=0;}
 home_sparkle_entry(s);  /* opened in this sample (its tile): Sea before the page is first drawn */
 s->down=down;
}
