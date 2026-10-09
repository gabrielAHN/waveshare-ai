/* Power button (power_button.h): AXP2101 PWRON IRQ reads + BOOT samples -> sleep toggle, power-off
 * countdown, cancel, power off. Edge polarity is not assumed; injected presses never power off. */
#include <assert.h>
#include <stdio.h>
#include "power_button.h"

static power_out irq(power_button *b, uint8_t bits, uint32_t ms) { return power_button_irq(b, bits, POWER_SRC_PWR, ms); }
static int frame(power_cmd_parser *p, const char *magic, unsigned char v, int bad_crc, unsigned char *out) {
  unsigned char f[9];
  memcpy(f, magic, 4);
  f[4] = v;
  uint32_t crc = provision_crc(&v, 1) ^ (bad_crc ? 1u : 0u);
  for (int k = 0; k < 4; k++) f[5 + k] = (unsigned char)(crc >> (8 * k));
  int r = 0;
  for (int k = 0; k < 9; k++) r = power_cmd_feed(p, f[k], out);
  return r;
}

int main(void) {
  /* AXP2101 register values: keep the unrelated bits, set only ours. */
  assert(axp_key_timing(0x00) == 0x04 && axp_key_timing(0xff) == 0xc4 && axp_key_timing(0x3f) == 0x04);
  assert(axp_key_poweroff(0x00) == 0x02 && axp_key_poweroff(0x05) == 0x06 && axp_key_poweroff(0xff) == 0xfe);
  assert(axp_key_irq_enable(0x00) == 0x0f && axp_key_irq_enable(0xf0) == 0xff);
  assert(axp_soft_off(0x00) == 0x01 && axp_soft_off(0x80) == 0x81);
  assert(power_secs_left(1000) == 4 && power_secs_left(1999) == 4 && power_secs_left(2000) == 3 && power_secs_left(4001) == 1 &&
         power_secs_left(4999) == 1 && power_secs_left(5000) == 0 && power_secs_left(90000) == 0);
  power_button b;
  power_out o;

  /* 1. Short press: one read with the SHORT bit (with or without the edges) = sleep toggle. */
  power_button_init(&b);
  o = irq(&b, PWR_IRQ_SHORT, 100);
  assert(o.act == POWER_ACT_SLEEP_TOGGLE && o.event == POWER_EV_SHORT && o.irq == 0x08 && o.src == POWER_SRC_PWR);
  o = irq(&b, PWR_IRQ_SHORT | PWR_IRQ_EDGES, 200);
  assert(o.act == POWER_ACT_SLEEP_TOGGLE && o.event == POWER_EV_SHORT);
  /* A press edge alone (either polarity) is only logged; other IRQ sources are ignored. */
  assert(irq(&b, PWR_IRQ_POS_EDGE, 300).act == POWER_ACT_NONE && irq(&b, PWR_IRQ_POS_EDGE, 300).event == POWER_EV_EDGE);
  assert(irq(&b, PWR_IRQ_NEG_EDGE, 300).act == POWER_ACT_NONE && irq(&b, PWR_IRQ_NEG_EDGE, 300).event == POWER_EV_EDGE);
  /* Both edges in one read (a fast tap, no SHORT yet): nothing but a log line. */
  o = irq(&b, PWR_IRQ_EDGES, 400);
  assert(o.act == POWER_ACT_NONE && o.event == POWER_EV_EDGE && !b.armed);
  assert(irq(&b, 0xf0, 500).event == POWER_EV_NONE && irq(&b, 0x00, 500).act == POWER_ACT_NONE);

  /* 2. Long press -> countdown 4,3,2,1 -> power off at 5 s (press = LONG time - 1 s). */
  power_button_init(&b);
  assert(irq(&b, PWR_IRQ_POS_EDGE, 0).event == POWER_EV_EDGE);
  o = irq(&b, PWR_IRQ_LONG, 1000);
  assert(o.act == POWER_ACT_OFF_ARMED && o.event == POWER_EV_LONG && o.secs_left == 4 && b.armed && b.press_ms == 0);
  assert(power_button_tick(&b, 1500).act == POWER_ACT_NONE);
  o = power_button_tick(&b, 2000);
  assert(o.act == POWER_ACT_OFF_ARMED && o.secs_left == 3);
  assert(power_button_tick(&b, 2050).act == POWER_ACT_NONE);
  assert(power_button_tick(&b, 3000).secs_left == 2 && power_button_tick(&b, 4000).secs_left == 1);
  assert(power_button_tick(&b, 4999).act == POWER_ACT_NONE);
  o = power_button_tick(&b, 5000);
  assert(o.act == POWER_ACT_POWER_OFF && o.held_ms == 5000 && o.secs_left == 0 && !o.dry_run && o.src == POWER_SRC_PWR);
  assert(!b.armed && power_button_tick(&b, 9000).act == POWER_ACT_NONE);

  /* 3. Release before 5 s cancels, whichever edge polarity the AXP2101 reports. */
  for (int pol = 0; pol < 2; pol++) {
    power_button_init(&b);
    assert(irq(&b, PWR_IRQ_LONG, 1000).act == POWER_ACT_OFF_ARMED);
    assert(power_button_tick(&b, 2000).secs_left == 3);
    o = irq(&b, pol ? PWR_IRQ_POS_EDGE : PWR_IRQ_NEG_EDGE, 2300);
    assert(o.act == POWER_ACT_OFF_CANCEL && o.event == POWER_EV_RELEASE && o.held_ms == 2300);
    assert(!b.armed && power_button_tick(&b, 8000).act == POWER_ACT_NONE);
  }
  /* LONG and an edge in the SAME read: held past 1 s and already let go -> cancel, never armed. */
  power_button_init(&b);
  o = irq(&b, PWR_IRQ_LONG | PWR_IRQ_NEG_EDGE, 1200);
  assert(o.act == POWER_ACT_OFF_CANCEL && o.event == POWER_EV_RELEASE && o.held_ms == 1000 && !b.armed);
  assert(power_button_tick(&b, 7000).act == POWER_ACT_NONE);
  /* SHORT IRQ while armed (released, tapped again): cancel only, no sleep toggle. */
  power_button_init(&b);
  irq(&b, PWR_IRQ_LONG, 1000);
  o = irq(&b, PWR_IRQ_SHORT, 2500);
  assert(o.act == POWER_ACT_OFF_CANCEL && o.held_ms == 2500 && !b.armed);
  /* A repeated LONG while counting down changes nothing. */
  power_button_init(&b);
  irq(&b, PWR_IRQ_LONG, 1000);
  o = irq(&b, PWR_IRQ_LONG, 1600);
  assert(o.act == POWER_ACT_NONE && o.event == POWER_EV_LONG && b.armed && b.press_ms == 0);
  /* The release read arrives after the 5 s were up (no tick in between): power off, not cancel. */
  o = irq(&b, PWR_IRQ_NEG_EDGE, 5200);
  assert(o.act == POWER_ACT_POWER_OFF && o.held_ms == 5200 && o.event == POWER_EV_RELEASE && !o.dry_run);
  /* Millisecond counter wrap. */
  power_button_init(&b);
  irq(&b, PWR_IRQ_LONG, UINT32_MAX - 100u);
  assert(power_button_tick(&b, 3000).act == POWER_ACT_OFF_ARMED);  /* 4101 ms held -> 1 */
  assert(b.secs_shown == 1 && power_button_tick(&b, 3898).act == POWER_ACT_NONE && power_button_tick(&b, 3899).act == POWER_ACT_POWER_OFF);

  /* 4. Injected (WPK1) presses never power off: a held test press ends in a dry run. */
  power_button_init(&b);
  o = power_button_irq(&b, power_test_irq(POWER_TEST_LONG), POWER_SRC_TEST, 1000);
  assert(o.act == POWER_ACT_OFF_ARMED && o.src == POWER_SRC_TEST && o.irq == PWR_IRQ_LONG);
  for (uint32_t t = 1000; t < 5000; t += 50) assert(power_button_tick(&b, t).act != POWER_ACT_POWER_OFF);
  o = power_button_tick(&b, 5000);
  assert(o.act == POWER_ACT_POWER_OFF && o.dry_run && o.src == POWER_SRC_TEST);
  power_button_init(&b);
  power_button_irq(&b, PWR_IRQ_LONG, POWER_SRC_TEST, 1000);
  o = power_button_irq(&b, PWR_IRQ_NEG_EDGE, POWER_SRC_TEST, 9000);  /* late injected release */
  assert(o.act == POWER_ACT_POWER_OFF && o.dry_run);
  power_button_init(&b);
  power_button_irq(&b, PWR_IRQ_LONG, POWER_SRC_TEST, 1000);
  o = power_button_irq(&b, power_test_irq(POWER_TEST_RELEASE), POWER_SRC_TEST, 3000);
  assert(o.act == POWER_ACT_OFF_CANCEL && o.held_ms == 3000 && o.src == POWER_SRC_TEST);
  assert(power_button_irq(&b, power_test_irq(POWER_TEST_SHORT), POWER_SRC_TEST, 4000).act == POWER_ACT_SLEEP_TOGGLE);
  assert(power_test_irq(POWER_TEST_SLEEP) == 0 && power_test_irq(POWER_TEST_REPORT) == 0);

  /* 5. BOOT: 40 ms debounce, the press toggles, a bounce restarts the window, holding does nothing. */
  power_button_init(&b);
  assert(power_button_boot(&b, false, 0).act == POWER_ACT_NONE);
  assert(power_button_boot(&b, true, 10).act == POWER_ACT_NONE);
  assert(power_button_boot(&b, false, 20).act == POWER_ACT_NONE); /* bounce */
  assert(power_button_boot(&b, true, 30).act == POWER_ACT_NONE);
  assert(power_button_boot(&b, true, 69).act == POWER_ACT_NONE);
  o = power_button_boot(&b, true, 70);
  assert(o.act == POWER_ACT_SLEEP_TOGGLE && o.event == POWER_EV_SHORT && o.src == POWER_SRC_BOOT && b.boot_stable);
  for (uint32_t t = 80; t <= 12000; t += 10) {  /* held 12 s: no second toggle, no countdown, never off */
    o = power_button_boot(&b, true, t);
    assert(o.act == POWER_ACT_NONE && o.event == POWER_EV_NONE);
    assert(power_button_tick(&b, t).act == POWER_ACT_NONE && !b.armed);
  }
  assert(power_button_boot(&b, false, 12010).act == POWER_ACT_NONE && power_button_boot(&b, false, 12050).act == POWER_ACT_NONE);
  assert(!b.boot_stable);
  assert(power_button_boot(&b, true, 13000).act == POWER_ACT_NONE && power_button_boot(&b, true, 13040).act == POWER_ACT_SLEEP_TOGGLE);
  /* BOOT while the power-off countdown runs: ignored. */
  power_button_init(&b);
  irq(&b, PWR_IRQ_LONG, 1000);
  power_button_boot(&b, true, 1100);
  assert(power_button_boot(&b, true, 1200).act == POWER_ACT_NONE && b.armed);

  /* 6. WPK1 frames: values 1..5 with a good CRC; anything else is rejected; stray bytes resync. */
  power_cmd_parser p;
  memset(&p, 0, sizeof p);
  unsigned char v = 0;
  for (unsigned char k = 1; k <= 5; k++) {
    v = 0;
    assert(frame(&p, "WPK1", k, 0, &v) == 1 && v == k);
  }
  assert(frame(&p, "WPK1", 0, 0, &v) == -1 && frame(&p, "WPK1", 6, 0, &v) == -1 && frame(&p, "WPK1", 2, 1, &v) == -1);
  assert(frame(&p, "WGS1", 1, 0, &v) == 0 && frame(&p, "WLV1", 1, 0, &v) == 0);
  /* "Wx", then "W" + a whole frame ("WW..."): the parser resyncs on the frame's own 'W'. */
  assert(power_cmd_feed(&p, 'W', &v) == 0 && power_cmd_feed(&p, 'x', &v) == 0 && power_cmd_feed(&p, 'W', &v) == 0);
  v = 0;
  assert(frame(&p, "WPK1", 3, 0, &v) == 1 && v == 3);

  /* 7. Names and the overlay view. */
  assert(!strcmp(power_src_name(POWER_SRC_PWR), "pwr") && !strcmp(power_src_name(POWER_SRC_BOOT), "boot") &&
         !strcmp(power_src_name(POWER_SRC_TEST), "test"));
  assert(!strcmp(power_event_name(POWER_EV_SHORT), "short") && !strcmp(power_event_name(POWER_EV_LONG), "long") &&
         !strcmp(power_event_name(POWER_EV_RELEASE), "release") && !strcmp(power_event_name(POWER_EV_EDGE), "edge"));
  power_view a = {0}, c = {0};
  c.secs = 3;
  assert(power_view_equal(&a, &c));  /* hidden: the digit does not matter */
  a.countdown = c.countdown = true;
  a.secs = 4;
  assert(!power_view_equal(&a, &c));
  a.secs = 3;
  assert(power_view_equal(&a, &c));
  puts("power_button_test ok");
  return 0;
}
