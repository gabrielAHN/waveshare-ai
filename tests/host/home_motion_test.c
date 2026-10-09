/* Motion (SPEC3 Contract G, on top of SPEC2 Contract M's slides) through the real touch path
 * (home_sample) and the real renderer. The Home gesture itself (bottom-edge UP toward centre) is
 * tests/host/home_pull_test.c (touch,
 * thresholds, conflicts, linked geometry, notes, WGS1 3) and home_blob_test.c (pixels); here:
 *  - Opening a tile: the page slides up over Home (ease-out, 220 ms); a touch ends a slide at once.
 *  - Bottom rows: the reserved 28 px strip takes no page tap; Home's own carousel remains unchanged.
 *  - Ask: the pull DOWN from the top band (y < 72, released at y >= 190) starts a new chat, as does
 *    the pull-up from the newest message; USB WGS1 15 replays the top pull.
 *  - Home carousel: 1:1 drag, 0.35x rubber band past the ends, flick (>= 0.35 px/ms over the last 80 ms
 *    and >= 24 px) = one tile, else the nearest tile; the settle lasts 120..300 ms by distance and
 *    starts at the finger's speed; neighbour art moves 0.8x of its paper; translation only.
 *  - Every animation is a function of elapsed time (irregular sample gaps give the same picture).
 *  - Compositor (the tile open), repaint only while moving, log notes; WGS1 3/1/15/5 replays.
 * Literal numbers pin the contract. `home_motion_test <section>` runs one section. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_render.h"
#include "gesture_replay.h"

#define N SPARKLES_PIXELS
static int64_t t = 1000000;
static uint16_t f0[N], f1[N], f2[N];
static void at(home_ui *s, int64_t dt_us, bool down, int x, int y) { t += dt_us; home_sample(s, t, down, x, y); }
static void sample(home_ui *s, bool down, int x, int y) { at(s, 10000, down, x, y); }
static void lift(home_ui *s) { sample(s, false, 0, 0); }  /* the panel reports no point on release */
static void idle(home_ui *s, int ms) { for (int i = 0; i < ms / 10; i++) lift(s); }
static void tap(home_ui *s, int x, int y) { sample(s, true, x, y); sample(s, true, x, y); lift(s); }
/* Finger: n equal steps of step_us from (x0,y0) to (x1,y1), `hold` still samples, then the lift. */
static void hold_path(home_ui *s, int x0, int y0, int x1, int y1, int n, int step_us, int hold) {
  for (int k = 0; k <= n; k++) at(s, k ? step_us : 10000, true, x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n);
  for (int k = 0; k < hold; k++) sample(s, true, x1, y1);
}
static void path(home_ui *s, int x0, int y0, int x1, int y1, int n, int step_us, int hold) {
  hold_path(s, x0, y0, x1, y1, n, step_us, hold);
  lift(s);
}
/* On Wi-Fi, paired, signed in to both providers: every tile is ON and opens its page. */
static home_ui ready(home_page page) {
  home_ui s;
  memset(&s, 0, sizeof s);
  s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; s.pair.live_ok = true;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED; s.phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s.phone_ha.st = s.phone.st;
#endif
  s.bots.data = (bots_data){.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", "atlas", "coding"};
  for (int i = 0; i < 3; i++) { strcpy(s.bots.data.b[i].id, ids[i]); strcpy(s.bots.data.b[i].name, ids[i]); s.bots.data.b[i].available = true; s.bots.data.b[i].reset_s = BOTS_NONE32; }
  s.page = page;
  return s;
}
static home_ui with_chat(void) {
  home_ui s = ready(HELPER);
  helper_view *h = &s.helper;
  for (int k = 0; k < 2; k++) { helper_log_push(&h->log, 'U', "a question"); helper_log_push(&h->log, 'B', "an answer"); }
  h->chat = true; h->cont = true; h->state = HV_DONE;
  return s;
}
static bool chat_kept(const home_ui *s, unsigned used) { return s->helper.cont && s->helper.chat && s->helper.log.used == used; }
static bool new_chat(const home_ui *s) { return !s->helper.cont && !s->helper.chat && !s->helper.log.used && !strcmp(s->helper.note, "New chat"); }
static const home_page pages[] = {SPARKLES, HELPER, SENSORS, SETTINGS};
#define PAGES (int)(sizeof pages / sizeof pages[0])

/* ---- C. Bottom rows: the reserved strip never taps page controls. ---- */
static void bottom_taps(void) {
  home_ui s = ready(HOME); tap(&s, 184, 430); idle(&s, 400);
  assert(s.page == HOME);                                     /* below Home's card: no tile */
  s = ready(HOME); tap(&s, 184, 20); assert(s.page == SPARKLES);
  s = ready(SENSORS); tap(&s, 184, 430);
  assert(s.page == SENSORS && !s.sensors.refresh && home_layer_rest(&s));
  s = ready(SENSORS); tap(&s, 184, 20); assert(s.sensors.refresh);
  s = ready(SETTINGS); s.settings_tab = SETTINGS_SOUND;
  for (int x = 24; x < 344; x += 20) {
    tap(&s, x, 432); idle(&s, 200);                           /* SET_HOME_BAND: empty rows, no button */
    assert(s.page == SETTINGS && !s.sound_off && !s.session_off && home_layer_rest(&s) && s.slide_kind == HOME_MOTION_NONE);
  }
  puts("C bottom rows: reserved strip takes no Sensor/Settings/Home-card tap");
}
/* ---- E. Ask: the top pull (y < 72, released at y >= 190) is a new chat again; so is the pull-up. ---- */
static void ask_new_chat(void) {
  home_ui s = with_chat();
  path(&s, 184, 40, 184, 230, 12, 16000, 0); idle(&s, 300);       /* from inside the 72 px top band */
  assert(s.page == HELPER && new_chat(&s) && s.helper.bot == 0);
  s = with_chat(); path(&s, 184, 20, 184, 212, 12, 16000, 0); idle(&s, 300);
  assert(s.page == HELPER && new_chat(&s));
  s = with_chat(); path(&s, 184, 71, 184, 191, 12, 16000, 0); idle(&s, 300);
  assert(s.page == HELPER && new_chat(&s));                        /* the band's last row, to y 191 */
  /* nothing until the release, and a short pull (not down to y 190) keeps the chat */
  s = with_chat(); unsigned used = s.helper.log.used;
  hold_path(&s, 184, 20, 184, 212, 12, 16000, 0);
  assert(chat_kept(&s, used));
  lift(&s); assert(new_chat(&s));
  s = with_chat(); path(&s, 184, 20, 184, 180, 12, 16000, 10); idle(&s, 300);
  assert(s.page == HELPER && chat_kept(&s, used));
  /* a drag that starts below the band scrolls the chat (older messages), never a new chat */
  s = with_chat(); path(&s, 184, 72, 184, 262, 12, 16000, 0); idle(&s, 300);
  assert(s.page == HELPER && chat_kept(&s, used));
  /* a top pull that drifts sideways is not a new chat */
  s = with_chat(); for (int k = 0; k <= 12; k++) sample(&s, true, 184 + 14 * k, 20 + 8 * k);
  lift(&s); idle(&s, 300);
  assert(s.page == HELPER && s.helper.cont && s.helper.log.used == used);
  /* the bottom-edge UP Home pull keeps the chat */
  s = with_chat(); path(&s, 184, 440, 184, 224, 18, 10000, 0); idle(&s, 600);
  assert(s.page == HOME && chat_kept(&s, used));
  /* the pull-up from the newest message is still a new chat */
  s = with_chat(); path(&s, 184, 280, 184, 150, 8, 10000, 0); idle(&s, 100);
  assert(s.page == HELPER && new_chat(&s));
  puts("E Ask: top pull = new chat (band 72, release >= 190); pull-up = new chat; the Home pull = Home");
}
/* ---- G. Carousel: 1:1, rubber band 0.35x, flick, nearest, settle 120..300 ms from the finger. ---- */
static void carousel(void) {
  home_ui s = ready(HOME); s.tile = 0;
  hold_path(&s, 100, 200, 200, 200, 10, 10000, 3); assert(s.drag_offset == 35);    /* past the first: 0.35x */
  hold_path(&s, 200, 200, 300, 200, 10, 10000, 0); assert(s.drag_offset == 70);
  lift(&s); idle(&s, 400); assert(s.tile == 0 && s.drag_offset == 0);
  s = ready(HOME); s.tile = HOME_TILES - 1;
  hold_path(&s, 300, 200, 200, 200, 10, 10000, 3); assert(s.drag_offset == -35);  /* past the last */
  lift(&s); idle(&s, 400); assert(s.tile == HOME_TILES - 1 && s.drag_offset == 0);
  s = ready(HOME); s.tile = 1;
  hold_path(&s, 300, 200, 200, 200, 10, 10000, 3); assert(s.drag_offset == -100); /* inside: 1:1 */
  hold_path(&s, 200, 200, 330, 210, 10, 10000, 3); assert(s.drag_offset == 30);
  lift(&s); idle(&s, 400);
  s = ready(HOME); s.tile = 0;
  hold_path(&s, 300, 200, 150, 200, 10, 10000, 10); assert(s.drag_offset == -150); /* tile 0, toward tile 1: 1:1 */
  lift(&s); idle(&s, 400); assert(s.tile == 0);
  /* flick: >= 0.35 px/ms and >= 24 px = one tile in its direction */
  s = ready(HOME); s.tile = 1; path(&s, 200, 220, 170, 220, 6, 10000, 0); idle(&s, 400);
  assert(s.tile == 2 && s.drag_offset == 0 && s.page == HOME);   /* 30 px at 0.5 px/ms */
  s = ready(HOME); s.tile = 1; path(&s, 170, 220, 200, 220, 6, 10000, 0); idle(&s, 400);
  assert(s.tile == 0);
  s = ready(HOME); s.tile = 1; path(&s, 200, 220, 177, 220, 1, 10000, 0); idle(&s, 400);
  assert(s.tile == 1 && s.page == HOME);                          /* 23 px, fast: too short */
  s = ready(HOME); s.tile = 1; path(&s, 200, 220, 170, 220, 10, 10000, 0); idle(&s, 400);
  assert(s.tile == 1);                                            /* 30 px at 0.3 px/ms: too slow */
  s = ready(HOME); s.tile = 1; path(&s, 200, 220, 176, 220, 4, 15000, 0); idle(&s, 400);
  assert(s.tile == 2);                                            /* 24 px at 0.4 px/ms */
  s = ready(HOME); s.tile = HOME_TILES - 1; path(&s, 200, 220, 120, 220, 4, 10000, 0); idle(&s, 400);
  assert(s.tile == HOME_TILES - 1 && s.drag_offset == 0);         /* no tile past the last */
  /* no flick: the nearest tile */
  s = ready(HOME); s.tile = 1; path(&s, 300, 220, 110, 220, 38, 25000, 10); idle(&s, 400);
  assert(s.tile == 2);                                            /* 190 px slowly: past half */
  s = ready(HOME); s.tile = 1; path(&s, 300, 220, 130, 220, 34, 25000, 10); idle(&s, 400);
  assert(s.tile == 1);                                            /* 170 px slowly: back */
  /* a carousel drag may start anywhere on Home, the bottom band too (only its tap does nothing) */
  s = ready(HOME); s.tile = 1; path(&s, 300, 440, 100, 440, 40, 25000, 10); idle(&s, 400);
  assert(s.tile == 2 && s.page == HOME);
  puts("G carousel: 1:1, rubber 0.35x, flick 0.35 px/ms & 24 px, nearest tile");
}
/* Settle length: ms from the release until the strip is at rest (1 ms probes). */
static int settle_ms(home_ui *s) {
  int64_t r = t;
  for (int k = 0; k < 1000; k++) { at(s, 1000, false, 0, 0); if (!s->drag_offset && !s->home_slide_from) return (int)((t - r) / 1000); }
  return -1;
}
static void settle(void) {
  /* released still (no speed): the settle length follows the remaining distance */
  home_ui s = ready(HOME); s.tile = 1; path(&s, 300, 220, 100, 220, 40, 25000, 10);
  assert(s.tile == 2 && s.drag_offset == 168);
  int far = settle_ms(&s);
  s = ready(HOME); s.tile = 1; path(&s, 300, 220, 240, 220, 12, 25000, 10);
  assert(s.tile == 1 && s.drag_offset == -60);
  int near = settle_ms(&s);
  printf("settle: 168 px in %d ms, 60 px in %d ms\n", far, near);
  assert(abs(far - 203) <= 2 && abs(near - 150) <= 2);  /* 120 + 180 * d / 368 */
  s = ready(HOME); s.tile = 1; path(&s, 200, 220, 176, 220, 4, 15000, 0);
  assert(s.tile == 2 && s.drag_offset == 344);
  int longest = settle_ms(&s);
  assert(longest >= 120 && longest <= 300);
  s = ready(HOME); s.tile = 1; path(&s, 300, 220, 296, 220, 4, 25000, 50);  /* 4 px, held 0.6 s: not a tap */
  int shortest = settle_ms(&s);
  assert(s.page == HOME && shortest >= 120 && shortest <= 125);
  /* the settle eases out: it moves monotonically and comes to rest gently */
  s = ready(HOME); s.tile = 1; path(&s, 300, 220, 100, 220, 40, 25000, 10);
  int prev = s.drag_offset, last_step = 99;
  for (int k = 0; k < 400 && (s.drag_offset || s.home_slide_from); k++) { at(&s, 1000, false, 0, 0); assert(s.drag_offset <= prev && s.drag_offset >= 0); if (s.drag_offset) last_step = prev - s.drag_offset; prev = s.drag_offset; }
  assert(!s.drag_offset && !s.home_slide_from && last_step <= 1);
  /* ...and starts at the finger's speed: a 2 px/ms flick keeps ~2 px/ms right after the release */
  s = ready(HOME); s.tile = 1;
  for (int k = 0; k <= 10; k++) at(&s, k ? 5000 : 10000, true, 250 - 10 * k, 220);  /* 100 px in 50 ms */
  at(&s, 5000, false, 0, 0);
  assert(s.tile == 2 && s.drag_offset == 268);
  int o0 = s.drag_offset; at(&s, 5000, false, 0, 0);
  float v = (s.drag_offset - o0) / 5.f;
  printf("settle starts at %.2f px/ms after a 2.00 px/ms flick\n", v);
  assert(fabsf(v + 2.f) <= .3f);
  puts("G settle: 120..300 ms by distance, monotone ease-out, starts at the finger's speed");
}
/* ---- Parallax and translation: tiles move horizontally only; art moves 0.8x of its paper. ---- */
static void parallax(void) {
  home_ui s = ready(HOME); s.tile = 0;
  assert(home_render(&s, f0, N));
  s.drag_offset = -100; assert(home_render(&s, f1, N));
  home_ui n = ready(HOME); n.tile = 1; assert(home_render(&n, f2, N));
  uint16_t paper0 = f0[50 * 368 + 184], paper1 = f2[50 * 368 + 184];
  assert(paper0 != paper1);
  for (int y = 0; y < 100; y++)
    for (int x = 0; x < 368; x++) assert(f1[y * 368 + x] == (x < 268 ? paper0 : paper1));  /* paper: -100 px exactly */
  int art = 0;
  for (int y = 108; y < 300; y++)
    for (int x = 88; x < 280; x++) {
      assert(f1[y * 368 + x - 80] == f0[y * 368 + x]);  /* the Sparkles art: -80 px (0.8x) */
      art += f0[y * 368 + x] != paper0;
    }
  assert(art > 5000);
  puts("G parallax: paper moves 1:1, art 0.8x, rows above the art are pure paper (translation only)");
}
/* ---- H. Elapsed time: irregular sample gaps give the same positions at the same moments. ---- */
static void elapsed_time_carousel(void) {
  home_ui s = ready(HOME); s.tile = 1; path(&s, 300, 220, 100, 220, 40, 25000, 10);
  home_ui base = s; int64_t t0 = t;
  static const int gaps[] = {3, 17, 1, 41, 7, 29, 2, 13, 55, 11, 5, 23, 37};
  home_ui b = base; int64_t tb = t0;
  for (int k = 0; k < 40; k++) {
    tb += gaps[k % 13] * 1000; home_sample(&b, tb, false, 0, 0);
    home_ui c = base; home_sample(&c, tb, false, 0, 0);       /* one jump straight to tb */
    home_ui r = base; for (int64_t q = t0 + 10000; q < tb; q += 10000) home_sample(&r, q, false, 0, 0);
    home_sample(&r, tb, false, 0, 0);                          /* regular 10 ms polling up to tb */
    assert(b.drag_offset == c.drag_offset && r.drag_offset == c.drag_offset && b.tile == c.tile);
  }
  /* speed estimate under irregular sampling: 0.5 px/ms flicks, 0.3 px/ms does not (both >= 24 px) */
  for (int fast = 0; fast < 2; fast++) {
    home_ui q = ready(HOME); q.tile = 1; int64_t start = t + 10000, w = 0;
    for (int k = 0; w < 100000; k++) { w += gaps[k % 13] * 1000; if (w > 100000) w = 100000;
      t = start + w; home_sample(&q, t, true, 200 - (int)((fast ? .5 : .3) * w / 1000), 220); }
    assert(abs(q.last_x - q.start_x) >= 24);
    lift(&q); idle(&q, 400);
    assert(q.tile == (fast ? 2 : 1));
  }
  /* the Home pull's speed rule under irregular sampling: 0.6 px/ms up goes Home, 0.4 does not */
  for (int fast = 0; fast < 2; fast++) {
    home_ui q = ready(SETTINGS); int64_t start = t + 10000, w = 0;
    home_sample(&q, start, true, 184, 440);
    for (int k = 0; w < 80000; k++) { w += gaps[k % 13] * 1000; if (w > 80000) w = 80000;
      t = start + w; home_sample(&q, t, true, 184, 440 - (int)((fast ? .6 : .4) * w / 1000)); }
    assert(q.start_y - q.last_y >= 24 && q.last_y > HOME_PULL_CENTER_Y);
    lift(&q); idle(&q, 600);
    assert(q.page == (fast ? HOME : SETTINGS));
  }
  puts("H carousel and Home pull: positions depend on elapsed time only; speed is time-weighted");
}
/* ---- I. The page layer: a tile's page slides up over Home; a touch ends a slide at once. ---- */
static void page_layer(void) {
  /* open: the page slides up over Home (ease-out, 220 ms) */
  home_ui s = ready(HOME); s.tile = 2; tap(&s, 184, 220);
  assert(s.page == SENSORS && s.slide_kind == HOME_MOTION_OPEN && s.slide_page == SENSORS && s.page_y == 448);
  assert(home_page_offset(&s) == 448 && !home_blob_shown(&s));
  int64_t r = t; int prev = s.page_y, open_mid = -1;
  while (s.slide_kind != HOME_MOTION_NONE) {
    at(&s, 1000, false, 0, 0);
    assert(s.page == SENSORS && s.page_y <= prev); prev = s.page_y;
    if (t - r == 110000) open_mid = s.page_y;
    assert(t - r <= 221000);
  }
  printf("open: 448 -> 0 in %lld ms, %d at 110 ms\n", (long long)((t - r) / 1000), open_mid);
  assert(s.page_y == 0 && open_mid >= 0 && open_mid <= 448 * 3 / 10);
  /* a touch during a slide ends it at once (the finger lands on the final layout) */
  s = ready(HOME); s.tile = 2; tap(&s, 184, 220); at(&s, 20000, true, 184, 200);
  assert(s.page_y == 0 && s.slide_kind == HOME_MOTION_NONE && s.page == SENSORS);
  lift(&s);
  /* ...also a touch in the pull zone during the open: the slide ends, the touch waits to be classified */
  s = ready(HOME); s.tile = 3; tap(&s, 184, 220); at(&s, 20000, true, 184, 440);
  assert(s.page == SETTINGS && home_layer_rest(&s) && s.pull_wait);
  lift(&s); idle(&s, 300);
  /* a page switched under a held pull (USB) drops the blob: not drawn, not motion; the touch is spent */
  s = ready(SETTINGS); hold_path(&s, 184, 440, 184, 400, 7, 10000, 0); assert(home_blob_shown(&s));
  s.page = SPARKLES;
  assert(!home_blob_shown(&s) && home_page_offset(&s) == 0 && !home_motion_moving(&s) && home_layer_rest(&s));
  sample(&s, true, 184, 390);
  assert(s.slide_kind == HOME_MOTION_NONE && home_layer_rest(&s));
  lift(&s); idle(&s, 300); assert(s.page == SPARKLES);
  puts("I page layer: opens 220 ms ease-out; a touch ends a slide; USB switch drops it");
}
/* Same moment, same picture: the open slide under irregular sampling (the pull's: home_pull_test H). */
static void elapsed_time_slides(void) {
  static const int gaps[] = {3, 17, 1, 41, 7, 29, 2, 13, 55, 11, 5, 23, 37};
  home_ui s = ready(HOME); s.tile = 3; tap(&s, 184, 220);
  assert(s.slide_kind == HOME_MOTION_OPEN);
  home_ui base = s, b = s; int64_t t0 = t, tb = t0;
  for (int k = 0; k < 30; k++) {
    tb += gaps[k % 13] * 1000; home_sample(&b, tb, false, 0, 0);
    home_ui c = base; home_sample(&c, tb, false, 0, 0);
    home_ui q = base; for (int64_t p = t0 + 10000; p < tb; p += 10000) home_sample(&q, p, false, 0, 0);
    home_sample(&q, tb, false, 0, 0);
    assert(b.page_y == c.page_y && q.page_y == c.page_y && b.slide_kind == c.slide_kind && b.page == c.page);
  }
  puts("T open slide: positions depend on elapsed time only");
}
/* home_visual_equal: false only while something moves. */
static void repaint_only_while_moving(void) {
  home_ui s = ready(SETTINGS);
  home_ui a = s; lift(&s); assert(home_visual_equal(&a, &s));
  sample(&s, true, 184, 440); home_ui b = s; sample(&s, true, 184, 440); assert(home_visual_equal(&b, &s));
  sample(&s, true, 184, 425); assert(!home_visual_equal(&b, &s));   /* the pull: the blob */
  b = s; sample(&s, true, 184, 425); assert(home_visual_equal(&b, &s));
  sample(&s, true, 184, 410);
  for (int k = 0; k < 10; k++) sample(&s, true, 184, 410);
  lift(&s);
  assert(s.slide_kind == HOME_MOTION_BACK);
  int moving = 0;
  while (s.slide_kind != HOME_MOTION_NONE) { b = s; at(&s, 10000, false, 0, 0); moving += !home_visual_equal(&b, &s); }
  assert(moving >= 10);
  b = s; idle(&s, 50); assert(home_visual_equal(&b, &s));  /* at rest: no repaint */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 416, 2, 10000, 0);
  assert(s.slide_kind == HOME_MOTION_OUT);
  moving = 0;
  while (s.slide_kind != HOME_MOTION_NONE) { b = s; at(&s, 10000, false, 0, 0); moving += !home_visual_equal(&b, &s); }
  assert(moving >= 10 && s.page == HOME);
  s = ready(HOME); s.tile = 0; tap(&s, 184, 220); b = s; at(&s, 30000, false, 0, 0); assert(!home_visual_equal(&b, &s));
  puts("R repaint only while the page/strip moves (a still circular reveal: none)");
}
/* ---- J. Compositor: a tile's page slides up over Home (the blob's pixels: home_blob_test). ---- */
static void compositor(void) {
  static uint16_t snapbuf[N], homebuf[N];
  home_ui o = ready(HOME); o.tile = 3; assert(home_render(&o, f1, N));
  tap(&o, 184, 220); assert(o.page == SETTINGS);
  home_ui ow = o; ow.slide_kind = HOME_MOTION_NONE; ow.page_y = 0; assert(home_render(&ow, f0, N));
  at(&o, 60000, false, 0, 0); int y = o.page_y; assert(y > 0 && y < 448);
  home_snapshot so = {.px = snapbuf, .home = homebuf};
  assert(home_compose(&o, f2, N, &so, NULL, -1));
  for (int r = 0; r < 448; r++) assert(!memcmp(f2 + r * 368, r < y ? f1 + r * 368 : f0 + (r - y) * 368, 368 * 2));
  assert(o.page == SETTINGS);                                  /* composing leaves the state alone */
  /* no PSRAM frame: live, no motion */
  home_snapshot off = {0};
  assert(home_compose(&o, f2, N, &off, NULL, -1) && !memcmp(f2, f0, sizeof f0));
  /* at rest compose == render */
  home_ui q = ready(SETTINGS); home_snapshot sq = {.px = snapbuf, .home = homebuf};
  assert(home_compose(&q, f2, N, &sq, NULL, -1) && home_render(&q, f1, N) && !memcmp(f2, f1, sizeof f1));
  /* a new motion (the pull after the open) never reuses an old snapshot */
  unsigned id = o.motion_id; idle(&o, 300); snapbuf[0] = 0x1234;
  hold_path(&o, 184, 440, 184, 390, 5, 10000, 0); assert(o.motion_id != id && home_blob_shown(&o));
  assert(home_compose(&o, f2, N, &so, NULL, -1) && so.motion == o.motion_id && snapbuf[0] != 0x1234 && so.home_motion == o.motion_id);
  lift(&o);
  puts("J compositor: OPEN snapshot retained; a new Home reveal reconstructs outgoing and cached Home");
}
/* ---- K. Log notes: HOME_SLIDE when a settle ends (HOME_GESTURE / HOME_PULL: home_pull_test I). ---- */
static void notes(void) {
  home_ui s = ready(SETTINGS); path(&s, 184, 300, 184, 100, 8, 10000, 0);
  assert(!s.note.gesture && !s.note.pull);                     /* not the pull: no note */
  s = ready(SETTINGS); tap(&s, 184, 430);
  assert(!s.note.gesture && !s.note.pull);                     /* a tap in the zone: no note */
  s = ready(HOME); s.tile = 1; path(&s, 300, 220, 100, 220, 40, 25000, 10);
  assert(!s.note.slide);
  memset(&s.note, 0, sizeof s.note);
  idle(&s, 400);
  assert(s.note.slide && s.note.from == 1 && s.note.to == 2 && s.note.ms == 202);
  puts("K notes: HOME_SLIDE from/to/ms; no Home note for touches that are not the pull");
}
/* ---- L. USB WGS1 replays drive the same code as a finger. ---- */
static void replay(home_ui *s, unsigned g) { int64_t c = t + 1; gesture_replay(s, g, &c); t = c; }
static void wgs1(void) {
  for (int k = 0; k < PAGES; k++) {
    home_ui s = k == 1 ? with_chat() : ready(pages[k]);
    unsigned used = s.helper.log.used;
    replay(&s, GESTURE_HOME); idle(&s, 600);
    assert(s.page == HOME && home_layer_rest(&s) && s.helper.log.used == used);  /* WGS1 3: the Home pull */
    assert(s.input.scene.strength == 0 && s.input.scene.trail_count == 0);         /* ...never paints */
  }
  for (int from = 0; from < HOME_TILES; from++) {
    home_ui s = with_chat(); s.page = HOME; s.tile = from;
    replay(&s, GESTURE_OPEN_ASK); idle(&s, 400);
    assert(s.page == HELPER && s.tile == 1 && home_layer_rest(&s) && s.helper.cont && s.helper.chat);  /* WGS1 5 */
  }
  home_ui s = with_chat(); replay(&s, GESTURE_NEW); idle(&s, 100);
  assert(s.page == HELPER && new_chat(&s));                                          /* WGS1 1 */
  s = with_chat(); replay(&s, GESTURE_TOP_NEW); idle(&s, 100);
  assert(s.page == HELPER && new_chat(&s));                                          /* WGS1 15 */
  /* 15 is the top pull, not the pull-up: it works with no chat on screen (the pull-up needs one) */
  s = ready(HELPER); s.helper.cont = true; replay(&s, GESTURE_TOP_NEW); idle(&s, 100);
  assert(s.page == HELPER && !s.helper.cont && !strcmp(s.helper.note, "New chat"));
  s = ready(HELPER); s.helper.cont = true; replay(&s, GESTURE_NEW); idle(&s, 100);
  assert(s.page == HELPER && s.helper.cont);
  puts("L WGS1 3 = the Home pull, 5 = open Ask, 1 = pull-up, 15 = top pull new chat");
}
/* ---- M. Settings safe area: the bottom 28 rows stay empty (rounded panel); no gesture band. ---- */
static void safe_area(void) {
#ifdef HOME_TOP_EDGE_PX
  assert(!"HOME_TOP_EDGE_PX is gone: nothing reserves the top band");
#endif
#ifdef HOME_BOTTOM_EDGE_Y
  assert(!"HOME_BOTTOM_EDGE_Y is gone: the Home pull starts in a zone, not a band");
#endif
  assert(SET_HOME_BAND == 28 && SET_BOTTOM_Y == 420 && HOME_PULL_ZONE_Y == 420 && HELPER_TOP_BAND == 72 && HELPER_TOP_NEW_Y == 190);
  assert(set_safe_px(184, 419) && !set_safe_px(184, 420) && !set_safe_px(184, 447));
  assert(set_safe_px(184, 16) && set_safe_px(184, 20) && !set_safe_px(184, 15));
  assert(!set_safe_px(16, 419) && !set_safe_px(351, 419));   /* rounded bottom corners */
  puts("M safe area: bottom 28 rows are the Home pull zone; the top band stays free");
}

int main(int argc, char **argv) {
  static const struct { const char *name; void (*run)(void); } sections[] = {
    {"C", bottom_taps}, {"E", ask_new_chat}, {"G", carousel}, {"S", settle}, {"P", parallax},
    {"H", elapsed_time_carousel}, {"I", page_layer}, {"T", elapsed_time_slides}, {"R", repaint_only_while_moving},
    {"J", compositor}, {"K", notes}, {"L", wgs1}, {"M", safe_area},
  };
  int ran = 0;
  for (size_t k = 0; k < sizeof sections / sizeof sections[0]; k++) {
    if (argc > 1 && strcmp(argv[1], sections[k].name)) continue;
    sections[k].run(); ran++;
  }
  assert(ran > 0);
  printf("home motion: %d sections PASS\n", ran);
  return 0;
}
