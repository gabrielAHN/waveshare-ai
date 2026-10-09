/* Power manager task: see power_main.h. Logs (SPEC Contract D, no secrets):
 *   POWER_PMU ... (power_pmu.c, boot)          POWER_KEY src= irq= event=      POWER_COUNTDOWN secs=
 *   POWER_SLEEP enter vbus= light= wifi= vbat= pct=                            POWER_SLEEP_WIFI stopped
 *   POWER_WAKE source= slept_ms= cycles= awake_ms=   POWER_OFF held_ms= dry_run=   POWER_OFF_CANCEL held_ms=
 *   POWER_AUTO_SLEEP idle_s= auto_min=         POWER_STATE ... (WPK1 5) */
#include "esp_timer.h"  /* before any product header: sparkles.h expands esp_timer_get_time() */
#include "power_main.h"
#include <assert.h>
#include <string.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "hal/usb_serial_jtag_ll.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "power_button.h"
#include "power_pmu.h"
#include "panel_ui.h"
#include "pmu_diag.h"
#include "home_wifi.h"

static const char *TAG = "power";
static power_ctx ctx;
static power_button pb;               /* under pb_lock (touch poller, WPK1 reader, this task) */
static SemaphoreHandle_t pb_lock, touch_go;
static QueueHandle_t msgs;
static TaskHandle_t task;
static bool asleep_flag, guard_flag, park_req, parked;
static int64_t last_input_us;
#define LOAD(v) __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_RELEASE)

typedef enum { MSG_KEY, MSG_TEST_SLEEP, MSG_TEST_REPORT } msg_kind;
typedef struct { msg_kind kind; power_out o; } power_msg;

/* One sleep (this task only). */
static struct {
  int64_t start_us, light_us, test_until_us;
  uint32_t cycles;
  int vbat0, pct0;
  bool test, wifi_off, light_err;
} sl;
static int vbus_state = -1;  /* power_pmu_vbus(): 1 USB, 0 battery, -1 unknown */
static bool last_light;
/* The ESP32-S3 disables the USB-Serial-JTAG pad in light sleep, so a host that was attached during a light
 * sleep drops the board and (seen on a Mac) never re-enumerates it when the pad comes back. After any
 * light sleep, once on USB power and out of light sleep, detach the D+ pull-up for 300 ms: the host sees
 * one clean unplug/replug and the console and flashing work again. */
static bool usb_dirty;
static void usb_reconnect(void) {
  usb_serial_jtag_pull_override_vals_t off = {.dp_pu = false, .dm_pu = false, .dp_pd = true, .dm_pd = true};
  usb_serial_jtag_ll_phy_enable_pull_override(&off);
  vTaskDelay(pdMS_TO_TICKS(300));
  usb_serial_jtag_ll_phy_disable_pull_override();
  ESP_LOGI(TAG, "POWER_USB_RECONNECT note=after-light-sleep");
}
static portMUX_TYPE report_mux = portMUX_INITIALIZER_UNLOCKED;
static char report[SLEEP_REPORT_MAX + 1];
static uint32_t report_seq, report_sent;

static uint32_t ms_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
bool power_sleeping(void) { return LOAD(asleep_flag); }
bool power_guard_paused(void) { return LOAD(guard_flag); }
void power_main_activity(void) { STORE(last_input_us, esp_timer_get_time()); }

static void post(msg_kind kind, const power_out *o) {
  power_msg m;
  memset(&m, 0, sizeof m);
  m.kind = kind;
  if (o) m.o = *o;
  if (msgs && xQueueSend(msgs, &m, 0) == pdTRUE && task) xTaskNotifyGive(task);
}

bool power_main_boot(bool pressed) {
  if (!pb_lock) return pressed;  /* before the power task starts */
  xSemaphoreTake(pb_lock, portMAX_DELAY);
  power_out o = power_button_boot(&pb, pressed, ms_now());
  bool stable = pb.boot_stable;
  xSemaphoreGive(pb_lock);
  if (o.event != POWER_EV_NONE) post(MSG_KEY, &o);
  return stable;
}

void power_main_touch_park(void) {
  if (!LOAD(park_req)) return;
  STORE(parked, true);
  while (xSemaphoreTake(touch_go, 0) == pdTRUE) {
  }
  while (LOAD(park_req)) xSemaphoreTake(touch_go, portMAX_DELAY);
  STORE(parked, false);
}

bool power_sleep_report(char *out, size_t cap, uint32_t *seq) {
  bool ok = false;
  taskENTER_CRITICAL(&report_mux);
  size_t n = strlen(report);
  if (report_seq != report_sent && n && n < cap) {
    memcpy(out, report, n + 1);
    *seq = report_seq;
    ok = true;
  }
  taskEXIT_CRITICAL(&report_mux);
  return ok;
}
void power_sleep_report_sent(uint32_t seq) {
  taskENTER_CRITICAL(&report_mux);
  if (seq == report_seq) report_sent = seq;
  taskEXIT_CRITICAL(&report_mux);
}

static bool boot_down(void) { return gpio_get_level((gpio_num_t)ctx.boot_gpio) == 0; }
static bool panel_is_on(void) { return __atomic_load_n(ctx.panel_on, __ATOMIC_ACQUIRE); }
static void set_view(bool countdown, int secs) {
  xSemaphoreTake(ctx.lock, portMAX_DELAY);
  ctx.ui->power.countdown = countdown;
  ctx.ui->power.secs = (int8_t)secs;
  xSemaphoreGive(ctx.lock);
}
/* Ask: a command talking to Hermes (Wi-Fi stays on for it while asleep) / a turn recording or running (no auto sleep). */
static void ask_state(bool *busy, bool *turn) {
  xSemaphoreTake(ctx.lock, portMAX_DELAY);
  const helper_view *h = &ctx.ui->helper;
  *busy = h->net_busy;
  *turn = h->net_busy || h->holding || h->state == HV_LISTENING || h->state == HV_TRANSCRIBING || h->state == HV_RUNNING ||
          h->state == HV_STOPPING;
  xSemaphoreGive(ctx.lock);
}
static sleep_wake wake_of(power_src s) { return s == POWER_SRC_BOOT ? SLEEP_WAKE_BOOT : s == POWER_SRC_TEST ? SLEEP_WAKE_TEST : SLEEP_WAKE_PWR; }

static void enter_sleep(bool test) {
  int64_t now = esp_timer_get_time();
  if (LOAD(asleep_flag)) {  /* WPK1 4 while already asleep: light sleep from now, wake in 20 s */
    if (test) {
      sl.test = true;
      sl.test_until_us = now + (int64_t)SLEEP_TEST_MS * 1000;
    }
    return;
  }
  pmu_sample m = pmu_read();
  memset(&sl, 0, sizeof sl);
  sl.start_us = now;
  sl.vbat0 = m.ok ? m.vbat_mv : -1;
  sl.pct0 = m.ok ? m.percent : -1;
  sl.test = test;
  if (test) sl.test_until_us = now + (int64_t)SLEEP_TEST_MS * 1000;
  vbus_state = m.ok ? (m.vbus ? 1 : 0) : -1;
  STORE(guard_flag, true);
  STORE(asleep_flag, true);
  xSemaphoreTake(ctx.lock, portMAX_DELAY);
  panel_power_set(ctx.panel, false, false);           /* the owner: quiesce, backlight 0, panel off + sleep */
  ctx.ui->power.countdown = false;
  panel_ui_suppress(ctx.ui, ctx.ui->input.stamp_us);  /* cancels an unsubmitted recording (existing rule) */
  bool busy = ctx.ui->helper.net_busy;
  xSemaphoreGive(ctx.lock);
  STORE(park_req, true);                              /* the touch poller stops at its next sample */
  if (!busy) {
    home_wifi_sleep(true);
    sl.wifi_off = true;
  }
  ESP_LOGI(TAG, "POWER_SLEEP enter vbus=%d light=%d wifi=%s vbat=%d pct=%d", vbus_state == 1,
           sleep_light_planned(vbus_state == 1, vbus_state >= 0, test), busy ? "kept" : "stopped", sl.vbat0, sl.pct0);
}

static void wake(sleep_wake why) {
  if (!LOAD(asleep_flag)) return;
  int64_t now = esp_timer_get_time();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable((gpio_num_t)ctx.boot_gpio);
  int64_t slept_us = now - sl.start_us;
  uint32_t awake_ms = sleep_awake_ms(slept_us, sl.light_us);
  /* esp_timer ran on through light sleep: re-arm both heartbeats BEFORE the guard resumes. */
  __atomic_store_n(ctx.owner_beat, now, __ATOMIC_RELAXED);
  __atomic_store_n(ctx.touch_beat, now, __ATOMIC_RELAXED);
  STORE(guard_flag, false);
  STORE(asleep_flag, false);
  home_wifi_sleep(false);  /* rejoins in the background; tiles show "Joining Wi-Fi" meanwhile */
  bool held = boot_down();
  xSemaphoreTake(ctx.lock, portMAX_DELAY);
  panel_power_set(ctx.panel, true, held);  /* the owner: sleep-out, 120 ms, display on, repaint */
  xSemaphoreGive(ctx.lock);
  STORE(park_req, false);
  xSemaphoreGive(touch_go);
  STORE(last_input_us, now);
  last_light = false;
  pmu_sample m = pmu_read();
  ESP_LOGI(TAG, "POWER_WAKE source=%s slept_ms=%lu cycles=%lu awake_ms=%lu", sleep_wake_name(why), (unsigned long)(slept_us / 1000),
           (unsigned long)sl.cycles, (unsigned long)awake_ms);
  sleep_report r = {(uint32_t)(slept_us / 1000), sl.cycles, awake_ms, why, m.ok && m.vbus, sl.vbat0, m.ok ? m.vbat_mv : -1, sl.pct0,
                    m.ok ? m.percent : -1};
  char text[SLEEP_REPORT_MAX + 1];
  if (sleep_report_format(text, sizeof text, &r) > 0) {
    taskENTER_CRITICAL(&report_mux);
    memcpy(report, text, sizeof report);
    report_seq++;
    taskEXIT_CRITICAL(&report_mux);
  }
  memset(&sl, 0, sizeof sl);
}

/* One manual light sleep: timer (AXP2101 key poll) + BOOT low (instant). Wi-Fi is already stopped. */
static void light_cycle(int ms) {
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000);
  gpio_wakeup_enable((gpio_num_t)ctx.boot_gpio, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  int64_t t0 = esp_timer_get_time();
  esp_err_t e = esp_light_sleep_start();
  int64_t t1 = esp_timer_get_time();
  if (e == ESP_OK) {
    sl.light_us += t1 - t0;
    sl.cycles++;
    usb_dirty = true;
    return;
  }
  if (!sl.light_err) ESP_LOGW(TAG, "POWER_LIGHT_SLEEP err=%s", esp_err_to_name(e));
  sl.light_err = true;
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static void power_off(const power_out *o) {
  set_view(true, 0);
  vTaskDelay(pdMS_TO_TICKS(250));  /* the 0 reaches the panel */
  if (o->dry_run) {
    ESP_LOGI(TAG, "POWER_OFF held_ms=%lu dry_run=1", (unsigned long)o->held_ms);
    vTaskDelay(pdMS_TO_TICKS(500));
    set_view(false, 0);
    return;
  }
  xSemaphoreTake(ctx.lock, portMAX_DELAY);
  panel_power_set(ctx.panel, false, false);  /* the owner quiesces the presenter, panel off + disp sleep */
  xSemaphoreGive(ctx.lock);
  for (int i = 0; i < 150 && panel_is_on(); i++) vTaskDelay(pdMS_TO_TICKS(10));
  ESP_LOGI(TAG, "POWER_OFF held_ms=%lu dry_run=0", (unsigned long)o->held_ms);
  vTaskDelay(pdMS_TO_TICKS(60));  /* let the line drain */
  bool ok = power_pmu_off();
  vTaskDelay(pdMS_TO_TICKS(1500));
  /* Still running: the write failed or the PMU kept us on. Never leave a dark, live board. */
  ESP_LOGE(TAG, "POWER_OFF_FAILED write_ok=%d", ok);
  xSemaphoreTake(ctx.lock, portMAX_DELAY);
  panel_power_set(ctx.panel, true, false);
  ctx.ui->power.countdown = false;
  xSemaphoreGive(ctx.lock);
}

static void handle(const power_out *o) {
  if (o->event != POWER_EV_NONE)
    ESP_LOGI(TAG, "POWER_KEY src=%s irq=0x%02x event=%s", power_src_name(o->src), (unsigned)o->irq, power_event_name(o->event));
  if (o->act != POWER_ACT_NONE) STORE(last_input_us, esp_timer_get_time());
  switch (o->act) {
    case POWER_ACT_SLEEP_TOGGLE:
      if (LOAD(asleep_flag)) wake(wake_of(o->src));
      else enter_sleep(false);
      break;
    case POWER_ACT_OFF_ARMED:  /* any page; wakes the board when asleep */
      if (LOAD(asleep_flag)) wake(wake_of(o->src));
      ESP_LOGI(TAG, "POWER_COUNTDOWN secs=%d", o->secs_left);
      set_view(true, o->secs_left);
      break;
    case POWER_ACT_OFF_CANCEL:
      ESP_LOGI(TAG, "POWER_OFF_CANCEL held_ms=%lu", (unsigned long)o->held_ms);
      set_view(false, 0);
      break;
    case POWER_ACT_POWER_OFF:
      power_off(o);
      break;
    default:
      break;
  }
}

static void report_state(void) {
  xSemaphoreTake(pb_lock, portMAX_DELAY);
  bool armed = pb.armed;
  int secs = pb.secs_shown;
  xSemaphoreGive(pb_lock);
  int64_t now = esp_timer_get_time();
  bool asleep = LOAD(asleep_flag);
  ESP_LOGI(TAG, "POWER_STATE asleep=%d light=%d armed=%d secs=%d vbus=%d wifi=%s parked=%d panel=%s cycles=%lu slept_ms=%lu auto_min=%d idle_s=%lu stack_free=%u",
           asleep, last_light, armed, armed ? secs : -1, vbus_state, home_wifi_is_asleep() ? "stopped" : "on", LOAD(parked),
           panel_is_on() ? "on" : "off", (unsigned long)sl.cycles, asleep ? (unsigned long)((now - sl.start_us) / 1000) : 0ul,
           WAVESHARE_AI_AUTO_SLEEP_MIN, (unsigned long)((now - LOAD(last_input_us)) / 1000000), (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void power_task(void *arg) {
  (void)arg;
  (void)power_pmu_init();
  vbus_state = power_pmu_vbus();
  int64_t vbus_us = esp_timer_get_time();
  for (;;) {
    /* 1. PWRON key IRQs (REG 0x49; the AXP2101 IRQ line reaches no ESP32 GPIO). */
    int irq = power_pmu_irq();
    if (irq > 0 && (irq & PWR_IRQ_KEY)) {
      xSemaphoreTake(pb_lock, portMAX_DELAY);
      power_out o = power_button_irq(&pb, (uint8_t)irq, POWER_SRC_PWR, ms_now());
      xSemaphoreGive(pb_lock);
      handle(&o);
    }
    /* 2. BOOT while the touch poller is parked (it feeds BOOT itself while awake). */
    if (LOAD(parked)) (void)power_main_boot(boot_down());
    /* 3. BOOT presses from the touch poller and WPK1 test frames. */
    power_msg m;
    while (xQueueReceive(msgs, &m, 0) == pdTRUE) {
      if (m.kind == MSG_KEY) handle(&m.o);
      else if (m.kind == MSG_TEST_SLEEP) {
        ESP_LOGI(TAG, "POWER_TEST sleep_s=%u light=1 note=wpk1-injected-not-physical", (unsigned)(SLEEP_TEST_MS / 1000));
        enter_sleep(true);
      } else report_state();
    }
    /* 4. Countdown seconds / power off. */
    xSemaphoreTake(pb_lock, portMAX_DELAY);
    power_out t = power_button_tick(&pb, ms_now());
    xSemaphoreGive(pb_lock);
    if (t.act != POWER_ACT_NONE) handle(&t);
    /* 5. Policy. */
    int64_t now = esp_timer_get_time();
    bool asleep = LOAD(asleep_flag);
    if (asleep || now - vbus_us >= 1000000) {
      vbus_state = power_pmu_vbus();
      vbus_us = now;
    }
    bool busy, turn;
    ask_state(&busy, &turn);
    int64_t idle = now - LOAD(last_input_us);
    sleep_in in = {asleep, vbus_state == 1, vbus_state >= 0, busy, turn,
                   asleep ? (uint32_t)((now - sl.start_us) / 1000) : 0u,
                   idle <= 0 ? 0u : (idle / 1000 > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)(idle / 1000)),
                   WAVESHARE_AI_AUTO_SLEEP_MIN, asleep && sl.test, boot_down(),
                   !panel_is_on() && LOAD(parked) && home_wifi_is_asleep()};
    sleep_out d = sleep_decide(&in);
    if (usb_dirty && vbus_state == 1 && !(asleep && d.light)) {
      usb_dirty = false;
      usb_reconnect();
    }
    if (!asleep && d.enter) {
      ESP_LOGI(TAG, "POWER_AUTO_SLEEP idle_s=%lu auto_min=%d", (unsigned long)(in.idle_ms / 1000), WAVESHARE_AI_AUTO_SLEEP_MIN);
      enter_sleep(false);
      continue;
    }
    if (asleep) {
      if (d.stop_wifi && !sl.wifi_off) {
        home_wifi_sleep(true);
        sl.wifi_off = true;
        ESP_LOGI(TAG, "POWER_SLEEP_WIFI stopped waited_ms=%lu ask_busy=%d", (unsigned long)in.asleep_ms, busy);
      }
      if (sl.test && now >= sl.test_until_us) {
        wake(SLEEP_WAKE_TEST);
        continue;
      }
      last_light = d.light;
      if (d.light) {
        light_cycle(d.poll_ms);
        continue;
      }
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(d.poll_ms));
  }
}

void power_main_start(const power_ctx *c) {
  ctx = *c;
  power_button_init(&pb);
  pb_lock = xSemaphoreCreateMutex();
  touch_go = xSemaphoreCreateBinary();
  msgs = xQueueCreate(8, sizeof(power_msg));
  assert(pb_lock && touch_go && msgs);
  STORE(last_input_us, esp_timer_get_time());
  ESP_LOGI(TAG, "POWER_READY auto_sleep_min=%d poll_ms=%d light_ms=%d off_hold_ms=%u", WAVESHARE_AI_AUTO_SLEEP_MIN, SLEEP_POLL_AWAKE_MS,
           SLEEP_POLL_LIGHT_MS, (unsigned)POWER_OFF_MS);
  /* CPU0 beside Wi-Fi/voice/live, above them (it mostly sleeps); POWER_STATE reports its stack high water. */
  assert(xTaskCreatePinnedToCore(power_task, "power", 4096, NULL, 3, &task, 0) == pdPASS);
}

#ifdef HELPER_VOICE_SELFTEST
void power_main_test(unsigned char value) {
  if (value == POWER_TEST_SLEEP) { post(MSG_TEST_SLEEP, NULL); return; }
  if (value == POWER_TEST_REPORT) { post(MSG_TEST_REPORT, NULL); return; }
  uint8_t bits = power_test_irq(value);
  if (!bits || !pb_lock) return;
  xSemaphoreTake(pb_lock, portMAX_DELAY);
  power_out o = power_button_irq(&pb, bits, POWER_SRC_TEST, ms_now());
  xSemaphoreGive(pb_lock);
  post(MSG_KEY, &o);
}
#endif
