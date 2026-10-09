/* Every Settings screen state, shared by tests/host/settings_ui_test.c (safe-area + tap checks) and
 * tests/preview/pair_preview.c (PPM previews -> tests/preview/settings_sheet.py). Host only. */
#pragma once
#include "home_render.h"
#define SETTINGS_URI "https://auth.example.com/consent/openid/device-authorization?user_code=QWRT7KXM"
#define SETTINGS_FP "479ad1dc62451c82f1cb229bf5bf629f" "1e2eb88b79d0307b6dcf1898a0cce5f7"
#define SETTINGS_STATES 54
/* Longest values the firmware can hold: 32-char SSID, 32-char bridge name, 32-char display name. */
#define SETTINGS_LONG_SSID "Home-Wi-Fi 5GHz Upstairs Office1"
#define SETTINGS_LONG_BRIDGE "Studio host in the far back room"
#define SETTINGS_LONG_NAME "Maria Fernanda Villanueva-Ortega"
static inline void settings_view(const home_ui*s,phone_view*v,unsigned state,unsigned flags,unsigned left,const char*code,const char*name,const char*uri){
 phone_status*st=&v->st;memset(st,0,sizeof *st);
 st->valid=true;st->state=(uint8_t)state;st->flags=(uint8_t)flags;st->expires_in=(uint16_t)left;st->received_us=s->live_now_us;
 snprintf(st->user_code,sizeof st->user_code,"%s",code);snprintf(st->name,sizeof st->name,"%s",name);snprintf(st->uri,sizeof st->uri,"%s",uri);
 if(state==PH_PENDING)phone_qr_encode(v,uri);else v->qr_ok=false;
}
/* The Hermes provider's sign-in (Ask) / the Home Assistant provider's own sign-in (Sensor). */
static inline void settings_phone(home_ui*s,unsigned state,unsigned flags,unsigned left,const char*code,const char*name,const char*uri){
 settings_view(s,&s->phone,state,flags,left,code,name,uri);
}
static inline void settings_ha(home_ui*s,unsigned state,unsigned flags,unsigned left,const char*code,const char*name,const char*uri){
 settings_view(s,&s->phone_ha,state,flags,left,code,name,uri);
}
/* Build state i (0..SETTINGS_STATES-1); returns its short name. Indices 0..15 are stable (tests use them);
 * 30.. are the provider states (the Home Assistant tab, shared sign-in, not set up on the bridge); 44.. the
 * Display tab (colour theme, theme.h) in Light and Dark with some accents, also without Wi-Fi; 49.. more of
 * the shared account tab (SPEC3 Contract S). */
static inline const char*settings_state(int i,home_ui*s){
 memset(s,0,sizeof *s);s->page=SETTINGS;s->live_now_us=100LL*1000*1000;s->input.stamp_us=s->live_now_us;
 s->settings_tab=SETTINGS_HERMES;
 s->saved=s->connected=true;strcpy(s->credentials.ssid,"Home-Wi-Fi 5GHz Upstairs"); /* long SSID: exercises wrapping */
 strcpy(s->status,"Connected to Wi-Fi");
 s->pair.state=PAIR_ENROLLED_UNPAIRED;s->pair.step=PV_IDLE;s->pair.live_http=200;strcpy(s->pair.bridge,"Studio host");
 strcpy(s->pair.base,"https://192.0.2.10:8098");pair_hex32(SETTINGS_FP,s->pair.fp);
 settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED,0,"","sam","");
 settings_ha(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED,0,"","sam","");
 switch(i){
  case 0:s->settings_tab=SETTINGS_WIFI;s->saved=s->connected=false;s->credentials.ssid[0]=0;s->status[0]=0;return "01-no-wifi";
  case 1:s->settings_tab=SETTINGS_WIFI;s->connected=false;strcpy(s->status,"Connecting saved network...");return "02-joining";
  case 2:s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_SCANNING;s->pair.bridge[0]=0;s->auto_scan=true;return "03-searching";
  case 3:s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_LIST;s->pair.bridge[0]=0;s->auto_scan=true;return "04-not-found";
  case 4:s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_LIST;s->pair.bridge[0]=0;s->auto_scan=true;
   pair_list_add(&s->pair.list,SETTINGS_LONG_BRIDGE,"192.0.2.10",8098,SETTINGS_FP);
   pair_list_add(&s->pair.list,"Office","192.0.2.11",8098,"00000000000000000000000000000000" "00000000000000000000000000000001");
   pair_list_add(&s->pair.list,"Third","192.0.2.12",8098,"00000000000000000000000000000000" "00000000000000000000000000000002");return "05-pick";
  case 5:s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_ENROLLING;s->pair.code_shown=false;return "06-securing";
  case 6:s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_ENROLLING;s->pair.code=895757;s->pair.code_shown=true;return "07-enroll-code";
  case 7:s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_ERROR;s->pair.bridge[0]=0;strcpy(s->pair.note,"Enrollment denied on the host");s->auto_scan=true;return "08-find-error";
  case 8:settings_phone(s,PH_NONE,PHONE_FLAG_REQUIRED,0,"","","");s->phone.showing=true;return "09-qr-getting";
  case 9:settings_phone(s,PH_PENDING,PHONE_FLAG_REQUIRED,599,"QWRT7KXM","",SETTINGS_URI);s->phone.showing=true;return "10-qr";
  case 10:settings_phone(s,PH_EXPIRED,PHONE_FLAG_REQUIRED,0,"","","");s->phone.showing=true;return "11-qr-expired";
  case 11:settings_phone(s,PH_REFUSED,PHONE_FLAG_REQUIRED,0,"","","");s->phone.showing=true;return "12-qr-refused";
  case 12:return "13-signed-in";
  case 13:s->phone.confirm_signout=true;return "14-signout-confirm";
  case 14:s->settings_tab=SETTINGS_SOUND;s->session_off=true;return "15-session-sparkle-off";
  case 15:s->pair.live_http=503;settings_phone(s,PH_NONE,PHONE_FLAG_REQUIRED,0,"","","");s->auto_qr=true;return "16-offline-not-signed-in";
  case 16:settings_phone(s,PH_DENIED,PHONE_FLAG_REQUIRED,0,"","","");s->phone.showing=true;return "17-qr-denied";
  case 17:settings_phone(s,PH_ERROR,PHONE_FLAG_REQUIRED,0,"","","");strcpy(s->phone.note,"Portal unreachable");s->phone.showing=true;return "18-qr-error";
  case 18:settings_phone(s,PH_NONE,PHONE_FLAG_REQUIRED,0,"","","");strcpy(s->phone.note,"Code lost - try again");s->phone.showing=true;return "19-qr-lost";
  case 19:s->phone.st.valid=false;return "20-no-status-yet";
  case 20:strcpy(s->credentials.ssid,SETTINGS_LONG_SSID);strcpy(s->pair.bridge,SETTINGS_LONG_BRIDGE);
   settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED,0,"",SETTINGS_LONG_NAME,"");return "21-long-names";
  case 21:s->settings_tab=SETTINGS_WIFI;return "22-wifi-page";
  case 22:s->settings_tab=SETTINGS_SOUND;return "23-sound-page";
  case 23:s->settings_tab=SETTINGS_SOUND;s->sound_off=s->session_off=true;return "24-sound-off";
  /* Home Wi-Fi first, iPhone hotspot fallback (user rule 2026-09-30); longest iPhone-style name. */
  case 24:s->settings_tab=SETTINGS_WIFI;s->hotspot_saved=s->on_hotspot=true;strcpy(s->hotspot_ssid,"Samantha Robinson's iPhone 15 P");
   strcpy(s->status,"Connected to iPhone hotspot");return "25-wifi-on-hotspot";
  case 25:s->settings_tab=SETTINGS_WIFI;s->hotspot_saved=true;strcpy(s->hotspot_ssid,"Sam's iPhone");return "26-wifi-home-hotspot-saved";
  case 26:s->settings_tab=SETTINGS_WIFI;s->saved=false;s->credentials.ssid[0]=0;s->hotspot_saved=true;s->connected=false;
   strcpy(s->hotspot_ssid,"Sam's iPhone");strcpy(s->status,"Connecting saved network...");return "27-wifi-hotspot-only-joining";
  /* Settings > Battery (battery_estimate.h): charging with a time to full, low on battery, no battery. */
  case 27:s->settings_tab=SETTINGS_BATTERY;s->battery=(battery_view){.known=true,.ok=true,.present=true,.vbus=true,.charging=true,.percent=58,.vbat_mv=3924,.eta_min=80};
   return "28-battery-charging";
  case 28:s->settings_tab=SETTINGS_BATTERY;s->battery=(battery_view){.known=true,.ok=true,.present=true,.percent=12,.vbat_mv=3561,.eta_min=-1};
   return "29-battery-low";
  case 29:s->settings_tab=SETTINGS_BATTERY;s->battery=(battery_view){.known=true,.ok=true,.vbus=true,.percent=-1,.vbat_mv=-1,.eta_min=-1};
   return "30-battery-missing";
  /* The Home Assistant provider's tab: its OWN sign-in view (phone_ha), titled
   * WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME, the same rows and screens as the Hermes tab. */
  case 30:s->settings_tab=SETTINGS_HOME_ASSISTANT;return "31-ha-signed-in";
  case 31:s->settings_tab=SETTINGS_HOME_ASSISTANT;settings_ha(s,PH_NONE,PHONE_FLAG_REQUIRED,0,"","","");s->auto_qr=true;return "32-ha-signed-out";
  case 32:s->settings_tab=SETTINGS_HOME_ASSISTANT;settings_ha(s,PH_PENDING,PHONE_FLAG_REQUIRED,599,"QWRT7KXM","",SETTINGS_URI);s->phone_ha.showing=true;return "33-ha-qr";
  case 33:s->settings_tab=SETTINGS_HOME_ASSISTANT;s->phone_ha.confirm_signout=true;return "34-ha-signout-confirm";
  /* Flag 8: one sign-in gateway shared by both providers (one QR signs in to both, sign-out signs out both).
   * SPEC3 Contract S: ONE account tab (the Hermes tab, its label as the title) covers both; the Home
   * Assistant tab is not reachable. Both providers carry flags 9. */
  case 34:settings_ha(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");
   settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");return "35-shared-signed-in";
  case 35:settings_ha(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");
   settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");
   s->phone.confirm_signout=true;return "36-shared-signout-confirm";
  /* 404 from the provider's status: not set up on the bridge (no buttons). */
  case 36:s->settings_tab=SETTINGS_HOME_ASSISTANT;phone_absent(&s->phone_ha);return "37-ha-not-set-up";
  case 37:s->settings_tab=SETTINGS_HOME_ASSISTANT;phone_absent(&s->phone_ha);strcpy(s->pair.bridge,SETTINGS_LONG_BRIDGE);return "38-ha-not-set-up-long-bridge";
  /* Only the Hermes answer has carried flag 8 so far: shared all the same. */
  case 38:s->settings_tab=SETTINGS_HERMES;settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");return "39-shared-hermes-flag";
  case 39:s->settings_tab=SETTINGS_HERMES;settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");
   s->phone.confirm_signout=true;return "40-shared-hermes-flag-signout-confirm";
  case 40:s->settings_tab=SETTINGS_HERMES;phone_absent(&s->phone);s->pair.bridge[0]=0;return "41-hermes-not-set-up";
  case 41:settings_ha(s,PH_REFUSED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");
   settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","sam","");return "42-shared-ha-refused";
  case 42:s->settings_tab=SETTINGS_HOME_ASSISTANT;settings_ha(s,PH_REFUSED,PHONE_FLAG_REQUIRED,0,"","","");s->phone_ha.showing=true;return "43-ha-qr-refused";
  /* Not paired with the bridge yet: the pairing screens show on whichever provider tab is open. */
  case 43:s->settings_tab=SETTINGS_HOME_ASSISTANT;s->pair.state=PAIR_NO_BRIDGE;s->pair.step=PV_LIST;s->pair.bridge[0]=0;s->auto_scan=true;
   pair_list_add(&s->pair.list,"Studio host","10.0.0.10",8098,SETTINGS_FP);return "44-ha-pick-mac";
  /* Settings > Display: Theme (Light / Dark) and Accent rows; the page itself follows the theme. */
  case 44:s->settings_tab=SETTINGS_DISPLAY;return "45-display-light";
  case 45:s->settings_tab=SETTINGS_DISPLAY;s->theme_mode=THEME_DARK;return "46-display-dark";
  case 46:s->settings_tab=SETTINGS_DISPLAY;s->theme_mode=THEME_DARK;s->accent=1;return "47-display-dark-blue";
  case 47:s->settings_tab=SETTINGS_DISPLAY;s->accent=4;return "48-display-light-purple";
  case 48:s->settings_tab=SETTINGS_DISPLAY;s->theme_mode=THEME_DARK;s->saved=s->connected=false;s->credentials.ssid[0]=0;s->status[0]=0;
   return "49-display-dark-no-wifi";
  /* The shared account tab: signed out, its QR (one flow, the Hermes one), the longest names with Home
   * Assistant refused, signed out with the longest bridge name, and before the Hermes answer. */
  case 49:settings_phone(s,PH_NONE,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","","");
   settings_ha(s,PH_NONE,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","","");s->auto_qr=true;return "50-shared-signed-out";
  case 50:settings_phone(s,PH_PENDING,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,599,"QWRT7KXM","",SETTINGS_URI);s->phone.showing=true;
   settings_ha(s,PH_NONE,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","","");return "51-shared-qr";
  case 51:strcpy(s->credentials.ssid,SETTINGS_LONG_SSID);strcpy(s->pair.bridge,SETTINGS_LONG_BRIDGE);
   settings_phone(s,PH_AUTHORIZED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"",SETTINGS_LONG_NAME,"");
   settings_ha(s,PH_REFUSED,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"",SETTINGS_LONG_NAME,"");return "52-shared-long-names-ha-refused";
  case 52:strcpy(s->pair.bridge,SETTINGS_LONG_BRIDGE);settings_phone(s,PH_NONE,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","","");
   settings_ha(s,PH_NONE,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","","");s->auto_qr=true;return "53-shared-signed-out-long-bridge";
  default:s->phone.st.valid=false;settings_ha(s,PH_NONE,PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED,0,"","","");return "54-shared-checking";
 }
}
