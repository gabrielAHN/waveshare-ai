#pragma once
/* Power manager (SPEC decisions 5-7): one small task on CPU0 polls the AXP2101 PWRON IRQs (50 ms
 * awake, every 250 ms light-sleep timer wake), takes BOOT presses from the touch poller (and samples
 * BOOT itself while that poller is parked), and runs sleep / wake / power off:
 *   sleep: panel off + panel sleep (the owner task, after present_quiesce), touch poller parked, no
 *          bridge polling, no recording / click, Wi-Fi stopped (after an in-flight Ask command, at most
 *          90 s), then on battery manual light sleep with timer + BOOT (GPIO0 low) wake sources.
 *   wake:  freeze-guard heartbeats re-armed, Wi-Fi rejoins in the background, panel back on, the last
 *          page repaints at once, touch stays suppressed until fingers / BOOT are released.
 *   off:   countdown overlay 4..1, then 0, panel off, POWER_OFF, AXP2101 REG 0x10 bit0.
 * Pure parts: power_button.h, sleep_policy.h, power_ui.h, panel_power.h (host-tested). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "home_ui.h"
#include "panel_power.h"
#include "sleep_policy.h"

typedef struct {
  home_ui *ui;
  SemaphoreHandle_t lock;            /* direct_main.c input_lock: guards ui and panel */
  panel_power *panel;                /* display state + wake-time input suppression */
  int64_t *owner_beat, *touch_beat;  /* freeze-guard heartbeats, re-armed at wake */
  const bool *panel_on;              /* the owner's applied panel state (atomic) */
  int boot_gpio;                     /* BOOT, active low */
} power_ctx;
void power_main_start(const power_ctx *ctx);

/* Touch poller (10 ms): feed the raw BOOT level; returns the debounced level. */
bool power_main_boot(bool pressed);
/* A touch: restarts the battery auto-sleep clock. */
void power_main_activity(void);
/* Touch poller: blocks here while asleep (no touch I2C, no input), returns at wake. */
void power_main_touch_park(void);

/* Asleep (or going to sleep): workers pause bridge polling, recording and the completion click. */
bool power_sleeping(void);
/* The freeze guard is paused (esp_timer runs on through light sleep). */
bool power_guard_paused(void);
/* X-Board-Sleep value of the last sleep, until a /v1/live request carried it (seq: pass it back). */
bool power_sleep_report(char *out, size_t cap, uint32_t *seq);
void power_sleep_report_sent(uint32_t seq);

#ifdef HELPER_VOICE_SELFTEST
/* WPK1 test frame (power_button.h): 1 short, 2 long start, 3 release, 4 20 s light sleep, 5 state. */
void power_main_test(unsigned char value);
#endif
