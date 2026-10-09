/* End-to-end parent contract: a centered circular aperture reveals real Home. */
#include <assert.h>
#include <math.h>
#include <string.h>
#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"
static home_ui ui,ref;
static uint16_t pixels[SPARKLES_PIXELS],home[SPARKLES_PIXELS],outgoing[SPARKLES_PIXELS];
int main(void){
 memset(&ui,0,sizeof ui);
 ui.connected=ui.saved=true;ui.pair.state=PAIR_ENROLLED_UNPAIRED;
 ui.pair.live_ok=true;ui.pair.live_http=200;
 ui.phone.st.valid=true;ui.phone.st.state=PH_AUTHORIZED;ui.phone.st.flags=PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 ui.phone_ha.st=ui.phone.st;
#endif
 ui.theme_mode=THEME_LIGHT;ui.accent=1; /* Existing Blue theme; no palette changes. */
 ui.page=SETTINGS;ui.settings_tab=SETTINGS_SOUND;ui.tile=home_tile_index(SETTINGS);
 int64_t t=1000000;
 home_sample(&ui,t,true,80,440);
 home_sample(&ui,t+=100000,true,80,428);
 assert(ui.edge&&ui.slide_kind==HOME_MOTION_DRAG&&ui.page==SETTINGS);
 home_sample(&ui,t+=100000,true,80,350);
 /* Circular from the first active state, fixed panel center even for an off-center finger. */
 assert(fabsf(ui.blob.cx-184.f)<.001f&&fabsf(ui.blob.cy-224.f)<.001f);
 assert(fabsf(ui.blob.w-ui.blob.h)<.001f&&fabsf(ui.blob.n-2.f)<.001f);
 ref=ui;ref.page=HOME;
 assert(home_render_page(&ref,home,SPARKLES_PIXELS));
 assert(home_render_page(&ui,outgoing,SPARKLES_PIXELS));
 home_snapshot snap={.px=outgoing,.home=home};
 assert(home_compose(&ui,pixels,SPARKLES_PIXELS,&snap,NULL,-1));
 /* Interior is the actual Home content, not a solid blue fill or transformed outgoing page. */
 for(int y=220;y<228;y++)for(int x=180;x<188;x++)assert(pixels[(size_t)y*368+x]==home[(size_t)y*368+x]);
 /* Aperture expands with upward travel instead of shrinking a page/tile to a point. */
 float diameter=ui.blob.w;
 home_sample(&ui,t+=100000,true,80,310);
 assert(ui.blob.w>diameter&&fabsf(ui.blob.w-ui.blob.h)<.001f);
 assert(fabsf(ui.blob.cx-184.f)<.001f&&fabsf(ui.blob.cy-224.f)<.001f);
 return 0;
}
