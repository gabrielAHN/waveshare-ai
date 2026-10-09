#include <assert.h>
#include <stdio.h>
#include "panel_power.h"

static panel_power_event sample(panel_power *p, unsigned ms, bool pressed) {
  return panel_power_sample(p, ms, pressed);
}

int main(void) {
  panel_power p;
  panel_power_init(&p, true, 30);
  assert(sample(&p, 0, false) == PANEL_POWER_NONE);
  assert(sample(&p, 10, true) == PANEL_POWER_NONE);
  assert(sample(&p, 20, false) == PANEL_POWER_NONE); /* bounce */
  assert(sample(&p, 30, true) == PANEL_POWER_NONE);
  assert(sample(&p, 59, true) == PANEL_POWER_NONE);
  assert(sample(&p, 60, true) == PANEL_POWER_OFF);
  assert(!p.display_on && panel_power_touch_suppressed(&p));
  assert(sample(&p, 100, false) == PANEL_POWER_NONE);
  assert(sample(&p, 130, false) == PANEL_POWER_NONE);
  assert(panel_power_touch_suppressed(&p));

  assert(sample(&p, 200, true) == PANEL_POWER_NONE);
  assert(sample(&p, 230, true) == PANEL_POWER_ON);
  assert(p.display_on && panel_power_touch_suppressed(&p));
  panel_power_touch_sample(&p, false); /* do not arm input before BOOT itself is released */
  assert(panel_power_touch_suppressed(&p));
  panel_power_touch_sample(&p, true); /* finger was already down at wake */
  assert(sample(&p, 260, false) == PANEL_POWER_NONE);
  assert(sample(&p, 289, false) == PANEL_POWER_NONE);
  assert(sample(&p, 290, false) == PANEL_POWER_WAKE_RELEASED);
  assert(panel_power_touch_suppressed(&p));
  panel_power_touch_sample(&p, true);
  assert(panel_power_touch_suppressed(&p));
  panel_power_touch_sample(&p, false);
  assert(!panel_power_touch_suppressed(&p));

  /* A held button never repeats, including timer wrap-safe sampling. */
  assert(sample(&p, 1000, true) == PANEL_POWER_NONE);
  assert(sample(&p, 1030, true) == PANEL_POWER_OFF);
  assert(sample(&p, 5000, true) == PANEL_POWER_NONE);
  panel_power_init(&p, true, 30);
  assert(sample(&p, UINT32_MAX - 10u, true) == PANEL_POWER_NONE);
  assert(sample(&p, 20, true) == PANEL_POWER_OFF);
  /* Power saving while asleep (user rule 2026-09-29): touch is polled at 10 Hz (not 100 Hz) and the
   * Wi-Fi radio may doze; awake, 100 Hz and the radio stays awake for voice uploads. */
  panel_power_init(&p, true, 30);
  assert(panel_power_poll_ms(&p) == 10 && !panel_power_radio_doze(&p));
  assert(sample(&p, 100, true) == PANEL_POWER_NONE && sample(&p, 130, true) == PANEL_POWER_OFF);
  assert(panel_power_poll_ms(&p) == 100 && panel_power_radio_doze(&p));
  assert(sample(&p, 200, false) == PANEL_POWER_NONE && sample(&p, 230, false) == PANEL_POWER_NONE);
  assert(sample(&p, 300, true) == PANEL_POWER_NONE && sample(&p, 330, true) == PANEL_POWER_ON);  /* press = on */
  assert(panel_power_poll_ms(&p) == 10 && !panel_power_radio_doze(&p));
  /* Power manager path (power_main.c): sleep/wake set directly; a wake with BOOT held keeps input
   * suppressed until BOOT is released and then until the finger lifts. */
  panel_power_init(&p, true, 30);
  panel_power_set(&p, false, false);
  assert(!p.display_on && panel_power_touch_suppressed(&p) && panel_power_poll_ms(&p) == 100);
  panel_power_set(&p, true, true);
  assert(p.display_on && p.boot_release_pending && p.touch_release_pending);
  panel_power_touch_sample(&p, false);
  assert(panel_power_touch_suppressed(&p)); /* BOOT still down */
  panel_power_boot_released(&p);
  panel_power_touch_sample(&p, true);
  assert(panel_power_touch_suppressed(&p)); /* finger still down */
  panel_power_touch_sample(&p, false);
  assert(!panel_power_touch_suppressed(&p));
  panel_power_set(&p, true, true); /* already on: nothing re-armed */
  assert(!p.boot_release_pending && !panel_power_touch_suppressed(&p));
  panel_power_set(&p, false, false);
  panel_power_set(&p, true, false); /* PWR-key wake: only the touch release is awaited */
  assert(!p.boot_release_pending && p.touch_release_pending);
  panel_power_touch_sample(&p, false);
  assert(!panel_power_touch_suppressed(&p));
  puts("panel_power_test ok");
}
