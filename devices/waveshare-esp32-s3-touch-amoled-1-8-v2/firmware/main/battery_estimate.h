#pragma once
/* Battery for Settings > Battery: the AXP2101 fuel gauge (pmu_diag.c pmu_read(), polled every
 * BATTERY_POLL_US by a small task, never on the render path) and a time-to-full estimate.
 * Pure C, host-tested in tests/host/battery_estimate_test.c.
 *
 * Estimate: while charging, the percent readings are kept as (time, percent) points, one per rise,
 * over a sliding window. Rate = percent risen / time between the oldest usable point and the latest
 * rise (a plateau longer than the usual step slows the rate down). It needs >= BATTERY_EST_MIN_RISE
 * percent of rise and >= BATTERY_EST_MIN_US of charging data; it resets whenever charging stops.
 * The answer is rounded to 5 min and clamped to 1 min .. 10 h. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pmu_diag.h"

#define BATTERY_POLL_US (15LL * 1000 * 1000)
#define BATTERY_EST_POINTS 32
#define BATTERY_EST_WINDOW_US (30LL * 60 * 1000 * 1000)
#define BATTERY_EST_MIN_US (3LL * 60 * 1000 * 1000)
#define BATTERY_EST_MIN_RISE 2
#define BATTERY_ETA_MIN 1
#define BATTERY_ETA_MAX (10 * 60)
#define BATTERY_LOW_PCT 15

typedef struct {
  bool active;           /* a charging run is being measured */
  int n, last_pct;
  int64_t start_us, last_us;
  int64_t t_us[BATTERY_EST_POINTS];
  int8_t pct[BATTERY_EST_POINTS];
  bool edge[BATTERY_EST_POINTS];  /* the point is a real rise (not the run's first reading) */
} battery_estimator;

static inline void battery_estimate_reset(battery_estimator *e) { memset(e, 0, sizeof *e); }
static inline void battery_est_push(battery_estimator *e, int64_t t, int pct, bool edge) {
  if (e->n == BATTERY_EST_POINTS) {
    memmove(e->t_us, e->t_us + 1, sizeof e->t_us[0] * (BATTERY_EST_POINTS - 1));
    memmove(e->pct, e->pct + 1, sizeof e->pct[0] * (BATTERY_EST_POINTS - 1));
    memmove(e->edge, e->edge + 1, sizeof e->edge[0] * (BATTERY_EST_POINTS - 1));
    e->n--;
  }
  e->t_us[e->n] = t;
  e->pct[e->n] = (int8_t)pct;
  e->edge[e->n] = edge;
  e->n++;
}
/* One reading. percent < 0 (no battery) or !charging ends the run. */
static inline void battery_estimate_add(battery_estimator *e, int64_t now, int percent, bool charging) {
  if (!charging || percent < 0 || percent > 100) { battery_estimate_reset(e); return; }
  if (!e->active || now < e->last_us) {
    battery_estimate_reset(e);
    e->active = true;
    e->start_us = e->last_us = now;
    e->last_pct = percent;
    battery_est_push(e, now, percent, false);
    return;
  }
  if (percent < e->last_pct) { e->n = 0; battery_est_push(e, now, percent, false); }  /* gauge stepped down: measure again */
  else if (percent > e->last_pct) battery_est_push(e, now, percent, true);
  e->last_us = now;
  e->last_pct = percent;
  /* Sliding window: drop points older than the window, but keep one at or beyond its edge and enough
   * rise to measure, so a slow charge still spans the window. */
  while (e->n > 2 && now - e->t_us[1] >= BATTERY_EST_WINDOW_US && e->pct[e->n - 1] - e->pct[1] >= BATTERY_EST_MIN_RISE) {
    memmove(e->t_us, e->t_us + 1, sizeof e->t_us[0] * (size_t)(e->n - 1));
    memmove(e->pct, e->pct + 1, sizeof e->pct[0] * (size_t)(e->n - 1));
    memmove(e->edge, e->edge + 1, sizeof e->edge[0] * (size_t)(e->n - 1));
    e->n--;
  }
}
/* Minutes -> shown minutes: nearest 5 min, then clamped to 1 min .. 10 h. */
static inline int battery_round_eta(double minutes) {
  if (!(minutes >= 0)) minutes = BATTERY_ETA_MAX;
  if (minutes > BATTERY_ETA_MAX + 5) minutes = BATTERY_ETA_MAX + 5;
  int m = (int)((minutes + 2.5) / 5.0) * 5;
  return m < BATTERY_ETA_MIN ? BATTERY_ETA_MIN : (m > BATTERY_ETA_MAX ? BATTERY_ETA_MAX : m);
}
/* Minutes to full, or -1 while still estimating (not charging, too little data or rise). */
static inline int battery_estimate_minutes(const battery_estimator *e) {
  if (!e->active || e->n < 2 || e->last_us - e->start_us < BATTERY_EST_MIN_US) return -1;
  int base = 0, last = e->n - 1;
  /* The run's first reading may have sat at that percent for a while: start from the first real
   * rise when that still leaves enough rise to measure. */
  if (!e->edge[0] && e->n >= 3 && e->pct[last] - e->pct[1] >= BATTERY_EST_MIN_RISE) base = 1;
  int rise = e->pct[last] - e->pct[base];
  int64_t dt = e->t_us[last] - e->t_us[base];
  if (rise < BATTERY_EST_MIN_RISE || dt <= 0) return -1;
  /* A plateau longer than the usual step means the charge slowed down: count the extra time. */
  int64_t step = dt / rise, plateau = e->last_us - e->t_us[last];
  if (plateau > step) dt += plateau - step;
  double per_pct_min = (double)dt / rise / 60e6, left = 100 - e->last_pct;
  return battery_round_eta(left <= 0 ? 0 : left * per_pct_min);
}

/* What Settings shows (copied into home_ui under the state lock by the poll task). */
typedef struct {
  bool known;      /* a reading arrived (first one right after boot) */
  bool ok;         /* the PMU answered */
  bool present, vbus, charging;
  int percent, vbat_mv, eta_min;  /* eta_min -1 = estimating */
} battery_view;
typedef enum { BATT_UNKNOWN, BATT_MISSING, BATT_ON_BATTERY, BATT_CHARGING, BATT_FULL, BATT_PLUGGED } battery_state;
static inline battery_state battery_state_of(const battery_view *b) {
  if (!b->known) return BATT_UNKNOWN;
  if (!b->ok || !b->present || b->percent < 0) return BATT_MISSING;
  if (!b->vbus) return BATT_ON_BATTERY;
  if (b->percent >= 100 || (!b->charging && b->percent >= 98)) return BATT_FULL;
  return b->charging ? BATT_CHARGING : BATT_PLUGGED;
}
static inline const char *battery_state_name(battery_state s) {
  static const char *n[] = {"unknown", "missing", "battery", "charging", "full", "plugged"};
  return (unsigned)s <= BATT_PLUGGED ? n[s] : "?";
}
/* Reading -> view; feeds the estimator (same code on the board and in the host tests). */
static inline battery_view battery_update(battery_estimator *e, const pmu_sample *s, int64_t now) {
  battery_view v = {.known = true, .ok = s->ok, .present = s->ok && s->battery, .vbus = s->ok && s->vbus,
                    .charging = s->ok && s->battery && s->charging, .percent = -1, .vbat_mv = -1, .eta_min = -1};
  if (v.present) {
    v.percent = s->percent < 0 ? -1 : (s->percent > 100 ? 100 : s->percent);
    v.vbat_mv = s->vbat_mv;
  }
  battery_estimate_add(e, now, v.percent, v.present && v.vbus && v.charging);
  if (battery_state_of(&v) == BATT_CHARGING) v.eta_min = battery_estimate_minutes(e);
  return v;
}
/* "1 h 20 min", "45 min", "2 h". */
static inline void battery_eta_text(int minutes, char *out, size_t cap) {
  if (!out || !cap) return;
  int h = minutes / 60, m = minutes % 60;
  if (!h) snprintf(out, cap, "%d min", m);
  else if (!m) snprintf(out, cap, "%d h", h);
  else snprintf(out, cap, "%d h %d min", h, m);
}
/* One state line: "On battery", "Charging - about 1 h 20 min to full", "Charging - estimating...",
 * "Fully charged", "Battery not detected" (the page shows the part before " - " big). */
static inline void battery_status_text(const battery_view *b, char *out, size_t cap) {
  if (!out || !cap) return;
  char eta[24];  /* "10 h 55 min" at most; keeps the line inside a 64-byte buffer */
  switch (battery_state_of(b)) {
    case BATT_UNKNOWN: snprintf(out, cap, "Checking battery..."); break;
    case BATT_MISSING: snprintf(out, cap, "Battery not detected"); break;
    case BATT_ON_BATTERY: snprintf(out, cap, "On battery"); break;
    case BATT_FULL: snprintf(out, cap, "Fully charged"); break;
    case BATT_PLUGGED: snprintf(out, cap, "Plugged in - not charging"); break;
    case BATT_CHARGING:
      if (b->eta_min < 0) snprintf(out, cap, "Charging - estimating...");
      else { battery_eta_text(b->eta_min, eta, sizeof eta); snprintf(out, cap, "Charging - about %s to full", eta); }
      break;
  }
}
static inline bool battery_visual_equal(const battery_view *a, const battery_view *b) {
  return battery_state_of(a) == battery_state_of(b) && a->percent == b->percent && a->eta_min == b->eta_min &&
         a->vbat_mv / 10 == b->vbat_mv / 10;
}
