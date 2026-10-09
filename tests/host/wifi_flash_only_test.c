/* Wi-Fi comes only from the flash (.env on the host -> tools/flash.sh -> USB). User rule 2026-10-02:
 * "Yes since already done via flash" (remove "Add with iPhone"); with it the board-hotspot "Set up by
 * phone" is gone too (its first QR joined the phone to the board's Wi-Fi). Settings > Wi-Fi only shows
 * status: no buttons, no QR, no setup network. RED first. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
#include "pair_usb.h"
static uint16_t px[SPARKLES_PIXELS];
static home_ui wifi_tab(bool saved,bool hotspot,bool connected,bool on_hotspot){
 home_ui s;memset(&s,0,sizeof s);s.page=SETTINGS;s.settings_tab=SETTINGS_WIFI;
 s.saved=saved;s.hotspot_saved=hotspot;s.connected=connected;s.on_hotspot=on_hotspot;
 if(saved)strcpy(s.credentials.ssid,"Home");
 if(hotspot)strcpy(s.hotspot_ssid,"Sam\xE2\x80\x99s iPhone");
 return s;
}
static bool drawn(const settings_record*r,const char*needle){for(int k=0;k<r->count;k++)if(strstr(r->t[k].text,needle))return true;return false;}
int main(void){
 const home_ui cases[]={wifi_tab(false,false,false,false),wifi_tab(true,false,false,false),wifi_tab(false,true,false,false),
                        wifi_tab(true,true,true,false),wifi_tab(true,true,true,true)};
 for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
  home_ui s=cases[i];
  /* No targets at all on the Wi-Fi page. */
  assert(home_settings_buttons(&s,0).count==0);
  /* WPC1 5/6 (first/second target) find nothing to press. */
  home_ui t=s;assert(!pair_cmd_apply(&t,PAIR_CMD_BUTTON_A)&&!pair_cmd_apply(&t,PAIR_CMD_BUTTON_B));
  /* A tap anywhere above the Home band changes nothing but the tap bookkeeping. */
  for(int y=60;y<400;y+=37)for(int x=40;x<330;x+=41){
   home_ui u=s;int64_t at=u.input.stamp_us+20000;home_sample(&u,at,true,x,y);home_sample(&u,at+40000,false,x,y);
   assert(u.page==SETTINGS&&u.settings_tab==SETTINGS_WIFI&&u.action==HOME_NONE&&!memcmp(&u.credentials,&s.credentials,sizeof s.credentials));
  }
  settings_record rec;memset(&rec,0,sizeof rec);set_rec=&rec;assert(home_render(&s,px,SPARKLES_PIXELS));set_rec=NULL;
  assert(!drawn(&rec,"Scan")&&!drawn(&rec,"iPhone setup")&&!drawn(&rec,"Home app")&&!drawn(&rec,"Set up by phone"));
  /* An iPhone name with a curly apostrophe is drawn as a plain one, never "???". */
  if(s.on_hotspot)assert(drawn(&rec,"Sam's iPhone")&&!drawn(&rec,"?"));
  /* Nothing saved: say where Wi-Fi comes from. */
  if(!s.saved&&!s.hotspot_saved)assert(drawn(&rec,".env")&&drawn(&rec,"flash"));
 }
 /* An unconfigured board opening Settings lands on the Wi-Fi page (status only, nothing starts). */
 home_ui h;memset(&h,0,sizeof h);h.page=HOME;h.settings_tab=SETTINGS_SOUND;h.tile=HOME_TILE_SETTINGS;
 home_tap(&h,184,224);assert(h.page==SETTINGS&&h.settings_tab==SETTINGS_WIFI);
 /* The retired USB action 8 (iPhone/Matter handover) is an unknown command now. */
 assert(!pair_cmd_known(8));
 puts("wifi_flash_only: Wi-Fi page is status only, no setup network, no iPhone handover: PASS");
}
