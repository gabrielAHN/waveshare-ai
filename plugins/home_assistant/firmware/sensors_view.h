#pragma once
/* Sensor page (Home tile "Sensor"): WHS1 frame from the bridge (GET /v1/home), which reads a sensor
 * Pi's readings from Home Assistant through the gateway with a machine credential
 * (bridge/waveshare_bridge/home_sensors.py). Pure C, host-tested (tests/host/home_sensors_test.c).
 * No IDF, network or storage.
 *
 * WHS1 (little-endian, SENSORS_FRAME_SIZE = 48):
 *   'WHS1' u8 version(1) u8 state(0 ok,1 not set up,2 unreachable,3 refused) u8 count(6) u8 0
 *   6 x { i16 value_x10 (0xFFFF unknown) u8 status(0 good,1 medium,2 bad,3 unknown) u8 0 u16 age_s }
 *   u32 crc32(all previous bytes)
 * Rows: TEMP C, HUM %, PRES hPa | PM1, PM2.5, PM10 (ug/m3). The page shows them as four plain-language
 * tiles (the sensor Pi dashboard's reading tiles): Air (PM1 + PM2.5 + PM10, worst wins),
 * Temperature, Humidity, Pressure; the word and colour follow the status the bridge sent. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "provision.h"
#include "wire_fields.h"

#define SENSORS_COUNT 6
#define SENSORS_FRAME_SIZE (8 + SENSORS_COUNT * 6 + 4)
#define SENSORS_NONE16 0xFFFFu
#define SENSORS_POLL_US (10LL * 1000 * 1000)    /* the Pi publishes every 10 s */
#define SENSORS_RETRY_US (5LL * 1000 * 1000)
#define SENSORS_REOPEN_US (2LL * 1000 * 1000)
#define SENSORS_FRESH_US (45LL * 1000 * 1000)   /* older than this: shown as "--" */
typedef enum { SS_OK, SS_SETUP, SS_UNREACHABLE, SS_REFUSED } sensors_state;
typedef enum { SQ_GOOD, SQ_MEDIUM, SQ_BAD, SQ_UNKNOWN } sensors_quality;
enum { SENSORS_TEMP, SENSORS_HUM, SENSORS_PRES, SENSORS_PM1, SENSORS_PM25, SENSORS_PM10 };
typedef struct { bool known; int16_t x10; uint8_t quality; uint16_t age_s; } sensors_row;
typedef struct { bool valid; uint8_t state; int64_t received_us; sensors_row r[SENSORS_COUNT]; } sensors_data;
/* http: last status (0 none/TLS, 409 phone sign-in, ...); busy is shared with the other live-slot
 * requests (home_ui.bots_busy). */
typedef struct { sensors_data data; int http; unsigned attempts, fails; int64_t attempt_us; bool refresh; } sensors_view;

static const char *const sensors_label[SENSORS_COUNT] = {"TEMP", "HUM", "PRES", "PM1", "PM2.5", "PM10"};

/* Fail-closed: `out` is written only when the whole frame validates. */
static inline bool sensors_decode(sensors_data *out, const unsigned char *f, size_t n, int64_t now_us) {
  if (!out || !f || n != SENSORS_FRAME_SIZE) return false;
  if (wire_u32(f + n - 4) != provision_crc(f, n - 4)) return false;
  if (memcmp(f, "WHS1", 4) || f[4] != 1 || f[5] > SS_REFUSED || f[6] != SENSORS_COUNT || f[7]) return false;
  sensors_data d;
  memset(&d, 0, sizeof d);
  d.state = f[5];
  for (int i = 0; i < SENSORS_COUNT; i++) {
    const unsigned char *p = f + 8 + i * 6;
    unsigned raw = (unsigned)p[0] | ((unsigned)p[1] << 8), age = (unsigned)p[4] | ((unsigned)p[5] << 8);
    if (p[2] > SQ_UNKNOWN || p[3]) return false;
    sensors_row *r = &d.r[i];
    r->known = raw != SENSORS_NONE16 && p[2] != SQ_UNKNOWN;
    if (d.state != SS_OK && r->known) return false;
    r->x10 = (int16_t)(uint16_t)raw;
    r->quality = r->known ? p[2] : SQ_UNKNOWN;
    r->age_s = (uint16_t)age;
  }
  d.valid = true;
  d.received_us = now_us;
  *out = d;
  return true;
}

/* Poll only while the page is visible: at once on open (unless one just finished), after a
 * failure every 5 s, otherwise every 10 s. */
static inline bool sensors_poll_due(const sensors_view *v, bool visible, int64_t opened_us, int64_t now_us) {
  if (!visible) return false;
  if (!v->attempts) return true;
  int64_t since = now_us - v->attempt_us;
  if ((v->refresh || v->attempt_us < opened_us) && since >= SENSORS_REOPEN_US) return true;
  if (v->fails && since >= SENSORS_RETRY_US) return true;
  return since >= SENSORS_POLL_US;
}
static inline bool sensors_fresh(const sensors_view *v, int64_t now_us) {
  /* 409 = the phone sign-in is gone: the bridge refused, so old numbers are not shown either */
  return v->http != 409 && v->data.valid && v->data.state == SS_OK && now_us - v->data.received_us <= SENSORS_FRESH_US;
}
/* Row value text ("23.4", "64", "1014", "5"), or "--" when unknown or stale. */
static inline void sensors_value_text(const sensors_view *v, int i, int64_t now_us, char *out, size_t cap) {
  if (!out || !cap) return;
  const sensors_row *r = i >= 0 && i < SENSORS_COUNT ? &v->data.r[i] : NULL;
  if (!r || !r->known || !sensors_fresh(v, now_us)) { snprintf(out, cap, "--"); return; }
  int x = r->x10, whole = (x < 0 ? -x : x) / 10, tenth = (x < 0 ? -x : x) % 10;
  if (i == 0) snprintf(out, cap, "%s%d.%d", x < 0 ? "-" : "", whole, tenth);
  else {
    /* Same as the Pi's f"{v:.0f}" (Python rounds halves to even: 74.5 -> 74, 75.5 -> 76). */
    int r = tenth > 5 || (tenth == 5 && (whole & 1)) ? whole + 1 : whole;
    snprintf(out, cap, "%s%d", x < 0 && r ? "-" : "", r);
  }
}
static inline sensors_quality sensors_row_quality(const sensors_view *v, int i, int64_t now_us) {
  if (i < 0 || i >= SENSORS_COUNT || !sensors_fresh(v, now_us) || !v->data.r[i].known) return SQ_UNKNOWN;
  return (sensors_quality)v->data.r[i].quality;
}
/* One short line above the tiles: why there are no numbers, else empty. */
static inline const char *sensors_note(const sensors_view *v, int64_t now_us) {
  if (v->http == 409) return "Sign in on Settings";
  if (!v->data.valid) return v->attempts ? "Sensors unavailable" : "Loading";
  if (v->data.state == SS_SETUP) return "Set up on the host";
  if (v->data.state == SS_REFUSED) return "Home Assistant refused";
  if (v->data.state == SS_UNREACHABLE) return "Home Assistant offline";
  if (!sensors_fresh(v, now_us)) return "No recent reading";
  return "";
}

/* ---- the four reading tiles (the Pi's sensor_oled.py GAUGES / READING_WORDS / AIR_WORDS) ---- */
/* Band gauge per row, in value_x10 units: scale lo..hi and the GOOD/MEDIUM/BAD bands as ascending
 * upper edges (the last edge = hi). PM uses a square-root scale so the narrow GOOD band stays visible.
 * The edges mirror the bridge's status() thresholds. */
typedef struct { int16_t lo, hi; uint8_t n, root; int16_t edge[5]; uint8_t band[5]; } sensors_gauge_spec;
static inline const sensors_gauge_spec *sensors_gauge(int row) {
  static const sensors_gauge_spec g[SENSORS_COUNT] = {
      {100, 340, 5, 0, {150, 180, 260, 300, 340}, {SQ_BAD, SQ_MEDIUM, SQ_GOOD, SQ_MEDIUM, SQ_BAD}},
      {100, 800, 5, 0, {200, 300, 600, 700, 800}, {SQ_BAD, SQ_MEDIUM, SQ_GOOD, SQ_MEDIUM, SQ_BAD}},
      {9750, 10450, 5, 0, {9850, 10000, 10250, 10350, 10450}, {SQ_BAD, SQ_MEDIUM, SQ_GOOD, SQ_MEDIUM, SQ_BAD}},
      {0, 600, 3, 1, {90, 354, 600, 0, 0}, {SQ_GOOD, SQ_MEDIUM, SQ_BAD, 0, 0}},
      {0, 600, 3, 1, {90, 354, 600, 0, 0}, {SQ_GOOD, SQ_MEDIUM, SQ_BAD, 0, 0}},
      {0, 2000, 3, 1, {540, 1540, 2000, 0, 0}, {SQ_GOOD, SQ_MEDIUM, SQ_BAD, 0, 0}}};
  return row >= 0 && row < SENSORS_COUNT ? &g[row] : NULL;
}
/* Where x10 sits on a gauge `span` px wide: 0..span, nearest px (clamped to the scale). */
static inline int sensors_gauge_px(int row, int x10, int span) {
  const sensors_gauge_spec *g = sensors_gauge(row);
  if (!g || span <= 0) return 0;
  int v = x10 < g->lo ? g->lo : (x10 > g->hi ? g->hi : x10), num = v - g->lo, den = g->hi - g->lo;
  if (g->root) return (int)floorf(sqrtf((float)num / (float)den) * (float)span + .5f);
  return (2 * num * span + den) / (2 * den);
}
/* One word per band, so the word never disagrees with the tile's colour: (bad-low, medium-low, good,
 * medium-high, bad-high). Which side of the GOOD band's midpoint the value is on picks low or high. */
static inline const char *sensors_reading_word(int row, sensors_quality q, int x10) {
  static const char *const words[3][5] = {{"Cold", "Cool", "Comfy", "Warm", "Hot"},          /* <15 15-18 18-26 26-30 >30 C */
                                          {"Very dry", "Dry", "Comfy", "Damp", "Humid"},     /* <20 20-30 30-60 60-70 >70 % */
                                          {"Stormy", "Low", "Normal", "High", "Very high"}}; /* <985 ..1000 ..1025 ..1035 >1035 */
  if (row < SENSORS_TEMP || row > SENSORS_PRES || q > SQ_BAD) return "--";
  if (q == SQ_GOOD) return words[row][2];
  const sensors_gauge_spec *g = sensors_gauge(row);
  int start = g->lo, glo = 0, ghi = 0;
  for (int k = 0; k < g->n; k++) {
    if (g->band[k] == SQ_GOOD) { glo = start; ghi = g->edge[k]; }
    start = g->edge[k];
  }
  bool low = 2 * x10 < glo + ghi;
  if (q == SQ_MEDIUM) return words[row][low ? 1 : 3];
  return words[row][low ? 0 : 4];
}
static inline const char *sensors_air_word(sensors_quality q) {
  return q == SQ_GOOD ? "Clean" : (q == SQ_MEDIUM ? "Okay" : (q == SQ_BAD ? "Poor" : "--"));
}
/* The worst fresh PM row (PM2.5 wins ties, then PM10, then PM1), or -1 when none is known. */
static inline int sensors_air_row(const sensors_view *v, int64_t now_us) {
  static const int order[3] = {SENSORS_PM25, SENSORS_PM10, SENSORS_PM1};
  int best = -1;
  sensors_quality worst = SQ_UNKNOWN;
  for (int k = 0; k < 3; k++) {
    sensors_quality q = sensors_row_quality(v, order[k], now_us);
    if (q == SQ_UNKNOWN) continue;
    if (best < 0 || q > worst) { best = order[k]; worst = q; }
  }
  return best;
}
/* The number line's two drawn-not-typed glyphs (the 9x17 font has neither). */
#define SENSORS_DOT '\x01'     /* middle dot: "PM2.5 . 12" */
#define SENSORS_DEGREE '\x02'  /* degree ring: "23.4 deg C" */
#define SENSORS_TILES 4
typedef struct {
  const char *label;  /* "Air", "Temperature", "Humidity", "Pressure" */
  const char *word;   /* "Comfy", "Very high", ...; "--" on a neutral card */
  sensors_quality q;  /* tile colour; SQ_UNKNOWN = neutral card (no verdict, stale, signed out) */
  int row;            /* the gauge's row (Air: the worst PM row) */
  int x10;            /* marker value (only when q != SQ_UNKNOWN) */
  char detail[20];    /* the number, small: "PM2.5 " DOT " 12", "23.4" DEGREE "C", "41%", "1012 hPa"; "" when neutral */
} sensors_tile;
/* Tile i (0 Air, 1 Temperature, 2 Humidity, 3 Pressure). No number without a fresh verdict. */
static inline sensors_tile sensors_tile_at(const sensors_view *v, int i, int64_t now_us) {
  static const char *const labels[SENSORS_TILES] = {"Air", "Temperature", "Humidity", "Pressure"};
  sensors_tile t;
  memset(&t, 0, sizeof t);
  t.label = labels[i < 0 || i >= SENSORS_TILES ? 0 : i];
  t.word = "--";
  t.q = SQ_UNKNOWN;
  t.row = i <= 0 || i >= SENSORS_TILES ? SENSORS_PM25 : i - 1;
  int row = i <= 0 || i >= SENSORS_TILES ? sensors_air_row(v, now_us) : i - 1;
  sensors_quality q = row < 0 ? SQ_UNKNOWN : sensors_row_quality(v, row, now_us);
  if (q == SQ_UNKNOWN) return t;
  char value[16];
  sensors_value_text(v, row, now_us, value, sizeof value);
  t.q = q;
  t.row = row;
  t.x10 = v->data.r[row].x10;
  if (row >= SENSORS_PM1) {
    t.word = sensors_air_word(q);
    snprintf(t.detail, sizeof t.detail, "%.6s %c %.8s", sensors_label[row], SENSORS_DOT, value);
  } else {
    t.word = sensors_reading_word(row, q, t.x10);
    if (row == SENSORS_TEMP) snprintf(t.detail, sizeof t.detail, "%.8s%cC", value, SENSORS_DEGREE);
    else snprintf(t.detail, sizeof t.detail, "%.8s%s", value, row == SENSORS_HUM ? "%" : " hPa");
  }
  return t;
}
