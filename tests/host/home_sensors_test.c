/* Sensor page (sensors_view.h + home_render.h sensors_render): WHS1 decode fails closed, poll only while
 * visible, value text (Python half-even rounding), staleness, and the four plain-language reading tiles
 * (the sensor Pi dashboard's design): absolute tile colours per status, the words at every band edge and
 * on both sides of the GOOD band, Air = worst of PM1/PM2.5/PM10 (PM2.5 wins ties, then PM10), the band
 * gauge's marker position (linear and square-root scales), neutral '--' cards when stale / unknown /
 * signed out, every pixel inside the panel's safe area and no text outside its tile.
 * SENSORS_PREVIEW=<dir> also writes raw little-endian RGB565 368x448 previews (*.rgb565). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_render.h"
/* Light + Orange Home tiles (SPEC3 Contract S): tile i's paper is white mixed toward the accent
 * (238,112,28) at 16 / 24 / 32 / 40 % in carousel order (independent oracle of theme.h theme_tile_paper). */
static uint16_t tile_paper(int tile){static const int pct[4]={16,24,32,40};int p=pct[tile%4];
 return sp_pack_lcd(255-((255-238)*p+50)/100,255-((255-112)*p+50)/100,255-((255-28)*p+50)/100);}

static uint16_t p[SPARKLES_PIXELS];

/* Pinned colours: the Pi dashboard's palette (sensor_oled.py _mix, Python round) packed to RGB565 by hand,
 * NOT computed with the renderer's own helpers. */
#define PAGE_565 0xFFBD          /* (250,244,232) */
#define INK_565 0x29A9           /* (47,52,74) */
#define CARD_565 0xF75B          /* (241,233,219) neutral tile */
#define RULE_565 0xDEB8          /* (222,212,196) neutral gauge */
#define CORE_565 0xFFFE          /* (255,252,246) marker core */
#define FILL_GOOD_565 0xB69E     /* (182,211,241) soft blue tile */
#define FILL_MEDIUM_565 0xFF14   /* (248,227,163) soft yellow tile */
#define FILL_BAD_565 0xF5B6      /* (246,183,181) soft red tile */
#define LABEL_GOOD_565 0x42AF    /* (66,87,121) */
#define LABEL_MEDIUM_565 0x6B09  /* (106,96,75) */
#define LABEL_BAD_565 0x6A2A     /* (104,70,85) */
#define LABEL_NEUTRAL_565 0x630E /* (98,96,117) */
#define R5(c) (((c) >> 11) & 31)
#define G6(c) (((c) >> 5) & 63)
#define B5(c) ((c) & 31)
/* gauge bands [band][tile status] */
static const uint16_t BAND_565[3][3] = {{0x753B, 0x7D5A, 0x7D1A}, {0xDDEC, 0xE60B, 0xE5CB}, {0xD3D0, 0xE3EE, 0xE3AF}};
static const uint16_t FILL_565[3] = {FILL_GOOD_565, FILL_MEDIUM_565, FILL_BAD_565};
static const uint16_t LABEL_565[3] = {LABEL_GOOD_565, LABEL_MEDIUM_565, LABEL_BAD_565};

static size_t frame(unsigned char *f, int state, const int16_t x10[6], const uint8_t q[6], uint16_t age) {
  memcpy(f, "WHS1", 4); f[4] = 1; f[5] = (unsigned char)state; f[6] = 6; f[7] = 0;
  for (int i = 0; i < 6; i++) {
    unsigned char *r = f + 8 + i * 6;
    uint16_t raw = x10 ? (uint16_t)x10[i] : 0xFFFF;
    r[0] = raw & 255; r[1] = raw >> 8; r[2] = q ? q[i] : 3; r[3] = 0; r[4] = age & 255; r[5] = age >> 8;
  }
  uint32_t crc = provision_crc(f, 44);
  f[44] = crc & 255; f[45] = (crc >> 8) & 255; f[46] = (crc >> 16) & 255; f[47] = crc >> 24;
  return SENSORS_FRAME_SIZE;
}
#define T0 (100LL * 1000000)
#define NONE ((int16_t)0xFFFF)
/* The bridge's status() (home_sensors.py), in value_x10 units, so fixtures carry consistent statuses. */
static uint8_t bridge_status(int row, int x10) {
  if (x10 == NONE) return SQ_UNKNOWN;
  if (row == 0) return x10 >= 180 && x10 <= 260 ? SQ_GOOD : (x10 >= 150 && x10 <= 300 ? SQ_MEDIUM : SQ_BAD);
  if (row == 1) return x10 >= 300 && x10 <= 600 ? SQ_GOOD : (x10 >= 200 && x10 <= 700 ? SQ_MEDIUM : SQ_BAD);
  if (row == 2) return x10 >= 10000 && x10 <= 10250 ? SQ_GOOD : (x10 >= 9850 && x10 <= 10350 ? SQ_MEDIUM : SQ_BAD);
  if (row <= 4) return x10 <= 90 ? SQ_GOOD : (x10 <= 354 ? SQ_MEDIUM : SQ_BAD);
  return x10 <= 540 ? SQ_GOOD : (x10 <= 1540 ? SQ_MEDIUM : SQ_BAD);
}
/* A fresh sensors_view with these readings (statuses as the bridge would send them). */
static sensors_view readings(int16_t t, int16_t h, int16_t pr, int16_t pm1, int16_t pm25, int16_t pm10) {
  const int16_t x10[6] = {t, h, pr, pm1, pm25, pm10};
  uint8_t q[6];
  for (int i = 0; i < 6; i++) q[i] = bridge_status(i, x10[i]);
  unsigned char f[SENSORS_FRAME_SIZE];
  sensors_view v;
  memset(&v, 0, sizeof v);
  v.attempts = 1; v.http = 200;
  frame(f, SS_OK, x10, q, 3);
  assert(sensors_decode(&v.data, f, sizeof f, T0));
  return v;
}

static int count_color(int x0, int y0, int x1, int y1, uint16_t c) {
  int n = 0;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) n += p[y * 368 + x] == c;
  return n;
}
static void save(const char *dir, const char *name) {
  if (!dir) return;
  char path[512]; snprintf(path, sizeof path, "%s/%s.rgb565", dir, name);
  FILE *f = fopen(path, "wb"); assert(f); fwrite(p, 2, SPARKLES_PIXELS, f); fclose(f);
}
static void render_page(const sensors_view *v, int64_t now, const phone_status *ph) {
  static home_ui s;
  memset(&s, 0, sizeof s);
  s.page = SENSORS; s.input.stamp_us = now; s.sensors = *v;
  if (ph) s.phone_ha.st = *ph;   /* the Sensor page reads the Home Assistant provider's sign-in */
  assert(home_render(&s, p, SPARKLES_PIXELS));
}
/* Every pixel that is not page paper: inside the rounded safe area (>= 16 px from the edges and the
 * corner arcs, out of the bottom swipe-Home band). */
static void assert_safe(const char *what) {
  for (int y = 0; y < 448; y++) for (int x = 0; x < 368; x++) {
    if (p[y * 368 + x] == PAGE_565 || set_safe_px(x, y)) continue;
    fprintf(stderr, "%s: pixel (%d,%d) outside the safe area\n", what, x, y); assert(0);
  }
}
/* Below the note, every drawn pixel belongs to a tile, and every text colour pixel sits inside its
 * tile's padded box (only the gauge marker may reach into the padding, beside the bar). */
static void assert_in_tiles(const char *what) {
  for (int y = SN_TILE_Y - 4; y < 448; y++) for (int x = 0; x < 368; x++) {
    uint16_t c = p[y * 368 + x];
    if (c == PAGE_565) continue;
    int inside = -1;
    for (int i = 0; i < SENSORS_TILES; i++) {
      int tx, ty; sensors_tile_box(i, &tx, &ty);
      if (x >= tx && x < tx + SN_TILE_W && y >= ty && y < ty + SN_TILE_H) inside = i;
    }
    if (inside < 0) { fprintf(stderr, "%s: stray pixel (%d,%d)\n", what, x, y); assert(0); }
    int tx, ty; sensors_tile_box(inside, &tx, &ty);
    bool text = c == INK_565 || c == LABEL_NEUTRAL_565 || c == LABEL_GOOD_565 || c == LABEL_MEDIUM_565 || c == LABEL_BAD_565;
    bool bar_rows = y >= ty + SN_TILE_H - SN_BAR_BOTTOM - SN_BAR_H - 4 && y < ty + SN_TILE_H - SN_BAR_BOTTOM + 4;
    if (text && !bar_rows && (x < tx + SN_PAD || x >= tx + SN_TILE_W - SN_PAD || y < ty + 12)) {
      fprintf(stderr, "%s: text pixel (%d,%d) outside tile %d's padded box\n", what, x, y, inside); assert(0);
    }
  }
}
/* Tile fill sampled near the top-right corner (no text there). */
static uint16_t tile_fill(int i) {
  int tx, ty; sensors_tile_box(i, &tx, &ty);
  return p[(ty + 10) * 368 + tx + SN_TILE_W - 10];
}
/* The marker's light core on the bar's centre row: mean column relative to the bar start, or -1. */
static int marker_col(int i) {
  int tx, ty; sensors_tile_box(i, &tx, &ty);
  int y = ty + SN_TILE_H - SN_BAR_BOTTOM - SN_BAR_H / 2, sum = 0, n = 0;
  for (int x = tx; x < tx + SN_TILE_W; x++) if (p[y * 368 + x] == CORE_565) { sum += x; n++; }
  return n ? sum / n - (tx + SN_PAD) : -1;
}
static int ink_in_rows(int i, int y0, int y1) {
  int tx, ty; sensors_tile_box(i, &tx, &ty);
  return count_color(tx, ty + y0, tx + SN_TILE_W, ty + y1, INK_565);
}

int main(void) {
  const char *dir = getenv("SENSORS_PREVIEW");
  unsigned char f[SENSORS_FRAME_SIZE];
  sensors_data d;
  static const int16_t VAL[6] = {234, 640, 10136, 30, 400, NONE};
  static const uint8_t QUAL[6] = {SQ_GOOD, SQ_MEDIUM, SQ_GOOD, SQ_GOOD, SQ_BAD, SQ_UNKNOWN};

  /* ---- decode ---- */
  frame(f, SS_OK, VAL, QUAL, 4);
  assert(sensors_decode(&d, f, sizeof f, T0) && d.valid && d.state == SS_OK && d.received_us == T0);
  assert(d.r[0].known && d.r[0].x10 == 234 && d.r[0].quality == SQ_GOOD && d.r[0].age_s == 4);
  assert(d.r[2].x10 == 10136 && !d.r[5].known && d.r[5].quality == SQ_UNKNOWN);
  sensors_data keep = d;
  unsigned char bad[SENSORS_FRAME_SIZE];
  for (int i = 0; i < SENSORS_FRAME_SIZE; i++) {       /* any single flipped byte is refused */
    memcpy(bad, f, sizeof f); bad[i] ^= 0x10;
    assert(!sensors_decode(&d, bad, sizeof bad, T0) && !memcmp(&d, &keep, sizeof d));
  }
  assert(!sensors_decode(&d, f, sizeof f - 1, T0));
  { uint8_t q7[6] = {7, 0, 0, 0, 0, 0}; frame(bad, SS_OK, VAL, q7, 1); assert(!sensors_decode(&d, bad, sizeof bad, T0)); }
  frame(bad, SS_UNREACHABLE, VAL, QUAL, 1);            /* values with a non-ok state: refused */
  assert(!sensors_decode(&d, bad, sizeof bad, T0));
  frame(bad, SS_SETUP, NULL, NULL, 0xFFFF);
  assert(sensors_decode(&d, bad, sizeof bad, T0) && d.state == SS_SETUP);

  /* ---- polling ---- */
  sensors_view v = {0};
  assert(!sensors_poll_due(&v, false, 0, T0));          /* never off-page */
  assert(sensors_poll_due(&v, true, T0, T0));           /* first open */
  v.attempts = 1; v.attempt_us = T0;
  assert(!sensors_poll_due(&v, true, T0 - 1, T0 + 1000000));
  assert(sensors_poll_due(&v, true, T0 - 1, T0 + SENSORS_POLL_US));
  v.fails = 1;
  assert(sensors_poll_due(&v, true, T0 - 1, T0 + SENSORS_RETRY_US));
  v.fails = 0; v.refresh = true;
  assert(!sensors_poll_due(&v, true, T0 - 1, T0 + 500000) && sensors_poll_due(&v, true, T0 - 1, T0 + SENSORS_REOPEN_US));
  v.refresh = false;
  assert(sensors_poll_due(&v, true, T0 + 1, T0 + SENSORS_REOPEN_US));   /* reopened after the last poll */

  /* ---- notes + value text ---- */
  memset(&v, 0, sizeof v);
  char t[16];
  assert(!strcmp(sensors_note(&v, T0), "Loading"));
  v.attempts = 1;
  assert(!strcmp(sensors_note(&v, T0), "Sensors unavailable"));
  v.http = 409; assert(!strcmp(sensors_note(&v, T0), "Sign in on Settings")); v.http = 200;
  frame(f, SS_OK, VAL, QUAL, 4); assert(sensors_decode(&v.data, f, sizeof f, T0));
  assert(!strcmp(sensors_note(&v, T0), ""));
  sensors_value_text(&v, 0, T0, t, sizeof t); assert(!strcmp(t, "23.4"));
  sensors_value_text(&v, 1, T0, t, sizeof t); assert(!strcmp(t, "64"));
  sensors_value_text(&v, 2, T0, t, sizeof t); assert(!strcmp(t, "1014"));   /* 1013.6 rounds */
  sensors_value_text(&v, 3, T0, t, sizeof t); assert(!strcmp(t, "3"));
  sensors_value_text(&v, 5, T0, t, sizeof t); assert(!strcmp(t, "--"));
  { sensors_view n = v; n.data.r[0].x10 = -35; sensors_value_text(&n, 0, T0, t, sizeof t); assert(!strcmp(t, "-3.5")); }
  /* Whole-number rows match the Pi's f"{v:.0f}" exactly: halves round to even. */
  { static const struct { int x10; const char *want; } half[] = {
      {745, "74"}, {755, "76"}, {9975, "998"}, {9965, "996"}, {753, "75"}, {757, "76"}, {5, "0"}, {15, "2"}, {-5, "0"}, {-15, "-2"}};
    for (unsigned k = 0; k < sizeof half / sizeof half[0]; k++) {
      sensors_view n = v; n.data.r[1].x10 = (int16_t)half[k].x10;
      sensors_value_text(&n, 1, T0, t, sizeof t);
      if (strcmp(t, half[k].want)) { fprintf(stderr, "x10=%d got %s want %s\n", half[k].x10, t, half[k].want); assert(0); }
    } }
  assert(sensors_row_quality(&v, 1, T0) == SQ_MEDIUM);
  int64_t late = T0 + SENSORS_FRESH_US + 1;               /* no new frame for 45 s: no numbers */
  sensors_value_text(&v, 0, late, t, sizeof t); assert(!strcmp(t, "--"));
  assert(!strcmp(sensors_note(&v, late), "No recent reading") && sensors_row_quality(&v, 0, late) == SQ_UNKNOWN);
  { sensors_view n = {0}; n.attempts = 1; frame(f, SS_SETUP, NULL, NULL, 0xFFFF); sensors_decode(&n.data, f, sizeof f, T0);
    assert(!strcmp(sensors_note(&n, T0), "Set up on the host")); }

  /* ---- palette: pinned absolute colours (RGB888 and the panel's RGB565) ---- */
  { sn_rgb c = sn_fill_rgb(SQ_GOOD); assert(c.r == 182 && c.g == 211 && c.b == 241); }   /* 182.5 -> 182, half-even */
  { sn_rgb c = sn_fill_rgb(SQ_MEDIUM); assert(c.r == 248 && c.g == 227 && c.b == 163); }
  { sn_rgb c = sn_fill_rgb(SQ_BAD); assert(c.r == 246 && c.g == 183 && c.b == 181); }
  { sn_rgb c = sn_fill_rgb(SQ_UNKNOWN); assert(c.r == 241 && c.g == 233 && c.b == 219); }
  { sn_rgb c = sn_label_rgb(SQ_GOOD); assert(c.r == 66 && c.g == 87 && c.b == 121); }
  { sn_rgb c = sn_label_rgb(SQ_UNKNOWN); assert(c.r == 98 && c.g == 96 && c.b == 117); }
  { sn_rgb c = sn_band_rgb(SQ_MEDIUM, SQ_GOOD); assert(c.r == 219 && c.g == 191 && c.b == 101); }
  for (int q = 0; q < 3; q++) {
    assert(sn_565(sn_fill_rgb((sensors_quality)q)) == FILL_565[q] && sn_565(sn_label_rgb((sensors_quality)q)) == LABEL_565[q]);
    for (int b = 0; b < 3; b++) assert(sn_565(sn_band_rgb((sensors_quality)b, (sensors_quality)q)) == BAND_565[b][q]);
  }
  assert(SN_PAGE == PAGE_565 && SN_INK == INK_565);
  /* the hues: GOOD blue (b > r), MEDIUM yellow (r, g > b), BAD red (r > g, b) */
  assert(B5(FILL_GOOD_565) > R5(FILL_GOOD_565) + 6);
  assert(R5(FILL_MEDIUM_565) > B5(FILL_MEDIUM_565) + 8 && G6(FILL_MEDIUM_565) / 2 > B5(FILL_MEDIUM_565) + 6);
  assert(R5(FILL_BAD_565) > G6(FILL_BAD_565) / 2 + 6 && R5(FILL_BAD_565) > B5(FILL_BAD_565) + 6);

  /* ---- words: every band edge (statuses as the bridge sends them) and both sides of the GOOD band ---- */
  { static const struct { int row, x10; const char *want; } w[] = {
      {0, -100, "Cold"}, {0, 149, "Cold"}, {0, 150, "Cool"}, {0, 179, "Cool"}, {0, 180, "Comfy"}, {0, 260, "Comfy"},
      {0, 261, "Warm"}, {0, 300, "Warm"}, {0, 301, "Hot"}, {0, 450, "Hot"},
      {1, 50, "Very dry"}, {1, 199, "Very dry"}, {1, 200, "Dry"}, {1, 299, "Dry"}, {1, 300, "Comfy"}, {1, 600, "Comfy"},
      {1, 601, "Damp"}, {1, 700, "Damp"}, {1, 701, "Humid"}, {1, 1000, "Humid"},
      {2, 9600, "Stormy"}, {2, 9849, "Stormy"}, {2, 9850, "Low"}, {2, 9999, "Low"}, {2, 10000, "Normal"}, {2, 10250, "Normal"},
      {2, 10251, "High"}, {2, 10350, "High"}, {2, 10351, "Very high"}, {2, 10600, "Very high"}};
    for (unsigned k = 0; k < sizeof w / sizeof w[0]; k++) {
      const char *got = sensors_reading_word(w[k].row, (sensors_quality)bridge_status(w[k].row, w[k].x10), w[k].x10);
      if (strcmp(got, w[k].want)) { fprintf(stderr, "row %d x10=%d: %s, want %s\n", w[k].row, w[k].x10, got, w[k].want); assert(0); }
    } }
  /* the side is the GOOD band's midpoint (22.0 C, 45 %, 1012.5 hPa): at it = the high word */
  assert(!strcmp(sensors_reading_word(0, SQ_MEDIUM, 219), "Cool") && !strcmp(sensors_reading_word(0, SQ_MEDIUM, 220), "Warm"));
  assert(!strcmp(sensors_reading_word(1, SQ_BAD, 449), "Very dry") && !strcmp(sensors_reading_word(1, SQ_BAD, 450), "Humid"));
  assert(!strcmp(sensors_reading_word(2, SQ_MEDIUM, 10124), "Low") && !strcmp(sensors_reading_word(2, SQ_MEDIUM, 10125), "High"));
  assert(!strcmp(sensors_reading_word(0, SQ_UNKNOWN, 220), "--") && !strcmp(sensors_reading_word(3, SQ_GOOD, 0), "--"));
  assert(!strcmp(sensors_air_word(SQ_GOOD), "Clean") && !strcmp(sensors_air_word(SQ_MEDIUM), "Okay") &&
         !strcmp(sensors_air_word(SQ_BAD), "Poor") && !strcmp(sensors_air_word(SQ_UNKNOWN), "--"));

  /* ---- tiles: Air = worst PM (PM2.5 wins ties, then PM10, then PM1) ---- */
  { sensors_view a = readings(220, 450, 10130, 30, 50, 100);          /* all GOOD: PM2.5 */
    sensors_tile at = sensors_tile_at(&a, 0, T0);
    assert(at.q == SQ_GOOD && at.row == SENSORS_PM25 && !strcmp(at.word, "Clean") && !strcmp(at.label, "Air"));
    assert(!strcmp(at.detail, "PM2.5 \x01 5"));
    a = readings(220, 450, 10130, 400, 120, 100);                      /* PM1 BAD beats PM2.5 MEDIUM */
    at = sensors_tile_at(&a, 0, T0);
    assert(at.q == SQ_BAD && at.row == SENSORS_PM1 && !strcmp(at.word, "Poor") && !strcmp(at.detail, "PM1 \x01 40"));
    a = readings(220, 450, 10130, 120, 200, 600);                      /* all MEDIUM: PM2.5 */
    at = sensors_tile_at(&a, 0, T0); assert(at.q == SQ_MEDIUM && at.row == SENSORS_PM25 && !strcmp(at.word, "Okay"));
    a = readings(220, 450, 10130, 120, 50, 600);                       /* PM10 and PM1 MEDIUM: PM10 */
    at = sensors_tile_at(&a, 0, T0); assert(at.row == SENSORS_PM10 && !strcmp(at.detail, "PM10 \x01 60"));
    a = readings(220, 450, 10130, 30, NONE, 100);                      /* PM2.5 unknown: PM10 (tie with PM1) */
    at = sensors_tile_at(&a, 0, T0); assert(at.q == SQ_GOOD && at.row == SENSORS_PM10);
    a = readings(220, 450, 10130, NONE, NONE, NONE);                   /* no PM verdict: neutral */
    at = sensors_tile_at(&a, 0, T0); assert(at.q == SQ_UNKNOWN && !strcmp(at.word, "--") && !at.detail[0]);
    sensors_tile tt = sensors_tile_at(&a, 1, T0), th = sensors_tile_at(&a, 2, T0), tp = sensors_tile_at(&a, 3, T0);
    assert(!strcmp(tt.label, "Temperature") && !strcmp(tt.word, "Comfy") && !strcmp(tt.detail, "22.0\x02" "C"));
    assert(!strcmp(th.label, "Humidity") && !strcmp(th.detail, "45%") && !strcmp(tp.label, "Pressure") && !strcmp(tp.detail, "1013 hPa"));
    /* stale and 409: no word, no number on any tile */
    a = readings(220, 450, 10130, 30, 50, 100);
    for (int i = 0; i < SENSORS_TILES; i++) {
      sensors_tile s1 = sensors_tile_at(&a, i, late);
      assert(s1.q == SQ_UNKNOWN && !strcmp(s1.word, "--") && !s1.detail[0]);
      a.http = 409; s1 = sensors_tile_at(&a, i, T0); a.http = 200;
      assert(s1.q == SQ_UNKNOWN && !strcmp(s1.word, "--") && !s1.detail[0]);
    } }

  /* ---- gauge: marker px on a 130 px bar (Pi: round((v - lo) / (hi - lo) * span), sqrt for PM) ---- */
  assert(SN_SPAN == 130);
  { static const struct { int row, x10, px; } g[] = {
      {0, 234, 73}, {0, 220, 65}, {0, 50, 0}, {0, 400, 130}, {1, 640, 100}, {1, 150, 9}, {2, 10136, 72}, {2, 10400, 121},
      {4, 120, 58}, {4, 400, 106}, {4, 90, 50}, {5, 540, 68}, {5, 1800, 123}, {3, 0, 0}, {3, 30, 29}};
    for (unsigned k = 0; k < sizeof g / sizeof g[0]; k++) {
      int got = sensors_gauge_px(g[k].row, g[k].x10, 130);
      if (got != g[k].px) { fprintf(stderr, "gauge row %d x10=%d: %d, want %d\n", g[k].row, g[k].x10, got, g[k].px); assert(0); }
    } }
  /* band edges (Temperature: 27 43 87 108; PM2.5: 50 100; PM10: 68 114) */
  assert(sensors_gauge_px(0, 150, 130) == 27 && sensors_gauge_px(0, 180, 130) == 43 && sensors_gauge_px(0, 260, 130) == 87 &&
         sensors_gauge_px(0, 300, 130) == 108 && sensors_gauge_px(4, 354, 130) == 100 && sensors_gauge_px(5, 1540, 130) == 114);

  /* ---- word fitting: never truncated, two scale-2 lines at most, inside the tile ---- */
  { static const char *const all[] = {"Cold", "Cool", "Comfy", "Warm", "Hot", "Very dry", "Dry", "Damp", "Humid", "Stormy",
                                      "Low", "Normal", "High", "Very high", "Clean", "Okay", "Poor", "--"};
    for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++) {
      sensors_word_fit w = sensors_fit_word(all[k], SN_SPAN);
      char joined[64];
      snprintf(joined, sizeof joined, "%s%s%s", w.line[0], w.lines > 1 ? " " : "", w.lines > 1 ? w.line[1] : "");
      assert(!strcmp(joined, all[k]) && w.scale == 2 && w.lines >= 1 && w.lines <= 2);
      for (int l = 0; l < w.lines; l++) assert((int)strlen(w.line[l]) * 9 * w.scale + 1 <= SN_SPAN);   /* +1 bold pass */
      assert((w.lines == 2) == (!strcmp(all[k], "Very dry") || !strcmp(all[k], "Very high")));
    }
    sensors_word_fit w = sensors_fit_word("Very high", SN_SPAN);
    assert(!strcmp(w.line[0], "Very") && !strcmp(w.line[1], "high"));
    w = sensors_fit_word("Unbreakables", SN_SPAN);              /* no space: scale 1, still whole */
    assert(w.lines == 1 && w.scale == 1 && !strcmp(w.line[0], "Unbreakables")); }
  /* labels and the widest numbers fit; the rows don't collide (label, word lines, number, marker) */
  assert((int)strlen("Temperature") * 9 <= SN_SPAN && (int)strlen("PM2.5 \x01 6554") * 9 <= SN_SPAN &&
         (int)strlen("-327.6\x02" "C") * 9 <= SN_SPAN && (int)strlen("3277 hPa") * 9 <= SN_SPAN);
  assert(SN_LABEL_Y + 17 <= SN_WORD2_Y && SN_WORD2_Y + SN_WORD_PITCH + 34 <= SN_DETAIL_Y && SN_WORD_Y + 34 <= SN_DETAIL_Y);
  assert(SN_WORD_PITCH >= 30);                                   /* line 1 descenders clear line 2 ascenders */
  assert(SN_DETAIL_Y + 17 <= SN_TILE_H - SN_BAR_BOTTOM - SN_BAR_H / 2 - (int)SN_DOT_R);
  /* the 2x2 grid is inside the safe area (also checked per pixel below) */
  for (int i = 0; i < SENSORS_TILES; i++) {
    int tx, ty; sensors_tile_box(i, &tx, &ty);
    assert(tx >= 16 && ty >= 16 && tx + SN_TILE_W <= 352 && ty + SN_TILE_H <= 420);
  }

  /* ---- render ---- */
  /* 1: all GOOD - four soft blue tiles, words Clean / Comfy / Comfy / Normal */
  sensors_view good = readings(234, 410, 10120, 30, 90, 100);
  render_page(&good, T0, NULL);
  save(dir, "1-all-good");
  assert_safe("all-good"); assert_in_tiles("all-good");
  assert(p[10 * 368 + 10] == PAGE_565);
  for (int i = 0; i < SENSORS_TILES; i++) assert(tile_fill(i) == FILL_GOOD_565);
  assert(!count_color(0, 0, 368, 448, FILL_MEDIUM_565) && !count_color(0, 0, 368, 448, FILL_BAD_565));
  assert(count_color(0, 0, 368, 448, LABEL_GOOD_565) > 400 && !count_color(0, 0, 368, 448, LABEL_BAD_565));
  for (int i = 0; i < SENSORS_TILES; i++) assert(ink_in_rows(i, SN_WORD_Y, SN_WORD_Y + 34) > 250);   /* the big word */
  { int above = 0;                                                     /* no title: only the tiles */
    for (int y = 0; y < SN_TILE_Y; y++) for (int x = 0; x < 368; x++) above += p[y * 368 + x] != PAGE_565;
    assert(above == 0); }
  /* markers: Temperature 23.4 at 73, Humidity 41 at round(31/70*130) = 58, Air PM2.5 9.0 at 50 */
  assert(marker_col(1) == 73 && marker_col(2) == 58 && marker_col(0) == 50);
  /* the Temperature bar's bands: BAD | MEDIUM | GOOD | MEDIUM | BAD, tinted for a GOOD tile */
  { int tx, ty; sensors_tile_box(1, &tx, &ty);
    int by = ty + SN_TILE_H - SN_BAR_BOTTOM - SN_BAR_H / 2, bx = tx + SN_PAD;
    assert(p[by * 368 + bx + 14] == BAND_565[SQ_BAD][SQ_GOOD] && p[by * 368 + bx + 35] == BAND_565[SQ_MEDIUM][SQ_GOOD]);
    assert(p[by * 368 + bx + 100] == BAND_565[SQ_MEDIUM][SQ_GOOD] && p[by * 368 + bx + 118] == BAND_565[SQ_BAD][SQ_GOOD]);
    assert(p[by * 368 + bx + 50] == BAND_565[SQ_GOOD][SQ_GOOD]); }

  /* 2: mixed, with the two-line words - Air Clean (GOOD), Temperature Cool (MEDIUM), Humidity Very dry
   * (BAD), Pressure Very high (BAD); each tile only in its own status colours */
  sensors_view mixed = readings(165, 150, 10400, 30, 50, 100);
  render_page(&mixed, T0, NULL);
  save(dir, "2-mixed-long-words");
  assert_safe("mixed"); assert_in_tiles("mixed");
  assert(tile_fill(0) == FILL_GOOD_565 && tile_fill(1) == FILL_MEDIUM_565 && tile_fill(2) == FILL_BAD_565 && tile_fill(3) == FILL_BAD_565);
  { static const int want[4] = {SQ_GOOD, SQ_MEDIUM, SQ_BAD, SQ_BAD};
    for (int i = 0; i < SENSORS_TILES; i++) {
      int tx, ty; sensors_tile_box(i, &tx, &ty);
      assert(count_color(tx, ty, tx + SN_TILE_W, ty + SN_TILE_H, LABEL_565[want[i]]) > 60);   /* label + number */
      for (int q = 0; q < 3; q++) {
        if (q == want[i]) continue;
        assert(!count_color(tx, ty, tx + SN_TILE_W, ty + SN_TILE_H, FILL_565[q]) && !count_color(tx, ty, tx + SN_TILE_W, ty + SN_TILE_H, LABEL_565[q]));
      }
    } }
  /* "Very" / "high": both word lines carry ink, the number keeps its row */
  assert(ink_in_rows(3, SN_WORD2_Y, SN_WORD2_Y + 30) > 150 && ink_in_rows(3, SN_WORD2_Y + SN_WORD_PITCH + 2, SN_WORD2_Y + SN_WORD_PITCH + 30) > 150);
  assert(ink_in_rows(2, SN_WORD2_Y, SN_WORD2_Y + 30) > 150 && ink_in_rows(2, SN_WORD2_Y + SN_WORD_PITCH + 2, SN_WORD2_Y + SN_WORD_PITCH + 30) > 100);
  { int tx, ty; sensors_tile_box(3, &tx, &ty);
    assert(count_color(tx, ty + SN_DETAIL_Y, tx + SN_TILE_W, ty + SN_DETAIL_Y + 17, LABEL_BAD_565) > 60); }
  assert(marker_col(3) == 121 && marker_col(2) == 9 && marker_col(1) == sensors_gauge_px(SENSORS_TEMP, 165, 130) &&
         marker_col(0) == sensors_gauge_px(SENSORS_PM25, 50, 130) && marker_col(0) == 38);   /* sqrt(5/60) * 130 = 37.5+ */
  { int tx, ty; sensors_tile_box(1, &tx, &ty);                       /* the number: label colour */
    assert(count_color(tx, ty + SN_DETAIL_Y, tx + SN_TILE_W, ty + SN_DETAIL_Y + 17, LABEL_MEDIUM_565) > 60); }

  /* 3: Air Poor from PM10, Temperature Hot, Humidity Humid, Pressure Stormy; markers clamp to the bar */
  sensors_view poor = readings(352, 780, 9700, 30, 50, 1800);
  render_page(&poor, T0, NULL);
  save(dir, "3-mixed-air-poor");
  assert_safe("poor"); assert_in_tiles("poor");
  for (int i = 0; i < SENSORS_TILES; i++) assert(tile_fill(i) == FILL_BAD_565);
  assert(marker_col(0) == 123 && marker_col(1) == 127 && marker_col(3) == 3);

  /* 4: stale (no frame for 45 s): four neutral cards with "--", no numbers, no markers, the note */
  render_page(&good, late, NULL);
  save(dir, "4-stale");
  assert_safe("stale"); assert_in_tiles("stale");
  for (int i = 0; i < SENSORS_TILES; i++) {
    assert(tile_fill(i) == CARD_565 && marker_col(i) < 0);
    int tx, ty; sensors_tile_box(i, &tx, &ty);
    assert(count_color(tx, ty + SN_DETAIL_Y, tx + SN_TILE_W, ty + SN_DETAIL_Y + 17, LABEL_NEUTRAL_565) == 0);   /* no number */
    assert(ink_in_rows(i, SN_WORD_Y, SN_WORD_Y + 34) > 20);                                                       /* "--" */
    int by = ty + SN_TILE_H - SN_BAR_BOTTOM - SN_BAR_H / 2;
    assert(count_color(tx + SN_PAD, by, tx + SN_TILE_W - SN_PAD, by + 1, RULE_565) >= 120);                    /* plain track */
    assert(!count_color(tx, by - 10, tx + SN_TILE_W, by + 10, INK_565));
  }
  for (int q = 0; q < 3; q++) assert(!count_color(0, 0, 368, 448, FILL_565[q]) && !count_color(0, 0, 368, 448, LABEL_565[q]));
  assert(count_color(0, SN_NOTE_Y, 368, SN_NOTE_Y + 17, LABEL_NEUTRAL_565) > 100);                             /* note line */
  /* unknown rows on a fresh frame: those tiles neutral, the rest coloured */
  sensors_view partial = readings(NONE, 450, NONE, NONE, NONE, NONE);
  render_page(&partial, T0, NULL);
  assert(tile_fill(0) == CARD_565 && tile_fill(1) == CARD_565 && tile_fill(2) == FILL_GOOD_565 && tile_fill(3) == CARD_565);
  assert_safe("partial"); assert_in_tiles("partial");

  /* 5: not signed in to the Home Assistant provider (its OWN sign-in view, home_ui.phone_ha -> treated as
   *    409): the note, neutral cards, no numbers anywhere */
  const phone_status out = {.valid = true, .state = PH_NONE, .flags = PHONE_FLAG_REQUIRED};
  const phone_status both = {.valid = true, .state = PH_AUTHORIZED, .flags = PHONE_FLAG_REQUIRED, .name = "sam"};
  render_page(&good, T0, &out);
  save(dir, "5-signed-out");
  assert_safe("signed-out"); assert_in_tiles("signed-out");
  for (int i = 0; i < SENSORS_TILES; i++) assert(tile_fill(i) == CARD_565 && marker_col(i) < 0);
  for (int q = 0; q < 3; q++) assert(!count_color(0, 0, 368, 448, FILL_565[q]) && !count_color(0, 0, 368, 448, LABEL_565[q]));
  { int tx, ty; sensors_tile_box(1, &tx, &ty);
    assert(count_color(tx, ty + SN_DETAIL_Y, tx + SN_TILE_W, ty + SN_DETAIL_Y + 17, LABEL_NEUTRAL_565) == 0); }
  { sensors_view n409 = good; n409.http = 409; static uint16_t a409[SPARKLES_PIXELS];
    memcpy(a409, p, sizeof p); render_page(&n409, T0, NULL); assert(!memcmp(a409, p, sizeof p)); }   /* = the bridge's 409 */
  render_page(&good, T0, &both);                                                    /* signed in to Home Assistant: numbers */
  assert(tile_fill(1) == FILL_GOOD_565);
  /* 5b: the bridge has no Home Assistant provider (404 on its sign-in status): no numbers, "Set up" note */
  { static home_ui u; memset(&u, 0, sizeof u); u.page = SENSORS; u.input.stamp_us = T0; u.sensors = good; u.phone_ha.absent = true;
    assert(home_render(&u, p, SPARKLES_PIXELS)); assert_safe("not-set-up"); assert_in_tiles("not-set-up");
    for (int i = 0; i < SENSORS_TILES; i++) assert(tile_fill(i) == CARD_565 && marker_col(i) < 0);
    assert(count_color(0, SN_NOTE_Y, 368, SN_NOTE_Y + 17, LABEL_NEUTRAL_565) > 100); }

  /* 6: no frame yet / bridge unreachable / Home Assistant offline: notes over neutral cards */
  { sensors_view n = {0};
    render_page(&n, T0, NULL); save(dir, "6-loading"); assert_safe("loading"); assert_in_tiles("loading");
    for (int i = 0; i < SENSORS_TILES; i++) assert(tile_fill(i) == CARD_565);
    n.attempts = 1; render_page(&n, T0, NULL); assert_safe("unavailable");
    frame(f, SS_UNREACHABLE, NULL, NULL, 0xFFFF); assert(sensors_decode(&n.data, f, sizeof f, T0));
    render_page(&n, T0, NULL); save(dir, "7-ha-offline"); assert_safe("offline"); assert_in_tiles("offline"); }

  /* ---- repaint only on change: data, a reading going stale, or the sign-in ---- */
  static home_ui s, b;
  memset(&s, 0, sizeof s); s.page = SENSORS; s.input.stamp_us = T0; s.sensors = good;
  b = s; assert(home_visual_equal(&s, &b));
  b.input.stamp_us = T0 + 5 * 1000000; assert(home_visual_equal(&s, &b));            /* still page: no repaint */
  b.input.stamp_us = late; assert(!home_visual_equal(&s, &b));                       /* went stale */
  s.input.stamp_us = late + 7 * 1000000; assert(home_visual_equal(&s, &b));          /* stale, still the same */
  b = s; b.sensors.data.r[0].x10++; assert(!home_visual_equal(&s, &b));
  b = s; b.sensors.http = 409; assert(!home_visual_equal(&s, &b));
  b = s; b.phone_ha.st = out; assert(!home_visual_equal(&s, &b));                    /* sign-out repaints */
  b = s; b.phone_ha.absent = true; assert(!home_visual_equal(&s, &b));               /* "not set up" repaints */
  b = s; b.phone.st = out; assert(home_visual_equal(&s, &b));                        /* the Hermes sign-in is not the Sensor's */

  /* ---- Home carousel: the Sensor tile and its own (Home Assistant) sign-in; Ask keeps the Hermes one ---- */
  /* No phone status yet = LOADING (grey with a spinner, "Checking sign-in", loading_state_test), and a tap
   * still opens the page; signed in to Home Assistant = the full tile. */
  memset(&s, 0, sizeof s); s.connected = true; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.tile = 2;
  assert(home_tile_state(&s, 1) == TILE_LOADING && home_tile_state(&s, 2) == TILE_LOADING);
  s.phone.st = both; assert(home_tile_state(&s, 1) == TILE_ON && home_tile_state(&s, 2) == TILE_LOADING);
  s.phone_ha.st = both;
  home_render(&s, p, SPARKLES_PIXELS);
  save(dir, "8-home-tile");
  assert(p[10 * 368 + 10] == tile_paper(2) && count_color(88, 108, 280, 300, HC_TEXT) > 1500);
  assert(!home_tile_disabled(&s, 1) && !home_tile_disabled(&s, 2));
  const phone_status hermes_only = {.valid = true, .state = PH_AUTHORIZED, .flags = PHONE_FLAG_REQUIRED, .name = "sam"};
  const phone_status refused = {.valid = true, .state = PH_REFUSED, .flags = PHONE_FLAG_REQUIRED, .name = "sam"};
  /* signed out of both: both tiles disabled, drawn grey with "Sign in"; a tap on Sensor opens the Home
   * Assistant QR (not the page, not the Hermes QR) */
  memset(&s, 0, sizeof s); s.phone.st = out; s.phone_ha.st = out; s.tile = 2; s.connected = true; s.pair.state = PAIR_ENROLLED_UNPAIRED;
  assert(home_tile_disabled(&s, 1) && home_tile_disabled(&s, 2) && !home_tile_disabled(&s, 0) && !home_tile_disabled(&s, 3));
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "9-home-tile-signed-out");
  assert(p[10 * 368 + 10] == HOME_TILE_OFF_PAPER && count_color(88, 108, 280, 300, HC_TEXT) == 0);
  assert(count_color(40, 388, 160, 418, HOME_TILE_OFF_INK) > 100);                 /* "Sign in" */
  home_tap(&s, 184, 200);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && s.phone_ha.showing && s.phone_ha.want_start);
  assert(!s.phone.showing && !s.phone.want_start);
  memset(&s, 0, sizeof s); s.connected = true; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.phone.st = out; s.phone_ha.st = out; s.tile = 1; home_tap(&s, 184, 200);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HERMES && s.phone.want_start && !s.phone_ha.want_start);   /* Ask: the Hermes QR */
  /* signed in to Hermes only: Ask on, Sensor still off; a tap on Sensor
   * opens the Home Assistant QR */
  memset(&s, 0, sizeof s); s.connected = true; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.phone.st = hermes_only; s.phone_ha.st = out; s.tile = 2;
  assert(!home_tile_disabled(&s, 1) && home_tile_disabled(&s, 2));
  home_tap(&s, 184, 200); assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && s.phone_ha.want_start && !s.phone.want_start);
  /* signed in, but not in the Home Assistant groups (refused there): Sensor off; a tap shows its status */
  memset(&s, 0, sizeof s); s.connected = true; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.phone.st = both; s.phone_ha.st = refused; s.tile = 2;
  assert(!home_tile_disabled(&s, 1) && home_tile_disabled(&s, 2));
  home_tap(&s, 184, 200); assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && !s.phone_ha.want_start && !s.phone.want_start);
  /* signed in to both: both on, taps open the pages, numbers shown */
  memset(&s, 0, sizeof s); s.connected = true; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.phone.st = both; s.phone_ha.st = both; s.tile = 2;
  assert(!home_tile_disabled(&s, 1) && !home_tile_disabled(&s, 2));
  home_render(&s, p, SPARKLES_PIXELS); assert(p[10 * 368 + 10] == tile_paper(2));
  b = s; b.phone_ha.st = out; assert(!home_visual_equal(&s, &b));                    /* sign-out repaints tiles */
  home_tap(&s, 184, 200); assert(s.page == SENSORS);
  s.input.stamp_us = T0; s.sensors = mixed; home_render(&s, p, SPARKLES_PIXELS);
  assert(tile_fill(3) == FILL_BAD_565);
  /* Settings while signed out: one sign-in target on each provider's own tab (its wording belongs to
   * the Settings tests) */
  memset(&s, 0, sizeof s); s.page = SETTINGS; s.settings_tab = SETTINGS_HERMES; s.connected = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED + 1; s.phone.st = out; s.phone_ha.st = both;
  settings_buttons sb = home_settings_buttons(&s, T0);
  assert(sb.count == 1 && sb.t[0].action == SA_SIGNIN && sb.t[0].primary);
  s.phone.st = both; sb = home_settings_buttons(&s, T0);
  assert(sb.count == 1 && sb.t[0].action == SA_SIGNOUT_ASK && !strcmp(sb.t[0].label, "sam"));
  s.settings_tab = SETTINGS_HOME_ASSISTANT; s.phone_ha.st = out; sb = home_settings_buttons(&s, T0);
  assert(sb.count == 1 && sb.t[0].action == SA_SIGNIN && sb.t[0].primary);
  s.phone_ha.st = both; sb = home_settings_buttons(&s, T0);
  assert(sb.count == 1 && sb.t[0].action == SA_SIGNOUT_ASK && !strcmp(sb.t[0].label, "sam"));
  puts("Sensor page: WHS1 fail-closed decode, visible-only polling, half-even value text, four reading tiles "
       "(pinned GOOD/MEDIUM/BAD tints, band words, Air worst-of with PM2.5 ties, linear + sqrt gauge markers), "
       "stale/unknown/signed-out -> neutral '--', safe area, text inside tiles, repaint on change, sign-in gate: PASS");
  return 0;
}
