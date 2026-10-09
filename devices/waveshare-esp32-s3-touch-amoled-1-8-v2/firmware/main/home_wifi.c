#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_timer.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/usb_serial_jtag.h"
#include "provision.h"
#include "live_view.h"
#include "bots_command.h"
#include "gesture_replay.h"
#include "voice_wire.h"
#include "helper_voice.h"
#include "home_pair.h"
#include "pair_usb.h"
#include "esp_timer.h"
#include "home_wifi.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "ssid_variant.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifi_choice.h"
#include "power_main.h"
static home_ui *ui;
static SemaphoreHandle_t lock;
static QueueHandle_t events;
static esp_netif_t *netif;
static nvs_handle_t nvs;
static bool storage_ready;
static uint8_t credential_slot;
static void wipe(void*p,size_t n){volatile unsigned char*q=p;while(n--)*q++=0;}
static int get_credentials(void*ctx,home_credentials*c){
 (void)ctx;if(!storage_ready)return -1;size_t n=sizeof *c;esp_err_t e=nvs_get_blob(nvs,credential_slot?"station2":"station",c,&n);
 if(e==ESP_ERR_NVS_NOT_FOUND)return 0;
 return e==ESP_OK&&n==sizeof *c?1:-1;
}
static bool put_credentials(void*ctx,const home_credentials*c){
 (void)ctx;if(!storage_ready)return false;
 /* Copy-on-write: never overwrite the currently selected working blob. */
 uint8_t next=credential_slot^1;const char*key=next?"station2":"station";
 home_credentials check={0};size_t n=sizeof check;
 bool ok=nvs_set_blob(nvs,key,c,sizeof *c)==ESP_OK&&nvs_commit(nvs)==ESP_OK&&
  nvs_get_blob(nvs,key,&check,&n)==ESP_OK&&n==sizeof check&&!memcmp(c,&check,n);
 wipe(&check,sizeof check);if(!ok)return false;
 uint8_t readback=255;
 ok=nvs_set_u8(nvs,"wifi_slot",next)==ESP_OK&&nvs_commit(nvs)==ESP_OK&&nvs_get_u8(nvs,"wifi_slot",&readback)==ESP_OK&&readback==next;
 if(ok)credential_slot=next;
 else {nvs_set_u8(nvs,"wifi_slot",credential_slot);nvs_commit(nvs);}
 return ok;
}
static bool erase_credentials(void*ctx){
 (void)ctx;if(!storage_ready)return false;
 esp_err_t e=nvs_erase_key(nvs,"station"),f=nvs_erase_key(nvs,"station2");
 return (e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND)&&(f==ESP_OK||f==ESP_ERR_NVS_NOT_FOUND)&&nvs_commit(nvs)==ESP_OK;
}
/* 'Session sparkle' toggle: one u8 NVS key ("session", 1=ON 0=OFF) in the
 * existing namespace. Absent => default ON. Commit then read back; a failed
 * read is reported, never treated as a successful write. */
/* iPhone Personal Hotspot (fallback network): one NVS blob "hotspot", written and read back. */
static int get_hotspot(void*ctx,home_credentials*c){
 (void)ctx;if(!storage_ready)return -1;size_t n=sizeof *c;esp_err_t e=nvs_get_blob(nvs,"hotspot",c,&n);
 if(e==ESP_ERR_NVS_NOT_FOUND)return 0;
 return e==ESP_OK&&n==sizeof *c?1:-1;
}
static bool put_hotspot(void*ctx,const home_credentials*c){
 (void)ctx;if(!storage_ready)return false;
 return nvs_set_blob(nvs,"hotspot",c,sizeof *c)==ESP_OK&&nvs_commit(nvs)==ESP_OK;
}
static bool erase_hotspot(void*ctx){
 (void)ctx;if(!storage_ready)return false;
 esp_err_t e=nvs_erase_key(nvs,"hotspot");return (e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND)&&nvs_commit(nvs)==ESP_OK;
}
static int load_session_toggle(void){
 if(!storage_ready)return -1;
 uint8_t v=1;esp_err_t e=nvs_get_u8(nvs,"session",&v);
 if(e==ESP_ERR_NVS_NOT_FOUND)return 2;
 return e==ESP_OK&&v<=1?v:-1;
}
static bool save_session_toggle(bool on){
 if(!storage_ready)return false;
 uint8_t readback=255;
 return nvs_set_u8(nvs,"session",on?1:0)==ESP_OK&&nvs_commit(nvs)==ESP_OK&&nvs_get_u8(nvs,"session",&readback)==ESP_OK&&readback==(on?1:0);
}
/* UI preference only. Audio playback is intentionally outside this component. Absent => ON. */
static int load_sound_toggle(void){
 if(!storage_ready)return -1;
 uint8_t v=1;esp_err_t e=nvs_get_u8(nvs,"sound",&v);
 if(e==ESP_ERR_NVS_NOT_FOUND)return 2;
 return e==ESP_OK&&v<=1?v:-1;
}
static bool save_sound_toggle(bool on){
 if(!storage_ready)return false;
 uint8_t readback=255;
 return nvs_set_u8(nvs,"sound",on?1:0)==ESP_OK&&nvs_commit(nvs)==ESP_OK&&nvs_get_u8(nvs,"sound",&readback)==ESP_OK&&readback==(on?1:0);
}
/* Selected Ask-page bot: one u8 NVS key ("bot", index 0..2) in the existing namespace. Absent or
 * corrupt => 0 (helper). Commit then read back. */
static int load_bot(void){
 if(!storage_ready)return -1;
 uint8_t v=0;esp_err_t e=nvs_get_u8(nvs,"bot",&v);
 if(e==ESP_ERR_NVS_NOT_FOUND)return -2;
 return e==ESP_OK?v:-1;
}
static bool save_bot(int index){
 if(!storage_ready||index<0||index>=BOTS_MAX)return false;
 uint8_t readback=255;
 return nvs_set_u8(nvs,"bot",(uint8_t)index)==ESP_OK&&nvs_commit(nvs)==ESP_OK&&nvs_get_u8(nvs,"bot",&readback)==ESP_OK&&readback==index;
}
/* The Sparkles swipe style is not stored: the page always opens on Sea. */
/* Colour theme (Settings > Display, theme.h): one u8 NVS key ("theme" = mode | accent << 4). Returns the
 * stored byte, -2 absent, -1 read error; the caller decodes it (invalid -> light + accent 0). Commit then
 * read back, like "bot". */
static int load_theme(void){
 if(!storage_ready)return -1;
 uint8_t v=0;esp_err_t e=nvs_get_u8(nvs,THEME_NVS_KEY,&v);
 if(e==ESP_ERR_NVS_NOT_FOUND)return -2;
 return e!=ESP_OK?-1:v;
}
static bool save_theme(uint8_t v){
 if(!storage_ready)return false;
 uint8_t readback=255;
 return nvs_set_u8(nvs,THEME_NVS_KEY,v)==ESP_OK&&nvs_commit(nvs)==ESP_OK&&nvs_get_u8(nvs,THEME_NVS_KEY,&readback)==ESP_OK&&readback==v;
}
/* WBS1: USB equivalent of a left/right swipe on the Ask page (same helper_swipe as touch). */
static bots_cmd_parser bots_parser;
static void feed_bots(unsigned char byte){
 unsigned char dir=0;int r=bots_cmd_feed(&bots_parser,byte,&dir);
 if(r<0){ESP_LOGW("home_wifi","BOT_SWIPE_REJECTED");return;}
 if(r!=1)return;
 xSemaphoreTake(lock,portMAX_DELAY);
 bool ok=false;int from=ui->helper.bot;
 if(ui->page==HELPER){home_bots_sync(ui,ui->input.stamp_us);ok=helper_swipe(&ui->helper,dir,0);home_bots_sync(ui,ui->input.stamp_us);}
 int to=ui->helper.bot;unsigned block=ui->helper.block;unsigned st=ui->helper.state;
 char id[BOTS_TEXT];snprintf(id,sizeof id,"%s",bots_id(&ui->bots,to));
 xSemaphoreGive(lock);
 ESP_LOGI("home_wifi","BOT_SWIPE dir=%s ok=%d from=%d to=%d bot=%s mic=%s state=%u source=usb_serial",dir==1?"next":"prev",ok,from,to,id,
  block?bots_reason_name(block):"enabled",st);
}
static volatile bool wifi_started;
/* Sleep (power_main.c): requested state, and the state the worker has applied. */
static bool wifi_sleep_req,wifi_sleep_ack;
void home_wifi_sleep(bool asleep){__atomic_store_n(&wifi_sleep_req,asleep,__ATOMIC_RELEASE);}
bool home_wifi_is_asleep(void){return __atomic_load_n(&wifi_sleep_req,__ATOMIC_ACQUIRE)&&__atomic_load_n(&wifi_sleep_ack,__ATOMIC_ACQUIRE);}
void home_wifi_power_save(bool asleep){
 if(!wifi_started)return;
 esp_err_t e=esp_wifi_set_ps(asleep?WIFI_PS_MIN_MODEM:WIFI_PS_NONE);
 ESP_LOGI("home_wifi","WIFI_PS %s code=%d",asleep?"min_modem":"none",e);
}
/* WGS1: gestures replayed as touch samples (bots_command.h, gesture_replay.h). */
static gesture_cmd_parser gesture_parser;
static void feed_gesture(unsigned char byte){
 unsigned char g=0;int r=gesture_cmd_feed(&gesture_parser,byte,&g);
 if(r<0){ESP_LOGW("home_wifi","GESTURE_REJECTED");return;}
 if(r!=1)return;
 xSemaphoreTake(lock,portMAX_DELAY);
 int64_t t=ui->input.stamp_us+1;
 gesture_replay(ui,g,&t);  /* gesture_replay.h: the touch samples a finger would make */
 if(g==GESTURE_SPARKLE_NEXT||g==GESTURE_SPARKLE_PREV){
  int page=ui->page,style=ui->input.scene.style;
  xSemaphoreGive(lock);
  ESP_LOGI("home_wifi","SPARKLE_STYLE gesture=%u page=%d style=%d name=%s source=usb-injected-not-finger",g,page,style,sparkle_style_name(style));
  return;
 }
 const helper_view*h=&ui->helper;
 int page=ui->page,bot=h->bot,cont=h->cont,chat=h->chat,scroll=h->scroll,smax=h->scroll_max;unsigned used=h->log.used,st=h->state;
 xSemaphoreGive(lock);
 ESP_LOGI("home_wifi","HELPER_CHAT gesture=%u page=%d bot=%d state=%u chat=%d cont=%d log_bytes=%u scroll=%d scroll_max=%d source=usb-injected-not-finger",
  g,page,bot,st,chat,cont,used,scroll,smax);
}
/* WVC1: USB equivalent of the Helper mic button (press/release/stop). Drives the SAME state
 * machine as touch; logged as usb_serial, never counted as a touch sample. */
static void apply_voice_command(unsigned char action){
 if(!WAVESHARE_AI_PLUGIN_AI){ESP_LOGW("home_wifi","VOICE_BUTTON_IGNORED note=ai-plugin-not-built");return;}
 xSemaphoreTake(lock,portMAX_DELAY);
 if(action==HELPER_CMD_PRESS&&ui->page!=HELPER){ui->page=HELPER;ui->consumed=true;}
 bool ok=helper_command(&ui->helper,ui->input.stamp_us,action);unsigned st=ui->helper.state;
 xSemaphoreGive(lock);
 ESP_LOGI("home_wifi","VOICE_BUTTON action=%u ok=%d state=%u source=usb_serial",action,ok,st);
}
#ifdef HELPER_VOICE_SELFTEST
static power_cmd_parser power_parser;
#endif
/* Feed one USB byte to the always-on voice parser (and, in VOICE_SELFTEST builds, the WPK1 power-key test frame). */
static void feed_voice(voice_cmd_parser*vp,unsigned char byte){
#ifdef HELPER_VOICE_SELFTEST
 {unsigned char pv=0;int pr=power_cmd_feed(&power_parser,byte,&pv);  /* WPK1: power button test (power_button.h) */
  if(pr==1){ESP_LOGI("home_wifi","POWER_TEST_FRAME value=%u source=usb-injected-not-physical",pv);power_main_test(pv);}
  else if(pr<0)ESP_LOGW("home_wifi","POWER_TEST_REJECTED");}
#endif
 unsigned char action=0;int r2=voice_cmd_feed(vp,byte,&action);
 if(r2==1)apply_voice_command(action);else if(r2<0)ESP_LOGW("home_wifi","VOICE_BUTTON_REJECTED");
}
/* Pairing USB frames (see pair_usb.h): WPC1 drives the Settings screen exactly like touch; WLB2 adds
 * a bridge candidate (address + certificate pin) when mDNS is unavailable. Neither carries or
 * stores a board secret, and neither can confirm an enrollment. */
static pair_cmd_parser pair_parser;
static pair_usb_bridge_parser bridge_parser;
static void feed_pair(unsigned char byte){
 unsigned char action=0;int r=pair_cmd_feed(&pair_parser,byte,&action);
 if(r==1){xSemaphoreTake(lock,portMAX_DELAY);bool ok=pair_cmd_apply(ui,action);unsigned step=ui->pair.step;xSemaphoreGive(lock);
  ESP_LOGI("home_wifi","PAIR_COMMAND action=%u ok=%d step=%u source=usb_serial",action,ok,step);}
 else if(r<0)ESP_LOGW("home_wifi","PAIR_COMMAND_REJECTED");
 pair_usb_bridge b={0};r=pair_usb_bridge_feed(&bridge_parser,byte,&b);
 if(r==1){xSemaphoreTake(lock,portMAX_DELAY);bool ok=pair_usb_bridge_apply(ui,&b);xSemaphoreGive(lock);
  ESP_LOGI("home_wifi","PAIR_BRIDGE_CANDIDATE ok=%d source=usb_serial note=wlb2-address-and-pin-only",ok);}
 else if(r<0)ESP_LOGW("home_wifi","PAIR_BRIDGE_REJECTED");
 provision_wipe(&b,sizeof b);
}
/* Event-loop context only publishes the most recent link event. It neither
 * blocks input nor performs storage, connection, rendering or secret logging. */
static void wifi_event(void*arg,esp_event_base_t base,int32_t id,void*data){
 (void)arg;(void)data;int event=0;
 if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED)event=1000+((wifi_event_sta_disconnected_t*)data)->reason;
 if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)event=2;
 if(event)xQueueOverwrite(events,&event);
}
static void status(const char*text,bool connected){
 xSemaphoreTake(lock,portMAX_DELAY);snprintf(ui->status,sizeof ui->status,"%s",text);ui->connected=connected;if(!connected)ui->ip[0]=0;xSemaphoreGive(lock);
}
static bool connect_saved(const home_credentials*c){
 /* RAM-only driver config prevents a second hidden persistent password. */
 esp_wifi_disconnect();esp_wifi_stop();if(esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK)return false;int discarded;while(xQueueReceive(events,&discarded,0)==pdTRUE){}
 wifi_config_t config={0};memcpy(config.sta.ssid,c->ssid,strlen(c->ssid));memcpy(config.sta.password,c->password,strlen(c->password));
 config.sta.pmf_cfg.capable=true;config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
 esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&config);wipe(&config,sizeof config);
 if(e!=ESP_OK){ESP_LOGW("home_wifi","STA config failed code=%d",e);return false;}
 e=esp_wifi_start();if(e!=ESP_OK){ESP_LOGW("home_wifi","STA start failed code=%d",e);return false;}
 /* USB-powered desk device: keep the radio awake so voice uploads are not paced by DTIM
  * wake-ups. TCP_MSS bounds each packet to the LAN path limit. */
 e=esp_wifi_set_ps(WIFI_PS_NONE);ESP_LOGI("home_wifi","WIFI_PS none code=%d",e);wifi_started=true;
 wifi_scan_config_t scan={.ssid=(uint8_t*)c->ssid};
 if(esp_wifi_scan_start(&scan,true)==ESP_OK){wifi_ap_record_t records[4];uint16_t count=4;if(esp_wifi_scan_get_ap_records(&count,records)==ESP_OK){ESP_LOGI("home_wifi","TARGET_SCAN matches=%u auth=%d channel=%u rssi=%d pairwise=%d group=%d",count,count?records[0].authmode:-1,count?records[0].primary:0,count?records[0].rssi:0,count?records[0].pairwise_cipher:-1,count?records[0].group_cipher:-1);
 if(count&&records[0].authmode==WIFI_AUTH_WPA3_PSK){wifi_config_t secure={0};if(esp_wifi_get_config(WIFI_IF_STA,&secure)==ESP_OK){secure.sta.pmf_cfg.required=true;esp_err_t updated=esp_wifi_set_config(WIFI_IF_STA,&secure);ESP_LOGI("home_wifi","WPA3_PMF_REQUIRED code=%d credential_match=%d",updated,memcmp(secure.sta.password,c->password,strlen(c->password))==0);}wipe(&secure,sizeof secure);}}}
 e=esp_wifi_connect();ESP_LOGI("home_wifi","STA_CONNECT_REQUEST code=%d",e);return e==ESP_OK;
}
static home_store store={get_credentials,put_credentials,erase_credentials,NULL};
static home_store hotspot_store={get_hotspot,put_hotspot,erase_hotspot,NULL};
static wifi_choice choice;
/* Join the network `choice` picked; the password is loaded from NVS and wiped right after. */
/* Is a network with exactly this name in range? (targeted scan; Wi-Fi must be started) */
static bool ssid_in_range(const char*ssid){
 wifi_scan_config_t scan={.ssid=(uint8_t*)ssid};uint16_t count=0;
 bool seen=esp_wifi_scan_start(&scan,true)==ESP_OK&&esp_wifi_scan_get_ap_num(&count)==ESP_OK&&count>0;
 esp_wifi_clear_ap_list();return seen;
}
/* iPhone hotspot names carry the iPhone's name, written by iOS with a curly apostrophe ("Sam’s iPhone");
 * a straight one is what people type. If the saved spelling isn't in range but the other one is, join
 * that one (the saved record is left as it is). */
static void hotspot_spelling(home_credentials*c){
 char alt[33];if(!ssid_apostrophe_variant(c->ssid,alt,sizeof alt))return;
 if(!wifi_started){if(esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK||esp_wifi_start()!=ESP_OK)return;wifi_started=true;}
 if(ssid_in_range(c->ssid))return;
 bool other=ssid_in_range(alt);
 ESP_LOGI("home_wifi","HOTSPOT_SPELLING saved_in_range=0 other_apostrophe_in_range=%d",other);
 if(other){memset(c->ssid,0,sizeof c->ssid);memcpy(c->ssid,alt,strlen(alt));}
}
static bool connect_choice(void){
 home_credentials c={0};bool ok=false;
 if(choice.active==WIFI_NET_HOME)ok=home_store_load(&store,&c);
 else if(choice.active==WIFI_NET_HOTSPOT){ok=home_store_load(&hotspot_store,&c);if(ok)hotspot_spelling(&c);}
 if(ok){
  xSemaphoreTake(lock,portMAX_DELAY);ui->on_hotspot=choice.active==WIFI_NET_HOTSPOT;xSemaphoreGive(lock);
  ESP_LOGI("home_wifi","WIFI_CHOICE net=%s",choice.active==WIFI_NET_HOTSPOT?"hotspot":"home");
  ok=connect_saved(&c);
 }
 wipe(&c,sizeof c);return ok;
}
/* On the hotspot: is the home network in range? (targeted scan, no connection change) */
static bool home_in_range(void){
 home_credentials c={0};if(!home_store_load(&store,&c))return false;
 wifi_scan_config_t scan={.ssid=(uint8_t*)c.ssid};uint16_t count=0;
 bool seen=esp_wifi_scan_start(&scan,true)==ESP_OK&&esp_wifi_scan_get_ap_num(&count)==ESP_OK&&count>0;
 esp_wifi_clear_ap_list();wipe(&c,sizeof c);return seen;
}
static void wifi_worker(void*arg){
 (void)arg;
 /* Never erase the shared NVS partition automatically on initialization errors. */
 esp_err_t e=nvs_flash_init();storage_ready=e==ESP_OK&&nvs_open("home_wifi",NVS_READWRITE,&nvs)==ESP_OK;
 if(storage_ready){uint8_t slot=0;esp_err_t se=nvs_get_u8(nvs,"wifi_slot",&slot);if(se==ESP_OK&&slot<=1)credential_slot=slot;else if(se!=ESP_ERR_NVS_NOT_FOUND)storage_ready=false;}
 home_credentials saved={0};bool wanted=storage_ready&&home_store_load(&store,&saved);
 home_credentials hs={0};bool hotspot=storage_ready&&home_store_load(&hotspot_store,&hs);
 int session_stored=load_session_toggle();
 int sound_stored=load_sound_toggle();
 int bot_stored=load_bot();
 int theme_stored=load_theme();
 uint8_t theme_mode=THEME_LIGHT,theme_accent=0;
 bool theme_ok=theme_stored>=0&&theme_nvs_decode((uint8_t)theme_stored,&theme_mode,&theme_accent);
 /* Before Wi-Fi/RF starts: load the pairing record and create the on-device identity. */
 #if WAVESHARE_AI_PLUGIN_BRIDGE
 if(storage_ready)home_pair_boot(ui,lock);  /* bridge pairing record + on-device identity */
 #endif
 /* Loaded before Wi-Fi starts, so the live worker (which waits for a link)
  * never polls ahead of a persisted OFF. */
 xSemaphoreTake(lock,portMAX_DELAY);if(wanted){ui->credentials=saved;}ui->saved=wanted;
 ui->hotspot_saved=hotspot;if(hotspot)memcpy(ui->hotspot_ssid,hs.ssid,sizeof ui->hotspot_ssid);if(session_stored==0){ui->session_off=true;memset(&ui->live,0,sizeof ui->live);}if(sound_stored==0)ui->sound_off=true;helper_restore_bot(&ui->helper,bots_restore(bot_stored));
 ui->theme_mode=theme_mode;ui->accent=theme_accent;xSemaphoreGive(lock);
 ESP_LOGI("home_wifi","THEME_LOADED mode=%u accent=%u stored=%s",(unsigned)theme_mode,(unsigned)theme_accent,
  theme_stored==-2?"absent-default-light":(theme_stored<0?"read-error-default-light":(theme_ok?"present":"invalid-default-light")));
 ESP_LOGI("home_wifi","BOT_LOADED index=%d stored=%s",bots_restore(bot_stored),bot_stored==-2?"absent-default-helper":(bot_stored<0||bot_stored>=BOTS_MAX?"read-error-default-helper":"present"));
 ESP_LOGI("home_wifi","SESSION_TOGGLE_LOADED on=%d stored=%s",session_stored!=0,session_stored==2?"absent-default-on":(session_stored<0?"read-error-default-on":"present"));
 ESP_LOGI("home_wifi","SOUND_TOGGLE_LOADED on=%d stored=%s",sound_stored!=0,sound_stored==2?"absent-default-on":(sound_stored<0?"read-error-default-on":"present"));
 esp_log_level_set("wifi",ESP_LOG_WARN);
 bool ready=esp_netif_init()==ESP_OK&&esp_event_loop_create_default()==ESP_OK;
 if(ready){netif=esp_netif_create_default_wifi_sta();ready=netif!=NULL;}
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
 if(ready)ready=esp_wifi_init(&init)==ESP_OK;
 if(ready)ready=esp_wifi_set_storage(WIFI_STORAGE_RAM)==ESP_OK&&esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK;
 if(ready)ready=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_event,NULL)==ESP_OK&&esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL)==ESP_OK;
 status(!storage_ready?"Storage unavailable":(!ready?"Wi-Fi unavailable":(wanted?"Connecting saved network...":"No saved network")),false);
 ESP_LOGI("home_wifi","worker ready=%d storage_ready=%d saved=%d hotspot=%d; credentials never logged; NVS unencrypted",ready,storage_ready,wanted,hotspot);
 int retries=0,last_reason=0;bool linked=false;int64_t deadline=0;
 provision_parser parser={0};live_view_parser view_parser={0};int64_t provision_until=esp_timer_get_time()+90LL*1000*1000;
 voice_cmd_parser voice_parser={0};
 usb_serial_jtag_driver_config_t usb={.rx_buffer_size=4096,.tx_buffer_size=1024};
 ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));usb_serial_jtag_vfs_use_driver();
 usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
 fcntl(STDIN_FILENO,F_SETFL,fcntl(STDIN_FILENO,F_GETFL,0)|O_NONBLOCK);
 ESP_LOGI("home_wifi","USB_PROVISION_READY window_s=90; input is not echoed; WVC1 helper-button, WBS1 bot-swipe, WGS1 chat-gesture, WPC1 settings (4 = phone QR, 5/6 = 1st/2nd large target, 7 = forget Wi-Fi) and WLB2 bridge-candidate commands accepted for the session");
 wifi_choice_start(&choice,wanted,hotspot);wanted=wanted||hotspot;
 if(ready&&wanted){bool started=connect_choice();deadline=esp_timer_get_time()+(started?25000000:3000000);}
 wipe(&saved,sizeof saved);wipe(&hs,sizeof hs);
 for(;;){
  if(provision_until&&esp_timer_get_time()<provision_until){
   unsigned char bytes[128];int n=read(STDIN_FILENO,bytes,sizeof bytes);
   for(int i=0;i<n;i++){
    feed_voice(&voice_parser,bytes[i]);
    unsigned char selected=0;int view_result=live_view_feed(&view_parser,bytes[i],&selected);
    if(view_result==1){
     if(!home_page_enabled(selected))selected=HOME;  /* page of a plugin that is not in this build */
     xSemaphoreTake(lock,portMAX_DELAY);ui->page=(home_page)selected;ui->drag_offset=0;ui->consumed=true;home_sparkle_entry(ui);xSemaphoreGive(lock);
     wipe(&parser,sizeof parser);provision_until=0;
     ESP_LOGI("home_wifi","SERVICE_SELECT page=%u source=physical_usb",selected);break;
    }
    feed_pair(bytes[i]);feed_bots(bytes[i]);feed_gesture(bytes[i]);
    provision_config c={0};int result=provision_feed(&parser,bytes[i],&c);
    if(result==1&&provision_network(&c)==PROVISION_NET_HOTSPOT){
     bool ok=home_store_save(&hotspot_store,&c.wifi);
     if(ok){hotspot=true;xSemaphoreTake(lock,portMAX_DELAY);ui->hotspot_saved=true;memcpy(ui->hotspot_ssid,c.wifi.ssid,sizeof ui->hotspot_ssid);xSemaphoreGive(lock);
      bool home_saved=choice.home;wifi_choice_start(&choice,home_saved,true);
      if(!linked&&ready){wanted=true;retries=0;bool started=connect_choice();deadline=esp_timer_get_time()+(started?25000000:3000000);}}
     ESP_LOGI("home_wifi","USB_PROVISION_%s net=hotspot",ok?"SAVED":"FAILED");provision_until=0;
    }else if(result==1){
     bool ok=home_store_save(&store,&c.wifi);
     if(ok){xSemaphoreTake(lock,portMAX_DELAY);ui->credentials=c.wifi;ui->action=HOME_SAVE;ui->busy=true;xSemaphoreGive(lock);}
     ESP_LOGI("home_wifi","USB_PROVISION_%s net=home",ok?"SAVED":"FAILED");provision_until=0;
    }else if(result<0)ESP_LOGW("home_wifi","USB_PROVISION_REJECTED");
    wipe(&c,sizeof c);if(result==1)break;
   }
   wipe(bytes,sizeof bytes);
  }else if(provision_until){wipe(&parser,sizeof parser);provision_until=0;}
  /* After the provisioning window closes, keep accepting the always-on frames (WVC1, WPC1/WLB2,
   * WBS1, WGS1; WPK1 in VOICE_SELFTEST builds) on the LOCAL USB serial. It stays a passive
   * receiver: nothing polls or is transmitted by the board. */
  if(!provision_until){
   static unsigned char ubytes[64];int un=read(STDIN_FILENO,ubytes,sizeof ubytes);
   for(int i=0;i<un;i++){
    feed_voice(&voice_parser,ubytes[i]);
    feed_pair(ubytes[i]);feed_bots(ubytes[i]);feed_gesture(ubytes[i]);
   }
   wipe(ubytes,sizeof ubytes);
  }
  /* Sleep (power_main.c): radio stopped while asleep, rejoined in the background at wake (tiles show
   * "Joining Wi-Fi" meanwhile). USB frames above keep working; everything below waits for the wake. */
  {static bool asleep_now;bool want=__atomic_load_n(&wifi_sleep_req,__ATOMIC_ACQUIRE);
   if(want!=asleep_now){
    if(want){if(ready){esp_wifi_disconnect();esp_wifi_stop();wifi_started=false;}linked=false;deadline=0;status("Wi-Fi off while asleep",false);}
    else if(ready&&wanted){retries=0;status("Reconnecting...",false);bool started=connect_choice();deadline=esp_timer_get_time()+(started?25LL*1000*1000:3LL*1000*1000);}
    else{status(!ready?"Wi-Fi unavailable":"No saved network",false);}
    asleep_now=want;__atomic_store_n(&wifi_sleep_ack,want,__ATOMIC_RELEASE);
    ESP_LOGI("home_wifi","WIFI_SLEEP %s",want?"stopped":"rejoining");
   }
   if(asleep_now){vTaskDelay(pdMS_TO_TICKS(50));continue;}}
  home_action action;home_credentials request={0};bool session_save,session_on,sound_save,sound_on,bot_save,theme_save;int bot_index;uint8_t theme_v;
  xSemaphoreTake(lock,portMAX_DELAY);action=ui->action;ui->action=HOME_NONE;if(action==HOME_SAVE)request=ui->credentials;
  session_save=ui->session_save;ui->session_save=false;session_on=!ui->session_off;
  sound_save=ui->sound_save;ui->sound_save=false;sound_on=!ui->sound_off;
  bot_save=ui->helper.bot_save;ui->helper.bot_save=false;bot_index=ui->helper.bot;
  theme_save=ui->theme_save;ui->theme_save=false;theme_v=theme_nvs_encode(ui->theme_mode,ui->accent);xSemaphoreGive(lock);
  if(theme_save){bool ok=save_theme(theme_v);ESP_LOGI("home_wifi","THEME_SAVED mode=%u accent=%u ok=%d",(unsigned)(theme_v&0x0fu),(unsigned)(theme_v>>4),ok);}
  if(bot_save){bool ok=save_bot(bot_index);ESP_LOGI("home_wifi","BOT_SAVED index=%d ok=%d",bot_index,ok);}
  if(session_save){bool ok=save_session_toggle(session_on);ESP_LOGI("home_wifi","SESSION_TOGGLE_SAVED on=%d ok=%d",session_on,ok);}
  if(sound_save){bool ok=save_sound_toggle(sound_on);ESP_LOGI("home_wifi","SOUND_TOGGLE_SAVED on=%d ok=%d",sound_on,ok);}
  if(action==HOME_SAVE){
   bool ok=storage_ready&&home_store_save(&store,&request);
   xSemaphoreTake(lock,portMAX_DELAY);ui->busy=false;if(ok)ui->saved=true;xSemaphoreGive(lock);
   if(ok){wanted=true;linked=false;retries=0;wifi_choice_start(&choice,true,hotspot);status(ready?"Saved. Connecting...":"Saved. Wi-Fi unavailable",false);if(ready){bool started=connect_saved(&request);deadline=esp_timer_get_time()+(started?25000000:3000000);}}
   else status("Save failed. Please retry",linked);
  }else if(action==HOME_FORGET){
   bool ok=storage_ready&&home_store_forget(&store)&&home_store_forget(&hotspot_store);
   if(ok){wanted=hotspot=false;wifi_choice_start(&choice,false,false);linked=false;deadline=0;if(ready){esp_wifi_disconnect();esp_wifi_stop();}status("Forgotten. Disconnected",false);
    xSemaphoreTake(lock,portMAX_DELAY);ui->hotspot_saved=ui->on_hotspot=false;wipe(ui->hotspot_ssid,sizeof ui->hotspot_ssid);xSemaphoreGive(lock);}
   else status("Forget failed. Please retry",linked);
   xSemaphoreTake(lock,portMAX_DELAY);ui->busy=false;if(ok){ui->saved=false;wipe(&ui->credentials,sizeof ui->credentials);}xSemaphoreGive(lock);
  }
  wipe(&request,sizeof request);
  int event;
  if(xQueueReceive(events,&event,0)==pdTRUE&&wanted&&ready){
   if(event>=1000){
    last_reason=event-1000;ESP_LOGI("home_wifi","STA_DISCONNECTED reason=%d",event-1000);linked=false;
    /* Home first, iPhone hotspot as the fallback (wifi_choice.h): a changed network joins now. */
    wifi_net before=choice.active,next=wifi_choice_failed(&choice,last_reason);
    if(next!=before&&next!=WIFI_NET_NONE){retries=0;status(next==WIFI_NET_HOTSPOT?"Home Wi-Fi not found. Trying iPhone hotspot...":"Trying home Wi-Fi...",false);bool started=connect_choice();deadline=esp_timer_get_time()+(started?25LL*1000*1000:3LL*1000*1000);}
    else{status(retries<5?"Disconnected. Retrying...":"Connection failed. Check settings",false);deadline=esp_timer_get_time()+3LL*1000*1000;}
   }
   else{
    wifi_ap_record_t ap;esp_netif_ip_info_t ip;
    if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK&&esp_netif_get_ip_info(netif,&ip)==ESP_OK&&ip.ip.addr){
     linked=true;retries=0;deadline=0;wifi_choice_connected(&choice);wifi_choice_on_link(&choice,esp_timer_get_time());
     status(choice.active==WIFI_NET_HOTSPOT?"Connected to iPhone hotspot":"Connected to Wi-Fi",true);
     xSemaphoreTake(lock,portMAX_DELAY);snprintf(ui->ip,sizeof ui->ip,IPSTR,IP2STR(&ip.ip));ui->on_hotspot=choice.active==WIFI_NET_HOTSPOT;xSemaphoreGive(lock);
     ESP_LOGI("home_wifi","STA_CONNECTED net=%s ip=" IPSTR " rssi=%d; internet reachability not tested",choice.active==WIFI_NET_HOTSPOT?"hotspot":"home",IP2STR(&ip.ip),ap.rssi);
    }
   }
  }
  /* On the hotspot (metered): every WIFI_HOME_CHECK_US, look for home and move back to it. */
  if(ready&&linked&&wifi_choice_check_home(&choice,esp_timer_get_time())){
   bool seen=home_in_range();ESP_LOGI("home_wifi","HOME_CHECK seen=%d",seen);
   if(seen){wifi_choice_home_seen(&choice);linked=false;retries=0;status("Home Wi-Fi found. Switching...",false);bool started=connect_choice();deadline=esp_timer_get_time()+(started?25LL*1000*1000:3LL*1000*1000);}
  }
  if(ready&&wanted&&!linked&&deadline&&esp_timer_get_time()>=deadline){
   if(retries++<5){esp_wifi_disconnect();esp_wifi_connect();deadline=esp_timer_get_time()+25LL*1000*1000;status("Connecting. Check network...",false);}
   else{
    /* Out of retries on this network: let the choice move on (hotspot <-> home) instead of stopping. */
    wifi_net before=choice.active,next=wifi_choice_failed(&choice,last_reason);
    if(next!=before&&next!=WIFI_NET_NONE){retries=0;bool started=connect_choice();deadline=esp_timer_get_time()+(started?25LL*1000*1000:3LL*1000*1000);}
    else{deadline=0;status(last_reason==WIFI_REASON_AUTH_FAIL?"Auth rejected. Check router":"Connection failed. Check settings",false);}
   }
  }

  vTaskDelay(pdMS_TO_TICKS(50));
 }
}
void home_wifi_start(home_ui *state,SemaphoreHandle_t mutex){
 ui=state;lock=mutex;events=xQueueCreate(1,sizeof(int));assert(events);
 assert(xTaskCreate(wifi_worker,"home_wifi",8192,NULL,3,NULL)==pdPASS);
}
