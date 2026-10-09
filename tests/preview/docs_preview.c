/* README previews: host simulations of the named device's real native renderer (firmware/main/home_render.h), NOT
 * photos of the panel. Writes PPM frames to OUT_DIR (default build/docs-preview); convert them with
 *   tools/render_docs_previews.sh   (builds this file, then ImageMagick -> docs/images/ PNGs)   */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOT_NO_LOCAL_OUTFITS 1  /* documentation renders use public art only */
#include "home_render.h"
static const char*dir;
static void save(home_ui*s,const char*name){
 uint16_t*p=malloc(SPARKLES_PIXELS*2);assert(p&&home_render(s,p,SPARKLES_PIXELS));
 char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n368 448\n255\n");
 for(int i=0;i<SPARKLES_PIXELS;i++){unsigned v=p[i];fputc((v>>11)*255/31,f);fputc(((v>>5)&63)*255/63,f);fputc((v&31)*255/31,f);}
 fclose(f);free(p);printf("%s\n",path);
}
/* A working board (Wi-Fi up, host linked, signed in to both providers, session feed answering): every tile usable. */
static void working(home_ui*s){
 memset(s,0,sizeof *s);s->connected=s->saved=true;s->pair.state=PAIR_ENROLLED_UNPAIRED;strcpy(s->pair.bridge,"Studio host");
 pair_live_result(&s->pair,200,1);s->phone.st.valid=true;s->phone.st.state=PH_AUTHORIZED;s->phone.st.flags=PHONE_FLAG_REQUIRED;
 strcpy(s->phone.st.name,"sam");s->phone_ha.st=s->phone.st;s->phone_ha.st.flags=PHONE_FLAG_REQUIRED;  /* Home Assistant: its own sign-in */
}
/* The Ask page on that board, bots answering from the bridge; the clock advances like the 100 Hz poller. */
static int64_t now=1000000;
static void ask(home_ui*s,int bot){
 working(s);s->page=HELPER;s->live_now_us=now;
 static const char*ids[3]={"helper","atlas","coding"},*names[3]={"Helper","Atlas","Coding"};
 s->bots.data=(bots_data){.valid=true,.count=3,.received_us=now};
 for(int i=0;i<3;i++){strcpy(s->bots.data.b[i].id,ids[i]);strcpy(s->bots.data.b[i].name,names[i]);s->bots.data.b[i].available=true;s->bots.data.b[i].reset_s=BOTS_NONE32;}
 helper_restore_bot(&s->helper,bot);home_bots_sync(s,now);
}
static void run(home_ui*s,float seconds){for(int i=0;i<(int)(seconds*100);i++){now+=10000;helper_tick(&s->helper,now);}s->live_now_us=now;home_bots_sync(s,now);}
static voice_command cmd(unsigned status,const char*said,const char*reply){
 voice_command c={.status=status};strcpy(c.id,"0123456789abcdef0123456789abcdef");
 snprintf(c.transcript,sizeof c.transcript,"%s",said);snprintf(c.text,sizeof c.text,"%s",reply);return c;
}
int main(int argc,char**argv){
 dir=argc>1?argv[1]:"build/docs-preview";
 static home_ui s;
 static const char*tile_frame[]={"hermes--home-sparkles","hermes--home-ask","home_assistant--home-sensor","home-settings"};
 for(int k=0;k<HOME_TILES;k++){working(&s);s.tile=k;save(&s,tile_frame[k]);}
 memset(&s,0,sizeof s);s.page=SPARKLES;s.input.scene.time=3;s.input.stamp_us=3000000;
 for(int i=0;i<120;i++)direct_sample(&s.input,3010000+i*10000,true,120+i,200);
 save(&s,"hermes--sparkles");
 memset(&s,0,sizeof s);s.page=SETTINGS;s.connected=s.saved=true;strcpy(s.credentials.ssid,"Example network");save(&s,"page-settings");
 /* Sensor: fresh readings, PM2.5 moderate (the Air tile reads "Okay"), the rest good. */
 working(&s);s.page=SENSORS;s.input.stamp_us=now;s.sensors.attempts=1;s.sensors.http=200;
 s.sensors.data=(sensors_data){.valid=true,.state=SS_OK,.received_us=now};
 {static const int16_t x10[SENSORS_COUNT]={234,410,10120,30,120,200};
  static const uint8_t q[SENSORS_COUNT]={SQ_GOOD,SQ_GOOD,SQ_GOOD,SQ_GOOD,SQ_MEDIUM,SQ_GOOD};
  for(int i=0;i<SENSORS_COUNT;i++)s.sensors.data.r[i]=(sensors_row){.known=true,.x10=x10[i],.quality=q[i]};}
 save(&s,"home_assistant--sensor");
 /* Ask: each bot waiting on an empty chat (hold the bot to talk). */
 static const char*bot_name[]={"helper","atlas","coding"};
 for(int b=0;b<3;b++){ask(&s,b);run(&s,2.2f);char n[48];snprintf(n,sizeof n,"hermes--ask-idle-%s",bot_name[b]);save(&s,n);}
 /* Listening: the bot opens its mouth and pulses with the voice. */
 ask(&s,0);run(&s,1);helper_press(&s.helper,now);
 for(int i=0;i<120;i++){s.helper.level_milli=300+(unsigned)(650*fabsf(sinf(i*.23f)));run(&s,.01f);}
 save(&s,"hermes--ask-listening");
 /* Running: compact working bot (Stop badge), Hermes' live status phrase above it. */
 helper_release(&s.helper,now);run(&s,.3f);
 voice_command c=cmd(VOICE_RUNNING,"What's on my calendar tomorrow morning?","");
 helper_apply(&s.helper,&c);helper_note(&s.helper,"Checking your calendar");run(&s,1.3f);
 save(&s,"hermes--ask-running");
 /* Done: the reply typed out, a happy bot. */
 c=cmd(VOICE_DONE,"What's on my calendar tomorrow morning?","You have two meetings tomorrow morning: a design review at 9:30 and a 1:1 with Sam at 11. Want me to block focus time after lunch?");
 helper_apply(&s.helper,&c);run(&s,3.4f);
 save(&s,"hermes--ask-done");
 /* Error: sad bot, the error in the chat. */
 ask(&s,1);run(&s,1);helper_press(&s.helper,now);run(&s,.8f);helper_release(&s.helper,now);run(&s,.2f);
 c=cmd(VOICE_ERROR,"Make me a map of the coffee shops","Hermes offline - try later");strcpy(c.transcript,"Make me a map of the coffee shops");
 helper_apply(&s.helper,&c);run(&s,.8f);
 save(&s,"hermes--ask-error");
 /* Settings > Battery: charging with a time-to-full estimate, and on battery. */
 working(&s);s.page=SETTINGS;s.settings_tab=SETTINGS_BATTERY;
 {battery_estimator e;battery_estimate_reset(&e);pmu_sample m={.ok=true,.vbus=true,.battery=true,.charging=true,.vbat_mv=3920};
  for(int i=0;i<=40;i++){m.percent=52+i/6;s.battery=battery_update(&e,&m,(int64_t)i*15*1000000);}}
 save(&s,"settings-battery-charging");
 {battery_estimator e;battery_estimate_reset(&e);pmu_sample m={.ok=true,.battery=true,.percent=64,.vbat_mv=3812};s.battery=battery_update(&e,&m,0);}
 save(&s,"settings-battery-on-battery");
 /* Settings > Display: the device default, Light with the Orange accent. */
 working(&s);s.page=SETTINGS;s.settings_tab=SETTINGS_DISPLAY;s.theme_mode=THEME_LIGHT;s.accent=0;save(&s,"settings-display");
 /* Settings > each provider's tab, signed in (titled with its sign-in label). */
 working(&s);s.page=SETTINGS;s.settings_tab=SETTINGS_HERMES;save(&s,"hermes--settings-hermes");
 working(&s);s.page=SETTINGS;s.settings_tab=SETTINGS_HOME_ASSISTANT;save(&s,"home_assistant--settings-home-assistant");
 /* Plugin pictures: "<plugin>--<name>" frames go directly to plugins/<plugin>/images/. */
 /* Hermes: the Hermes tab signed in, the Sound tab, Sparkles in its Sunset style, Ask (done) in Dark. */
 working(&s);s.page=SETTINGS;s.settings_tab=SETTINGS_SOUND;save(&s,"hermes--settings-sound");
 memset(&s,0,sizeof s);s.page=SPARKLES;s.input.scene.time=3;s.input.stamp_us=3000000;
 s.input.scene.style=s.input.scene.style_from=1;
 sp_direct_water_time=-1;  /* the water cache still holds the Sea frame above: start it fresh */
 for(int i=0;i<120;i++)direct_sample(&s.input,3010000+i*10000,true,120+i,200);
 save(&s,"hermes--sparkles-sunset");
 ask(&s,0);run(&s,1);helper_press(&s.helper,now);
 for(int i=0;i<60;i++){s.helper.level_milli=300+(unsigned)(650*fabsf(sinf(i*.23f)));run(&s,.01f);}
 helper_release(&s.helper,now);run(&s,.3f);
 c=cmd(VOICE_DONE,"What's on my calendar tomorrow morning?","You have two meetings tomorrow morning: a design review at 9:30 and a 1:1 with Sam at 11. Want me to block focus time after lunch?");
 helper_apply(&s.helper,&c);run(&s,3.4f);s.theme_mode=THEME_DARK;
 save(&s,"hermes--ask-done-dark");
 /* Home Assistant: the Sensor tile waiting for its sign-in, the shared account tab (one sign-in for both
  * providers), and the Sensor page in Dark. */
 working(&s);s.tile=home_tile_index(SENSORS);s.phone_ha.st.state=PH_NONE;save(&s,"home_assistant--home-sensor-sign-in");
 working(&s);s.page=SETTINGS;s.settings_tab=SETTINGS_HERMES;s.signin_shared=true;
 s.phone.st.flags|=PHONE_FLAG_SHARED;s.phone_ha.st.flags|=PHONE_FLAG_SHARED;save(&s,"home_assistant--settings-shared");
 working(&s);s.page=SENSORS;s.input.stamp_us=now;s.sensors.attempts=1;s.sensors.http=200;
 s.sensors.data=(sensors_data){.valid=true,.state=SS_OK,.received_us=now};
 {static const int16_t x10[SENSORS_COUNT]={234,410,10120,30,120,200};
  static const uint8_t q[SENSORS_COUNT]={SQ_GOOD,SQ_GOOD,SQ_GOOD,SQ_GOOD,SQ_MEDIUM,SQ_GOOD};
  for(int i=0;i<SENSORS_COUNT;i++)s.sensors.data.r[i]=(sensors_row){.known=true,.x10=x10[i],.quality=q[i]};}
 s.theme_mode=THEME_DARK;save(&s,"home_assistant--sensor-dark");
 return 0;
}
