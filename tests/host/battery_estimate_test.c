/* Settings > Battery time-to-full estimate (battery_estimate.h) and its state line:
 * rate from the percent rise over a sliding window, >= 2 % of rise and >= 3 min of charging data,
 * reset when charging stops, ETA rounded to 5 min and clamped to 1 min .. 10 h. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "battery_estimate.h"

#define MIN_US (60LL * 1000 * 1000)
#define POLL (BATTERY_POLL_US)
/* Charge from `pct0` at `per_pct_s` seconds per percent, polled every 15 s for `secs`; returns the last ETA. */
static int charge(battery_estimator *e, int64_t *now, double *pct, double per_pct_s, int secs) {
  int eta = -1;
  for (int k = 0; k * 15 < secs; k++) {
    *now += POLL;
    *pct += 15.0 / per_pct_s;
    if (*pct > 100) *pct = 100;
    battery_estimate_add(e, *now, (int)*pct, true);
    eta = battery_estimate_minutes(e);
  }
  return eta;
}
static void rounding_and_clamps(void) {
  assert(battery_round_eta(0) == 1 && battery_round_eta(2.4) == 1);   /* rounds to 0 -> clamped to 1 min */
  assert(battery_round_eta(2.5) == 5 && battery_round_eta(7.4) == 5 && battery_round_eta(7.5) == 10);
  assert(battery_round_eta(82.4) == 80 && battery_round_eta(82.5) == 85 && battery_round_eta(83) == 85);
  assert(battery_round_eta(597.4) == 595 && battery_round_eta(599) == 600 && battery_round_eta(5000) == 600 && battery_round_eta(NAN) == 600);
  char t[24];
  battery_eta_text(80, t, sizeof t); assert(!strcmp(t, "1 h 20 min"));
  battery_eta_text(45, t, sizeof t); assert(!strcmp(t, "45 min"));
  battery_eta_text(120, t, sizeof t); assert(!strcmp(t, "2 h"));
  battery_eta_text(600, t, sizeof t); assert(!strcmp(t, "10 h"));
  battery_eta_text(1, t, sizeof t); assert(!strcmp(t, "1 min"));
}
static void steady_rate(void) {
  battery_estimator e; battery_estimate_reset(&e);
  int64_t now = 5 * MIN_US; double pct = 40.0;
  battery_estimate_add(&e, now, (int)pct, true);
  assert(battery_estimate_minutes(&e) == -1);
  /* 1 % per 2 min: under 3 min of data -> still estimating */
  int eta = charge(&e, &now, &pct, 120, 165);
  assert(eta == -1);
  /* 3 min of data but only 1 % of rise -> still estimating */
  eta = charge(&e, &now, &pct, 120, 30);
  assert(e.last_us - e.start_us >= BATTERY_EST_MIN_US && e.last_pct - e.pct[0] < 2 && eta == -1);
  /* 2 % of rise: an answer near (100 - pct) * 2 min, a multiple of 5 */
  eta = charge(&e, &now, &pct, 120, 150);
  int left = 100 - e.last_pct;
  printf("steady 1%%/2min at %d%%: eta=%d min (exact %d)\n", e.last_pct, eta, left * 2);
  assert(eta > 0 && eta % 5 == 0 && abs(eta - left * 2) <= 10);
  /* later in the run the window measures exactly the rate */
  eta = charge(&e, &now, &pct, 120, 20 * 60);
  left = 100 - e.last_pct;
  printf("steady after 20 min at %d%%: eta=%d (exact %d)\n", e.last_pct, eta, left * 2);
  assert(eta == battery_round_eta(left * 2.0) || abs(eta - left * 2) <= 5);
}
static void reset_when_charging_stops(void) {
  battery_estimator e; battery_estimate_reset(&e);
  int64_t now = 0; double pct = 30;
  battery_estimate_add(&e, now, 30, true);
  assert(charge(&e, &now, &pct, 60, 10 * 60) > 0);
  now += POLL; battery_estimate_add(&e, now, (int)pct, false);  /* unplugged */
  assert(!e.active && battery_estimate_minutes(&e) == -1);
  now += POLL; battery_estimate_add(&e, now, (int)pct, true);    /* plugged back in: start over */
  assert(e.active && battery_estimate_minutes(&e) == -1);
  assert(charge(&e, &now, &pct, 60, 150) == -1);                 /* < 3 min of the new run */
  assert(charge(&e, &now, &pct, 60, 60) > 0);
  /* no battery reading (-1) also ends the run; so does time running backwards */
  battery_estimate_add(&e, now + POLL, -1, true); assert(!e.active);
  battery_estimate_add(&e, now, 50, true); battery_estimate_add(&e, now + 4 * MIN_US, 53, true);
  battery_estimate_add(&e, now + 1 * MIN_US, 54, true);
  assert(e.start_us == now + 1 * MIN_US && battery_estimate_minutes(&e) == -1);
  /* the gauge stepping down while charging measures again (no negative rate) */
  battery_estimate_reset(&e); now = 0; pct = 60;
  battery_estimate_add(&e, now, 60, true);
  charge(&e, &now, &pct, 60, 6 * 60);
  now += POLL; battery_estimate_add(&e, now, e.last_pct - 3, true);
  assert(e.n == 1 && battery_estimate_minutes(&e) == -1);
}
static void clamps(void) {
  /* very slow: 1 % per 25 min over 2 h -> clamped to 10 h */
  battery_estimator e; battery_estimate_reset(&e);
  int64_t now = 0; double pct = 10;
  battery_estimate_add(&e, now, 10, true);
  int eta = charge(&e, &now, &pct, 25 * 60, 120 * 60);
  printf("slow: %d%% eta=%d\n", e.last_pct, eta);
  assert(eta == BATTERY_ETA_MAX);
  /* very fast, almost full: rounds below 5 min -> at least 1 min */
  battery_estimate_reset(&e); now = 0; pct = 90;
  battery_estimate_add(&e, now, 90, true);
  eta = charge(&e, &now, &pct, 25, 4 * 60);
  printf("fast near full: %d%% eta=%d\n", e.last_pct, eta);
  assert(e.last_pct >= 99 && eta >= BATTERY_ETA_MIN && eta <= 5);
}
static void sliding_window_and_plateau(void) {
  /* fast for 40 min, then 4x slower: within the window the estimate follows the slower rate */
  battery_estimator e; battery_estimate_reset(&e);
  int64_t now = 0; double pct = 5;
  battery_estimate_add(&e, now, 5, true);
  charge(&e, &now, &pct, 60, 40 * 60);
  int fast = battery_estimate_minutes(&e);
  int eta = charge(&e, &now, &pct, 240, 40 * 60);
  int left = 100 - e.last_pct;
  printf("window: fast eta=%d, after slowing eta=%d at %d%% (exact %d)\n", fast, eta, e.last_pct, left * 4);
  assert(eta >= left * 4 - 15 && eta <= left * 4 + 15);
  assert(now - e.t_us[0] <= BATTERY_EST_WINDOW_US + 5 * MIN_US);  /* old points slid out */
  /* a stall far longer than the usual step slows the estimate down */
  battery_estimate_reset(&e); now = 0; pct = 40;
  battery_estimate_add(&e, now, 40, true);
  int before = charge(&e, &now, &pct, 60, 10 * 60);
  for (int k = 0; k < 40; k++) { now += POLL; battery_estimate_add(&e, now, e.last_pct, true); }
  int stalled = battery_estimate_minutes(&e);
  printf("plateau: before=%d after a 10 min stall=%d\n", before, stalled);
  assert(stalled > before);
}
static void states_and_text(void) {
  char t[64];
  battery_view v = {0};
  battery_status_text(&v, t, sizeof t); assert(battery_state_of(&v) == BATT_UNKNOWN);
  v = (battery_view){.known = true, .ok = false};
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "Battery not detected"));
  v = (battery_view){.known = true, .ok = true, .present = false, .vbus = true, .percent = -1};
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "Battery not detected"));
  v = (battery_view){.known = true, .ok = true, .present = true, .percent = 64};
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "On battery") && battery_state_of(&v) == BATT_ON_BATTERY);
  v.vbus = v.charging = true; v.eta_min = -1;
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "Charging - estimating..."));
  v.eta_min = 80;
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "Charging - about 1 h 20 min to full"));
  v.percent = 100;
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "Fully charged"));
  v.percent = 98; v.charging = false;
  battery_status_text(&v, t, sizeof t); assert(!strcmp(t, "Fully charged"));
  v.percent = 97;
  battery_status_text(&v, t, sizeof t); assert(battery_state_of(&v) == BATT_PLUGGED && !strcmp(t, "Plugged in - not charging"));
  v.percent = 98; v.charging = true;
  assert(battery_state_of(&v) == BATT_CHARGING);
  /* PMU reading -> view (the code the poll task runs) */
  battery_estimator e; battery_estimate_reset(&e);
  pmu_sample m = {.ok = true, .battery = true, .vbus = false, .percent = 120, .vbat_mv = 4180};
  v = battery_update(&e, &m, 0);
  assert(v.known && v.present && v.percent == 100 && v.vbat_mv == 4180 && v.eta_min == -1 && !e.active);
  m.vbus = m.charging = true; m.percent = 50;
  for (int k = 0; k <= 24; k++) { m.percent = 50 + k / 4; v = battery_update(&e, &m, (int64_t)k * POLL); }
  assert(e.active && battery_state_of(&v) == BATT_CHARGING && v.eta_min > 0);
  m.battery = false; v = battery_update(&e, &m, 25 * POLL);
  assert(battery_state_of(&v) == BATT_MISSING && v.percent == -1 && v.eta_min == -1 && !e.active);
  m.ok = false; v = battery_update(&e, &m, 26 * POLL);
  assert(battery_state_of(&v) == BATT_MISSING);
  /* repaint only on a visible change */
  battery_view a = {.known = true, .ok = true, .present = true, .percent = 64, .vbat_mv = 3812, .eta_min = -1}, b = a;
  b.vbat_mv = 3815; assert(battery_visual_equal(&a, &b));
  b.percent = 63; assert(!battery_visual_equal(&a, &b));
}
int main(void) {
  rounding_and_clamps();
  steady_rate();
  reset_when_charging_stops();
  clamps();
  sliding_window_and_plateau();
  states_and_text();
  puts("battery_estimate: rate over a sliding window, >=2% and >=3 min, reset on unplug/drop/clock, 5-min rounding, 1 min..10 h clamps, state lines: PASS");
  return 0;
}
