/* Sleep policy (sleep_policy.h): when to sleep, light sleep only on battery (or the WPK1 4 test),
 * Wi-Fi kept at most 90 s for an in-flight Ask command, poll cadence, freeze guard paused while
 * asleep, and the X-Board-Sleep report text. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sleep_policy.h"

static sleep_in awake_battery(void) {
  sleep_in in;
  memset(&in, 0, sizeof in);
  in.vbus_known = true;
  in.auto_min = 5;
  return in;
}
static sleep_in asleep_battery(void) {
  sleep_in in = awake_battery();
  in.asleep = true;
  in.quiet = true;
  return in;
}
/* Full-match check of the header grammar (the bridge validates it with a regex). */
static bool report_ok(const char *s) {
  unsigned long a, b, c;
  char wake[8];
  int vbus, v0, v1, p0, p1, used = 0;
  if (sscanf(s, "slept_s=%lu cycles=%lu awake_ms=%lu wake=%7[a-z] vbus=%d vbat0=%d vbat1=%d pct0=%d pct1=%d%n", &a, &b, &c, wake, &vbus,
             &v0, &v1, &p0, &p1, &used) != 9)
    return false;
  if ((size_t)used != strlen(s) || strlen(s) > 160) return false;
  if (strcmp(wake, "pwr") && strcmp(wake, "boot") && strcmp(wake, "auto") && strcmp(wake, "usb") && strcmp(wake, "test")) return false;
  for (const char *p = s; *p; p++)
    if (*p < 0x20 || *p > 0x7e || (p[0] == ' ' && p[1] == ' ')) return false;
  return vbus == 0 || vbus == 1;
}

int main(void) {
  /* 1. Awake: auto sleep only on battery, after auto_min minutes idle, never during an Ask turn. */
  assert(WAVESHARE_AI_AUTO_SLEEP_MIN == 0);  /* default build: never */
  sleep_in in = awake_battery();
  in.idle_ms = 5 * 60000 - 1;
  sleep_out o = sleep_decide(&in);
  assert(!o.enter && o.poll_ms == 50 && !o.guard_paused && !o.light && !o.stop_wifi);
  in.idle_ms = 5 * 60000;
  assert(sleep_decide(&in).enter);
  in.vbus = true;
  assert(!sleep_decide(&in).enter); /* USB power: never */
  in.vbus = false;
  in.vbus_known = false;
  assert(!sleep_decide(&in).enter); /* PMU not answering: treat as USB */
  in.vbus_known = true;
  in.ask_turn = true;
  assert(!sleep_decide(&in).enter); /* recording / running */
  in.ask_turn = false;
  in.ask_busy = true;
  assert(!sleep_decide(&in).enter);
  in.ask_busy = false;
  in.auto_min = 0;
  in.idle_ms = UINT32_MAX;
  assert(!sleep_decide(&in).enter); /* 0 = never */
  in.auto_min = 120;
  in.idle_ms = 120u * 60000u;
  assert(sleep_decide(&in).enter);

  /* 2. Asleep on battery, everything quiet: light sleep, 250 ms timer, guard paused, Wi-Fi off. */
  in = asleep_battery();
  o = sleep_decide(&in);
  assert(!o.enter && o.light && o.stop_wifi && o.poll_ms == 250 && o.guard_paused);
  /* Not quiet yet (panel / touch / Wi-Fi still stopping): no light sleep, 50 ms polls. */
  in.quiet = false;
  o = sleep_decide(&in);
  assert(!o.light && o.poll_ms == 50 && o.guard_paused && o.stop_wifi);
  /* BOOT held: never light sleep (a low-level wake fires at once); poll 10 ms to debounce it. */
  in = asleep_battery();
  in.boot_down = true;
  o = sleep_decide(&in);
  assert(!o.light && o.poll_ms == 10);
  /* USB power: no light sleep (the USB-Serial-JTAG pad would drop), still asleep with Wi-Fi off. */
  in = asleep_battery();
  in.vbus = true;
  o = sleep_decide(&in);
  assert(!o.light && o.stop_wifi && o.poll_ms == 50 && o.guard_paused);
  in.vbus_known = false;
  in.vbus = false;
  assert(!sleep_decide(&in).light);
  /* WPK1 4 test: light sleep even on USB. */
  in = asleep_battery();
  in.vbus = true;
  in.test = true;
  o = sleep_decide(&in);
  assert(o.light && o.poll_ms == 250);
  assert(sleep_light_planned(true, true, true) && sleep_light_planned(false, true, false) && !sleep_light_planned(true, true, false) &&
         !sleep_light_planned(false, false, false));

  /* 3. An Ask command in flight keeps Wi-Fi (and so no light sleep) for at most 90 s. */
  in = asleep_battery();
  in.ask_busy = true;
  in.asleep_ms = 0;
  o = sleep_decide(&in);
  assert(!o.stop_wifi && !o.light && o.poll_ms == 50);
  in.asleep_ms = 89999;
  assert(!sleep_decide(&in).stop_wifi);
  in.asleep_ms = 90000;
  o = sleep_decide(&in);
  assert(o.stop_wifi && o.light);
  in.asleep_ms = 1000;
  in.ask_busy = false; /* finished early */
  assert(sleep_decide(&in).stop_wifi);

  /* 4. Freeze guard: paused across a 60 s light sleep; resuming WITHOUT re-arming the beats would
   *    restart the board, re-arming them at wake keeps it running. */
  const int64_t s = 1000000;
  int64_t owner = 10 * s, touch = 10 * s, now = 70 * s;
  assert(sleep_guard_check(now, owner, touch, true) == FREEZE_NONE);
  assert(sleep_guard_check(now, owner, touch, false) == FREEZE_OWNER);
  owner = touch = now;
  assert(sleep_guard_check(now + 4 * s, owner, touch, false) == FREEZE_NONE);
  assert(sleep_guard_check(now + 6 * s, owner, touch + 6 * s, false) == FREEZE_OWNER); /* a real freeze still counts */
  in = asleep_battery();
  assert(sleep_decide(&in).guard_paused);
  in.asleep = false;
  assert(!sleep_decide(&in).guard_paused);

  /* 5. X-Board-Sleep report. */
  char text[SLEEP_REPORT_MAX + 1];
  sleep_report r = {3723456, 14890, 4210, SLEEP_WAKE_PWR, false, 3912, 3870, 71, 68};
  assert(sleep_report_format(text, sizeof text, &r) > 0);
  assert(!strcmp(text, "slept_s=3723 cycles=14890 awake_ms=4210 wake=pwr vbus=0 vbat0=3912 vbat1=3870 pct0=71 pct1=68"));
  assert(report_ok(text));
  r = (sleep_report){999, 0, 999, SLEEP_WAKE_BOOT, true, -1, -7, -1, 250};
  assert(sleep_report_format(text, sizeof text, &r) > 0);
  assert(!strcmp(text, "slept_s=0 cycles=0 awake_ms=999 wake=boot vbus=1 vbat0=-1 vbat1=-1 pct0=-1 pct1=100") && report_ok(text));
  r = (sleep_report){UINT32_MAX, UINT32_MAX, UINT32_MAX, SLEEP_WAKE_TEST, true, 99999, 70000, 100, 100};
  int n = sleep_report_format(text, sizeof text, &r);
  assert(n > 0 && n <= 160 && report_ok(text) && strstr(text, "wake=test") && strstr(text, "vbat0=65535"));
  for (int w = SLEEP_WAKE_PWR; w <= SLEEP_WAKE_TEST; w++) {
    r.wake = (sleep_wake)w;
    assert(sleep_report_format(text, sizeof text, &r) > 0 && report_ok(text));
  }
  assert(!strcmp(sleep_wake_name(SLEEP_WAKE_AUTO), "auto") && !strcmp(sleep_wake_name(SLEEP_WAKE_USB), "usb"));
  char tiny[40];
  assert(sleep_report_format(tiny, sizeof tiny, &r) == -1); /* never a cut header */
  assert(sleep_awake_ms(5 * s, 4 * s) == 1000 && sleep_awake_ms(5 * s, 6 * s) == 0 && sleep_awake_ms(2 * s, 0) == 2000);
  printf("sleep_policy_test ok (%s)\n", text);
  return 0;
}
