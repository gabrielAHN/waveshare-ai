/* Parent acceptance: a reserved bottom contact remains eligible until a slow center release. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"
static home_ui ui,reference;
static uint16_t pixels[SPARKLES_PIXELS],plain_home[SPARKLES_PIXELS],outgoing[SPARKLES_PIXELS];
static int64_t clock_us;
static void ready(void){
 memset(&ui,0,sizeof ui);ui.connected=ui.saved=true;
 ui.pair.state=PAIR_ENROLLED_UNPAIRED;ui.pair.live_ok=true;ui.pair.live_http=200;
 ui.phone.st.valid=true;ui.phone.st.state=PH_AUTHORIZED;ui.phone.st.flags=PHONE_FLAG_REQUIRED|PHONE_FLAG_SHARED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
 ui.phone_ha.st=ui.phone.st;
#endif
 ui.page=SETTINGS;ui.settings_tab=SETTINGS_SOUND;ui.tile=home_tile_index(SETTINGS);
 ui.theme_mode=THEME_LIGHT;ui.accent=1;clock_us=1000000;
}
static void sample(bool down,int x,int y){clock_us+=10000;home_sample(&ui,clock_us,down,x,y);}
static void slow_center_release(void){
 ready();sample(true,184,447);
 for(int i=0;i<40;i++)sample(true,184,447); /* a normal press before moving, > former deadline/buffer */
 for(int y=446;y>=224;y--)sample(true,184,y); /* slow, below flick speed */
 for(int i=0;i<12;i++)sample(true,184,224); /* stationary at center: measured release velocity zero */
 assert(ui.page==SETTINGS); /* user chose commit on release, not while held */
 sample(false,0,0); /* CST816S reports no point on physical release */
 if(ui.page!=HOME){fprintf(stderr,"slow center release rejected: edge=%d consumed=%d waiting=%d page=%d\n",ui.edge,ui.consumed,ui.pull_wait,ui.page);}
 assert(ui.page==HOME);
 for(int i=0;i<30;i++)sample(false,0,0);
 assert(ui.page==HOME&&ui.slide_kind==HOME_MOTION_NONE);
}
static void already_travelled_promotion(void){
 ready();sample(true,80,447);
 for(int i=0;i<40;i++)sample(true,80,447);
 sample(true,80,340); /* sparse first motion sample: do not restart progress at recognition */
 assert(ui.edge&&ui.slide_kind==HOME_MOTION_DRAG&&ui.page==SETTINGS);
 home_blob expected=home_blob_pull(80,447,107.f,0.f);
 assert(ui.blob.w>0.f&&fabsf(ui.blob.w-expected.w)<.001f&&fabsf(ui.blob.h-expected.h)<.001f);
 assert(fabsf(ui.blob.cx-184.f)<.001f&&fabsf(ui.blob.cy-224.f)<.001f&&fabsf(ui.blob.n-2.f)<.001f);
 reference=ui;reference.page=HOME;
 assert(home_render_page(&reference,plain_home,SPARKLES_PIXELS));
 assert(home_render_page(&ui,outgoing,SPARKLES_PIXELS));
 home_snapshot snapshot={.px=outgoing,.home=plain_home};
 assert(home_compose(&ui,pixels,SPARKLES_PIXELS,&snapshot,NULL,-1));
 for(int y=220;y<228;y++)for(int x=180;x<188;x++)assert(pixels[(size_t)y*368+x]==plain_home[(size_t)y*368+x]);
}
int main(int argc,char**argv){
 if(argc==1||!strcmp(argv[1],"release"))slow_center_release();
 if(argc==1||!strcmp(argv[1],"progress"))already_travelled_promotion();
 puts("Bottom contact: slow center release commits; promotion uses all travelled motion and actual Home pixels");
 return 0;
}
