/* Kotaro's motion on the Ask page (SPEC3 Contract K), through the real poller (home_sample ->
 * helper_tick / helper_touch) and the real pose (helper_bot_pose -> bot_animate -> bot_art_draw):
 *  - idle routines (stretch, yawn, scratch, sniff, chase his tail): one every 6..12 s of calm, a
 *    shuffled order from a PRNG seeded by the clock (deterministic: the same clock gives the same
 *    show), every routine within 100 s, never during listening / thinking / working / a sad or
 *    stopped face / loading / disabled, never while a finger is on the page, after a reply only once
 *    the happy wag has calmed down;
 *  - nap: 2 minutes with no touch and no activity -> he lies down (eyes shut, slow breathing, Zz); any
 *    touch or activity wakes him with a stretch; coming back to the page he is simply awake;
 *  - celebrate: a reply arriving (not a reply restored by a bot swipe) -> a jump with a full spin;
 *  - tap: a quick tap on him -> ears perk, a bounce, a heart; beside him -> nothing;
 *  - hold: the press arming -> he leans in before listening; a sent command -> a nod (a cancelled
 *    short hold -> no nod);
 *  - smooth: over a whole session (all of the above, mood switches, loop-free idle) his body never
 *    jumps more than a cell between 20 fps frames (head two), no one-frame pops, and he only ever
 *    turns round through squashed turn frames (never an instant mirror flip);
 *  - all timing is real elapsed time (a 7/13 ms poller gives the same nap and celebration). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"

static int fails;
#define CHECK(cond, ...)                                       \
  do {                                                         \
    if (!(cond)) {                                             \
      static int shown_;                                       \
      if (shown_++ < 3) fprintf(stderr, "FAIL: " __VA_ARGS__); \
      fails++;                                                 \
    }                                                          \
  } while (0)

static int64_t t;
static int tick_ms = 10;
static void sample(home_ui *s, bool down, int x, int y) {
  t += tick_ms * 1000;
  home_sample(s, t, down, x, y);
}
static void idle(home_ui *s, float seconds) {
  for (int i = 0; i < (int)(seconds * 1000 / tick_ms); i++) sample(s, false, 0, 0);
}
static void finger_tap(home_ui *s, int x, int y) {
  sample(s, true, x, y); sample(s, true, x, y); sample(s, false, 0, 0);
}
static home_ui runnable(void) {
  home_ui s;
  memset(&s, 0, sizeof s);
  s.page = HELPER;
  s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; s.pair.live_ok = true;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED; s.phone.st.flags = PHONE_FLAG_REQUIRED;
  s.bots.data = (bots_data){.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", "atlas", "coding"};
  for (int i = 0; i < 3; i++) {
    strcpy(s.bots.data.b[i].id, ids[i]); strcpy(s.bots.data.b[i].name, ids[i]);
    s.bots.data.b[i].available = true; s.bots.data.b[i].reset_s = BOTS_NONE32;
  }
  return s;
}
static void keep_fresh(home_ui *s) { s->bots.data.received_us = t; s->live_now_us = t; }
static voice_command cmd(unsigned status, const char *reply) {
  voice_command c = {.status = status};
  strcpy(c.id, "0123456789abcdef0123456789abcdef");
  snprintf(c.text, sizeof c.text, "%s", reply);
  snprintf(c.transcript, sizeof c.transcript, "what is up");
  return c;
}
static bot_motion motion(const home_ui *s) {
  bot_pose b = helper_bot_pose(&s->helper, BOT_LOOK_HELPER);
  return bot_animate(&b);
}
static uint16_t fr[SPARKLES_PIXELS];

/* ---- 1. routines ------------------------------------------------------------------------------- */
typedef struct { int kind; float start, end; } rt_seen;
static int watch_routines(home_ui *s, float seconds, rt_seen *out, int cap) {
  int n = 0, cur = s->helper.kot.routine;  /* one already running is not a new one */
  float now = 0;
  for (int i = 0; i < (int)(seconds * 1000 / tick_ms); i++) {
    sample(s, false, 0, 0);
    keep_fresh(s);
    now += tick_ms / 1000.f;
    int r = s->helper.kot.routine;
    if (r && !cur && n < cap) out[n++] = (rt_seen){r, now, 0};
    if (!r && cur && n) out[n - 1].end = now;
    cur = r;
  }
  return n;
}
static void routines(void) {
  rt_seen a[64], b[64];
  t = 5000000;
  home_ui s = runnable();
  int n = watch_routines(&s, 100.f, a, 64);
  unsigned seen = 0;
  for (int i = 0; i < n; i++) seen |= 1u << a[i].kind;
  printf("routines in the first 100 s of calm:");
  for (int i = 0; i < n; i++) printf(" %d@%.1f", a[i].kind, a[i].start);
  printf("\n");
  CHECK(seen == 0x3e, "not every routine within 100 s (seen mask %x)\n", seen);
  CHECK(n >= 5 && a[0].start >= BOT_ROUTINE_SETTLE_S + BOT_ROUTINE_GAP_MIN - .05f && a[0].start <= BOT_ROUTINE_SETTLE_S + BOT_ROUTINE_GAP_MAX + .05f,
        "first routine at %.2f s\n", n ? a[0].start : -1.f);
  for (int i = 1; i < n; i++) {
    float gap = a[i].start - a[i - 1].end;
    CHECK(gap >= BOT_ROUTINE_GAP_MIN - .05f && gap <= BOT_ROUTINE_GAP_MAX + .05f, "gap %.2f s before routine %d\n", gap, i);
    CHECK(a[i].kind != a[i - 1].kind, "the same routine twice in a row (%d)\n", a[i].kind);
    CHECK(fabsf(a[i - 1].end - a[i - 1].start - bot_routine_len[a[i - 1].kind]) < .05f, "routine %d cut short\n", a[i - 1].kind);
  }
  /* deterministic: the same clock, the same show */
  t = 5000000;
  home_ui s2 = runnable();
  int n2 = watch_routines(&s2, 100.f, b, 64);
  CHECK(n2 == n, "a second run differs (%d vs %d routines)\n", n2, n);
  for (int i = 0; i < n && i < n2; i++) CHECK(a[i].kind == b[i].kind && fabsf(a[i].start - b[i].start) < .001f, "a second run differs at %d\n", i);
  /* random-looking: three rounds of five (kept awake by a tap beside him every 50 s) are not one fixed loop */
  t = 9000000;
  s = runnable();
  int kinds[64], k = 0;
  for (int round = 0; round < 8 && k < 15; round++) {
    int m = watch_routines(&s, 50.f, a, 64);
    for (int i = 0; i < m && k < 64; i++) kinds[k++] = a[i].kind;
    finger_tap(&s, 40, 200);
  }
  bool loop = k >= 15;
  for (int i = 5; i < 15 && i < k; i++) loop &= kinds[i] == kinds[i - 5];
  printf("15 routines in a row:");
  for (int i = 0; i < k && i < 15; i++) printf(" %d", kinds[i]);
  printf("\n");
  CHECK(k >= 15 && !loop, "the routines repeat one fixed order\n");
  /* never outside calm idle, never while a finger is on the page */
  static const helper_state busy[] = {HV_LISTENING, HV_TRANSCRIBING, HV_RUNNING, HV_STOPPING, HV_ERROR, HV_INTERRUPTED};
  for (size_t i = 0; i < sizeof busy / sizeof *busy; i++) {
    s = runnable();
    s.helper.state = busy[i];
    if (busy[i] == HV_RUNNING || busy[i] == HV_TRANSCRIBING) strcpy(s.helper.id, "0123456789abcdef0123456789abcdef");
    s.helper.chat = true;
    int m = 0;
    for (int j = 0; j < 4000; j++) {
      if (busy[i] == HV_LISTENING) s.helper.press_us = t;  /* (no auto-send) */
      sample(&s, false, 0, 0); keep_fresh(&s);
      m += s.helper.kot.routine != 0;
    }
    CHECK(!m, "a routine during state %d\n", busy[i]);
  }
  s = runnable();
  for (int j = 0; j < 3000; j++) { sample(&s, true, 40, 200); keep_fresh(&s); CHECK(!s.helper.kot.routine, "a routine while touching\n"); }
  s = runnable();
  s.helper.block = BR_EXHAUSTED;
  for (int j = 0; j < 3000; j++) { sample(&s, false, 0, 0); s.helper.block = BR_EXHAUSTED; CHECK(!s.helper.kot.routine, "a routine while disabled\n"); }
  /* after a reply: the happy wag first, routines once it has calmed down */
  s = runnable();
  s.helper.state = HV_DONE; s.helper.chat = true;
  float first = -1;
  for (int j = 0; j < 3000 && first < 0; j++) { sample(&s, false, 0, 0); keep_fresh(&s); if (s.helper.kot.routine) first = j * tick_ms / 1000.f; }
  CHECK(first >= BOT_CALM_S, "a routine %.2f s into the happy wag\n", first);
  printf("routines: every one within 100 s, gaps 6..12 s, shuffled, deterministic, only in calm idle\n");
}

/* ---- 2. nap ------------------------------------------------------------------------------------ */
static void nap(void) {
  t = 20000000;
  home_ui s = runnable();
  idle(&s, BOT_NAP_S - .2f);
  bot_motion m = motion(&s);
  CHECK(m.lie == 0 && helper_nap_depth(s.helper.kot.idle_s) == 0, "lying down before 2 minutes\n");
  idle(&s, BOT_LIE_S + .6f);
  m = motion(&s);
  CHECK(m.lie >= .999f && m.eyes == BOT_EYE_ASLEEP && m.zzz > 0, "not asleep after 2 minutes (lie %.2f eyes %d)\n", m.lie, m.eyes);
  /* slow breathing: his flank rises and falls every few seconds */
  int ups = 0, downs = 0, flips = 0;
  float last = m.by;
  for (int i = 0; i < 600; i++) {
    sample(&s, false, 0, 0);
    m = motion(&s);
    ups += m.by < 0; downs += m.by == 0;
    flips += m.by != last;
    last = m.by;
  }
  CHECK(ups > 50 && downs > 50 && flips >= 2 && flips <= 6, "breathing: %d up, %d down, %d changes in 6 s\n", ups, downs, flips);
  /* the drawing: eyes shut, Zz, inside the box */
  bot_pose b = helper_bot_pose(&s.helper, BOT_LOOK_CODING);
  for (int i = 0; i < SPARKLES_PIXELS; i++) fr[i] = 0x0841;
  bot_art_draw(fr, &b);
  CHECK(!bot_grid_get()->clipped, "napping Kotaro is cut off\n");
  /* any touch wakes him with a stretch (no tap heart) */
  finger_tap(&s, 40, 200);
  CHECK(s.helper.kot.idle_s < .1f && s.helper.kot.wake_s > 0 && s.helper.kot.wake_from > .99f && !s.helper.kot.tap_s, "a touch does not wake him\n");
  float max_bow = 0, lie0 = motion(&s).lie;
  for (int i = 0; i < (int)(BOT_WAKE_S * 100) + 10; i++) {
    sample(&s, false, 0, 0);
    m = motion(&s);
    if (m.bow > max_bow) max_bow = m.bow;
  }
  CHECK(lie0 > .9f && max_bow > .8f && m.lie == 0 && !s.helper.kot.wake_s, "no wake stretch (bow %.2f, lie %.2f)\n", max_bow, m.lie);
  /* a tap on him while asleep wakes him too (no heart) */
  idle(&s, BOT_NAP_S + BOT_LIE_S + .5f);
  finger_tap(&s, HELPER_BOT_X, HELPER_BOT_Y);
  CHECK(s.helper.kot.wake_s > 0 && !s.helper.kot.tap_s, "a tap on the sleeping dog: wake %.2f tap %.2f\n", s.helper.kot.wake_s, s.helper.kot.tap_s);
  /* activity wakes him: a command arriving over USB */
  idle(&s, BOT_NAP_S + BOT_LIE_S + .5f);
  CHECK(motion(&s).lie > .99f, "not asleep again\n");
  helper_command(&s.helper, t, HELPER_CMD_PRESS);
  sample(&s, false, 0, 0);
  CHECK(s.helper.kot.idle_s == 0 && s.helper.kot.wake_s > 0, "activity does not wake him\n");
  /* away from the page and back: simply awake */
  s = runnable();
  idle(&s, BOT_NAP_S + BOT_LIE_S + .5f);
  t += 30LL * 1000 * 1000;
  sample(&s, false, 0, 0);
  CHECK(s.helper.kot.idle_s < .1f && !s.helper.kot.wake_s && motion(&s).lie == 0, "back on the page he is still asleep\n");
  printf("nap: lies down after 2 minutes, slow breaths and Zz, a touch or activity wakes him with a stretch\n");
}

/* ---- 3. celebrate, tap, hold, nod ----------------------------------------------------------------- */
static void reactions(void) {
  t = 40000000;
  home_ui s = runnable();
  idle(&s, 1);
  s.helper.chat = true; s.helper.state = HV_RUNNING; strcpy(s.helper.id, "0123456789abcdef0123456789abcdef");
  idle(&s, 1);
  voice_command c = cmd(VOICE_DONE, "All done.");
  helper_apply(&s.helper, &c);
  sample(&s, false, 0, 0);
  CHECK(s.helper.kot.cheer_s > 0, "no celebration when the reply arrives\n");
  float max_jump = 0, turned = 0, prev = 0;
  bool mirrored = false, squashed = false;
  for (int i = 0; i < (int)(BOT_CHEER_S * 100) + 5; i++) {
    sample(&s, false, 0, 0);
    bot_motion m = motion(&s);
    if (m.jump > max_jump) max_jump = m.jump;
    if (i) turned += fabsf(m.turn - prev);
    prev = m.turn;
    mirrored |= bot_mirrored(&m);
    squashed |= bot_turn_px(&m, 2) < 2;
  }
  CHECK(max_jump >= 2.5f && turned > 6.f && mirrored && squashed, "celebrate: jump %.2f cells, turn %.2f rad\n", max_jump, turned);
  CHECK(!s.helper.kot.cheer_s && helper_bot_mood(&s.helper) == BOT_HAPPY, "celebration does not end in the happy wag\n");
  /* a reply restored by swiping bots does not celebrate */
  helper_memo_save(&s.helper);
  s.helper.state = HV_IDLE;
  idle(&s, .5f);
  s.helper.memo[1].state = HV_DONE;
  helper_select(&s.helper, 1, 0);
  idle(&s, .1f);
  CHECK(s.helper.state == HV_DONE && !s.helper.kot.cheer_s, "a restored reply celebrates\n");
  /* tap: ears perk, a bounce, a heart */
  t = 50000000;
  s = runnable();
  idle(&s, 2);
  finger_tap(&s, HELPER_BOT_X, HELPER_BOT_Y);
  CHECK(s.helper.kot.tap_s > 0 && s.helper.state == HV_IDLE && !s.helper.want_record, "a tap on him: no reaction\n");
  float bounce = 0, ear = 0;
  int heart = 0;
  bot_colors col = bot_colors_of(&(bot_pose){.look = BOT_LOOK_HELPER});
  for (int i = 0; i < 40; i++) {
    sample(&s, false, 0, 0);
    bot_motion m = motion(&s);
    if (m.jump > bounce) bounce = m.jump;
    if (m.ear_n < ear) ear = m.ear_n;
    if (i == 20) {
      bot_pose b = helper_bot_pose(&s.helper, BOT_LOOK_HELPER);
      for (int k = 0; k < SPARKLES_PIXELS; k++) fr[k] = 0x0841;
      bot_art_draw(fr, &b);
      for (int k = 0; k < SPARKLES_PIXELS; k++) heart += fr[k] == col.heart;
    }
  }
  CHECK(bounce >= .8f && ear <= -2.f && heart > 40, "tap: bounce %.2f, ears %.1f, heart %d px\n", bounce, ear, heart);
  idle(&s, 1);
  CHECK(!s.helper.kot.tap_s, "the tap reaction does not end\n");
  finger_tap(&s, 40, 200);
  CHECK(!s.helper.kot.tap_s, "a tap beside him makes him react\n");
  /* hold: he leans in while the press arms, then listens; release (sent) = a nod */
  s = runnable();
  idle(&s, 2);
  bot_motion rest = motion(&s);
  float lean_at_arm = 0;
  for (int i = 0; i < 38; i++) {
    sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
    if (i == 30) lean_at_arm = s.helper.kot.lean;
  }
  bot_motion lean = motion(&s);
  CHECK(s.helper.armed && s.helper.state == HV_IDLE && lean_at_arm > .99f, "no lean-in while arming (%.2f)\n", lean_at_arm);
  CHECK(lean.ear_n <= -1.5f && (lean.hx != rest.hx || lean.hy != rest.hy) && lean.eyes == BOT_EYE_OPEN, "the lean-in looks like rest\n");
  for (int i = 0; i < 60; i++) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  CHECK(s.helper.state == HV_LISTENING && s.helper.kot.lean < .01f && helper_bot_mood(&s.helper) == BOT_LISTEN, "listening: lean %.2f\n", s.helper.kot.lean);
  for (int i = 0; i < 60; i++) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  sample(&s, false, 0, 0);
  CHECK(s.helper.state == HV_TRANSCRIBING && s.helper.kot.nod_s > 0, "no nod after sending\n");
  float nod = 0;
  for (int i = 0; i < 60; i++) {
    sample(&s, false, 0, 0);
    bot_motion m = motion(&s);
    bot_pose b = helper_bot_pose(&s.helper, BOT_LOOK_HELPER);
    b.nod_s = 0;
    bot_motion q = bot_animate(&b);
    if (m.hy - q.hy > nod) nod = m.hy - q.hy;
  }
  CHECK(nod >= 1.f && !s.helper.kot.nod_s, "the nod: %.2f cells\n", nod);
  /* a cancelled short hold: no nod */
  s = runnable();
  idle(&s, 1);
  for (int i = 0; i < 50; i++) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  sample(&s, false, 0, 0);
  CHECK(s.helper.state == HV_IDLE && !s.helper.kot.nod_s, "a cancelled hold nods\n");
  printf("reactions: celebrate (jump %.1f cells + spin) on a reply, tap = bounce + ears + heart, hold = lean-in, send = nod\n", max_jump);
}

/* ---- 4. smooth over a whole session --------------------------------------------------------------- */
typedef struct { int body_x, body_y, head_x, head_y, cw; bool mirror; } frame_pos;
static frame_pos where(const home_ui *s) {
  bot_pose b = helper_bot_pose(&s->helper, BOT_LOOK_HELPER);
  bot_motion m = bot_animate(&b);
  int cp = s->helper.chat ? 2 : 4;
  bot_body B;
  bot_body_at(&B, m.bow, m.lie);
  int bx, by, hx, hy, tuck;
  bot_offsets(&m, &B, &bx, &by, &hx, &hy, &tuck);
  int lift = bot_ri(m.jump * cp);
  return (frame_pos){bx * cp, by * cp - lift, hx * cp, hy * cp - lift, bot_turn_px(&m, cp), bot_mirrored(&m)};
}
static int frames_checked, pops, turns_seen;
static frame_pos hist[3];
static int nhist;
static void frame(const home_ui *s, const char *phase) {
  frame_pos p = where(s);
  int cp = s->helper.chat ? 2 : 4;
  if (nhist) {
    frame_pos q = hist[(nhist - 1) % 3];
    if (p.cw == cp && q.cw == cp && q.mirror == p.mirror) {
      CHECK(abs(p.body_x - q.body_x) <= cp && abs(p.body_y - q.body_y) <= cp + 1, "%s: body jumps %d,%d px in one frame\n", phase, p.body_x - q.body_x, p.body_y - q.body_y);
      CHECK(abs(p.head_x - q.head_x) <= 2 * cp && abs(p.head_y - q.head_y) <= 2 * cp + 1, "%s: head jumps %d,%d px in one frame (routine %d at %.2f s, nap %.2f, wake %.2f)\n", phase,
            p.head_x - q.head_x, p.head_y - q.head_y, s->helper.kot.routine, s->helper.kot.routine_s, helper_nap_depth(s->helper.kot.idle_s), s->helper.kot.wake_s);
    }
    /* he turns round only through a squashed turn frame */
    if (p.mirror != q.mirror) {
      turns_seen++;
      CHECK(p.cw < cp || q.cw < cp, "%s: an instant mirror flip\n", phase);
    }
    if (nhist >= 2) {  /* a one-frame pop: up and straight back by a cell or more */
      frame_pos r = hist[(nhist - 2) % 3];
      int a = q.body_y - r.body_y, b = p.body_y - q.body_y;
      if (abs(a) >= cp && abs(b) >= cp && (a > 0) != (b > 0) && p.cw == cp && q.cw == cp && r.cw == cp) {
        pops++;
        CHECK(0, "%s: a one-frame pop (%d then %d px; routine %d at %.2f s, idle %.2f s, orbs t %.2f)\n", phase, a, b, s->helper.kot.routine, s->helper.kot.routine_s,
              s->helper.kot.idle_s, s->helper.orbs.time);
      }
    }
  }
  hist[nhist % 3] = p;
  nhist++;
  frames_checked++;
  /* now and then the real drawing: inside the box, nothing off the grid */
  if (frames_checked % 7 == 0) {
    bot_pose b = helper_bot_pose(&s->helper, frames_checked % 2 ? BOT_LOOK_CODING : BOT_LOOK_HELPER);
    for (int k = 0; k < SPARKLES_PIXELS; k++) fr[k] = 0x0841;
    bot_art_draw(fr, &b);
    helper_box bx = helper_bot_box(&s->helper);
    int out = 0;
    for (int y = 0; y < 448; y++)
      for (int x = 0; x < 368; x++) out += fr[y * 368 + x] != 0x0841 && (x < bx.x0 || x >= bx.x1 || y < bx.y0 || y >= bx.y1);
    CHECK(!out && !bot_grid_get()->clipped, "%s: %d px outside the box\n", phase, out);
  }
}
/* Run the poller for `seconds` with a finger (or not), checking a frame every 50 ms (20 fps). */
static void play(home_ui *s, float seconds, bool down, int x, int y, const char *phase) {
  int n = (int)(seconds * 1000 / tick_ms), per = 50 / tick_ms;
  for (int i = 0; i < n; i++) {
    sample(s, down, x, y);
    keep_fresh(s);
    if (s->helper.state == HV_LISTENING && !s->helper.holding) s->helper.press_us = t;
    if (i % per == 0) frame(s, phase);
  }
}
static void session(void) {
  t = 70000000;
  frames_checked = pops = turns_seen = nhist = 0;
  home_ui s = runnable();
  play(&s, 40, false, 0, 0, "idle routines");
  finger_tap(&s, HELPER_BOT_X, HELPER_BOT_Y);
  play(&s, 1, false, 0, 0, "tap");
  play(&s, 2.2f, true, HELPER_BOT_X, HELPER_BOT_Y, "arm + listen");
  sample(&s, false, 0, 0);
  play(&s, 1, false, 0, 0, "sent: nod, thinking");
  voice_command c = cmd(VOICE_RUNNING, "");
  helper_apply(&s.helper, &c);
  play(&s, 3, false, 0, 0, "working");
  c = cmd(VOICE_DONE, "Here you go: a short answer.");
  helper_apply(&s.helper, &c);
  play(&s, 30, false, 0, 0, "celebrate, wag, calm");
  play(&s, 1.5f, true, HELPER_BOT_X, HELPER_BOT_Y, "listen again");
  sample(&s, false, 0, 0);
  c = cmd(VOICE_ERROR, "Hermes offline");
  play(&s, .5f, false, 0, 0, "thinking");
  helper_apply(&s.helper, &c);
  play(&s, 5, false, 0, 0, "sad");
  s.helper.state = HV_IDLE; s.helper.chat = false;
  play(&s, BOT_NAP_S + 8, false, 0, 0, "to the nap");
  play(&s, .2f, true, 40, 200, "woken");
  play(&s, 4, false, 0, 0, "wake stretch");
  s.helper.block = BR_LOADING;
  play(&s, 2, false, 0, 0, "loading");
  s.helper.block = 0;
  play(&s, 12, false, 0, 0, "idle again");
  printf("smooth: %d frames at 20 fps over a whole session, no body jump > 1 cell, no pops, %d mirror changes all through turn frames\n", frames_checked, turns_seen);
  CHECK(turns_seen >= 2, "no turns seen in the session (%d)\n", turns_seen);
}

/* ---- 5. real elapsed time --------------------------------------------------------------------------- */
static float nap_at(int ms_a, int ms_b) {
  t = 90000000;
  home_ui s = runnable();
  float now = 0;
  for (int i = 0; now < BOT_NAP_S + 5; i++) {
    tick_ms = i % 2 ? ms_a : ms_b;
    sample(&s, false, 0, 0);
    now += tick_ms / 1000.f;
    if (helper_nap_depth(s.helper.kot.idle_s) > 0) break;
  }
  tick_ms = 10;
  return now;
}
static void elapsed_time(void) {
  float a = nap_at(10, 10), b = nap_at(7, 13), c = nap_at(30, 3);
  printf("nap starts at %.2f s (10 ms poller), %.2f s (7/13 ms), %.2f s (30/3 ms)\n", a, b, c);
  CHECK(fabsf(a - b) < .05f && fabsf(a - c) < .05f && fabsf(a - BOT_NAP_S) < .05f, "the nap follows the tick rate, not the clock\n");
}

int main(void) {
  routines();
  nap();
  reactions();
  session();
  elapsed_time();
  if (fails) {
    fprintf(stderr, "kotaro_motion: %d failures\n", fails);
    return 1;
  }
  puts("kotaro_motion: routines, nap, celebrate, tap, lean-in, nod, smooth motion through real elapsed time: PASS");
  return 0;
}
