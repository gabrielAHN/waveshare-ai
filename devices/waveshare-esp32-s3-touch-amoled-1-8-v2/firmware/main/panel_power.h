#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  PANEL_POWER_NONE,
  PANEL_POWER_OFF,
  PANEL_POWER_ON,
  PANEL_POWER_WAKE_RELEASED,
} panel_power_event;

typedef struct {
  uint32_t changed_ms;
  uint32_t debounce_ms;
  bool display_on;
  bool raw_pressed;
  bool stable_pressed;
  bool boot_release_pending;
  bool touch_release_pending;
} panel_power;

static inline void panel_power_init(panel_power *p, bool display_on, uint32_t debounce_ms) {
  *p = (panel_power){.debounce_ms = debounce_ms, .display_on = display_on};
}

static inline panel_power_event panel_power_sample(panel_power *p, uint32_t now_ms, bool pressed) {
  if (pressed != p->raw_pressed) {
    p->raw_pressed = pressed;
    p->changed_ms = now_ms;
    return PANEL_POWER_NONE;
  }
  if (pressed == p->stable_pressed || (uint32_t)(now_ms - p->changed_ms) < p->debounce_ms)
    return PANEL_POWER_NONE;
  p->stable_pressed = pressed;
  if (pressed) {
    p->display_on = !p->display_on;
    if (p->display_on) {
      p->boot_release_pending = true;
      p->touch_release_pending = true;
    }
    return p->display_on ? PANEL_POWER_ON : PANEL_POWER_OFF;
  }
  if (p->boot_release_pending) {
    p->boot_release_pending = false;
    return PANEL_POWER_WAKE_RELEASED;
  }
  return PANEL_POWER_NONE;
}

/* On wake, consume any contact already held and do not expose input again until
 * the touch controller has reported a physical release. */
static inline void panel_power_touch_sample(panel_power *p, bool touching) {
  if (p->display_on && !p->boot_release_pending && p->touch_release_pending && !touching)
    p->touch_release_pending = false;
}

static inline bool panel_power_touch_suppressed(const panel_power *p) {
  return !p->display_on || p->boot_release_pending || p->touch_release_pending;
}

/* Display off: touch is polled at 10 Hz until the power task parks the poller (power_main.c: no touch
 * I2C at all while asleep); input stays suppressed. Sleep itself (Wi-Fi off, light sleep on battery,
 * an in-flight Ask command finishing first) is sleep_policy.h. A PWR or BOOT press wakes it. */
static inline int panel_power_poll_ms(const panel_power *p) { return p->display_on ? 10 : 100; }
static inline bool panel_power_radio_doze(const panel_power *p) { return !p->display_on; }

/* The power manager (power_button.h / power_main.c) decides sleep and wake now (PWR key, BOOT, auto
 * sleep); it sets the display state here. A wake suppresses input exactly like the BOOT toggle above:
 * until BOOT (when it is still held) is released, then until the touch controller reports no contact. */
static inline void panel_power_set(panel_power *p, bool on, bool boot_held) {
  if (on == p->display_on) return;
  p->display_on = on;
  if (on) {
    p->boot_release_pending = boot_held;
    p->touch_release_pending = true;
  }
}
/* The debounced BOOT level is up again (power_button.h owns the debounce now). */
static inline void panel_power_boot_released(panel_power *p) { p->boot_release_pending = false; }
