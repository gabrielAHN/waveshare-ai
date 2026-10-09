#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "orbs.h"
#include "sparkles.h"

static void mean_rgb(const uint16_t *p, double out[3]) {
  double r = 0, g = 0, b = 0;
  for (int i = 0; i < ORBS_PIXELS; i++) {
    r += (p[i] >> 11) * 255.0 / 31;
    g += ((p[i] >> 5) & 63) * 255.0 / 63;
    b += (p[i] & 31) * 255.0 / 31;
  }
  out[0] = r / ORBS_PIXELS;
  out[1] = g / ORBS_PIXELS;
  out[2] = b / ORBS_PIXELS;
}

static double radius_sum(const orbs_motion *m) {
  float x[ORBS_MAX], y[ORBS_MAX], r[ORBS_MAX];
  int n = orbs_layout(m, x, y, r);
  double s = 0;
  for (int i = 0; i < n; i++) s += r[i];
  return s;
}

static void advance(orbs_motion *m, float seconds, orbs_mode mode, float level) {
  for (int i = 0; i < (int)(seconds * 100); i++) orbs_advance(m, 0.01f, mode, level);
}

int main(void) {
  static uint16_t guard[ORBS_PIXELS + 2], again[ORBS_PIXELS], spark[SPARKLES_PIXELS + 2];
  uint16_t *frame = guard + 1;
  orbs_motion m = {0};

  /* 4-7 orbs in every mode, all on screen-ish, positive radii. */
  for (int mode = ORBS_IDLE; mode <= ORBS_DONE; mode++) {
    orbs_motion t = {0};
    advance(&t, 3, (orbs_mode)mode, .7f);
    float x[ORBS_MAX], y[ORBS_MAX], r[ORBS_MAX];
    int n = orbs_layout(&t, x, y, r);
    assert(n >= 4 && n <= 7);
    for (int i = 0; i < n; i++) assert(r[i] > 10 && r[i] < 140 && x[i] > -60 && x[i] < 428 && y[i] > -60 && y[i] < 508);
  }

  /* Bounded cost: low-res field only, every output pixel written once, guards intact. */
  guard[0] = 0x1234;
  guard[ORBS_PIXELS + 1] = 0x4321;
  memset(frame, 0xA5, ORBS_PIXELS * 2);
  orbs_render(&m, frame);
  assert(guard[0] == 0x1234 && guard[ORBS_PIXELS + 1] == 0x4321);
  assert(orbs_field_samples == ORBS_GW * ORBS_GH);
  assert(orbs_field_evals <= ORBS_GW * ORBS_GH * ORBS_MAX && ORBS_GW * ORBS_GH * 60 <= ORBS_PIXELS);  /* field <= 1/60 of pixels (8 px grid: the Ask page's hot spot) */
  memset(again, 0x5A, sizeof again);
  orbs_render(&m, again);
  assert(!memcmp(frame, again, sizeof again));  /* deterministic; no sentinel survived either fill */

  /* Palette clearly distinct from Sparkles' pale blue/ivory: dark violet base, warm glow. */
  double orb[3], sp[3];
  mean_rgb(frame, orb);
  sparkles_state s = {.time = 2, .usage = SP_USAGE_DEFAULT};
  assert(sparkles_render_direct(&s, spark + 1, SPARKLES_PIXELS));
  mean_rgb(spark + 1, sp);
  printf("mean rgb orbs=(%.0f,%.0f,%.0f) sparkles=(%.0f,%.0f,%.0f)\n", orb[0], orb[1], orb[2], sp[0], sp[1], sp[2]);
  assert(sp[2] >= sp[0]);                              /* sparkles: blue >= red */
  assert(orb[0] > orb[2] + 25 && orb[1] > orb[2] + 15);  /* orbs: warm white + yellow (R and G lead, blue lowest) */
  /* No purple anywhere: every pixel has R >= B and G >= B - 8 (violet/magenta would break it). */
  for (int i = 0; i < ORBS_PIXELS; i++) {
    int r = (frame[i] >> 11) * 8, g = ((frame[i] >> 5) & 63) * 4, b = (frame[i] & 31) * 8;
    assert(r >= b && g + 8 >= b);
  }
  /* The corner (between orbs) is near-white paper; the most saturated pixel is golden yellow. */
  uint16_t corner = frame[0];
  assert((corner >> 11) * 8 >= 240 && ((corner >> 5) & 63) * 4 >= 228 && (corner & 31) * 8 >= 216);
  int sat = -1, sr = 0, sg = 0, sb = 0;
  for (int i = 0; i < ORBS_PIXELS; i++) {
    int r = (frame[i] >> 11) * 8, g = ((frame[i] >> 5) & 63) * 4, b = (frame[i] & 31) * 8;
    if (r - b > sat) { sat = r - b; sr = r; sg = g; sb = b; }
  }
  printf("most saturated rgb=(%d,%d,%d)\n", sr, sg, sb);
  assert(sr >= 232 && sg >= 176 && sb <= 64);  /* yellow core: green channel high (orange would be ~120) */

  /* Listening swells with mic level; running orbits faster and brighter; done calms. */
  orbs_motion quiet = {0}, loud = {0};
  advance(&quiet, 1, ORBS_LISTEN, 0);
  advance(&loud, 1, ORBS_LISTEN, 1);
  assert(radius_sum(&loud) > radius_sum(&quiet) * 1.15);
  orbs_motion idle = {0}, run = {0};
  advance(&idle, 2, ORBS_IDLE, 0);
  advance(&run, 2, ORBS_RUN, 0);
  assert(run.phase > idle.phase * 2 && run.energy > idle.energy);
  double ri[3], rr[3];
  orbs_render(&idle, frame);
  mean_rgb(frame, ri);
  orbs_render(&run, frame);
  mean_rgb(frame, rr);
  assert(rr[0] + rr[1] - 2 * rr[2] > ri[0] + ri[1] - 2 * ri[2]);  /* running: more yellow glow on the page */
  advance(&run, 4, ORBS_DONE, 0);
  assert(run.speed < 1.0f && run.energy < 1.0f);
  /* dt is wall-clock: one big step equals many small steps closely (no per-frame clamp). */
  orbs_motion a = {0}, b = {0};
  advance(&a, 1, ORBS_IDLE, 0);
  orbs_advance(&b, 1.0f, ORBS_IDLE, 0);
  assert(fabsf(a.phase - b.phase) < 0.05f);

  /* Host cost bound (the device is measured separately). */
  clock_t t0 = clock();
  for (int i = 0; i < 20; i++) {
    orbs_advance(&m, 0.05f, ORBS_RUN, 0.5f);
    orbs_render(&m, frame);
  }
  double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 20;
  printf("host orbs_render %.2f ms/frame, field %dx%d\n", ms, ORBS_GW, ORBS_GH);
  assert(ms < 25);
  puts("orbs: 4-7 metaballs, bounded low-res field, guards, white paper + yellow glow palette, no purple, level swell, run/done motion: PASS");
}
