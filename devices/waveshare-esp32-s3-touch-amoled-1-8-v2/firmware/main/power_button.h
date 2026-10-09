#pragma once
/* Power button (AXP2101 PWRON key) and BOOT button: pure state machine, host-tested in
 * tests/host/power_button_test.c. The device side (power_main.c) feeds it AXP2101 REG 0x49 reads (every
 * 50 ms awake, every 250 ms light-sleep timer wake), BOOT samples and the WPK1 test frames, and acts
 * on what it returns.
 *
 *   PWR short press (released before the 1 s IRQLEVEL)  -> sleep / wake
 *   PWR held 1 s (LONG IRQ)                             -> countdown overlay 4..1 (press = now - 1 s)
 *   released before 5 s (an edge or short IRQ)          -> cancel
 *   held 5 s                                            -> power off (REG 0x10 bit0)
 *   BOOT press (40 ms debounce)                         -> sleep / wake; holding it does nothing else
 *
 * The polarity of the two PWRON edge IRQs is NOT assumed (not verified on hardware): either edge
 * after (or in the same read as) the LONG IRQ is the release. An injected (WPK1, test) press never
 * powers the board off: it ends in a dry run. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "provision.h"

/* AXP2101 REG 0x49 IRQ status 2 (write 1 to clear) = REG 0x41 IRQ enable 2 bit layout. */
#define PWR_IRQ_POS_EDGE 0x01u
#define PWR_IRQ_NEG_EDGE 0x02u
#define PWR_IRQ_LONG 0x04u
#define PWR_IRQ_SHORT 0x08u
#define PWR_IRQ_EDGES (PWR_IRQ_POS_EDGE | PWR_IRQ_NEG_EDGE)
#define PWR_IRQ_KEY 0x0fu
#define POWER_LONG_MS 1000u        /* REG 0x27 IRQLEVEL 00: the LONG IRQ fires after 1 s held */
#define POWER_OFF_MS 5000u         /* firmware power-off hold (the AXP2101 backstop is 6 s) */
#define POWER_BOOT_DEBOUNCE_MS 40u

/* AXP2101 register values (read-modify-write; datasheet V1.4).
 * REG 0x27: bits 7:6 kept, 5:4 IRQLEVEL 00 = 1 s, 3:2 OFFLEVEL 01 = 6 s, 1:0 ONLEVEL 00 = 128 ms. */
static inline uint8_t axp_key_timing(uint8_t old) { return (uint8_t)((old & 0xc0u) | (0u << 4) | (1u << 2) | 0u); }
/* REG 0x22: bit1 = PWRON held past OFFLEVEL powers off (on), bit0 = 0 power off (not restart). */
static inline uint8_t axp_key_poweroff(uint8_t old) { return (uint8_t)((old & ~0x03u) | 0x02u); }
/* REG 0x41: enable the four PWRON IRQs (edges, long, short); other sources untouched. */
static inline uint8_t axp_key_irq_enable(uint8_t old) { return (uint8_t)(old | PWR_IRQ_KEY); }
/* REG 0x10: bit0 = software power off (everything but VRTC). */
static inline uint8_t axp_soft_off(uint8_t old) { return (uint8_t)(old | 0x01u); }

typedef enum { POWER_SRC_PWR, POWER_SRC_BOOT, POWER_SRC_TEST } power_src;
typedef enum { POWER_ACT_NONE, POWER_ACT_SLEEP_TOGGLE, POWER_ACT_OFF_ARMED, POWER_ACT_OFF_CANCEL, POWER_ACT_POWER_OFF } power_act;
/* The POWER_KEY log line's event (NONE = no line). */
typedef enum { POWER_EV_NONE, POWER_EV_SHORT, POWER_EV_LONG, POWER_EV_RELEASE, POWER_EV_EDGE } power_event;

static inline const char *power_src_name(power_src s) { return s == POWER_SRC_BOOT ? "boot" : s == POWER_SRC_TEST ? "test" : "pwr"; }
static inline const char *power_event_name(power_event e) {
  static const char *n[] = {"none", "short", "long", "release", "edge"};
  return e <= POWER_EV_EDGE ? n[e] : "?";
}

typedef struct {
  power_act act;
  power_event event;
  power_src src;
  uint8_t irq;        /* the REG 0x49 value (or the injected bits) that produced this */
  int secs_left;      /* OFF_ARMED: 4..1 (seconds until off); POWER_OFF: 0 */
  uint32_t held_ms;   /* OFF_CANCEL / POWER_OFF: how long the key was held */
  bool dry_run;       /* POWER_OFF from an injected press: log it, never cut the power */
} power_out;

/* The countdown overlay as drawn by power_ui.h (a field of home_ui; zero = nothing drawn). */
typedef struct {
  bool countdown;
  int8_t secs;        /* 4..1 while held, 0 at power off */
} power_view;
static inline bool power_view_equal(const power_view *a, const power_view *b) {
  return a->countdown == b->countdown && (!a->countdown || a->secs == b->secs);
}

typedef struct {
  bool armed;
  power_src armed_src;
  uint32_t press_ms;
  int secs_shown;
  bool boot_raw, boot_stable;
  uint32_t boot_changed_ms;
} power_button;

static inline void power_button_init(power_button *b) { memset(b, 0, sizeof *b); }
/* Seconds left on the countdown after `held` ms: 1000 -> 4, 4999 -> 1, >= 5000 -> 0. */
static inline int power_secs_left(uint32_t held) { return held >= POWER_OFF_MS ? 0 : (int)((POWER_OFF_MS - held + 999u) / 1000u); }

/* Countdown progress (call every poll): OFF_ARMED when the shown second changes, POWER_OFF once. */
static inline power_out power_button_tick(power_button *b, uint32_t now_ms) {
  power_out o;
  memset(&o, 0, sizeof o);
  if (!b->armed) return o;
  uint32_t held = now_ms - b->press_ms;
  o.src = b->armed_src;
  if (held >= POWER_OFF_MS) {
    b->armed = false;
    o.act = POWER_ACT_POWER_OFF;
    o.secs_left = 0;
    o.held_ms = held;
    o.dry_run = b->armed_src == POWER_SRC_TEST;
    return o;
  }
  int s = power_secs_left(held);
  if (s != b->secs_shown) {
    b->secs_shown = s;
    o.act = POWER_ACT_OFF_ARMED;
    o.secs_left = s;
  }
  return o;
}

/* One REG 0x49 read (or injected bits). Only the PWRON bits count; 0 = nothing happened. */
static inline power_out power_button_irq(power_button *b, uint8_t irq, power_src src, uint32_t now_ms) {
  power_out o;
  memset(&o, 0, sizeof o);
  o.src = src;
  o.irq = irq;
  uint8_t key = (uint8_t)(irq & PWR_IRQ_KEY);
  if (!key) return o;
  bool edge = (key & PWR_IRQ_EDGES) != 0, released = edge || (key & PWR_IRQ_SHORT);
  if (b->armed) {
    uint32_t held = now_ms - b->press_ms;
    if (held >= POWER_OFF_MS) {  /* the 5 s were already up: the release came too late */
      power_out t = power_button_tick(b, now_ms);
      t.irq = irq;
      t.event = released ? POWER_EV_RELEASE : POWER_EV_LONG;
      return t;
    }
    if (released) {  /* let go (a short IRQ means let go and tapped again): cancel only */
      b->armed = false;
      o.act = POWER_ACT_OFF_CANCEL;
      o.event = POWER_EV_RELEASE;
      o.held_ms = held;
      return o;
    }
    o.event = POWER_EV_LONG;  /* a repeated LONG while counting down: nothing new */
    return o;
  }
  if (key & PWR_IRQ_LONG) {
    if (released) {  /* held past 1 s and let go within this one read: never armed */
      o.act = POWER_ACT_OFF_CANCEL;
      o.event = POWER_EV_RELEASE;
      o.held_ms = POWER_LONG_MS;
      return o;
    }
    b->armed = true;
    b->armed_src = src;
    b->press_ms = now_ms - POWER_LONG_MS;
    b->secs_shown = power_secs_left(POWER_LONG_MS);
    o.act = POWER_ACT_OFF_ARMED;
    o.event = POWER_EV_LONG;
    o.secs_left = b->secs_shown;
    return o;
  }
  if (key & PWR_IRQ_SHORT) {
    o.act = POWER_ACT_SLEEP_TOGGLE;
    o.event = POWER_EV_SHORT;
    return o;
  }
  o.event = POWER_EV_EDGE;  /* a press (or a release we were not counting): nothing to do */
  return o;
}

/* BOOT (GPIO0, active low) sample: 40 ms debounce; the press toggles sleep/wake (instant wake from
 * light sleep through the GPIO wake source); holding it does nothing else. While the power-off
 * countdown runs, BOOT is ignored (the countdown owns the screen). */
static inline power_out power_button_boot(power_button *b, bool pressed, uint32_t now_ms) {
  power_out o;
  memset(&o, 0, sizeof o);
  o.src = POWER_SRC_BOOT;
  if (pressed != b->boot_raw) {
    b->boot_raw = pressed;
    b->boot_changed_ms = now_ms;
    return o;
  }
  if (pressed == b->boot_stable || (uint32_t)(now_ms - b->boot_changed_ms) < POWER_BOOT_DEBOUNCE_MS) return o;
  b->boot_stable = pressed;
  if (pressed && !b->armed) {
    o.act = POWER_ACT_SLEEP_TOGGLE;
    o.event = POWER_EV_SHORT;
  }
  return o;
}

/* ---- WPK1 test frame (HELPER_VOICE_SELFTEST builds only): "WPK1" + value + CRC32 LE of the value.
 * 1 = short press, 2 = long-press start, 3 = release, 4 = 20 s light sleep (even on USB) then wake,
 * 5 = report POWER_STATE. Same framing as WLV1/WVC1. ---- */
#define POWER_TEST_SHORT 1
#define POWER_TEST_LONG 2
#define POWER_TEST_RELEASE 3
#define POWER_TEST_SLEEP 4
#define POWER_TEST_REPORT 5
typedef struct { unsigned char bytes[9]; unsigned used; } power_cmd_parser;
static inline int power_cmd_feed(power_cmd_parser *p, unsigned char byte, unsigned char *value) {
  if (!p->used && byte != 'W') return 0;
  p->bytes[p->used++] = byte;
  if (p->used <= 4) {
    if (memcmp(p->bytes, "WPK1", p->used)) {  /* stray prefix: restart (this byte may begin a frame) */
      p->used = 0;
      if (byte == 'W') p->bytes[p->used++] = byte;
    }
    return 0;
  }
  if (p->used < 9) return 0;
  unsigned char v = p->bytes[4];
  uint32_t crc = 0;
  for (int k = 0; k < 4; k++) crc |= (uint32_t)p->bytes[5 + k] << (8 * k);
  p->used = 0;
  if (crc != provision_crc(&v, 1) || v < POWER_TEST_SHORT || v > POWER_TEST_REPORT) return -1;
  *value = v;
  return 1;
}
/* The REG 0x49 bits a test press stands for (0 for the non-key commands). */
static inline uint8_t power_test_irq(unsigned char v) {
  return v == POWER_TEST_SHORT ? PWR_IRQ_SHORT : v == POWER_TEST_LONG ? PWR_IRQ_LONG : v == POWER_TEST_RELEASE ? PWR_IRQ_NEG_EDGE : 0;
}
