#include "esp_timer.h"
#include "home_live.h"
#include "live_transport.h"
#include "home_pair.h"
#include "pair_tls.h"
#include "pmu_diag.h"
#include "power_main.h"
#include "esp_random.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/task.h"
#include <time.h>
#include <strings.h>
static home_ui*state;
static SemaphoreHandle_t state_lock;
static esp_err_t response_event(esp_http_client_event_t*event){
 live_http_body*b=event->user_data;
 if(event->event_id==HTTP_EVENT_ON_HEADER&&!strcasecmp(event->header_key,"Content-Type"))b->media_ok=!strcasecmp(event->header_value,"application/octet-stream");
 if(event->event_id==HTTP_EVENT_ON_DATA&&!live_body_append(b,event->data,event->data_len))return ESP_FAIL;
 return ESP_OK;
}
/* Secret-free boot diagnosis for the bridge log (reset reason + PMU power-off source), so a crash
 * while running on battery is visible without USB. boot=<random per boot>, not a device id. */
static void boot_header(esp_http_client_handle_t c){
 static char text[160];
 if(!text[0]){char pmu[112];pmu_boot_summary(pmu,sizeof pmu);snprintf(text,sizeof text,"boot=%08lx %s",(unsigned long)esp_random(),pmu);}
 esp_http_client_set_header(c,"X-Board-Boot",text);
}
/* X-Board-Sleep (SPEC Contract B.4): the sleep that just ended, on the first /v1/live after a wake
 * (power_main.c, sleep_policy.h format). Returns the report's sequence number, 0 = none pending. */
static uint32_t sleep_header(esp_http_client_handle_t c){
 static char text[SLEEP_REPORT_MAX+1];uint32_t seq=0;
 if(!power_sleep_report(text,sizeof text,&seq))return 0;
 esp_http_client_set_header(c,"X-Board-Sleep",text);return seq;
}
/* Pinned to the enrolled bridge's certificate fingerprint; bearer = on-device identity (hex). */
static bool fetch(const pair_record*r,const unsigned char token[32],live_state*out,int*status,int64_t*elapsed){
 live_http_body body={0};char auth[80],hex[65],url[PAIR_URL_MAX+16];
 pair_hex(token,hex);snprintf(auth,sizeof auth,"Bearer %s",hex);provision_wipe(hex,sizeof hex);
 pair_url(url,sizeof url,r->base,"/v1/live");pair_pin_require(pair_tls_ctx(PAIR_TLS_LIVE),r->fp);
 esp_http_client_config_t config={.url=url,.crt_bundle_attach=pair_tls_attach_live,.timeout_ms=2000,.disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=512,.event_handler=response_event,.user_data=&body};
 int64_t start=esp_timer_get_time();esp_http_client_handle_t client=esp_http_client_init(&config);bool ok=false;*status=0;
 if(client){
  esp_err_t error=esp_http_client_set_header(client,"Authorization",auth);boot_header(client);uint32_t slept=sleep_header(client);
  if(error==ESP_OK)error=esp_http_client_perform(client);
  *status=esp_http_client_get_status_code(client);
  if(!pair_tls_ctx(PAIR_TLS_LIVE)->matched)*status=0; /* never trust a response without the pin */
  if(slept&&*status)power_sleep_report_sent(slept);  /* the bridge has it */
  /* Freshness is stamped when the verified body ARRIVES, not when the TLS
   * handshake began: the bridge already refuses (503) samples older than 3 s,
   * and a slow multi-second handshake must not age a just-received snapshot
   * past LIVE_TTL_US before the next poll. */
  if(error==ESP_OK&&*status){ok=live_body_finish(&body,*status,esp_timer_get_time(),out);}
  esp_http_client_cleanup(client);
 }
 provision_wipe(auth,sizeof auth);*elapsed=esp_timer_get_time()-start;return ok;
}
#if WAVESHARE_AI_PLUGIN_AI
/* ---- Ask-page bots: GET /v1/bots (WBT1, per-bot availability from the official quota), same pinned
 * TLS slot as /v1/live and serialized with it on this worker (one live TLS session at a time). ---- */
typedef struct {unsigned char data[BOTS_FRAME_SIZE];size_t length;bool media_ok,overflow;} bots_body;
static esp_err_t bots_event(esp_http_client_event_t*event){
 bots_body*b=event->user_data;
 if(event->event_id==HTTP_EVENT_ON_HEADER&&!strcasecmp(event->header_key,"Content-Type"))b->media_ok=!strcasecmp(event->header_value,"application/octet-stream");
 if(event->event_id==HTTP_EVENT_ON_DATA){
  if(event->data_len<0||(size_t)event->data_len>sizeof b->data-b->length){b->overflow=true;return ESP_FAIL;}
  memcpy(b->data+b->length,event->data,(size_t)event->data_len);b->length+=(size_t)event->data_len;
 }
 return ESP_OK;
}
static bool fetch_bots(const pair_record*r,const unsigned char token[32],bots_data*out,int*status,int64_t*elapsed){
 static bots_body body;memset(&body,0,sizeof body);char auth[80],hex[65],url[PAIR_URL_MAX+16];
 pair_hex(token,hex);snprintf(auth,sizeof auth,"Bearer %s",hex);provision_wipe(hex,sizeof hex);
 pair_url(url,sizeof url,r->base,"/v1/bots");pair_pin_require(pair_tls_ctx(PAIR_TLS_LIVE),r->fp);
 esp_http_client_config_t config={.url=url,.crt_bundle_attach=pair_tls_attach_live,.timeout_ms=4000,.disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=512,.event_handler=bots_event,.user_data=&body};
 int64_t start=esp_timer_get_time();esp_http_client_handle_t client=esp_http_client_init(&config);bool ok=false;*status=0;
 if(client){
  esp_err_t error=esp_http_client_set_header(client,"Authorization",auth);boot_header(client);
  if(error==ESP_OK)error=esp_http_client_perform(client);
  *status=esp_http_client_get_status_code(client);
  if(!pair_tls_ctx(PAIR_TLS_LIVE)->matched)*status=0; /* never trust a response without the pin */
  /* The reset countdown starts when the verified body ARRIVES (reset_in_s is bridge-relative). */
  if(error==ESP_OK&&*status==200&&body.media_ok&&!body.overflow)ok=bots_decode(out,body.data,body.length,esp_timer_get_time());
  esp_http_client_cleanup(client);
 }
 provision_wipe(auth,sizeof auth);*elapsed=esp_timer_get_time()-start;return ok;
}
#endif

#if WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
/* ---- Sensor page: GET /v1/home (WHS1, Home Assistant sensor readings fetched by the bridge through
 * your auth proxy; the bridge holds that machine credential, never the board). Same pinned live slot, only while
 * the Sensor page is visible, never during voice. ---- */
typedef struct {unsigned char data[SENSORS_FRAME_SIZE];size_t length;bool media_ok,overflow;} sensors_body;
static esp_err_t sensors_event(esp_http_client_event_t*event){
 sensors_body*b=event->user_data;
 if(event->event_id==HTTP_EVENT_ON_HEADER&&!strcasecmp(event->header_key,"Content-Type"))b->media_ok=!strcasecmp(event->header_value,"application/octet-stream");
 if(event->event_id==HTTP_EVENT_ON_DATA){
  if(event->data_len<0||(size_t)event->data_len>sizeof b->data-b->length){b->overflow=true;return ESP_FAIL;}
  memcpy(b->data+b->length,event->data,(size_t)event->data_len);b->length+=(size_t)event->data_len;
 }
 return ESP_OK;
}
static bool fetch_sensors(const pair_record*r,const unsigned char token[32],sensors_data*out,int*status,int64_t*elapsed){
 static sensors_body body;memset(&body,0,sizeof body);char auth[80],hex[65],url[PAIR_URL_MAX+16];
 pair_hex(token,hex);snprintf(auth,sizeof auth,"Bearer %s",hex);provision_wipe(hex,sizeof hex);
 pair_url(url,sizeof url,r->base,"/v1/home");pair_pin_require(pair_tls_ctx(PAIR_TLS_LIVE),r->fp);
 esp_http_client_config_t config={.url=url,.crt_bundle_attach=pair_tls_attach_live,.timeout_ms=8000,.disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=512,.event_handler=sensors_event,.user_data=&body};
 int64_t start=esp_timer_get_time();esp_http_client_handle_t client=esp_http_client_init(&config);bool ok=false;*status=0;
 if(client){
  esp_err_t error=esp_http_client_set_header(client,"Authorization",auth);boot_header(client);
  if(error==ESP_OK)error=esp_http_client_perform(client);
  *status=esp_http_client_get_status_code(client);
  if(!pair_tls_ctx(PAIR_TLS_LIVE)->matched)*status=0; /* never trust a response without the pin */
  if(error==ESP_OK&&*status==200&&body.media_ok&&!body.overflow)ok=sensors_decode(out,body.data,body.length,esp_timer_get_time());
  esp_http_client_cleanup(client);
 }
 provision_wipe(auth,sizeof auth);*elapsed=esp_timer_get_time()-start;return ok;
}
static void sensors_service(bool connected,int64_t opened_us){
 if(!connected||time(NULL)<1700000000)return;
 int64_t now=esp_timer_get_time();
 xSemaphoreTake(state_lock,portMAX_DELAY);
 /* Only while the Home Assistant provider's sign-in is not known to be off (signed out, refused or not
  * set up on the bridge: the bridge would answer 409 home_auth). */
 bool due=!home_bots_paused(state)&&!home_ha_off(state)&&sensors_poll_due(&state->sensors,state->page==SENSORS,opened_us,now);
 if(due){state->bots_busy=true;state->sensors.refresh=false;}
 xSemaphoreGive(state_lock);
 if(!due)return;
 static unsigned polls;pair_record record;unsigned char token[32];sensors_data d={0};int status=0;int64_t elapsed=0;
 if(!home_pair_load(&record,token))status=BOTS_HTTP_UNPAIRED;
 else fetch_sensors(&record,token,&d,&status,&elapsed);
 provision_wipe(token,sizeof token);++polls;
 xSemaphoreTake(state_lock,portMAX_DELAY);
 state->bots_busy=false;
 state->sensors.http=d.valid?200:(status==200?0:status);state->sensors.attempts++;state->sensors.attempt_us=esp_timer_get_time();
 if(d.valid){state->sensors.data=d;state->sensors.fails=0;}
 else if(state->sensors.fails<250)state->sensors.fails++;
 unsigned known=0;for(int i=0;i<SENSORS_COUNT;i++)known+=d.r[i].known;
 xSemaphoreGive(state_lock);
 ESP_LOGI("home_live","SENSORS_POLL n=%u http=%d valid=%d state=%u known=%u/%d elapsed_ms=%lld",polls,status,d.valid,d.state,known,SENSORS_COUNT,(long long)(elapsed/1000));
}

#endif
#if WAVESHARE_AI_PLUGIN_SIGNIN
/* ---- Phone sign-in, one per provider: POST /v1/pair/phone/{start,forget}, GET /v1/pair/phone/status
 * (WPH1) with `X-Provider: hermes` (Ask) or `X-Provider: home_assistant` (Sensor); same pinned slot,
 * serialized with /v1/live and /v1/bots on this worker. The bridge does each provider's device grant at
 * that provider's gateway; the board only receives the public user code, the verification URL (drawn as
 * a QR) and a name. 404 = that provider is not set up on the bridge. Logs carry the provider, state,
 * http status, code length and the URL's host+path only -- never the query. ---- */
typedef struct {unsigned char data[PHONE_FRAME_SIZE];size_t length;bool media_ok,overflow;} phone_body;
static esp_err_t phone_event(esp_http_client_event_t*event){
 phone_body*b=event->user_data;
 if(event->event_id==HTTP_EVENT_ON_HEADER&&!strcasecmp(event->header_key,"Content-Type"))b->media_ok=!strcasecmp(event->header_value,"application/octet-stream");
 if(event->event_id==HTTP_EVENT_ON_DATA){
  if(event->data_len<0||(size_t)event->data_len>sizeof b->data-b->length){b->overflow=true;return ESP_FAIL;}
  memcpy(b->data+b->length,event->data,(size_t)event->data_len);b->length+=(size_t)event->data_len;
 }
 return ESP_OK;
}
static bool fetch_phone(const pair_record*r,const unsigned char token[32],const char*provider,const char*path,bool post,phone_status*out,int*status,int64_t*elapsed){
 static phone_body body;memset(&body,0,sizeof body);char auth[80],hex[65],url[PAIR_URL_MAX+32];
 pair_hex(token,hex);snprintf(auth,sizeof auth,"Bearer %s",hex);provision_wipe(hex,sizeof hex);
 pair_url(url,sizeof url,r->base,path);pair_pin_require(pair_tls_ctx(PAIR_TLS_LIVE),r->fp);
 /* start waits for the bridge's gateway round trip (<= 8 s there). */
 esp_http_client_config_t config={.url=url,.crt_bundle_attach=pair_tls_attach_live,.timeout_ms=post?12000:4000,.disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=512,.event_handler=phone_event,.user_data=&body,.method=post?HTTP_METHOD_POST:HTTP_METHOD_GET};
 int64_t start=esp_timer_get_time();esp_http_client_handle_t client=esp_http_client_init(&config);bool ok=false;*status=0;
 if(client){
  esp_err_t error=esp_http_client_set_header(client,"Authorization",auth);boot_header(client);
  if(error==ESP_OK)error=esp_http_client_set_header(client,"X-Provider",provider);
  if(error==ESP_OK)error=esp_http_client_perform(client);
  *status=esp_http_client_get_status_code(client);
  if(!pair_tls_ctx(PAIR_TLS_LIVE)->matched)*status=0; /* never trust a response without the pin */
  if(error==ESP_OK&&*status==200&&body.media_ok&&!body.overflow)ok=phone_decode(out,body.data,body.length,esp_timer_get_time());
  esp_http_client_cleanup(client);
 }
 provision_wipe(auth,sizeof auth);*elapsed=esp_timer_get_time()-start;return ok;
}
static const char*phone_state_name(unsigned s){static const char*n[]={"none","pending","authorized","denied","expired","refused","error"};return s<=PH_ERROR?n[s]:"?";}
/* host+path of the verification URL (query dropped: it carries the user code). */
static void phone_uri_log(const char*uri,char*out,size_t cap){
 size_t n=0;const char*u=strncmp(uri,"https://",8)?uri:uri+8;
 while(u[n]&&u[n]!='?'&&u[n]!='#'&&n+1<cap){out[n]=u[n];n++;}
 out[n]=0;
}
/* One provider's sign-in (tab = SETTINGS_HERMES / SETTINGS_HOME_ASSISTANT): the same rules for both. */
static void phone_service_one(bool connected,int tab){
 if(!connected||time(NULL)<1700000000)return;
 int64_t now=esp_timer_get_time();
 const char*provider=settings_tab_name(tab);   /* "hermes" / "home_assistant" = the X-Provider value */
 xSemaphoreTake(state_lock,portMAX_DELAY);
 phone_view*v=home_tab_phone(state,tab);
 if(!v){xSemaphoreGive(state_lock);return;}
 bool enrolled=state->pair.state>=PAIR_ENROLLED_UNPAIRED;
 /* Polled where its tiles or its tab are (home_phone_visible): Hermes on Home/Ask/Settings, Home
  * Assistant on Home/Sensor/its own tab, or the one shared account tab (SPEC3 Contract S: both are
  * polled while it shows); every ~3 s while its QR shows. */
 bool visible=home_phone_visible(state,tab);
 bool start=enrolled&&v->want_start,forget=enrolled&&v->want_forget;
 bool poll=enrolled&&!start&&!forget&&!home_bots_paused(state)&&phone_poll_due(v,visible,now);
 bool voice=state->helper.net_busy||state->helper.state==HV_LISTENING;
 if(!voice)v->want_start=v->want_forget=false; /* keep a tapped request pending until voice is done */
 /* Never start a phone TLS request while a voice command owns the network (back-and-forth talk:
  * the next press can land while this poll is mid-handshake). bots_busy marks ANY live-slot request,
  * so the voice upload waits for it instead of running two TLS handshakes in internal RAM at once. */
 if(voice)start=forget=poll=false;
 if(start||forget||poll){v->refresh=false;v->poll_us=now;state->bots_busy=true;}
 xSemaphoreGive(state_lock);
 if(!start&&!forget&&!poll)return;
 const char*path=start?"/v1/pair/phone/start":(forget?"/v1/pair/phone/forget":"/v1/pair/phone/status");
 pair_record record;unsigned char token[32];phone_status st={0};int status=0;int64_t elapsed=0;bool ok=false;
 if(home_pair_load(&record,token))ok=fetch_phone(&record,token,provider,path,start||forget,&st,&status,&elapsed);
 provision_wipe(token,sizeof token);
 xSemaphoreTake(state_lock,portMAX_DELAY);
 state->bots_busy=false;
 v->http=status;v->poll_us=esp_timer_get_time();
 bool changed_uri=false,absent=!ok&&status==404;
 if(ok){
  changed_uri=home_provider_apply(state,tab,&st,poll);
  if(changed_uri)phone_qr_encode(v,st.uri);
 }else if(absent){
  phone_absent(v);   /* this provider is not set up on the bridge: its tiles say "Not set up" */
 }else if(start){
  snprintf(v->note,sizeof v->note,"%s",status==503?"Portal unreachable":(status==429?"Wait a moment":(status==401?"Bridge doesn't know board":"Bridge unreachable")));
 }
 home_bots_sync(state,esp_timer_get_time());
 unsigned sel_block=state->helper.block;bool qr=v->qr_ok;int qsize=phone_qr_size(v);bool one_tab=home_signin_shared(state);
 xSemaphoreGive(state_lock);
 char where[64]="";if(ok&&st.uri[0])phone_uri_log(st.uri,where,sizeof where);
 if(start||forget||changed_uri||!ok||st.state!=PH_PENDING)
  ESP_LOGI("home_live","PHONE_%s http=%d valid=%d state=%s flags=%u expires_in=%u code_len=%u name_len=%u uri=%s qr=%d qr_size=%d request_us=%lld mic=%s provider=%s%s shared_tab=%d",
   start?"START":(forget?"FORGET":"STATUS"),status,ok,ok?phone_state_name(st.state):"-",ok?st.flags:0,ok?st.expires_in:0,(unsigned)strlen(st.user_code),(unsigned)strlen(st.name),
   where[0]?where:"-",qr,qsize,(long long)elapsed,sel_block?bots_reason_name(sel_block):"enabled",provider,absent?" not_set_up=1":"",one_tab);
 provision_wipe(&st,sizeof st);
}
/* Each provider whose tiles need a sign-in (plugins.h): Hermes for Ask, Home Assistant for Sensor. */
static void phone_service(bool connected){
#if WAVESHARE_AI_PLUGIN_AI
 phone_service_one(connected,SETTINGS_HERMES);
#endif
#if WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
 phone_service_one(connected,SETTINGS_HOME_ASSISTANT);
#endif
}
#endif
#if WAVESHARE_AI_PLUGIN_AI
static void bots_log(int http,const bots_data*d,int64_t elapsed,unsigned polls){
 if(!d->valid){ESP_LOGI("home_live","BOTS_SAMPLE http=%d valid=0 request_us=%lld polls=%u",http,(long long)elapsed,polls);return;}
 char row[BOTS_MAX][72];
 for(int i=0;i<BOTS_MAX;i++){
  if(i>=d->count){row[i][0]=0;continue;}
  const bots_entry*e=&d->b[i];
  snprintf(row[i],sizeof row[i],"%.11s:%.11s,avail=%d,reason=%s,reset_s=%ld%s",e->id,e->provider,e->available,bots_reason_name(e->reason),
   e->reset_s==BOTS_NONE32?-1L:(long)e->reset_s,e->stale?",stale":"");
 }
 ESP_LOGI("home_live","BOTS_SAMPLE http=%d valid=1 count=%u flags=%u request_us=%lld polls=%u %s %s %s",http,d->count,d->flags,(long long)elapsed,polls,row[0],row[1],row[2]);
}
/* One /v1/bots poll when the Ask page is visible and due, never while the mic records or a voice
 * command talks to the bridge. bots_busy lets the voice upload wait for us. */
static void bots_service(bool connected,int64_t opened_us){
 if(!connected||time(NULL)<1700000000)return; /* not ready (Wi-Fi/SNTP): not an attempt */
 int64_t now=esp_timer_get_time();
 xSemaphoreTake(state_lock,portMAX_DELAY);
 bool due=!home_bots_paused(state)&&bots_poll_due(&state->bots,home_bots_surface(state),opened_us,now);
 if(due){state->bots_busy=true;state->bots.refresh=false;}
 xSemaphoreGive(state_lock);
 if(!due)return;
 static unsigned polls;pair_record record;unsigned char token[32];bots_data d={0};int status=0;int64_t elapsed=0;
 if(!home_pair_load(&record,token))status=BOTS_HTTP_UNPAIRED;
 else fetch_bots(&record,token,&d,&status,&elapsed);
 provision_wipe(token,sizeof token);++polls;
 xSemaphoreTake(state_lock,portMAX_DELAY);
 state->bots_busy=false;
 state->bots.http=d.valid?200:(status==200?0:status);state->bots.attempts++;state->bots.attempt_us=esp_timer_get_time();
 if(d.valid){state->bots.data=d;state->bots.fails=0;} /* keep the last good reading on any failure */
 else if(state->bots.fails<250)state->bots.fails++;
 home_bots_sync(state,esp_timer_get_time());
 unsigned sel=(unsigned)state->helper.bot,block=state->helper.block;
 xSemaphoreGive(state_lock);
 bots_log(status,&d,elapsed,polls);
 ESP_LOGI("home_live","BOTS_SELECTED index=%u mic=%s",sel,block?bots_reason_name(block):"enabled");
}
#endif
static void worker(void*unused){
 (void)unused;bool sntp=false;int last_state=-1,last_page=-1;unsigned sequence=0,last_level=255;int64_t bots_opened=0;
 for(;;){
  bool connected,off,helper_busy;int page;
  /* Asleep (power_main.c): no bridge polling at all (live feed, bots, sensors, phone sign-in). */
  if(power_sleeping()){last_state=-1;vTaskDelay(pdMS_TO_TICKS(250));continue;}
  xSemaphoreTake(state_lock,portMAX_DELAY);connected=state->connected;off=state->session_off;helper_busy=state->page==HELPER||state->helper.net_busy;page=state->page;xSemaphoreGive(state_lock);
  /* Connect-page requests (scan/enroll/forget) run here, so pairing TLS never overlaps live TLS. */
  if(home_pair_service(connected)){last_state=-1;vTaskDelay(pdMS_TO_TICKS(100));continue;}
  /* Ask page: a page change is an 'open' (re-poll unless a poll just finished). Serialized with the
   * live poll on the same pinned slot (one TLS session at a time); pauses itself for voice. */
  if(page!=last_page)bots_opened=esp_timer_get_time();
  last_page=page;
  if(!sntp&&connected){esp_sntp_config_t clock=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");clock.wait_for_sync=false;sntp=esp_netif_sntp_init(&clock)==ESP_OK;}
  /* Plugin services (plugins.h): each is compiled in only with its plugin. */
#if WAVESHARE_AI_PLUGIN_AI
  bots_service(connected,bots_opened);
#endif
#if WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
  sensors_service(connected,bots_opened);
#endif
#if WAVESHARE_AI_PLUGIN_SIGNIN
  phone_service(connected);
#endif
  (void)bots_opened;
  live_state sample={0};pair_record record;unsigned char token[32];int status=0,phase=0;int64_t elapsed=0;
  /* 'Session sparkle' OFF: no config read, no TLS, no HTTP -- polling stops
   * entirely and the snapshot stays cleared (calm default ambient). */
  /* Without the Sparkles plugin nothing draws the live feed: never poll it (phase 7). */
  if(!WAVESHARE_AI_PLUGIN_SPARKLES){phase=7;}
  else if(off){phase=5;home_pair_live_forget();}
  else if(!connected){home_pair_live_forget();}   /* Sparkles tile: "Joining Wi-Fi", then loading again */
  /* Helper page / voice command in flight: no parallel TLS session (internal RAM) and no
   * markers are drawn there anyway. The feed goes stale -> calm, and resumes on leaving. */
  else if(helper_busy){phase=6;}
  else if(connected){
   if(!sntp){esp_sntp_config_t clock=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");clock.wait_for_sync=false;sntp=esp_netif_sntp_init(&clock)==ESP_OK;}
   if(time(NULL)<1700000000){phase=1;}
   else if(!home_pair_load(&record,token)){phase=2;}
   else{
    /* Claim the live TLS slot atomically against a mic press; skip this round if voice won. */
    xSemaphoreTake(state_lock,portMAX_DELAY);bool go=!state->helper.net_busy&&state->page!=HELPER&&state->helper.state!=HV_LISTENING;if(go)state->bots_busy=true;xSemaphoreGive(state_lock);
    if(go){phase=fetch(&record,token,&sample,&status,&elapsed)?3:4;home_pair_live_status(status);
     xSemaphoreTake(state_lock,portMAX_DELAY);state->bots_busy=false;xSemaphoreGive(state_lock);}
    else phase=6;
   }
  }
  provision_wipe(token,sizeof token);
  /* Re-check under the lock: a toggle to OFF during a fetch must not let a
   * late sample resurrect markers/level. */
  xSemaphoreTake(state_lock,portMAX_DELAY);if(state->session_off){memset(&sample,0,sizeof sample);}state->live=sample;xSemaphoreGive(state_lock);
  if(phase!=last_state&&(phase==5||last_state==5))ESP_LOGI("home_live","LIVE_POLLING %s note=session-sparkle-toggle",phase==5?"stopped":"resumed");
  unsigned level=sample.valid?sample.level:255;
  if(phase!=last_state||level!=last_level||(phase==3&&sequence%4==0)){
   ESP_LOGI("home_live","LIVE_SAMPLE phase=%d http=%d valid=%d format=WLS4 count=%u level=%d flags=%u bytes=%u request_us=%lld sequence=%u",phase,status,sample.valid,sample.count,sample.valid?(int)sample.level:-1,sample.flags,sample.valid?(unsigned)(8+sample.count*10):0u,(long long)elapsed,sequence);
  }
  ++sequence;last_state=phase;last_level=level;
  vTaskDelay(pdMS_TO_TICKS(off||helper_busy||phase==2?500:1500));
 }
}
void home_live_start(home_ui*s,SemaphoreHandle_t mutex){state=s;state_lock=mutex;assert(xTaskCreatePinnedToCore(worker,"home_live",16384,NULL,2,NULL,0)==pdPASS);}
