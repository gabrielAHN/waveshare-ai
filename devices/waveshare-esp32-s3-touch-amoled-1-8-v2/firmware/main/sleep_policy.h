#pragma once
/* Sleep policy (pure, tests/host/sleep_policy_test.c). Sleep = low drain, fast wake (SPEC decisions 6-7):
 * panel off + panel sleep, no rendering, touch not polled, audio closed, bridge polling paused, Wi-Fi
 * radio stopped (after an in-flight Ask command finishes, at most SLEEP_WIFI_KEEP_MS), and on battery
 * the chip waits in manual LIGHT sleep, waking every SLEEP_POLL_LIGHT_MS on a timer to read the
 * AXP2101 key IRQs (its IRQ line is not wired to an ESP32 GPIO) and at once on BOOT (GPIO0 low).
 * On USB power (AXP2101 VBUS good) light sleep is skipped: the ESP32-S3 disables the USB-Serial-JTAG
 * pad while in light sleep, which drops the console (the WPK1 4 test forces it anyway).
 * esp_timer keeps counting through light sleep, so the freeze guard (freeze_guard.h) is paused while
 * asleep and both heartbeats are re-armed at wake before it resumes. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "freeze_guard.h"

/* Battery auto sleep, minutes without a touch or button press (Kconfig CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN,
 * passed by this device's main/CMakeLists.txt; host tests may pass -DWAVESHARE_AI_AUTO_SLEEP_MIN=n). 0 = never. */
#ifndef WAVESHARE_AI_AUTO_SLEEP_MIN
#define WAVESHARE_AI_AUTO_SLEEP_MIN 0
#endif
_Static_assert(WAVESHARE_AI_AUTO_SLEEP_MIN >= 0 && WAVESHARE_AI_AUTO_SLEEP_MIN <= 120, "WAVESHARE_AI_AUTO_SLEEP_MIN must be 0..120 minutes");

#define SLEEP_POLL_AWAKE_MS 50     /* AXP2101 key IRQ poll while awake (and asleep without light sleep) */
#define SLEEP_POLL_LIGHT_MS 250    /* light-sleep timer wake */
#define SLEEP_POLL_BOOT_MS 10      /* BOOT is down: debounce it before sleeping again */
#define SLEEP_WIFI_KEEP_MS 90000u  /* asleep: keep Wi-Fi at most this long for an Ask command */
#define SLEEP_TEST_MS 20000u       /* WPK1 4: light sleep for 20 s, then wake (wake=test) */

typedef struct {
  bool asleep;        /* the board is asleep (or going to sleep) */
  bool vbus;          /* AXP2101 VBUS good = USB power */
  bool vbus_known;    /* the PMU answered (unknown is treated as USB: no light sleep) */
  bool ask_busy;      /* an Ask command is talking to Hermes (helper net_busy) */
  bool ask_turn;      /* an Ask turn is recording or running: never auto sleep */
  uint32_t asleep_ms; /* time since sleep began */
  uint32_t idle_ms;   /* time since the last touch or button press */
  int auto_min;       /* auto sleep minutes on battery, 0 = never */
  bool test;          /* WPK1 4: light sleep even on USB */
  bool boot_down;     /* BOOT is pressed right now (a low-level GPIO wake would fire at once) */
  bool quiet;         /* panel off, touch poller parked, Wi-Fi stopped */
} sleep_in;

typedef struct {
  bool enter;         /* awake: go to sleep now (battery auto sleep) */
  bool stop_wifi;     /* asleep: stop Wi-Fi now (false = keep it for the in-flight Ask command) */
  bool light;         /* asleep: the next wait is a manual light sleep */
  int poll_ms;        /* next key poll / light-sleep timer */
  bool guard_paused;  /* freeze guard paused (esp_timer runs on through light sleep) */
} sleep_out;

static inline sleep_out sleep_decide(const sleep_in *in) {
  sleep_out o = {false, false, false, SLEEP_POLL_AWAKE_MS, false};
  if (!in->asleep) {
    o.enter = in->auto_min > 0 && in->vbus_known && !in->vbus && !in->ask_turn && !in->ask_busy && !in->boot_down &&
              in->idle_ms >= (uint32_t)in->auto_min * 60000u;
    return o;
  }
  o.guard_paused = true;
  o.stop_wifi = !in->ask_busy || in->asleep_ms >= SLEEP_WIFI_KEEP_MS;
  bool battery = in->vbus_known && !in->vbus;
  o.light = o.stop_wifi && in->quiet && !in->boot_down && (battery || in->test);
  o.poll_ms = in->boot_down ? SLEEP_POLL_BOOT_MS : (o.light ? SLEEP_POLL_LIGHT_MS : SLEEP_POLL_AWAKE_MS);
  return o;
}
/* Light sleep will be used once Wi-Fi is off (the POWER_SLEEP enter line's light=). */
static inline bool sleep_light_planned(bool vbus, bool vbus_known, bool test) { return test || (vbus_known && !vbus); }

/* The freeze guard while asleep: paused (a heartbeat goes stale across a long light sleep). */
static inline int sleep_guard_check(int64_t now, int64_t owner_beat, int64_t touch_beat, bool paused) {
  return paused ? FREEZE_NONE : freeze_check(now, owner_beat, touch_beat);
}

/* ---- Sleep report: the X-Board-Sleep header on the first /v1/live after a wake (SPEC Contract B.4)
 * "slept_s=<u32> cycles=<u32> awake_ms=<u32> wake=<pwr|boot|auto|usb|test> vbus=<0|1> vbat0=<mV>
 *  vbat1=<mV> pct0=<int> pct1=<int>" (these keys in this order, single spaces, ASCII, <= 160 bytes;
 * vbat/pct -1 when unknown). ---- */
#define SLEEP_REPORT_MAX 160
typedef enum { SLEEP_WAKE_PWR, SLEEP_WAKE_BOOT, SLEEP_WAKE_AUTO, SLEEP_WAKE_USB, SLEEP_WAKE_TEST } sleep_wake;
static inline const char *sleep_wake_name(sleep_wake w) {
  static const char *n[] = {"pwr", "boot", "auto", "usb", "test"};
  return w <= SLEEP_WAKE_TEST ? n[w] : "pwr";
}
typedef struct {
  uint32_t slept_ms, cycles, awake_ms;
  sleep_wake wake;
  bool vbus;
  int vbat0, vbat1, pct0, pct1;  /* at sleep / at wake; -1 = unknown */
} sleep_report;
static inline int sleep_mv(int v) { return v < 0 ? -1 : (v > 65535 ? 65535 : v); }
static inline int sleep_pct(int v) { return v < 0 ? -1 : (v > 100 ? 100 : v); }
/* Time awake while asleep = everything not spent inside light sleep. */
static inline uint32_t sleep_awake_ms(int64_t slept_us, int64_t light_us) {
  int64_t a = slept_us - light_us;
  return a <= 0 ? 0u : (uint32_t)(a / 1000);
}
static inline int sleep_report_format(char *out, size_t cap, const sleep_report *r) {
  int n = snprintf(out, cap, "slept_s=%lu cycles=%lu awake_ms=%lu wake=%s vbus=%d vbat0=%d vbat1=%d pct0=%d pct1=%d",
                   (unsigned long)(r->slept_ms / 1000u), (unsigned long)r->cycles, (unsigned long)r->awake_ms,
                   sleep_wake_name(r->wake), r->vbus ? 1 : 0, sleep_mv(r->vbat0), sleep_mv(r->vbat1), sleep_pct(r->pct0),
                   sleep_pct(r->pct1));
  return n > 0 && (size_t)n < cap && n <= SLEEP_REPORT_MAX ? n : -1;
}
