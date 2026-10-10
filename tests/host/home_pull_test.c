/* The Home pull (W14, SPEC7) through the real touch path (home_sample), one section per contract:
 *  A  zone: on Sparkles / Ask / Sensor / Settings an UP drag from y >= 420 goes Home; the former
 *     lower-third DOWN gesture is page-owned, while bottom taps/holds/sideways/down are reserved.
 *  B  classification: >= 12 px up with up > 1.5|dx| = the pull; other bottom-strip input is consumed.
 *  C  conflicts: Kotaro hold-to-talk (armed at the ORIGINAL down time), Stop, Kotaro tap, bot swipes,
 *     chat scrolling, the pull-up and top pull new chat, the link button, Sparkles painting (still,
 *     sideways, upward strokes below the retired lower-third boundary), the
 *     Sparkles flick, every Settings button in the zone, Settings tab swipes, Sensor taps.
 *  D  release: centre, or >= 0.5 px/ms up (last 80 ms) after >= 24 px = Home, else spring back.
 *  E  held: a fixed-center circle expands from classification to diagonal corner cover.
 *  F  commit: Home is live inside it and a partial flick finishes expanding continuously within
 *     240 ms; a touch during it is Home's.
 *  G  cancel: <= 220 ms, bounded contraction to zero, then the page in place.
 *  H  elapsed time: the same moment gives the same shape whatever the sample gaps.
 *  I  log notes (HOME_PULL start / HOME_GESTURE / HOME_PULL end) and WGS1 3 (the pull), 5, 1, 15.
 * `home_pull_test <section>` runs one section. -DPULL_BASE_ONLY builds only the sections that need no
 * new API (A, C, D), to show them failing against the old bottom-edge swipe-up (RED). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_render.h"
#include "gesture_replay.h"
#include "settings_states.h"

static int64_t t = 1000000;
static inline void at(home_ui *s, int64_t dt_us, bool down, int x, int y) { t += dt_us; home_sample(s, t, down, x, y); }
static inline void sample(home_ui *s, bool down, int x, int y) { at(s, 10000, down, x, y); }
static inline void lift(home_ui *s) { sample(s, false, 0, 0); }  /* the panel reports no point on release */
static inline void idle(home_ui *s, int ms) { for (int i = 0; i < ms / 10; i++) lift(s); }
static inline void tap(home_ui *s, int x, int y) { sample(s, true, x, y); sample(s, true, x, y); lift(s); }
/* Finger: n equal steps of step_us from (x0,y0) to (x1,y1), `hold` still samples (no lift). */
static inline void hold_path(home_ui *s, int x0, int y0, int x1, int y1, int n, int step_us, int hold) {
  for (int k = 0; k <= n; k++) at(s, k ? step_us : 10000, true, x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n);
  for (int k = 0; k < hold; k++) sample(s, true, x1, y1);
}
static inline void path(home_ui *s, int x0, int y0, int x1, int y1, int n, int step_us, int hold) {
  hold_path(s, x0, y0, x1, y1, n, step_us, hold);
  lift(s);
}
/* On Wi-Fi, paired, signed in to both providers: every tile is ON and opens its page. */
static inline home_ui ready(home_page page) {
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
static inline home_ui with_chat(void) {
  home_ui s = ready(HELPER);
  helper_view *h = &s.helper;
  for (int k = 0; k < 2; k++) { helper_log_push(&h->log, 'U', "a question"); helper_log_push(&h->log, 'B', "an answer"); }
  h->chat = true; h->cont = true; h->state = HV_DONE;
  return s;
}
static inline bool new_chat(const home_ui *s) { return !s->helper.cont && !s->helper.chat && !s->helper.log.used && !strcmp(s->helper.note, "New chat"); }
static const home_page pages[] = {SPARKLES, HELPER, SENSORS, SETTINGS};
#define PAGES (int)(sizeof pages / sizeof pages[0])
#define ZONE 420   /* the literal contract (HOME_PULL_ZONE_Y) */

/* ---- A. The zone: UP from the bottom edge goes Home; the retired lower third belongs to pages. ---- */
static void zone(void) {
  for (int k = 0; k < PAGES; k++) {
    home_page p = pages[k];
    home_ui s = ready(p); path(&s, 184, 440, 184, 224, 18, 10000, 0);
    assert(s.page == HOME);                                   /* Home at the release */
    idle(&s, 600); assert(s.page == HOME);
    s = ready(p); path(&s, 184, ZONE, 184, 224, 16, 10000, 0); idle(&s, 600);
    assert(s.page == HOME);                                   /* the zone's first row */
    s = ready(p); path(&s, 184, ZONE - 1, 184, 224, 16, 10000, 0); idle(&s, 600);
    assert(s.page == p);                                      /* one row above: page-owned */
    s = ready(p); path(&s, 184, 440, 184, 447, 7, 10000, 0); idle(&s, 600);
    assert(s.page == p);                                      /* 7 px of room: never a pull */
    s = ready(p); path(&s, 184, 330, 184, 410, 9, 10000, 0); idle(&s, 600);
    assert(s.page == p);                                      /* retired lower-third DOWN is page-owned */
    s = ready(p); path(&s, 60, 440, 300, 430, 9, 10000, 0); idle(&s, 600);
    assert(s.page == p);                                      /* sideways in bottom strip is reserved */
    s = ready(p); path(&s, 184, 20, 184, 200, 9, 10000, 0); idle(&s, 600);
    assert(s.page == p);                                      /* a swipe down from the top */
  }
  home_ui h = ready(HOME); h.tile = 1; path(&h, 184, 440, 184, 224, 18, 10000, 0); idle(&h, 600);
  assert(h.page == HOME && h.tile == 1 && h.slide_kind == HOME_MOTION_NONE);   /* Home has no pull */
  puts("A zone: bottom-edge UP = Home; y<420 is page-owned; bottom tap/down/sideways reserved");
}

/* ---- C. Conflicts: controls in or near the zone keep their normal behavior. ---- */
static void conflicts_ask(void) {
  /* Kotaro hold-to-talk on the chat layout's compact Kotaro (y 322..418, in the zone): recording starts
   * exactly 400 ms after the ORIGINAL down (the touch waited 150 ms to be classified). */
  home_ui s = with_chat();
  int bx = HELPER_BOT_X, by = HELPER_CHAT_BOT_Y;
  assert(by < ZONE);
  sample(&s, true, bx, by); int64_t t0 = t;
  while (t - t0 < 390000) { sample(&s, true, bx, by); assert(s.helper.state != HV_LISTENING); }
  sample(&s, true, bx, by);
  assert(t - t0 == 400000 && s.helper.state == HV_LISTENING && s.helper.press_us == t0 + 400000 && s.page == HELPER);
  for (int k = 0; k < 60; k++) sample(&s, true, bx, by + (k & 1));   /* talking, the finger trembles */
  lift(&s);
  assert(s.page == HELPER && s.helper.state == HV_TRANSCRIBING && s.helper.want_send);
  /* a short tap on Kotaro: nothing recorded, he reacts */
  s = with_chat(); tap(&s, bx, by);
  assert(s.page == HELPER && s.helper.state == HV_DONE && !s.helper.want_record && s.helper.kot.tap_s > 0);
  /* Stop on the working (compact) Kotaro */
  s = with_chat(); s.helper.state = HV_RUNNING; strcpy(s.helper.id, "c0ffee"); tap(&s, bx, by);
  assert(s.page == HELPER && s.helper.state == HV_STOPPING && s.helper.want_stop);
  /* the welcome layout's big Kotaro (above the zone): the hold arms at once, as always */
  s = ready(HELPER); sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y); t0 = t;
  while (t - t0 < 400000) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_LISTENING && s.helper.press_us == t0 + 400000);
  lift(&s);
  /* bot swipes, from the zone and from above it */
  s = with_chat(); path(&s, 300, 350, 140, 354, 8, 10000, 0); idle(&s, 300);
  assert(s.page == HELPER && s.helper.bot == 1);
  s = ready(HELPER); path(&s, 300, 330, 140, 330, 8, 10000, 0); idle(&s, 300);
  assert(s.page == HELPER && s.helper.bot == 1);
  s = ready(HELPER); path(&s, 300, 200, 140, 200, 8, 10000, 0); idle(&s, 300);
  assert(s.page == HELPER && s.helper.bot == 1);
  /* chat scrolling (older messages) in the chat, above the zone */
  s = with_chat(); s.helper.scroll_max = 500; path(&s, 184, 150, 184, 290, 10, 10000, 0); idle(&s, 100);
  assert(s.page == HELPER && s.helper.scroll > 0 && s.helper.cont && s.helper.chat);
  /* the pull-up new chat, also when it starts in the zone (WGS1 1 starts at y 300) */
  s = with_chat(); path(&s, 184, 280, 184, 150, 8, 10000, 0); idle(&s, 100);
  assert(s.page == HELPER && new_chat(&s));
  s = with_chat(); path(&s, 184, 330, 184, 180, 8, 10000, 0); idle(&s, 100);
  assert(s.page == HELPER && new_chat(&s));
  /* the top pull-down new chat (top band 72, released >= 190) */
  s = with_chat(); path(&s, 184, 20, 184, 212, 12, 16000, 0); idle(&s, 300);
  assert(s.page == HELPER && new_chat(&s));
  /* a bottom-edge pull is Home: nothing recorded, the chat kept */
  s = with_chat(); unsigned used = s.helper.log.used; path(&s, bx, 440, bx, 224, 18, 10000, 0);
  assert(s.page == HOME && s.helper.log.used == used && !s.helper.want_record && s.helper.state == HV_DONE);
  puts("C Ask: zone hold arms at the original down (400 ms), tap, Stop, swipes, scroll, pull-up, top pull");
}
static void conflicts_sparkles(void) {
  /* a still finger in the zone paints once it is classified (<= 150 ms); its first point is where the
   * finger went down */
  home_ui s = ready(SPARKLES); sample(&s, true, 100, 360);
  for (int k = 0; k < 20; k++) sample(&s, true, 100, 360);
  assert(s.page == SPARKLES && s.input.down && s.input.scene.strength > 0 && s.input.scene.trail_count > 0);
  { const sp_trail_particle *p0 = &s.input.scene.trail[0];
    assert(fabsf(p0->x - 100) <= 40 && fabsf(p0->y - 360) <= 40); }
  lift(&s);
  /* sideways and upward strokes from the zone paint, from their start */
  s = ready(SPARKLES); path(&s, 40, 380, 320, 390, 14, 10000, 0);
  assert(s.page == SPARKLES && s.input.scene.trail_count > 6);
  { float minx = 999; for (unsigned i = 0; i < s.input.scene.trail_count; i++) if (s.input.scene.trail[i].x < minx) minx = s.input.scene.trail[i].x;
    assert(minx <= 80); }
  s = ready(SPARKLES); path(&s, 184, 419, 184, 200, 11, 10000, 0);
  assert(s.page == SPARKLES && s.input.scene.trail_count > 6);
  /* the waiting points are painted too: a finger that jumps 80 px sideways right after going down
   * still leaves its trail from where it went down, not only from where it was classified */
  s = ready(SPARKLES); sample(&s, true, 40, 380); sample(&s, true, 120, 380); sample(&s, true, 140, 380);
  { float minx = 999; for (unsigned i = 0; i < s.input.scene.trail_count; i++) if (s.input.scene.trail[i].x < minx) minx = s.input.scene.trail[i].x;
    assert(s.page == SPARKLES && s.input.scene.trail_count > 0 && minx <= 70); }
  lift(&s);
  /* above the zone and in the top band the stroke paints from its first sample */
  s = ready(SPARKLES); sample(&s, true, 90, 200); assert(s.input.down && s.input.scene.trail_count > 0); lift(&s);
  s = ready(SPARKLES); sample(&s, true, 90, 2); assert(s.input.down); lift(&s);
  /* bottom-edge UP is Home: nothing painted, the palette untouched */
  s = ready(SPARKLES); float theme = s.input.scene.theme;
  path(&s, 120, 440, 184, 224, 18, 10000, 0);
  assert(s.page == HOME && s.input.scene.strength == 0 && s.input.scene.trail_count == 0 && s.input.scene.theme == theme);
  /* the quick sideways flick still changes the style, also from the zone */
  s = ready(SPARKLES); path(&s, 284, 360, 134, 362, 8, 10000, 0);
  assert(s.page == SPARKLES && s.input.scene.style == 1);
  puts("C Sparkles: former lower-third strokes paint from the first point; bottom-edge UP = Home");
}
/* What a Settings action changes (the fields home_settings_do writes). */
static inline bool settings_effect_equal(const home_ui *a, const home_ui *b) {
  bool same = a->page == b->page && a->settings_tab == b->settings_tab && a->sound_off == b->sound_off &&
              a->session_off == b->session_off && a->theme_mode == b->theme_mode && a->accent == b->accent &&
              a->auto_qr == b->auto_qr && a->pair.want_scan == b->pair.want_scan && a->pair.step == b->pair.step &&
              a->pair.want_cancel == b->pair.want_cancel && a->pair.enroll_requested == b->pair.enroll_requested;
  for (int k = SETTINGS_HERMES; k <= SETTINGS_HOME_ASSISTANT; k++) {
    const phone_view *x = home_tab_phone_c(a, k), *y = home_tab_phone_c(b, k);
    if (x && y) same = same && x->want_start == y->want_start && x->showing == y->showing &&
                       x->confirm_signout == y->confirm_signout && x->want_forget == y->want_forget && x->refresh == y->refresh;
  }
  return same;
}
static void conflicts_settings_sensor(void) {
  int found = 0;
  for (int i = 0; i < SETTINGS_STATES; i++) {
    home_ui base; settings_state(i, &base);
    settings_buttons b = home_settings_buttons(&base, base.live_now_us);
    for (int k = 0; k < b.count; k++) {
      int cx = b.t[k].x + b.t[k].w / 2, cy = b.t[k].y + b.t[k].h / 2;
      if (cy < 300 || cy >= ZONE) continue;
      found++;
      /* a tap on a button in the zone does exactly what the button does */
      home_ui a = base, r = base; int64_t ta = base.input.stamp_us + 20000;
      home_sample(&a, ta, true, cx, cy); home_sample(&a, ta + 30000, true, cx, cy); home_sample(&a, ta + 60000, false, 0, 0);
      home_sample(&r, ta, false, 0, 0); home_sample(&r, ta + 30000, false, 0, 0); home_sample(&r, ta + 60000, false, 0, 0);
      (void)home_settings_do(&r, b.t[k].action);
      assert(settings_effect_equal(&a, &r));
      /* a vertical drag from the former lower third stays page-owned and does not press the button */
      home_ui g = base, n = base; int64_t tg = base.input.stamp_us + 20000;
      for (int s = 0; s <= 6; s++) home_sample(&g, tg + s * 10000, true, cx, cy + 10 * s + (s ? 4 : 0));
      home_sample(&g, tg + 70000, false, 0, 0);
      for (int s = 0; s <= 7; s++) home_sample(&n, tg + s * 10000, false, 0, 0);
      assert(g.page == base.page);
      assert(settings_effect_equal(&g, &n));
    }
  }
  assert(found >= 8);
  /* a tab swipe from the zone still turns the page */
  home_ui s = ready(SETTINGS); s.settings_tab = SETTINGS_SOUND; path(&s, 300, 360, 150, 364, 8, 10000, 0);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_BATTERY);
  /* Sensor: a tap on a lower tile reads again; its old DOWN drag is not Home. */
  s = ready(SENSORS); tap(&s, 184, 330); assert(s.page == SENSORS && s.sensors.refresh);
  s = ready(SENSORS); path(&s, 184, 330, 184, 410, 9, 10000, 0); assert(s.page == SENSORS && !s.sensors.refresh);
  s = ready(SENSORS); path(&s, 184, 440, 184, 224, 18, 10000, 0); assert(s.page == HOME && !s.sensors.refresh);
  printf("C Settings: %d former lower-third buttons keep taps; tab swipes; Sensor routing preserved\n", found);
}

/* ---- D. Release thresholds: centre, or 0.5 px/ms UP over the last 80 ms after 24 px. ---- */
static void thresholds(void) {
  home_ui s = ready(SETTINGS); path(&s, 184, 440, 184, 400, 40, 10000, 10); idle(&s, 600);
  assert(s.page == SETTINGS);                                /* short, slow, still at the end: back */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 224, 54, 10000, 10); idle(&s, 600);
  assert(s.page == HOME);                                    /* centre commits even slowly */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 416, 2, 10000, 0); idle(&s, 600);
  assert(s.page == HOME);                                    /* a short flick: 24 px at 1.2 px/ms */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 417, 2, 10000, 0); idle(&s, 600);
  assert(s.page == SETTINGS);                                /* 23 px, fast: too short */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 396, 8, 10000, 0); idle(&s, 600);
  assert(s.page == HOME);                                    /* 44 px at 0.55 px/ms */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 404, 8, 10000, 0); idle(&s, 600);
  assert(s.page == SETTINGS);                                /* 36 px at 0.45 px/ms */
  /* the speed is over the last 80 ms only: fast early, then slow = back (under 56 px) */
  s = ready(SETTINGS); hold_path(&s, 184, 440, 184, 400, 4, 10000, 0); hold_path(&s, 184, 400, 184, 390, 10, 10000, 0); lift(&s); idle(&s, 600);
  assert(s.page == SETTINGS);
  /* measured at release: far up, then reverse to only 30 px = back */
  s = ready(SETTINGS); hold_path(&s, 184, 440, 184, 350, 9, 10000, 0); hold_path(&s, 184, 350, 184, 410, 30, 10000, 5); lift(&s); idle(&s, 600);
  assert(s.page == SETTINGS);
  /* a diagonal bottom pull commits by upward travel */
  s = ready(SETTINGS); path(&s, 150, 440, 190, 224, 24, 10000, 10); idle(&s, 600);
  assert(s.page == HOME);
  /* ...a 45 degree bottom motion is reserved but not Home */
  s = ready(SETTINGS); path(&s, 150, 440, 230, 360, 16, 10000, 10); idle(&s, 600);
  assert(s.page == SETTINGS);
  /* a touch that broke off (a read error: consumed) springs back even from far down */
  s = ready(SETTINGS); hold_path(&s, 184, 440, 184, 300, 12, 10000, 0); s.consumed = true; lift(&s); idle(&s, 600);
  assert(s.page == SETTINGS);
  puts("D release: centre or >= 0.5 px/ms UP after >=24 px = Home; slow short reversals cancel");
}

#ifndef PULL_BASE_ONLY
/* ---- B. Classification: 12 px up with up > 1.5|dx| = pull; other bottom input is reserved. ---- */
static void classification(void) {
  assert(HOME_PULL_ZONE_Y == ZONE && HOME_PULL_DECIDE_PX == 12);
  for (int k = 0; k < PAGES; k++) {
    home_ui s = ready(pages[k]);
    sample(&s, true, 184, 440); assert(s.pull_wait && !s.edge && s.slide_kind == HOME_MOTION_NONE);
    sample(&s, true, 184, 429); assert(s.pull_wait && !s.edge);                 /* 11 px: undecided */
    sample(&s, true, 184, 428);                                                 /* 12 px up: the pull */
    assert(!s.pull_wait && s.edge && s.slide_kind == HOME_MOTION_DRAG && s.slide_page == pages[k] && s.page == pages[k]);
    lift(&s); idle(&s, 400);
    s = ready(pages[k]); sample(&s, true, 184, ZONE - 1); assert(!s.pull_wait && !s.edge); lift(&s);
  }
  /* the direction rule: up > 1.5 |dx| */
  home_ui s = ready(SETTINGS); sample(&s, true, 184, 440); sample(&s, true, 191, 428);
  assert(!s.pull_wait && s.edge);                                               /* 12 up, 7 across */
  lift(&s); idle(&s, 400);
  s = ready(SETTINGS); sample(&s, true, 184, 440); sample(&s, true, 192, 428);
  assert(s.pull_wait && !s.edge && !s.consumed);                                /* diagonal: still reserved */
  sample(&s, true, 184, 400); assert(!s.pull_wait && s.edge);                    /* later net UP qualifies */
  lift(&s); idle(&s, 100);
  s = ready(SETTINGS); sample(&s, true, 184, 440); sample(&s, true, 196, 439);
  assert(s.pull_wait && !s.edge && !s.consumed);                                /* 12 across: still reserved */
  sample(&s, true, 184, 400); assert(!s.pull_wait && s.edge); lift(&s);           /* later net UP qualifies */
  s = ready(SETTINGS); sample(&s, true, 184, 440); sample(&s, true, 184, 447);
  assert(s.pull_wait && !s.edge); lift(&s);                                     /* short down then lift */
  s = ready(SETTINGS); sample(&s, true, 184, 440); sample(&s, true, 195, 429);
  assert(s.pull_wait); sample(&s, true, 196, 429); assert(s.pull_wait && !s.edge && !s.consumed);
  sample(&s, true, 184, 400); assert(s.edge); lift(&s);
  /* a fast first upward sample classifies at once */
  s = ready(SETTINGS); sample(&s, true, 184, 440); sample(&s, true, 190, 400); assert(s.edge); lift(&s); idle(&s, 600);
  /* Elapsed time does not expire a reserved contact; it can qualify later. */
  s = ready(SETTINGS); sample(&s, true, 184, 440); int64_t t0 = t;
  while (t - t0 < 140000) { sample(&s, true, 184, 440 - (int)((t - t0) / 30000)); assert(s.pull_wait); }
  sample(&s, true, 184, 436); assert(t - t0 == 150000 && s.pull_wait && !s.edge && !s.consumed);
  sample(&s, true, 184, 400); assert(s.edge && s.slide_kind == HOME_MOTION_DRAG);
  lift(&s); idle(&s, 300); assert(s.page == SETTINGS);                         /* qualified, but slow/partial release cancels */
  /* a lift while waiting is reserved and does not tap Sensor */
  s = ready(SENSORS); sample(&s, true, 184, 440); lift(&s); assert(!s.pull_wait && !s.sensors.refresh);
  /* a page switched (USB) while the touch waits: the touch is spent, neither the pull nor the page's */
  s = ready(SETTINGS); sample(&s, true, 184, 440); s.page = SPARKLES; sample(&s, true, 184, 425);
  assert(!s.pull_wait && !s.edge && s.consumed && s.slide_kind == HOME_MOTION_NONE && !s.input.down);
  lift(&s); assert(s.page == SPARKLES && s.input.scene.trail_count == 0);
  /* Home never waits */
  s = ready(HOME); sample(&s, true, 184, 440); assert(!s.pull_wait); lift(&s);
  puts("B classify: 12 px UP & up > 1.5|dx| = pull; other bottom input stays reserved");
}

/* ---- E. Held: centre-normalised circular radius, opacity, reverse and stationary hold. ---- */
static float area(const home_blob *b) {   /* superellipse area: 4ab G(1+1/n)^2 / G(1+2/n) */
  float g = tgammaf(1 + 1 / b->n);
  return b->w * b->h * g * g / tgammaf(1 + 2 / b->n);
}
static void held(void) {
  home_ui s = ready(SETTINGS);
  int ax = 150, ay = 440;
  sample(&s, true, ax, ay);
  home_blob prev = {184, 224, 0, 0, 2, 0};
  float prev_area = 0.f;
  const int ys[] = {428,400,360,320,280,240,224};
  for (size_t k=0;k<sizeof ys/sizeof ys[0];k++) {
    int y=ys[k],up=ay-y,dx=(184-ax)*up/(ay-HOME_PULL_CENTER_Y);
    sample(&s, true, ax + dx, y);
    assert(s.edge && home_blob_shown(&s) && s.page == SETTINGS);
    const home_blob *b = &s.blob;
    assert(b->cx == 184.f && b->cy == 224.f && b->w == b->h && b->n == 2.f);
    assert(b->w >= prev.w && b->h >= prev.h && b->alpha >= prev.alpha && area(b) >= prev_area);
    prev = *b; prev_area = area(b);
  }
  assert(s.blob.w == HOME_REVEAL_DIAMETER && s.blob.h == HOME_REVEAL_DIAMETER);
  home_ui still = s; sample(&s, true, 184, 224); assert(home_visual_equal(&still, &s));
  sample(&s, true, ax + 20, 320); home_blob reverse = s.blob;
  home_ui q = ready(SETTINGS); sample(&q, true, ax, ay); at(&q, 140000, true, ax + 20, 320);
  assert(!memcmp(&reverse, &q.blob, sizeof reverse));
  sample(&s, true, 184, 224); lift(&s);
  at(&s, 2000, false, 0, 0); assert(s.page == HOME && !home_blob_shown(&s));
  puts("E held: monotonic centre-normalised geometry/opacity, reverse, stationary hold");
}

/* ---- F. Partial commit: continuous expansion to corner cover within 240 ms. ---- */
static void drop(void) {
  home_ui s = ready(HELPER);
  hold_path(&s, 184, 440, 184, 416, 2, 10000, 0);   /* 24 px at 1.2 px/ms: flick */
  home_blob held_blob = s.blob;
  lift(&s);
  assert(s.page == HOME && s.slide_kind == HOME_MOTION_OUT && s.slide_page == HELPER && home_blob_shown(&s));
  assert(!memcmp(&s.blob, &held_blob, sizeof held_blob));
  int64_t r = t;
  float prev_w = s.blob.w, prev_h = s.blob.h; int frames = 0;
  while (s.slide_kind == HOME_MOTION_OUT) {
    at(&s, 5000, false, 0, 0);
    if (s.slide_kind != HOME_MOTION_OUT) break;
    assert(s.page == HOME);
    assert(s.blob.w >= prev_w && s.blob.h >= prev_h);
    assert(s.blob.w <= HOME_REVEAL_DIAMETER && s.blob.h <= HOME_REVEAL_DIAMETER);
    assert(s.blob.cx == 184.f && s.blob.cy == 224.f && s.blob.n == 2.f);
    prev_w = s.blob.w; prev_h = s.blob.h; frames++;
  }
  int dur = (int)((t - r) / 1000);
  assert(dur == HOME_EXIT_FINISH_MS && frames > 20);
  assert(s.page == HOME && s.slide_kind == HOME_MOTION_NONE && !home_blob_shown(&s) && home_layer_rest(&s));
  assert(s.note.done && s.note.done_home && s.note.done_ms == HOME_EXIT_FINISH_MS);
  home_ui e = ready(SETTINGS); path(&e, 184, 440, 184, 224, 18, 10000, 0);
  at(&e, 2000, false, 0, 0); assert(e.page == HOME && e.slide_kind == HOME_MOTION_NONE);  /* cover edge is off-panel */
  /* any new touch during the finish is Home's: the reveal ends and the carousel takes it */
  home_ui h = ready(SPARKLES); h.tile = 0; path(&h, 184, 440, 184, 416, 2, 10000, 0);
  at(&h, 50000, false, 0, 0); assert(h.slide_kind == HOME_MOTION_OUT);
  hold_path(&h, 300, 200, 150, 200, 10, 10000, 0);
  assert(h.page == HOME && !home_blob_shown(&h) && h.drag_offset == -150);
  lift(&h); idle(&h, 400); assert(h.page == HOME && h.tile == 1);
  h = ready(SENSORS); path(&h, 184, 440, 184, 416, 2, 10000, 0); at(&h, 50000, false, 0, 0);
  h.tile = 3; tap(&h, 184, 220); assert(h.page == SETTINGS);  /* a tap during the drop opens a tile */
  puts("F commit: partial flick expands the centered reveal in 240 ms; a new touch belongs Home");
}

/* ---- G. Cancel: <= 220 ms, bounded contraction to zero, then the outgoing page itself. ---- */
static void spring(void) {
  home_ui s = ready(SETTINGS); hold_path(&s, 184, 440, 184, 390, 50, 10000, 10); lift(&s);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK && home_blob_shown(&s));
  assert(s.note.gesture && !s.note.home && s.note.offset == 50);
  int64_t r = t; float previous = s.blob.w; bool contracting = false;
  while (s.slide_kind == HOME_MOTION_BACK) {
    at(&s, 1000, false, 0, 0);
    if (s.slide_kind != HOME_MOTION_BACK) break;
    assert(s.page == SETTINGS && s.blob.w >= 0.f && s.blob.w <= HOME_REVEAL_DIAMETER);
    assert(s.blob.w == s.blob.h && s.blob.cx == 184.f && s.blob.cy == 224.f && s.blob.n == 2.f);
    if(s.blob.w<previous)contracting=true;
    if(contracting)assert(s.blob.w<=previous+.01f);
    previous=s.blob.w;
  }
  int dur = (int)((t - r) / 1000);
  printf("circle cancel from 50 px: %d ms\n", dur);
  assert(dur <= 220 && contracting);
  assert(s.page == SETTINGS && home_layer_rest(&s) && s.note.done && !s.note.done_home);
  /* the longest pull still springs back within 220 ms */
  s = ready(SETTINGS); hold_path(&s, 184, 440, 184, 250, 30, 20000, 10); s.consumed = true; lift(&s); r = t;
  while (s.slide_kind == HOME_MOTION_BACK) at(&s, 1000, false, 0, 0);
  assert((t - r) / 1000 <= 220 && s.page == SETTINGS);
  puts("G cancel: <= 220 ms, bounded contraction to exact outgoing page");
}

/* ---- H. Same moment, same blob: drop and spring back under irregular sampling. ---- */
static void elapsed(void) {
  static const int gaps[] = {3, 17, 1, 41, 7, 29, 2, 13, 55, 11, 5, 23, 37};
  for (int kind = 0; kind < 2; kind++) {
    home_ui s = ready(SETTINGS);
    if (kind == 0) path(&s, 184, 440, 184, 400, 40, 10000, 10);
    else path(&s, 184, 440, 184, 416, 2, 10000, 0);
    assert(s.slide_kind == (kind == 0 ? HOME_MOTION_BACK : HOME_MOTION_OUT));
    home_ui base = s, b = s; int64_t t0 = t, tb = t0;
    for (int k = 0; k < 30; k++) {
      tb += gaps[k % 13] * 1000; home_sample(&b, tb, false, 0, 0);
      home_ui c = base; home_sample(&c, tb, false, 0, 0);
      home_ui q = base; for (int64_t p = t0 + 10000; p < tb; p += 10000) home_sample(&q, p, false, 0, 0);
      home_sample(&q, tb, false, 0, 0);
      assert(b.slide_kind == c.slide_kind && q.slide_kind == c.slide_kind && b.page == c.page);
      if (home_blob_shown(&c)) assert(!memcmp(&b.blob, &c.blob, sizeof c.blob) && !memcmp(&q.blob, &c.blob, sizeof c.blob));
    }
  }
  puts("H restore/finish positions depend on elapsed time only");
}

/* ---- I. Log notes and the USB WGS1 replays. ---- */
static inline void replay(home_ui *s, unsigned g) { int64_t c = t + 1; gesture_replay(s, g, &c); t = c; }
static void notes_wgs1(void) {
  home_ui s = ready(SETTINGS);
  sample(&s, true, 184, 440); sample(&s, true, 184, 430); assert(!s.note.pull);
  sample(&s, true, 184, 420); assert(s.note.pull && s.note.wait_ms == 20);
  memset(&s.note, 0, sizeof s.note);
  for (int k = 1; k <= 20; k++) sample(&s, true, 184, 420 - 10 * k);
  lift(&s);
  assert(s.note.gesture && s.note.home && s.note.offset == 220 && s.note.speed_milli >= 950 && s.note.speed_milli <= 1050);
  assert(s.note.drag_ms == 210 && s.note.anim_ms <= 1);
  memset(&s.note, 0, sizeof s.note); idle(&s, 200);
  assert(!s.note.gesture && s.note.done && s.note.done_home);  /* its reveal edge was already off-panel */
  s = ready(SETTINGS); path(&s, 184, 440, 184, 400, 40, 10000, 10);
  assert(s.note.gesture && !s.note.home && s.note.offset == 40 && s.note.speed_milli == 0 && s.note.anim_ms >= 120 && s.note.anim_ms <= 220);
  int back_ms = s.note.anim_ms; memset(&s.note, 0, sizeof s.note); idle(&s, 400);
  assert(s.note.done && !s.note.done_home && s.note.done_ms == back_ms);
  s = ready(SETTINGS); tap(&s, 184, 440);
  assert(!s.note.gesture && !s.note.pull);                    /* reserved bottom tap: no Home note */
  /* WGS1 3 = the pull from every page (Ask with its chat kept, nothing painted);  */
  for (int k = 0; k < PAGES; k++) {
    home_ui w = k == 1 ? with_chat() : ready(pages[k]);
    unsigned used = w.helper.log.used;
    replay(&w, GESTURE_HOME);
    assert(w.page == HOME && w.note.pull && w.note.gesture && w.note.home && w.note.anim_ms <= 1);
    at(&w,2000,false,0,0);assert(w.slide_kind==HOME_MOTION_NONE);
    idle(&w, 200);
    assert(w.page == HOME && home_layer_rest(&w) && w.helper.log.used == used);
    assert(w.input.scene.strength == 0 && w.input.scene.trail_count == 0);
  }
  /* WGS1 5 unchanged: Home tile swipes + tap reopen Ask with the same chat */
  for (int from = 0; from < HOME_TILES; from++) {
    home_ui q = with_chat(); q.page = HOME; q.tile = from;
    replay(&q, GESTURE_OPEN_ASK); idle(&q, 400);
    assert(q.page == HELPER && q.tile == 1 && q.helper.cont && q.helper.chat);
  }
  /* WGS1 1 (page-owned pull-up from y 300) and 15 (top pull) are still new chats */
  home_ui q = with_chat(); replay(&q, GESTURE_NEW); idle(&q, 100); assert(q.page == HELPER && new_chat(&q));
  q = with_chat(); replay(&q, GESTURE_TOP_NEW); idle(&q, 100); assert(q.page == HELPER && new_chat(&q));
  puts("I notes: HOME_PULL start wait_ms, HOME_GESTURE offset/speed/drag_ms/anim_ms, HOME_PULL end ms; WGS1 3/5/1/15");
}
#endif

int main(int argc, char **argv) {
  static const struct { const char *name; void (*run)(void); } sections[] = {
    {"A", zone}, {"C", conflicts_ask}, {"CS", conflicts_sparkles}, {"CT", conflicts_settings_sensor}, {"D", thresholds},
#ifndef PULL_BASE_ONLY
    {"B", classification}, {"E", held}, {"F", drop}, {"G", spring}, {"H", elapsed}, {"I", notes_wgs1},
#endif
  };
  int ran = 0;
  for (size_t k = 0; k < sizeof sections / sizeof sections[0]; k++) {
    if (argc > 1 && strcmp(argv[1], sections[k].name)) continue;
    sections[k].run(); ran++;
  }
  assert(ran > 0);
  printf("home pull: %d sections PASS\n", ran);
  return 0;
}
