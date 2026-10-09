/* Home Wi-Fi first, iPhone Personal Hotspot as the fallback (user rule 2026-09-30).
 * RED first: the rules live in the named device's firmware/main/wifi_choice.h. */
#include <assert.h>
#include <stdio.h>
#include "wifi_choice.h"

#define NO_AP 201
#define AUTH_FAIL 202
#define BEACON_TIMEOUT 200

static void home_first(void){
 wifi_choice c;wifi_choice_start(&c,true,true);
 assert(c.active==WIFI_NET_HOME);
 /* Home not in range: hotspot at once (no point retrying a network that isn't there). */
 assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOTSPOT);
 /* Hotspot failing: keep retrying it (home was just missing), never give up for good. */
 for(int i=0;i<20;i++){wifi_net n=wifi_choice_failed(&c,AUTH_FAIL);assert(n==WIFI_NET_HOTSPOT||n==WIFI_NET_HOME);}
}
static void home_flaky_then_fallback(void){
 wifi_choice c;wifi_choice_start(&c,true,true);
 /* Home is there but rejects the join: a few retries first (routers drop the first try). */
 assert(wifi_choice_failed(&c,AUTH_FAIL)==WIFI_NET_HOME);
 assert(wifi_choice_failed(&c,BEACON_TIMEOUT)==WIFI_NET_HOME);
 assert(wifi_choice_failed(&c,AUTH_FAIL)==WIFI_NET_HOTSPOT);   /* 3rd home failure -> hotspot */
 /* When the hotspot keeps failing too, it goes back to trying home. */
 assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOTSPOT);
 assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOTSPOT);
 assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOME);
}
static void only_one_saved(void){
 wifi_choice c;wifi_choice_start(&c,true,false);
 for(int i=0;i<10;i++)assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOME);
 wifi_choice_start(&c,false,true);assert(c.active==WIFI_NET_HOTSPOT);
 for(int i=0;i<10;i++)assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOTSPOT);
 wifi_choice_start(&c,false,false);assert(c.active==WIFI_NET_NONE);
 assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_NONE);
}
static void connected_resets(void){
 wifi_choice c;wifi_choice_start(&c,true,true);
 wifi_choice_failed(&c,AUTH_FAIL);wifi_choice_failed(&c,AUTH_FAIL);
 wifi_choice_connected(&c);assert(c.fails==0&&c.active==WIFI_NET_HOME);
 /* Home dropping out after it worked: retry home first, not an instant switch. */
 assert(wifi_choice_failed(&c,BEACON_TIMEOUT)==WIFI_NET_HOME);
}
static void back_home_from_hotspot(void){
 wifi_choice c;wifi_choice_start(&c,true,true);
 assert(wifi_choice_failed(&c,NO_AP)==WIFI_NET_HOTSPOT);wifi_choice_connected(&c);
 /* On the hotspot: look for home every WIFI_HOME_CHECK_US, and only then. */
 int64_t t=1000;wifi_choice_on_link(&c,t);
 assert(!wifi_choice_check_home(&c,t+WIFI_HOME_CHECK_US-1));
 assert(wifi_choice_check_home(&c,t+WIFI_HOME_CHECK_US));
 assert(!wifi_choice_check_home(&c,t+WIFI_HOME_CHECK_US+1));   /* re-armed after a check */
 /* Home seen: switch to it. */
 assert(wifi_choice_home_seen(&c)==WIFI_NET_HOME&&c.active==WIFI_NET_HOME&&c.fails==0);
 /* On home, never scans for home. */
 wifi_choice_on_link(&c,t);assert(!wifi_choice_check_home(&c,t+10*WIFI_HOME_CHECK_US));
 /* No home saved: no checks while on the hotspot. */
 wifi_choice h;wifi_choice_start(&h,false,true);wifi_choice_connected(&h);wifi_choice_on_link(&h,0);
 assert(!wifi_choice_check_home(&h,10*WIFI_HOME_CHECK_US));
}
int main(void){
 home_first();home_flaky_then_fallback();only_one_saved();connected_resets();back_home_from_hotspot();
 puts("wifi_choice: home first, hotspot fallback, back to home, single-network and reset rules PASS");
}
