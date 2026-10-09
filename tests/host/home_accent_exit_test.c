/* Accent Home exit through the real touch and compositor path.
 * Slices are added in the normative RED -> GREEN order. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"

#define N SPARKLES_PIXELS
static uint16_t frame[N], frame2[N], frame3[N], frame4[N], frame5[N];
static uint16_t home[N], outgoing[N], poison1[N], poison2[N], last1[N], last2[N], homebuf[N];
static int64_t now_us = 1000000;

static home_ui ready(home_page page) {
  home_ui s;
  memset(&s, 0, sizeof s);
  s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED;
  s.pair.live_http = 200;
  s.pair.live_ok = true;
  s.phone.st.valid = true;
  s.phone.st.state = PH_AUTHORIZED;
  s.phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s.phone_ha.st = s.phone.st;
#endif
  s.page = page;
  s.tile = 1;
  s.theme_mode = THEME_LIGHT;
  s.accent = 3;
  return s;
}

static void sample(home_ui *s, int dt_us, bool down, int x, int y) {
  now_us += dt_us;
  home_sample(s, now_us, down, x, y);
}

/* Deliberately independent of the renderer: pixels well away from the circle edge are wholly Home
 * or wholly outgoing. The AA-edge suite owns the omitted boundary band. */
static float shape_level(const home_blob *b, int x, int y) {
  float ux = fabsf(((float)x + .5f - b->cx) / (b->w * .5f));
  float uy = fabsf(((float)y + .5f - b->cy) / (b->h * .5f));
  return powf(ux, b->n) + powf(uy, b->n);
}

/* Slice 1: a centered circle reveals actual Home over the real outgoing page. */
static void immediate_flat_accent(void) {
  home_ui s = ready(SETTINGS);
  home_ui page = s;
  assert(home_render(&page, outgoing, N));

  sample(&s, 10000, true, 150, 440);
  sample(&s, 10000, true, 150, 428);
  assert(s.edge && s.slide_kind == HOME_MOTION_DRAG && s.page == SETTINGS);
  sample(&s,10000,true,150,350);

  home_ui h = s;
  h.page = HOME;
  h.slide_kind = HOME_MOTION_NONE;
  assert(home_render(&h, home, N));
  for (int i = 0; i < N; i++) {
    poison1[i] = (uint16_t)(0xA55Au ^ (unsigned)i * 17u);
    poison2[i] = (uint16_t)(0x5AA5u ^ (unsigned)i * 29u);
    last1[i] = (uint16_t)(0x1357u ^ (unsigned)i * 7u);
    last2[i] = (uint16_t)(0xECA8u ^ (unsigned)i * 11u);
  }
  home_snapshot null_outgoing = {.px = NULL, .home = homebuf};
  assert(home_compose(&s, frame, N, &null_outgoing, outgoing, SETTINGS));

  int interior = 0, exterior = 0;
  for (int y = 0; y < 448; y++) for (int x = 0; x < 368; x++) {
    float q = shape_level(&s.blob, x, y);
    uint16_t got = frame[y * 368 + x];
    if (q <= .75f) { assert(got == home[y * 368 + x]); interior++; }
    if (q >= 1.25f) { assert(got == outgoing[y * 368 + x]); exterior++; }
  }
  assert(interior > 1000 && exterior > 1000);
  assert(frame[224 * 368 + 184] != outgoing[224 * 368 + 184]);

  home_snapshot poisoned_a = {.px = poison1, .motion = s.motion_id, .page = s.slide_page, .valid = false,
                              .home = homebuf, .home_motion = s.motion_id,
                              .home_key = home_cache_key(&s), .home_valid = true};
  memcpy(homebuf, home, sizeof home);
  assert(home_compose(&s, frame2, N, &poisoned_a, outgoing, SETTINGS));
  home_snapshot poisoned_b = {.px = poison2, .motion = s.motion_id, .page = s.slide_page, .valid = false};
  assert(home_compose(&s, frame3, N, &poisoned_b, outgoing, SETTINGS));
  home_snapshot no_home_cache = {.px = NULL, .home = NULL};
  assert(home_compose(&s, frame4, N, &no_home_cache, outgoing, SETTINGS));
  assert(home_compose(&s, frame5, N, NULL, outgoing, SETTINGS));
  assert(!memcmp(frame, frame2, sizeof frame));
  assert(!memcmp(frame, frame3, sizeof frame));
  assert(!memcmp(frame, frame4, sizeof frame));
  assert(!memcmp(frame, frame5, sizeof frame));
  puts("slice 1: centered circular aperture reveals Home over actual outgoing source");
}

static void poison_sources(const home_ui *s, home_snapshot *snap, unsigned salt) {
  for (int i = 0; i < N; i++) {
    poison1[i] = (uint16_t)(0xA55Au ^ (unsigned)i * (17u + salt));
    last1[i] = (uint16_t)(0x1357u ^ (unsigned)i * (7u + salt));
  }
  *snap = (home_snapshot){.px = poison1, .motion = s->motion_id, .page = s->slide_page, .valid = false};
}

static void render_home_ref(const home_ui *s) {
  home_ui h = *s;
  h.page = HOME;
  h.slide_kind = HOME_MOTION_NONE;
  assert(home_render(&h, home, N));
}

/* Slice 2: held geometry is a pure function of upward travel: a fixed-center circle expands to cover,
 * reverses exactly, ignores horizontal motion, and never changes under a stationary hold. */
static void held_geometry(void) {
  const int ax = 138, ay = 440;
  const int ys[] = {428, 400, 360, 320, 280, 240, 224};
  home_ui s = ready(SETTINGS);
  sample(&s, 10000, true, ax, ay);
  float prev_w = 0.f, prev_h = 0.f, prev_area = 0.f;
  int changed_mid = 0;
  for (size_t k = 0; k < sizeof ys / sizeof ys[0]; k++) {
    int y = ys[k], dx = (184 - ax) * (ay - y) / (ay - HOME_PULL_CENTER_Y);
    sample(&s, 10000, true, ax + dx, y);
    assert(s.edge && s.slide_kind == HOME_MOTION_DRAG && s.page == SETTINGS);
    assert(s.blob.w >= prev_w && s.blob.h >= prev_h);
    float a = s.blob.w * s.blob.h;
    assert(a >= prev_area);
    if (y > HOME_PULL_CENTER_Y) {
      assert(s.blob.w >= 0 && s.blob.h >= 0);
      assert(s.blob.cx == 184.f && s.blob.cy == 224.f && s.blob.w == s.blob.h && s.blob.n == 2.f);
    } else {
      assert(s.blob.w == HOME_REVEAL_DIAMETER && s.blob.h == HOME_REVEAL_DIAMETER);
      assert(s.blob.cx == 184.f && s.blob.cy == 224.f);
    }
    render_home_ref(&s);
    home_snapshot poisoned;
    poison_sources(&s, &poisoned, (unsigned)k);
    assert(home_compose(&s, frame, N, &poisoned, last1, SETTINGS));
    if (y == HOME_PULL_CENTER_Y) assert(!memcmp(frame, home, sizeof frame));
    else assert(memcmp(frame, home, sizeof frame));
    if (y == 320) {
      uint16_t px = frame[224 * 368 + 184];
      assert(px == home[224 * 368 + 184] && frame[0] != home[0]);
      changed_mid = 1;
    }
    prev_w = s.blob.w; prev_h = s.blob.h; prev_area = a;
  }
  assert(changed_mid && s.blob.w == HOME_REVEAL_DIAMETER && s.blob.h == HOME_REVEAL_DIAMETER);

  home_ui before_hold = s;
  sample(&s, 90000, true, 184, HOME_PULL_CENTER_Y);
  assert(home_visual_equal(&before_hold, &s));
  assert(!memcmp(&before_hold.blob, &s.blob, sizeof s.blob));

  sample(&s, 10000, true, ax + 24, 320);
  home_blob reversed = s.blob;
  assert(reversed.w > 0 && reversed.h > 0);
  home_ui same = ready(SETTINGS);
  sample(&same, 10000, true, ax, ay);
  sample(&same, 140000, true, ax + 24, 320);
  assert(!memcmp(&reversed, &same.blob, sizeof reversed));
  home_ui still = same;
  sample(&same, 120000, true, ax + 24, 320);
  assert(home_visual_equal(&still, &same) && !memcmp(&still.blob, &same.blob, sizeof same.blob));

  sample(&s, 10000, true, 184, HOME_PULL_CENTER_Y);
  sample(&s, 10000, false, 0, 0);
  assert(s.page == HOME);
  render_home_ref(&s);
  for (int ms = 0; ms <= 240; ms += 20) {
    if (ms) sample(&s, 20000, false, 0, 0);
    home_snapshot poisoned;
    poison_sources(&s, &poisoned, (unsigned)(20 + ms));
    assert(home_compose(&s, frame, N, &poisoned, last1, SETTINGS));
    assert(!memcmp(frame, home, sizeof frame));
  }
  puts("slice 2: bottom-to-centre circular expansion, exact reversal and full corner cover");
}

static void stationary_all_pages(void) {
  const home_page pages[] = {SPARKLES, HELPER, SENSORS, SETTINGS};
  for (size_t k = 0; k < sizeof pages / sizeof pages[0]; k++) {
    home_ui s = ready(pages[k]);
    sample(&s, 10000, true, 146, 440);
    sample(&s, 10000, true, 150, 408);
    assert(s.edge && s.slide_kind == HOME_MOTION_DRAG && s.page == pages[k]);
    home_snapshot snap;
    poison_sources(&s, &snap, (unsigned)(40 + k));
    snap.home = homebuf;
    assert(home_compose(&s, frame, N, &snap, last1, pages[k]));
    home_ui held = s;
    sample(&s, 140000, true, 150, 408);
    assert(!memcmp(&held.blob, &s.blob, sizeof s.blob));
    assert(home_visual_equal(&held, &s));
    assert(home_compose(&s, frame2, N, &snap, last2, pages[k]));
    assert(!memcmp(frame, frame2, sizeof frame));
  }
  puts("slice 2 stationary: all outgoing pages keep identical geometry/frame and request no repaint");
}

static void release_settles(void) {
  const int ax = 152, ay = 440;
  home_ui flick = ready(HELPER);
  assert(home_render(&flick,outgoing,N));
  sample(&flick, 10000, true, ax, ay);
  sample(&flick, 10000, true, ax, ay - 12);
  sample(&flick, 10000, true, ax, ay - 24);
  home_blob held = flick.blob;
  home_snapshot snap;
  poison_sources(&flick, &snap, 70);
  snap.home = homebuf;
  assert(home_compose(&flick, frame, N, &snap, outgoing, HELPER));
  memcpy(frame5, frame, sizeof frame);
  sample(&flick, 10000, false, 0, 0);
  assert(flick.page == HOME && flick.slide_kind == HOME_MOTION_OUT);
  assert(flick.note.home && flick.note.offset == 24 && flick.note.speed_milli >= 500);
  assert(!memcmp(&held, &flick.blob, sizeof held));
  assert(home_compose(&flick, frame, N, &snap, outgoing, HELPER));
  assert(!memcmp(frame, frame5, sizeof frame));
  float prev_w = flick.blob.w, prev_h = flick.blob.h;
  int elapsed = 0;
  while (flick.slide_kind == HOME_MOTION_OUT && elapsed <= 250) {
    sample(&flick, 10000, false, 0, 0);
    elapsed += 10;
    if (flick.slide_kind == HOME_MOTION_OUT) {
      assert(flick.blob.w >= prev_w && flick.blob.h >= prev_h);
      prev_w = flick.blob.w; prev_h = flick.blob.h;
      poison_sources(&flick, &snap, (unsigned)(80 + elapsed));
      snap.home = homebuf;
      assert(home_compose(&flick, frame, N, &snap, outgoing, HELPER));
    }
  }
  assert(flick.slide_kind == HOME_MOTION_NONE && elapsed <= 240);
  assert(flick.note.anim_ms <= 240 && flick.note.done && flick.note.done_home);
  render_home_ref(&flick);
  assert(home_compose(&flick, frame, N, NULL, last2, HELPER));
  assert(!memcmp(frame, home, sizeof frame));

  home_ui cancel = ready(SETTINGS);
  assert(home_render(&cancel, outgoing, N));
  sample(&cancel, 10000, true, ax, ay);
  sample(&cancel, 100000, true, ax, ay - 12);
  sample(&cancel, 100000, true, ax, ay - 20);
  held = cancel.blob;
  sample(&cancel, 10000, false, 0, 0);
  assert(cancel.page == SETTINGS && cancel.slide_kind == HOME_MOTION_BACK);
  assert(!cancel.note.home && cancel.note.offset == 20 && cancel.note.anim_ms <= 220);
  assert(!memcmp(&held, &cancel.blob, sizeof held));
  render_home_ref(&cancel);
  prev_w = cancel.blob.w; prev_h = cancel.blob.h; elapsed = 0;
  bool cancel_reversed = false;
  while (cancel.slide_kind == HOME_MOTION_BACK && elapsed <= 230) {
    poison_sources(&cancel, &snap, (unsigned)(120 + elapsed));
    snap.home = homebuf;
    assert(home_compose(&cancel, frame, N, &snap, outgoing, SETTINGS));
    assert(frame[0] == outgoing[0]);
    sample(&cancel, 10000, false, 0, 0);
    elapsed += 10;
    if (cancel.slide_kind == HOME_MOTION_BACK) {
      if (cancel.blob.w <= prev_w && cancel.blob.h <= prev_h) cancel_reversed = true;
      if (cancel_reversed) assert(cancel.blob.w <= prev_w+.01f && cancel.blob.h <= prev_h+.01f);
      assert(cancel.blob.w >= 0.f && cancel.blob.w <= HOME_REVEAL_DIAMETER);
      assert(cancel.blob.h >= 0.f && cancel.blob.h <= HOME_REVEAL_DIAMETER);
      prev_w = cancel.blob.w; prev_h = cancel.blob.h;
    }
  }
  assert(cancel_reversed);
  assert(cancel.slide_kind == HOME_MOTION_NONE && elapsed <= 220);
  assert(cancel.page == SETTINGS && cancel.note.done && !cancel.note.done_home);
  assert(home_compose(&cancel, frame, N, NULL, last2, SETTINGS));
  assert(!memcmp(frame, outgoing, sizeof frame));
  puts("slice 3: partial flick completes continuously <=240 ms; cancel restores exact page <=220 ms");
}

static void themes_and_cache(void) {
  for (int mode = THEME_LIGHT; mode <= THEME_DARK; mode++) for (int accent_index = 0; accent_index < THEME_ACCENTS; accent_index++) {
    home_ui s = ready(SENSORS);
    s.theme_mode = (uint8_t)mode;
    s.accent = (uint8_t)accent_index;
    sample(&s, 10000, true, 142, 440);
    sample(&s, 10000, true, 146, 408);
    assert(s.edge && s.slide_kind == HOME_MOTION_DRAG);
    home_ui outgoing_state=s;outgoing_state.slide_kind=HOME_MOTION_NONE;
    assert(home_render(&outgoing_state,outgoing,N));
    home_snapshot snap;
    poison_sources(&s, &snap, (unsigned)(200 + mode * 10 + accent_index));
    snap.home = homebuf;
    assert(home_compose(&s, frame, N, &snap, outgoing, SENSORS));
    render_home_ref(&s);
    assert(snap.home_valid && snap.home_key == home_cache_key(&s));
    assert(!memcmp(homebuf, home, sizeof home));
    int interior = 0, exterior = 0;
    for (int y = 0; y < 448; y++) for (int x = 0; x < 368; x++) {
      float q = shape_level(&s.blob, x, y);
      if (q <= .75f) {assert(frame[y*368+x]==home[y*368+x]);interior++;}
      if (q >= 1.25f) {exterior++;}
    }
    assert(interior > 10 && exterior > 1000);

    homebuf[0] = 0x1234;
    assert(home_compose(&s, frame2, N, &snap, outgoing, SENSORS));
    assert(homebuf[0] == 0x1234);                /* same motion/theme/tile reuses cached real Home */

    home_ui foreign = s;
    foreign.theme_mode = (uint8_t)(mode == THEME_LIGHT ? THEME_DARK : THEME_LIGHT);
    foreign.accent = (uint8_t)((accent_index + 2) % THEME_ACCENTS);
    assert(home_render(&foreign, frame4, N));    /* another render leaves a foreign current theme */
    assert(theme_mode_now() == foreign.theme_mode && theme_accent_now() == foreign.accent);
    assert(home_compose(&s, frame4, N, &snap, outgoing, SENSORS));
    assert(!memcmp(frame2, frame4, sizeof frame2));
    assert(theme_mode_now() == s.theme_mode && theme_accent_now() == s.accent);

    home_ui before = s;
    s.theme_mode = (uint8_t)(mode == THEME_LIGHT ? THEME_DARK : THEME_LIGHT);
    s.accent = (uint8_t)((accent_index + 1) % THEME_ACCENTS);
    assert(!home_visual_equal(&before, &s));
    render_home_ref(&s);
    assert(home_compose(&s, frame3, N, &snap, outgoing, SENSORS));
    assert(snap.home_key == home_cache_key(&s) && homebuf[0] != 0x1234);
    assert(!memcmp(homebuf, home, sizeof home));
    for (int y = 0; y < 448; y++) for (int x = 0; x < 368; x++) {
      float q = shape_level(&s.blob, x, y);
      if (q <= .75f) assert(frame3[y * 368 + x] == home[y * 368 + x]);
    }
  }
  puts("slice 4 preservation: 2 modes x 5 accents cache/reuse/invalidation and held theme changes");
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "stationary")) {
    stationary_all_pages();
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "geometry")) {
    held_geometry();
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "settle")) {
    release_settles();
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "theme")) {
    themes_and_cache();
    return 0;
  }
  immediate_flat_accent();
  stationary_all_pages();
  held_geometry();
  release_settles();
  themes_and_cache();
  puts("home accent exit: slices 1-4 PASS");
  return 0;
}
