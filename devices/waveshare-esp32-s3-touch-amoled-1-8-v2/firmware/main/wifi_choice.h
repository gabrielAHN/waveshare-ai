#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Which saved network to join (user rule 2026-09-30): home Wi-Fi first, the iPhone Personal
 * Hotspot as the fallback, and back to home as soon as it is in range again (hotspot data is
 * metered). Pure logic: home_wifi.c feeds it disconnect reasons and link events.
 *
 * - Home not in range (reason 201 NO_AP_FOUND): switch at once.
 * - Home in range but failing (auth, timeouts): WIFI_HOME_TRIES attempts, then switch.
 * - Hotspot failing WIFI_HOTSPOT_TRIES times (e.g. Personal Hotspot screen closed): back to home.
 *   Never gives up: the phone's hotspot comes and goes.
 * - On the hotspot, look for home every WIFI_HOME_CHECK_US; when it is seen, switch to it. */
#define WIFI_HOME_TRIES 3
#define WIFI_HOTSPOT_TRIES 3
#define WIFI_HOME_CHECK_US (180LL*1000*1000)
#define WIFI_REASON_NO_AP 201
typedef enum {WIFI_NET_NONE,WIFI_NET_HOME,WIFI_NET_HOTSPOT} wifi_net;
typedef struct {bool home,hotspot;wifi_net active;int fails;int64_t next_home_check;} wifi_choice;
static inline void wifi_choice_start(wifi_choice*c,bool home,bool hotspot){
 *c=(wifi_choice){.home=home,.hotspot=hotspot,.active=home?WIFI_NET_HOME:hotspot?WIFI_NET_HOTSPOT:WIFI_NET_NONE};
}
static inline wifi_net wifi_choice_switch(wifi_choice*c,wifi_net to){c->active=to;c->fails=0;return to;}
/* A failed or dropped join on the active network; returns the network to try next. */
static inline wifi_net wifi_choice_failed(wifi_choice*c,int reason){
 if(c->active==WIFI_NET_NONE)return WIFI_NET_NONE;
 c->fails++;
 if(c->active==WIFI_NET_HOME){
  if(!c->hotspot)return WIFI_NET_HOME;
  if(reason==WIFI_REASON_NO_AP||c->fails>=WIFI_HOME_TRIES)return wifi_choice_switch(c,WIFI_NET_HOTSPOT);
  return WIFI_NET_HOME;
 }
 if(!c->home)return WIFI_NET_HOTSPOT;
 if(c->fails>=WIFI_HOTSPOT_TRIES)return wifi_choice_switch(c,WIFI_NET_HOME);
 return WIFI_NET_HOTSPOT;
}
static inline void wifi_choice_connected(wifi_choice*c){c->fails=0;}
/* Link up at `now`: arm the periodic home check when that link is the hotspot. */
static inline void wifi_choice_on_link(wifi_choice*c,int64_t now){
 c->next_home_check=c->active==WIFI_NET_HOTSPOT&&c->home?now+WIFI_HOME_CHECK_US:0;
}
/* True once per WIFI_HOME_CHECK_US while on the hotspot with a home network saved. */
static inline bool wifi_choice_check_home(wifi_choice*c,int64_t now){
 if(c->active!=WIFI_NET_HOTSPOT||!c->home||!c->next_home_check||now<c->next_home_check)return false;
 c->next_home_check=now+WIFI_HOME_CHECK_US;return true;
}
/* A home check found the home network in range: switch to it. */
static inline wifi_net wifi_choice_home_seen(wifi_choice*c){
 c->next_home_check=0;return wifi_choice_switch(c,WIFI_NET_HOME);
}
