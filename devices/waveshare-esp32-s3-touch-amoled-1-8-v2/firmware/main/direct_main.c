#include <assert.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "driver/gpio.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "home_render.h"
#include "home_wifi.h"
#include "home_live.h"
#include "plugins.h"
#if WAVESHARE_AI_PLUGIN_AI
#include "helper_voice.h"
#endif
#include "direct_present.h"
#include "panel_power.h"
#include "panel_ui.h"
#include "pmu_diag.h"
#include "freeze_guard.h"
#include "power_main.h"
#include "sleep_policy.h"
#include "esp_system.h"
#define W SPARKLES_W
#define H SPARKLES_H
#define WAVESHARE_AI_BOOT_GPIO GPIO_NUM_0
#define WAVESHARE_AI_BOOT_ACTIVE_LEVEL 0
#define BOOT_DEBOUNCE_MS 40u
static const char *TAG="ws_direct";
static SemaphoreHandle_t done,input_lock;
/* ~25 KB each with the chat history: kept in PSRAM (.ext_ram.bss), never in scarce internal RAM. */
static EXT_RAM_BSS_ATTR home_ui ui;
/* Guaranteed owner-only fallback for a circular reveal when either optional snapshot allocation is
 * unavailable. It replaces large automatic home_ui copies and never aliases the presented frames. */
static EXT_RAM_BSS_ATTR uint16_t home_reveal_fallback[W*H];
static panel_power power_state;
static int64_t origin;
/* Freeze guard heartbeats (freeze_guard.h): plain atomics, never a lock, so a stuck task cannot
 * also stop the guard. RTC_NOINIT keeps the last freeze's cause across the restart. */
static int64_t owner_beat,touch_beat;
static bool panel_hw_on=true;  /* the panel state the owner last applied (power_main.c waits on it) */
static uint32_t owner_phase,present_phase,guard_page;
static RTC_NOINIT_ATTR freeze_record last_freeze;
#define BEAT(v) __atomic_store_n(&(v),esp_timer_get_time(),__ATOMIC_RELAXED)
#define PHASE(v,p) __atomic_store_n(&(v),(uint32_t)(p),__ATOMIC_RELAXED)
static void freeze_guard_task(void *arg){
 (void)arg;
 for(;;){
  vTaskDelay(pdMS_TO_TICKS(500));
  int64_t now=esp_timer_get_time();
  /* Paused while asleep: esp_timer runs on through light sleep (power_main.c re-arms both beats). */
  int why=sleep_guard_check(now,__atomic_load_n(&owner_beat,__ATOMIC_RELAXED),__atomic_load_n(&touch_beat,__ATOMIC_RELAXED),power_guard_paused());
  if(why==FREEZE_NONE)continue;
  uint32_t op=__atomic_load_n(&owner_phase,__ATOMIC_RELAXED),pp=__atomic_load_n(&present_phase,__ATOMIC_RELAXED);
  if(why==FREEZE_OWNER&&op==PH_WAIT_PANEL&&pp>=PP_SENDING)why=FREEZE_PRESENT;  /* the panel transfer hung */
  last_freeze=(freeze_record){FREEZE_MAGIC,(uint32_t)why,op,pp,__atomic_load_n(&guard_page,__ATOMIC_RELAXED),(uint32_t)(now/1000000)};
  ESP_LOGE(TAG,"FREEZE task=%s owner_phase=%u present_phase=%u page=%u uptime_s=%u: restarting",
           freeze_reason_name((uint32_t)why),(unsigned)op,(unsigned)pp,(unsigned)last_freeze.page,(unsigned)last_freeze.uptime_s);
  vTaskDelay(pdMS_TO_TICKS(50));  /* let the log line drain */
  esp_restart();
 }
}
static bool transfer_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *arg){
 (void)io;(void)event;(void)arg;
 BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(done,&wake);return wake==pdTRUE;
}
/* Only this task touches the touch driver. It never touches display resources.
 * Run independently of CPU rendering / LCD waits. No fake contact is ever injected here. */
/* Settings > Battery: read the AXP2101 every BATTERY_POLL_US on this small low-priority task (a few
 * short I2C reads on the shared bus), never on the render path; the view lands in `ui` under the
 * state lock and the time-to-full estimate lives here (battery_estimate.h). One log line per change. */
static void battery_poll(void *arg){
 (void)arg;static battery_estimator est;battery_estimate_reset(&est);
 int logged_state=-1,logged_pct=-2,logged_eta=-2;
 for(;;){
  if(power_sleeping()){vTaskDelay(pdMS_TO_TICKS(1000));continue;}  /* asleep: no I2C here (light sleep) */
  pmu_sample m=pmu_read();int64_t now=esp_timer_get_time();
  battery_view v=battery_update(&est,&m,now);
  xSemaphoreTake(input_lock,portMAX_DELAY);ui.battery=v;xSemaphoreGive(input_lock);
  battery_state st=battery_state_of(&v);
  if((int)st!=logged_state||v.percent!=logged_pct||v.eta_min!=logged_eta){
   ESP_LOGI(TAG,"BATTERY state=%s pct=%d vbus=%d chg=%d vbat_mv=%d eta_min=%d",battery_state_name(st),v.percent,v.vbus,v.charging,v.vbat_mv,v.eta_min);
   logged_state=(int)st;logged_pct=v.percent;logged_eta=v.eta_min;
  }
  vTaskDelay(pdMS_TO_TICKS(BATTERY_POLL_US/1000));
 }
}
static void touch_poll(void *arg){
 esp_lcd_touch_handle_t touch=arg;TickType_t tick=xTaskGetTickCount();
 home_page logged_page=HOME;  /* the page the HOME_PAGE log last reported (boot: Home, HOME_READY) */
 for(;;){
  BEAT(touch_beat);
  bool boot_pressed=gpio_get_level((gpio_num_t)WAVESHARE_AI_BOOT_GPIO)==WAVESHARE_AI_BOOT_ACTIVE_LEVEL;
  esp_lcd_touch_point_data_t p={0};uint8_t count=0;
  esp_err_t err=esp_lcd_touch_read_data(touch);
  if(err==ESP_OK)err=esp_lcd_touch_get_data(touch,&p,&count,1);
  int64_t now=esp_timer_get_time()-origin;
  bool boot_down=power_main_boot(boot_pressed);  /* power_button.h: a BOOT press = sleep / wake (power_main.c) */
  if(err==ESP_OK&&count>0)power_main_activity();  /* restarts the battery auto-sleep clock */
  xSemaphoreTake(input_lock,portMAX_DELAY);
  if(!boot_down)panel_power_boot_released(&power_state);
  if(err==ESP_OK)panel_power_touch_sample(&power_state,count>0);
  bool suppress=panel_power_touch_suppressed(&power_state);
  xSemaphoreGive(input_lock);
  bool contact=!suppress&&err==ESP_OK&&count>0;unsigned sx=p.x,sy=p.y;
  /* Bridge the controller's single-poll dropouts and report the release at the last real point. */
  {static touch_debounce debounce;int dx_,dy_;
   if(suppress||err!=ESP_OK)debounce.down=false;
   else{contact=touch_debounce_step(&debounce,contact,(int)sx,(int)sy,&dx_,&dy_);if(contact){sx=(unsigned)dx_;sy=(unsigned)dy_;}}}
  xSemaphoreTake(input_lock,portMAX_DELAY);
  home_page previous=ui.page;
  /* A page changed between polls (USB WGS1 replays, WLV1, WPC1 run under the lock outside this
   * poller): logged below too, or those page changes never show in the log. */
  bool usb_page=previous!=logged_page;
  if(err!=ESP_OK){++ui.input.errors;ui.consumed=true;}
  if(suppress){
   panel_ui_suppress(&ui,now);
  }else home_sample(&ui,now,contact,sx,sy);
  home_page page=ui.page;
  home_motion_note note=ui.note;memset(&ui.note,0,sizeof ui.note);  /* also notes of USB WGS1 replays */
  xSemaphoreGive(input_lock);
  /* No key coordinates/timing or credentials in serial logs. */
  if(usb_page)ESP_LOGI(TAG,"HOME_PAGE page=%d source=usb stamp_us=%lld",previous,(long long)now);
  if(previous!=page)ESP_LOGI(TAG,"HOME_PAGE page=%d stamp_us=%lld touch_stack_free=%u",page,(long long)now,(unsigned)uxTaskGetStackHighWaterMark(NULL));
  logged_page=page;
  if(note.pull)ESP_LOGI(TAG,"HOME_PULL start page=%d wait_ms=%d",previous,note.wait_ms);
  if(note.gesture)ESP_LOGI(TAG,"HOME_GESTURE kind=%s offset=%d speed_milli=%d drag_ms=%d anim_ms=%d",note.home?"home":"cancel",note.offset,note.speed_milli,note.drag_ms,note.anim_ms);
  if(note.done)ESP_LOGI(TAG,"HOME_PULL end kind=%s ms=%d",note.done_home?"drop":"back",note.done_ms);
  if(note.slide)ESP_LOGI(TAG,"HOME_SLIDE from=%d to=%d ms=%d",note.from,note.to,note.ms);
  /* Asleep: poll 10x slower (power saving); input stays suppressed until wake anyway. */
  xSemaphoreTake(input_lock,portMAX_DELAY);int poll_ms=panel_power_poll_ms(&power_state);xSemaphoreGive(input_lock);
  power_main_touch_park();  /* asleep: parked here (no touch I2C) until the wake */
  if(poll_ms!=10)tick=xTaskGetTickCount();
  xTaskDelayUntil(&tick,pdMS_TO_TICKS(poll_ms));
 }
}
static int send_stripe(void *arg,int y,int h,const uint8_t *pixels){
 PHASE(present_phase,PP_DRAW);
 esp_err_t err=esp_lcd_panel_draw_bitmap((esp_lcd_panel_handle_t)arg,0,y,W,y+h,pixels);
 PHASE(present_phase,PP_SENDING);
 if(err!=ESP_OK)ESP_LOGE(TAG,"draw error: %s",esp_err_to_name(err));
 return err==ESP_OK;
}
static int wait_stripe(void *arg){
 (void)arg;PHASE(present_phase,PP_WAIT);bool ok=xSemaphoreTake(done,pdMS_TO_TICKS(2000))==pdTRUE;PHASE(present_phase,PP_SENDING);return ok;
}
/* Pipelined presentation: the panel transfer (~29 ms of DMA per frame) ran after each render with the
 * CPU idle, so a frame cost render + transfer (~70 ms, 14 fps, measured over USB). Two PSRAM frames:
 * the presenter streams one to the panel while the owner renders the next -> max(render, transfer).
 * One frame in flight; the owner hands a frame over only once the previous one fully reached the
 * panel, so a frame is never drawn into while it is being sent. Fail-closed: on a
 * transfer failure the presenter halts without reusing either DMA buffer or the frame. */
typedef struct{esp_lcd_panel_handle_t panel;uint8_t*a,*b;}present_ctx;
static QueueHandle_t present_queue;
static SemaphoreHandle_t present_idle;
static int64_t present_last_us;
static void presenter(void *arg){
 const present_ctx*c=arg;
 for(;;){
  const uint16_t*frame=NULL;PHASE(present_phase,PP_WAITING);xQueueReceive(present_queue,&frame,portMAX_DELAY);
  PHASE(present_phase,PP_SENDING);
  int64_t t0=esp_timer_get_time();
  if(!direct_present(frame,c->a,c->b,DIRECT_DMA_BYTES,send_stripe,wait_stripe,c->panel)){
   /* Never reuse uncertain DMA memory: stop here; the freeze guard restarts the board (it sees the
    * owner stuck waiting for this frame), which releases the transfer safely. Was: halted forever. */
   ESP_LOGE(TAG,"display transfer failed/timeout; presenter halted, freeze guard will restart");
   for(;;)vTaskSuspend(NULL);
  }
  __atomic_store_n(&present_last_us,esp_timer_get_time()-t0,__ATOMIC_RELAXED);
  xSemaphoreGive(present_idle);
 }
}
/* The panel is quiet (no frame in flight): required before panel power commands. */
static void present_quiesce(void){xSemaphoreTake(present_idle,portMAX_DELAY);xSemaphoreGive(present_idle);}
static void owner(void *arg){
 (void)arg;
 ESP_LOGI(TAG,"MODE=direct-native-rgb565; touch target=100Hz; callback-complete presentation");
 const esp_app_desc_t *desc=esp_app_get_description();char hash[65];
 for(int i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",desc->app_elf_sha256[i]);
 ESP_LOGI(TAG,"DIRECT_ID elf_sha256=%s",hash);
 if(last_freeze.magic==FREEZE_MAGIC)ESP_LOGW(TAG,"LAST_FREEZE task=%s owner_phase=%u present_phase=%u page=%u uptime_s=%u",
  freeze_reason_name(last_freeze.reason),(unsigned)last_freeze.owner_phase,(unsigned)last_freeze.present_phase,(unsigned)last_freeze.page,(unsigned)last_freeze.uptime_s);
 last_freeze.magic=0;
 /* CPU0, above the voice/live workers: it only reads atomics and sleeps. */
 assert(xTaskCreatePinnedToCore(freeze_guard_task,"freeze_guard",3072,NULL,4,NULL,0)==pdPASS);
 esp_lcd_touch_handle_t touch=NULL;esp_lcd_panel_handle_t panel=NULL;esp_lcd_panel_io_handle_t io=NULL;
 ESP_LOGI(TAG,"initializing touch before display (BSP V2 gap detection)");
 ESP_ERROR_CHECK(bsp_touch_new(NULL,&touch));
 (void)pmu_diag_init(); /* read-only: last PMU power-off source + ESP reset reason */
 bsp_display_config_t display={.max_transfer_sz=DIRECT_DMA_BYTES};
 ESP_ERROR_CHECK(bsp_display_new(&display,&panel,&io));
 done=xSemaphoreCreateBinary();input_lock=xSemaphoreCreateMutex();assert(done&&input_lock);
 esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=transfer_done};
 ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io,&callbacks,NULL));
 /* CPU0, lowest priority: the PMU poll for Settings > Battery (needs input_lock; the PMU is set up above). */
 assert(xTaskCreatePinnedToCore(battery_poll,"battery",4096,NULL,1,NULL,0)==pdPASS);
 ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));ESP_ERROR_CHECK(bsp_display_brightness_set(70));
 /* Internal pull-up as well: a floating BOOT line must never toggle the screen off by itself. */
 gpio_config_t boot_cfg={.pin_bit_mask=1ULL<<WAVESHARE_AI_BOOT_GPIO,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,
                         .pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
 ESP_ERROR_CHECK(gpio_config(&boot_cfg));
 uint16_t *shadows[2]={heap_caps_calloc(W*H,sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
                      heap_caps_calloc(W*H,sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)};
 uint8_t *dma_a=heap_caps_malloc(DIRECT_DMA_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
 uint8_t *dma_b=heap_caps_malloc(DIRECT_DMA_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
 assert(shadows[0]&&shadows[1]&&dma_a&&dma_b&&(((uintptr_t)dma_a|(uintptr_t)dma_b)&3)==0);
 static present_ctx present;present=(present_ctx){panel,dma_a,dma_b};
 present_queue=xQueueCreate(1,sizeof(const uint16_t*));present_idle=xSemaphoreCreateBinary();
 assert(present_queue&&present_idle);xSemaphoreGive(present_idle);
 /* CPU1, with the owner: the panel's SPI bus and its interrupt were set up here, and the esp_lcd SPI
  * calls wait on them with portMAX_DELAY. From CPU0 the presenter froze the board inside
  * esp_lcd_panel_draw_bitmap twice in three 10-min Ask-page soaks (FREEZE task=present, no transfer
  * timeout logged). Higher priority than the owner so a finished stripe is refilled at once. */
 assert(xTaskCreatePinnedToCore(presenter,"direct_present",4096,&present,6,NULL,1)==pdPASS);
 int back=0;
 /* Page slides and the Home pull's circle (home_render.h home_compose): one PSRAM frame holds the
  * untransformed outgoing page and one holds Home inside the expanding aperture, rendered once per pull.
  * last_frame = the frame last handed to the panel and last_page its page when it was a whole page at
  * rest (a snapshot source), else -1. */
 static home_snapshot page_snap;
 home_reveal_set_scratch(home_reveal_fallback);
 page_snap.px=heap_caps_malloc(W*H*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(page_snap.px)page_snap.home=heap_caps_malloc(W*H*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 ESP_LOGI(TAG,"HOME_SNAPSHOT bytes=%u ok=%d home_cache=%d%s",(unsigned)(W*H*sizeof(uint16_t)),page_snap.px!=NULL,page_snap.home!=NULL,page_snap.px?"":" fallback=owner-scratch");
 const uint16_t*last_frame=NULL;int last_page=-1;
 origin=esp_timer_get_time();
 panel_power_init(&power_state,true,BOOT_DEBOUNCE_MS);
 direct_set_usage(&ui.input,SP_USAGE_DEFAULT); /* calm mid default; ambient-density scalar, RAM only */
 ESP_LOGI(TAG,"USAGE_DEFAULT usage_milli=%d note=ambient-density-only-RAM",(int)(SP_USAGE_DEFAULT*1000));
 assert(xTaskCreatePinnedToCore(touch_poll,"direct_touch",6144,touch,6,NULL,1)==pdPASS); /* 4 KB left ~2.5 KB free */
 /* Power key + sleep/wake/off (power_main.c): AXP2101 PWRON IRQ poll, BOOT presses, light sleep. */
 {power_ctx pc={&ui,input_lock,&power_state,&owner_beat,&touch_beat,&panel_hw_on,WAVESHARE_AI_BOOT_GPIO};power_main_start(&pc);}
 home_wifi_start(&ui,input_lock);
#if WAVESHARE_AI_PLUGIN_BRIDGE
 home_live_start(&ui,input_lock);  /* bridge worker: pairing, phone sign-in, live feed, bots, sensors */
#endif
#if WAVESHARE_AI_PLUGIN_AI
 helper_voice_start(&ui,input_lock);
#endif
 ESP_LOGI(TAG,"HOME_READY page=0 tiles=%d providers=%s plugins=%s%s%s; pull down from the lower part = Home",HOME_TILES,
  WAVESHARE_AI_PROVIDER_HERMES?(WAVESHARE_AI_PROVIDER_HOME_ASSISTANT?"hermes,home_assistant":"hermes"):(WAVESHARE_AI_PROVIDER_HOME_ASSISTANT?"home_assistant":"none"),
  WAVESHARE_AI_PLUGIN_SPARKLES?"sparkles,":"",WAVESHARE_AI_PLUGIN_AI?"ai,":"",WAVESHARE_AI_PLUGIN_HOME_ASSISTANT?"home_assistant,":"");
 unsigned presents=0;int64_t last_present=0,render_sum=0,transfer_sum=0,last_health=0;
 /* UI snapshots live in static PSRAM storage, keeping per-bot history and provider sign-in views
  * off this 16 KB stack. Only this task touches them. */
 static EXT_RAM_BSS_ATTR home_ui painted,screen;bool has_painted=false;helper_state painted_state=HV_IDLE;
 bool hardware_display_on=true;
 for(;;){
  int64_t start=esp_timer_get_time();
  BEAT(owner_beat);PHASE(owner_phase,PH_LOOP);
  xSemaphoreTake(input_lock,portMAX_DELAY);bool display_on=power_state.display_on;xSemaphoreGive(input_lock);
  if(display_on!=hardware_display_on){
   PHASE(owner_phase,PH_POWER);present_quiesce();
   /* Sleep (power_main.c decides: PWR key, BOOT, auto sleep): backlight 0, panel off and the panel
    * controller in sleep mode; the power task then parks touch, stops Wi-Fi and light-sleeps on battery.
    * Never abort on a panel command failure: log and retry next loop. */
   /* Wake reverses it: sleep-out, 120 ms, display on, and the last page repaints at once. */
   esp_err_t perr;
   if(display_on){perr=esp_lcd_panel_disp_sleep(panel,false);if(perr==ESP_OK){vTaskDelay(pdMS_TO_TICKS(120));perr=esp_lcd_panel_disp_on_off(panel,true);}
    if(perr==ESP_OK)perr=bsp_display_brightness_set(70);}
   else{perr=bsp_display_brightness_set(0);if(perr==ESP_OK)perr=esp_lcd_panel_disp_on_off(panel,false);if(perr==ESP_OK)perr=esp_lcd_panel_disp_sleep(panel,true);}
   home_wifi_power_save(!display_on);
   if(perr!=ESP_OK){ESP_LOGW(TAG,"PANEL_POWER_RETRY display=%s err=%s",display_on?"on":"off",esp_err_to_name(perr));vTaskDelay(pdMS_TO_TICKS(50));continue;}
   hardware_display_on=display_on;__atomic_store_n(&panel_hw_on,display_on,__ATOMIC_RELEASE);
   if(display_on){has_painted=false;last_page=-1;}
   xSemaphoreTake(input_lock,portMAX_DELAY);
   ESP_LOGI(TAG,"PANEL_POWER display=%s owner_stack_free=%u page=%d bot=%d helper=%d session=%d command_present=%d stamp_us=%lld",display_on?"on":"off",
            (unsigned)uxTaskGetStackHighWaterMark(NULL),ui.page,ui.helper.bot,ui.helper.state,!ui.session_off,ui.helper.id[0]!=0,(long long)start);
   xSemaphoreGive(input_lock);
  }
  if(!display_on){vTaskDelay(pdMS_TO_TICKS(10));continue;}
  PHASE(owner_phase,PH_SNAPSHOT);
  xSemaphoreTake(input_lock,portMAX_DELAY);screen=ui;xSemaphoreGive(input_lock);screen.live_now_us=start;
  __atomic_store_n(&guard_page,(uint32_t)screen.page,__ATOMIC_RELAXED);
  if(has_painted&&home_visual_equal(&screen,&painted)){
   if(screen.page!=SETTINGS&&start-last_health>=1000000){direct_input health=screen.input;
    ESP_LOGI(TAG,"DIRECT_IDLE n=%u stamp_us=%lld page=%d touch_samples=%u touch_stamp_us=%lld touch_max_gap_us=%lld touch_errors=%u heap=%u",presents,(long long)start,screen.page,health.samples,(long long)health.stamp_us,(long long)health.max_interval_us,health.errors,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));last_health=start;
   }
   vTaskDelay(pdMS_TO_TICKS(10));continue;
  }
  direct_input snap=screen.input;
  int64_t sample_age=esp_timer_get_time()-origin-snap.stamp_us;
  memset(sp_stages,0,sizeof sp_stages);
  uint16_t*shadow=shadows[back];
  PHASE(owner_phase,PH_RENDER);
  assert(home_compose(&screen,shadow,W*H,&page_snap,last_frame,last_page));  /* = home_render unless a page slides */
  int64_t render_end=esp_timer_get_time();
  /* The previous frame must be fully on the panel (the presenter halts forever on a failure, so this
   * then blocks forever too: no buffer is reused). Then hand this one over and render into the other. */
  PHASE(owner_phase,PH_WAIT_PANEL);
  xSemaphoreTake(present_idle,portMAX_DELAY);
  PHASE(owner_phase,PH_HANDOFF);
  const uint16_t*handoff=shadow;xQueueSend(present_queue,&handoff,portMAX_DELAY);back^=1;
  last_frame=shadow;last_page=home_layer_rest(&screen)&&!screen.power.countdown?(int)screen.page:-1;
  int64_t stamp=esp_timer_get_time(),transfer_us=__atomic_load_n(&present_last_us,__ATOMIC_RELAXED);++presents;painted=screen;has_painted=true;
  if(screen.page==HELPER){xSemaphoreTake(input_lock,portMAX_DELAY);ui.helper.scroll_max=helper_scroll_extent;xSemaphoreGive(input_lock);}
  render_sum+=render_end-start;transfer_sum+=transfer_us;
  xSemaphoreTake(input_lock,portMAX_DELAY);direct_input latest=ui.input;xSemaphoreGive(input_lock);
  /* Home tile link states (tile_plugins.h), logged on change: state names and fixed status text only. */
  {
   static char tiles_logged[160];
   char line[160];size_t used=0;line[0]=0;
   for(int t=0;t<HOME_TILES&&used<sizeof line;t++){
    tile_status ts=home_tile_status(&screen,t);
    int n=snprintf(line+used,sizeof line-used,"%s%s=%s%s%s",t?" ":"",home_tiles[t].name,ts.state==TILE_ON?"on":ts.state==TILE_LOADING?"loading":"off",
     ts.label[0]?":":"",ts.label);
    if(n<0){break;}
    used+=(size_t)n;
   }
   if(strcmp(line,tiles_logged)){memcpy(tiles_logged,line,sizeof line);ESP_LOGI(TAG,"HOME_TILES %s",line);}
  }
  /* Settings screens are event-driven stills: log which screen/targets were painted. Actions only,
   * never labels (a label can be the signed-in display name) or the SSID. */
  static uint32_t painted_settings=0;
  if(home_settings_page(&screen)){
   settings_screen m=home_settings_screen(&screen);settings_buttons b=home_settings_buttons(&screen,start);
   char acts[64]="";size_t used=0;
   for(int k=0;k<b.count;k++){const char*a=settings_action_name(b.t[k].action);size_t n=strlen(a);
    if(used+n+2<sizeof acts){if(k)acts[used++]=',';memcpy(acts+used,a,n);used+=n;acts[used]=0;}}
   int min_h=0;for(int k=0;k<b.count;k++)if(!min_h||b.t[k].h<min_h)min_h=b.t[k].h;
   uint32_t key=provision_crc((const unsigned char*)acts,strlen(acts))^((uint32_t)m+1u)*2654435761u^(uint32_t)screen.session_off;
   if(key!=painted_settings)ESP_LOGI(TAG,"SETTINGS_SCREEN screen=%s targets=%d actions=%s min_target_h=%d signed_in=%d sparkle=%d render_us=%lld transfer_us=%lld owner_stack_free=%u",
    settings_screen_name(m),b.count,acts[0]?acts:"-",min_h,screen.phone.st.valid&&screen.phone.st.state==PH_AUTHORIZED,!screen.session_off,
    (long long)(render_end-start),(long long)transfer_us,(unsigned)uxTaskGetStackHighWaterMark(NULL));
   painted_settings=key;
  }else painted_settings=0;
  /* Every frame while the page layer moves (the Home pull's drag, drop or spring back; a tile opening):
   * the board measures the gesture's frame interval from these rows (blob = the pull's motion kind:
   * 1 held, 2 spring back, 3 drop; 0 otherwise). */
  bool layer_moving=!home_layer_rest(&screen);
  bool log_timing=layer_moving||(screen.page!=SETTINGS&&(presents<=5||presents%15==0||(screen.page==HELPER&&screen.helper.state!=painted_state)));
  painted_state=screen.helper.state;
  if(log_timing){
   unsigned trail_alive=0;
   for(unsigned i=0;i<screen.input.scene.trail_count&&i<SP_TRAIL_MAX;i++){
    float age=screen.input.scene.time-screen.input.scene.trail[i].born;
    if(age>=0&&age<SP_TRAIL_LIFE)++trail_alive;
   }
   ESP_LOGI(TAG,"DIRECT_ACTIVITY ambient=%d trail_alive=%u live_flags=%u owner_stack_free=%u",screen.page==SPARKLES&&home_live_density(&screen)>0,trail_alive,screen.live.flags,(unsigned)uxTaskGetStackHighWaterMark(NULL));
  }
  if(log_timing)ESP_LOGI(TAG,
   "DIRECT_TIMING n=%u stamp_us=%lld render_us=%lld water_us=%lld glint_us=%lld composite_us=%lld transfer_us=%lld interval_us=%lld sample_age_us=%lld touch_samples=%u touch_stamp_us=%lld touch_max_gap_us=%lld touch_errors=%u edges=%u heap=%u render_sum_us=%lld transfer_sum_us=%lld water_updates=%u page=%d live=%u strength_milli=%d hold_ms=%d usage_milli=%d density=%d live_level=%d session=%d helper_state=%d mic_level_milli=%u orbs_us=%lld bot=%d mic_block=%u slide=%d present_wait_us=%lld blob=%d",
   presents,(long long)stamp,(long long)(render_end-start),(long long)(sp_stages[1]-sp_stages[0]),(long long)(sp_stages[2]-sp_stages[1]),(long long)(sp_stages[3]-sp_stages[2]),(long long)transfer_us,(long long)(last_present?stamp-last_present:0),(long long)sample_age,latest.samples,(long long)latest.stamp_us,(long long)latest.max_interval_us,latest.errors,latest.edges,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(long long)render_sum,(long long)transfer_sum,sp_direct_water_updates,screen.page,home_live_points(&screen),(int)(screen.input.scene.strength*1000),(int)(screen.input.scene.hold*1000),(int)(screen.input.scene.usage*1000),home_live_density(&screen),home_live_density(&screen)-1,!screen.session_off,screen.page==HELPER?(int)screen.helper.state:-1,screen.helper.level_milli,(long long)(screen.page==HELPER?render_end-start:0),screen.page==HELPER?screen.helper.bot:-1,(unsigned)screen.helper.block,(int)screen.helper.slide,(long long)(stamp-render_end),home_blob_shown(&screen)?(int)screen.slide_kind:0);
  last_present=stamp;
  /* Yield at least one tick: no bogus 60Hz deadline; measured completions are
   * the presentation rate. Poller can preempt CPU rendering at higher priority. */
  vTaskDelay(1);
 }
}
/* Pinned to CPU1 (with the touch poller). Unpinned, this always-rendering task (the Ask page animates
 * continuously) kept pre-empting the CPU0-pinned voice/live TLS work on CPU0 while CPU1 sat idle, so
 * IDLE0 starved for 5 s during back-and-forth talk -> task watchdog reset (seen on device:
 * task_wdt IDLE0, CPU0: direct_owner in panel draw). CPU0 keeps Wi-Fi/LWIP/voice/live; CPU1 draws. */
void app_main(void){assert(xTaskCreatePinnedToCore(owner,"direct_owner",16*1024,NULL,5,NULL,1)==pdPASS);}
