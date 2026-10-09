/* Parent-frozen acceptance: bottom edge -> centre is UP, not the previous DOWN gesture. */
#include <assert.h>
#include <string.h>
#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"
static home_ui ui;
static uint16_t pixels[SPARKLES_PIXELS], home[SPARKLES_PIXELS];
int main(void){
 memset(&ui,0,sizeof ui);
 ui.connected=ui.saved=true;
 ui.pair.state=PAIR_ENROLLED_UNPAIRED;
 ui.pair.live_ok=true;ui.pair.live_http=200;
 ui.phone.st.valid=true;ui.phone.st.state=PH_AUTHORIZED;ui.phone.st.flags=PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 ui.phone_ha.st=ui.phone.st;
#endif
 ui.page=SETTINGS;ui.settings_tab=SETTINGS_SOUND;ui.tile=home_tile_index(SETTINGS);
 int64_t t=1000000;
 home_sample(&ui,t,true,184,440);
 home_sample(&ui,t+=10000,true,184,428);
 assert(ui.edge && ui.slide_kind==HOME_MOTION_DRAG && ui.page==SETTINGS);
 for(int y=416;y>=224;y-=12)home_sample(&ui,t+=10000,true,184,y);
 home_snapshot snap={.home=home};
 assert(home_compose(&ui,pixels,SPARKLES_PIXELS,&snap,NULL,-1));
 home_sample(&ui,t+=10000,false,0,0);
 assert(ui.page==HOME);
 assert(home_compose(&ui,pixels,SPARKLES_PIXELS,&snap,NULL,-1));
 return 0;
}
