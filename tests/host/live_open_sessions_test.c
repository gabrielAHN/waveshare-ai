/* Sparkles shows every open Hermes session (user, 2026-10-07: "A glow for every open session, brighter
 * and busier while one is working"). WLS4 = the session header (count = all open sessions, level, flags) +
 * count * (u64 id, u8 provider, u8 state; bit 0 = working). The ambient density follows the WORKING
 * sessions only (calm when none works); every open session gets a glow, idle ones dimmer and smaller. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"

static size_t wls4(unsigned char *w, unsigned count, unsigned level, unsigned flags, const uint8_t *state) {
  memcpy(w, "WLS4", 4); w[4] = (unsigned char)count; w[5] = 0; w[6] = (unsigned char)level; w[7] = (unsigned char)flags;
  for (unsigned i = 0; i < count; i++) {
    uint64_t id = 1000 + 7 * i;
    for (int j = 0; j < 8; j++) w[8 + i * 10 + j] = (unsigned char)(id >> (8 * j));
    w[16 + i * 10] = (unsigned char)(1 + i % 3); w[17 + i * 10] = state[i];
  }
  return 8 + 10 * count;
}

static void decode(void) {
  unsigned char w[LIVE_WIRE_MAX];
  live_state s;
  uint8_t st[4] = {0, 1, 0, 0};
  size_t n = wls4(w, 4, 3, LIVE_FLAG_MEASURED, st);
  assert(live_decode(&s, w, n, 100));
  assert(s.count == 4 && live_working(&s) == 1 && s.idle_count == 3 && s.level == 3);
  assert(s.idle[0] && !s.idle[1] && s.providers[1] == 2);
  assert(live_active(&s, 101));                       /* one working => active */
  uint8_t idle[4] = {0, 0, 0, 0};
  n = wls4(w, 4, 0, 0, idle);
  assert(live_decode(&s, w, n, 100) && s.count == 4 && live_working(&s) == 0);
  assert(live_fresh(&s, 101) && !live_active(&s, 101)); /* open but nobody working: calm */
  /* invalid: level without a working session, unknown state bits, bad provider, wrong length */
  n = wls4(w, 4, 2, 0, idle); assert(!live_decode(&s, w, n, 100));
  uint8_t bad[4] = {0, 2, 0, 0}; n = wls4(w, 4, 0, 0, bad); assert(!live_decode(&s, w, n, 100));
  n = wls4(w, 4, 0, 0, idle); w[16] = LIVE_PROVIDER_MAX + 1; assert(!live_decode(&s, w, n, 100));
  n = wls4(w, 4, 0, 0, idle); assert(!live_decode(&s, w, n - 1, 100));
  assert(live_decode(&s, (const unsigned char *)"WLS4\0\0\0\0", 8, 100) && s.count == 0);
  /* Only WLS4 is accepted; a WLS3 response cannot replace the snapshot. */
  unsigned char w3[8 + 9];
  memcpy(w3, "WLS3", 4); w3[4] = 1; w3[5] = 0; w3[6] = 2; w3[7] = 1;
  uint64_t id = 42; for (int j = 0; j < 8; j++) w3[8 + j] = (unsigned char)(id >> (8 * j)); w3[16] = 1;
  live_state keep = s;
  assert(!live_decode(&s, w3, sizeof w3, 100) && !memcmp(&s, &keep, sizeof s));
}

static home_ui board(const uint8_t *state, unsigned level) {
  static unsigned char w[LIVE_WIRE_MAX];
  home_ui s; memset(&s, 0, sizeof s);
  s.page = SPARKLES; s.live_now_us = 200; s.input.scene.time = 3.f;
  size_t n = wls4(w, 4, level, level ? LIVE_FLAG_MEASURED : 0, state);
  assert(live_decode(&s.live, w, n, 100));
  return s;
}
static uint16_t a[SPARKLES_PIXELS];
static unsigned energy(const home_ui *s) {  /* how much the glows change a mid-grey page */
  for (int i = 0; i < SPARKLES_PIXELS; i++) a[i] = 0x8410;
  home_live_overlay(s, a);
  unsigned e = 0;
  for (int i = 0; i < SPARKLES_PIXELS; i++) if (a[i] != 0x8410) e++;
  return e;
}

static void glows(void) {
  uint8_t idle[4] = {0, 0, 0, 0}, one[4] = {0, 1, 0, 0}, all[4] = {1, 1, 1, 1};
  home_ui i = board(idle, 0), o = board(one, 3), w = board(all, 3);
  /* every open session glows, even when none is working */
  assert(home_live_points(&i) == 4 && energy(&i) > 0);
  /* the ambient field is calm with nobody working, and busier when someone works */
  assert(home_live_density(&i) == 0 && home_live_density(&o) == 4);
  /* working glows are brighter and bigger than idle ones */
  assert(energy(&o) > energy(&i) && energy(&w) > energy(&o));
  /* idle glows still drift (the page repaints), session toggle OFF hides them all */
  home_ui i2 = i; i2.input.scene.time += .5f; assert(!home_visual_equal(&i, &i2));
  i.session_off = true; assert(home_live_points(&i) == 0 && energy(&i) == 0);
}

int main(void) {
  decode();
  glows();
  puts("live_open_sessions: WLS4 lists every open session with its working bit; density follows the working ones; every open session glows, working ones brighter: PASS");
  return 0;
}
