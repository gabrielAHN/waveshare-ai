/* Kotaro is sleepy while loading (user, 2026-10-07: "on voice tile on loading the animation should be
 * sleepi"). Loading = still finding out (no Wi-Fi yet, checking sign-in, the host not answering yet):
 * on the Ask page and on the Home Ask tile he dozes in colour, eyes shut, a slow drowsy nod and Zz
 * drifting up; no spinner. Closed (disabled) stays the grey sleep; on stays the still plain Kotaro. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"

static uint16_t a[SPARKLES_PIXELS], b[SPARKLES_PIXELS];
#define BG 0x0841

static bot_pose pose(float t) {
  bot_pose p; memset(&p, 0, sizeof p);
  p.mood = BOT_LOADING; p.t = t; p.mood_t = t; p.u = 1.f; p.cx = 184; p.cy = 230; p.look = BOT_LOOK_PLAIN;
  return p;
}

/* The loading motion: eyes never open, Zz on, no spinner, a smooth drowsy nod (no jumps). */
static void loading_motion_is_sleepy(void) {
  float prev_hy = 0, prev_by = 0, lo = 1e9f, hi = -1e9f;
  for (int f = 0; f < 2000; f++) {
    float t = f * .05f;  /* 20 fps for 100 s */
    bot_pose p = pose(t);
    bot_motion m = bot_animate(&p);
    assert(m.eyes == BOT_EYE_ASLEEP || m.eyes == BOT_EYE_HALF);
    assert(m.sleep_z && !m.spinner && !m.cloud);
    assert(m.mouth == BOT_MOUTH_LINE);
    assert(m.ear_f > 0 && m.ear_n > 0);  /* relaxed ears */
    if (f) { assert(fabsf(m.hy - prev_hy) < .25f); assert(fabsf(m.by - prev_by) < .25f); }
    prev_hy = m.hy; prev_by = m.by;
    lo = fminf(lo, m.hy); hi = fmaxf(hi, m.hy);
  }
  assert(hi - lo >= .8f);  /* he really nods off */
}

/* The Home Ask tile: loading = sleepy and animated (two moments differ), in colour, inside its icon box;
 * on = still (time does not change it); closed = the grey sleep. */
static int draw_tile(uint16_t *p, int state, float t) {
  for (int i = 0; i < SPARKLES_PIXELS; i++) p[i] = BG;
  home_bots_tile(p, 0, state, t);
  int out = 0;
  for (int y = 0; y < 448; y++)
    for (int x = 0; x < 368; x++)
      if (p[y * 368 + x] != BG && (x < 88 || x >= 280 || y < 108 || y >= 300)) out++;
  return out;
}
static unsigned diff(void) { unsigned n = 0; for (int i = 0; i < SPARKLES_PIXELS; i++) n += a[i] != b[i]; return n; }
static unsigned warm(const uint16_t *p) {  /* apricot fur pixels (red clearly above blue) */
  unsigned n = 0;
  for (int i = 0; i < SPARKLES_PIXELS; i++) { uint16_t v = p[i]; if (v == BG) continue; if (((v >> 11) & 31) > (v & 31) + 6) n++; }
  return n;
}
static void home_tile_states(void) {
  for (float t = 0; t < 30; t += .37f) { assert(!draw_tile(a, HOME_BOT_TILE_LOADING, t)); }
  draw_tile(a, HOME_BOT_TILE_LOADING, 1.f); draw_tile(b, HOME_BOT_TILE_LOADING, 2.2f);
  assert(diff() > 20);                 /* loading animates */
  assert(warm(a) > 500);               /* in colour, not the grey sleep */
  draw_tile(a, HOME_BOT_TILE_ON, 1.f); draw_tile(b, HOME_BOT_TILE_ON, 9.f);
  assert(diff() == 0);                 /* on: the still plain Kotaro */
  draw_tile(a, HOME_BOT_TILE_OFF, 1.f);
  assert(warm(a) < 50);                /* closed: greys */
}

/* The Ask page while loading draws the sleepy Kotaro (no spinner ring above his ear). */
static void ask_page_loading(void) {
  helper_view h; memset(&h, 0, sizeof h);
  h.block = BR_LOADING;
  assert(helper_bot_mood(&h) == BOT_LOADING);
}

int main(void) {
  loading_motion_is_sleepy();
  home_tile_states();
  ask_page_loading();
  puts("kotaro_sleepy: loading = dozing (eyes shut, drowsy nod, Zz, no spinner) on the Ask page and the Home tile; on = still, closed = grey: PASS");
  return 0;
}
