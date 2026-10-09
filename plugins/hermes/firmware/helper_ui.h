#pragma once
/* Voice page state machine (hold-to-talk -> the selected Hermes bot/profile; swipe left/right
 * between bots). Pure C, host-tested in tests/host/helper_ui_test.c and tests/host/bots_ui_test.c. The 100 Hz touch poller drives it under the owner's snapshot mutex;
 * the helper_voice worker consumes the want_* requests and feeds results back through
 * helper_apply()/helper_fail(). No audio, network or storage here. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "orbs.h"
#include "voice_wire.h"

typedef enum { HV_IDLE, HV_LISTENING, HV_TRANSCRIBING, HV_RUNNING, HV_STOPPING, HV_DONE, HV_ERROR, HV_INTERRUPTED } helper_state;

/* The bot graphic IS the hold-to-talk control (no mic button): each bot is a small character drawn by
 * bot_art.h, big and centred on an empty chat, compact on the bottom row in the chat layout. Geometry in
 * units of the big drawing (1 unit = 1 px at scale 1), around the body centre:
 *   body BOT_BODY_W x BOT_BODY_H; ears, stop badge and the working orbit stay within +-BOT_HALF_W;
 *   the antenna emblem (and its glow) reaches BOT_TOP above; the soft shadow / listening pulse
 *   BOT_BOTTOM below (BOT_BOTTOM_COMPACT in the chat layout, which draws no shadow). */
#define BOT_BODY_W 124
#define BOT_BODY_H 108
#define BOT_BODY_R 42
#define BOT_HALF_W 82
#define BOT_TOP 108               /* big: room above his head for a jump (celebrate, tap bounce) */
#define BOT_TOP_COMPACT 94        /* chat layout: the status row sits right above */
#define BOT_BOTTOM 68
#define BOT_BOTTOM_COMPACT 62
#define HELPER_BOT_X 184
#define HELPER_BOT_Y 208          /* welcome layout: body centre */
#define HELPER_BOT_SCALE 1.1f
#define HELPER_BOT_TEXT_Y 306     /* welcome layout: status / hint / why-disabled text under the bot */
#define HELPER_BOT_SLOP 8         /* fingertip margin around the graphic's box */
#define HELPER_TRANSCRIPT_Y 86
#define HELPER_TRANSCRIPT_LINES 3
#define HELPER_REPLY_Y 330
#define HELPER_REPLY_LINES 5
#define HELPER_COLS 36
/* Chat layout (after the first command): compact bot on the bottom row, bubbles above. */
#define HELPER_CHAT_Y 374                      /* the button row */
#define HELPER_CHAT_BOT_Y 380                  /* compact bot: body centre */
#define HELPER_CHAT_SCALE 0.62f
#define HELPER_STATUS_Y 306                    /* chat layout: the one status/notice row */
#define HELPER_BUBBLE_TOP 20
#define HELPER_BUBBLE_BOTTOM 298
#define HELPER_LINE 18
#define HELPER_USER_COLS 30
#define HELPER_BOT_COLS 34
#define HELPER_WRAP_MAX 64
#define HELPER_WAVE 22
#define HELPER_REVEAL_CPS 70.f
#define HELPER_MIN_US (400LL * 1000)
/* Gesture disambiguation on the Ask page: a press on the bot that stays within HELPER_ARM_SLOP px
 * starts recording after HELPER_ARM_US; horizontal travel > HELPER_SWIPE_PX before release is a
 * bot swipe (never a recording). At the first/last bot a swipe only nudges (rubber band). */
#define HELPER_ARM_US (400LL * 1000)
#define HELPER_ARM_SLOP 12
#define HELPER_SWIPE_PX 40
#define HELPER_NUDGE_PX 44
#define HELPER_BOTS 3
/* Bot pill + page dots at the top of the page; chat bubbles never paint above HELPER_CHAT_CLIP. */
#define HELPER_PILL_Y 10
#define HELPER_PILL_H 28
#define HELPER_DOTS_Y 46
#define HELPER_CHAT_CLIP 60
#define HELPER_MAX_US (15LL * 1000 * 1000)
/* Conversation: earlier turns of the current Hermes session stay on screen (scroll with a vertical
 * drag in the chat). Pulling the newest message up past HELPER_NEW_PX and releasing starts a new
 * session (the next hold sends session.new). The log keeps the newest turns that fit. */
#define HELPER_LOG_MAX 4096
#define HELPER_SCROLL_SLOP 14
#define HELPER_NEW_PX 72
#define HELPER_PULL_MAX 110
/* Top swipe: a drag that starts in the top band (above the chat: the bot pill and dots) and comes down
 * to the centre starts a new session on the current bot (user rule 2026-09-29). */
#define HELPER_TOP_BAND 72
#define HELPER_TOP_NEW_Y 190
enum { HELPER_CMD_PRESS = 1, HELPER_CMD_RELEASE = 2, HELPER_CMD_STOP = 3 };
enum { HELPER_SWIPE_NEXT = 1, HELPER_SWIPE_PREV = 2 };

/* Kotaro's life on the Ask page (bot_art.h draws him; helper_tick advances this from real elapsed
 * time). All zero = sitting at rest (the Home tile's Kotaro has none of it):
 *  - mood easing: the page inputs his mood comes from (state, block, command id) and the ones before
 *    the last change, so the drawing eases from the old mood's pose into the new one (BOT_BLEND_S);
 *  - idle routines (stretch, yawn, scratch, sniff, chase his tail): one every BOT_ROUTINE_GAP_MIN..MAX s
 *    of calm (idle, or a while after a reply), in a shuffled order picked by a PRNG seeded from the
 *    clock; a routine always plays to its end (no pops), and none starts while you touch the page;
 *  - nap: BOT_NAP_S with no touch and no activity on the page and he lies down; any touch or activity
 *    wakes him (a stretch, BOT_WAKE_S);
 *  - reactions: a tap on him (BOT_TAP_S: ears perk, a bounce, a heart), the hold arming (lean 0..1:
 *    he leans in), a command sent (BOT_NOD_S: a nod), a reply arriving (BOT_CHEER_S: jump + spin).
 * Event clocks count seconds since the event, 0 = none. */
#define BOT_BLEND_S .3f
#define BOT_NAP_S 120.f
#define BOT_LIE_S 2.f
#define BOT_WAKE_S 1.6f
#define BOT_TAP_S .8f
#define BOT_NOD_S .5f
#define BOT_CHEER_S 1.6f
#define BOT_CALM_S 5.f            /* after a reply: the happy wag, then idle routines again */
#define BOT_ROUTINE_GAP_MIN 6.f
#define BOT_ROUTINE_GAP_MAX 12.f
#define BOT_ROUTINE_SETTLE_S 1.5f /* a new mood settles this long before a routine */
enum { BOT_RT_NONE, BOT_RT_STRETCH, BOT_RT_YAWN, BOT_RT_SCRATCH, BOT_RT_SNIFF, BOT_RT_CHASE, BOT_RT_N };
static const float bot_routine_len[BOT_RT_N] = {0, 2.4f, 2.f, 2.2f, 3.2f, 2.4f};
typedef struct {
  unsigned char key_state, key_block, from_state, from_block;
  bool key_id, from_id;
  float switch_s, from_t;          /* seconds since the mood inputs changed; the old mood's clock then */
  unsigned char routine, bag, last; /* running routine (BOT_RT_*), routines played this round, the last */
  float routine_s, calm_s, gap_s;  /* its clock; calm seconds so far; calm needed for the next one */
  float idle_s, wake_s, wake_from; /* no touch / activity; waking from a nap that was wake_from deep */
  float tap_s, lean, nod_s, cheer_s;
  float breath_s;                  /* seconds of stillness (his breathing restarts from rest after any move) */
} helper_kotaro;
/* His breath, breath_s seconds after he last held still: in (body up a cell) or out. */
static inline bool helper_breath_up(float breath_s) { return breath_s > 0 && sinf(breath_s * 1.85f - .9f) > .35f; }
/* How far he is lying down after idle_s seconds of quiet (0 sitting .. 1 asleep). */
static inline float helper_nap_depth(float idle_s) {
  float v = (idle_s - BOT_NAP_S) / BOT_LIE_S;
  v = v < 0 ? 0 : (v > 1 ? 1 : v);
  return v * v * (3 - 2 * v);
}

/* What each bot keeps while another one is shown (RAM only): the last exchange + its outcome. */
/* Earlier turns: records of role byte ('U' you, 'B' bot, 'E' error) + text + NUL, oldest first. */
typedef struct { uint16_t used; char data[HELPER_LOG_MAX]; } helper_log;

typedef struct {
  helper_state state;     /* HV_IDLE / HV_DONE / HV_ERROR / HV_INTERRUPTED */
  bool chat, cont;
  char transcript[VOICE_TRANSCRIPT_MAX + 1];
  char reply[VOICE_TEXT_MAX + 1];
  char note[48];
  helper_log log;
} helper_memo;

typedef struct {
  helper_state state;
  bool holding, want_record, want_send, want_cancel, want_stop, net_busy;
  bool chat;              /* a command was sent: chat layout (sticky until a cancelled empty hold) */
  float user_in, bot_in;  /* 0..1 entrance progress of the user / reply bubbles */
  float reveal;           /* reply characters revealed so far (typewriter) */
  float wave_t;
  unsigned char wave[HELPER_WAVE]; /* recent mic levels 0..250 for the listening waveform */
  int64_t press_us, stamp_us;
  unsigned level_milli;
  char id[33];
  char transcript[VOICE_TRANSCRIPT_MAX + 1];
  char reply[VOICE_TEXT_MAX + 1];
  char note[48];
  orbs_motion orbs;
  /* Bots: selected index, how many exist, why the selected bot's mic is disabled (bots_reason,
   * refreshed by the page owner; 0 = enabled), per-bot memory, and bot_save = persist selection. */
  int bot, nbots;
  unsigned char block;
  bool bot_save;
  helper_memo memo[HELPER_BOTS];
  /* Conversation: cont = the next command continues this bot's Hermes session (false = "new": at
   * boot and after a swipe-up); log = earlier turns; scroll = px scrolled back from the newest
   * message (>= 0) while pull = px the newest was pulled up (swipe-up for a new chat);
   * scroll_max = the renderer's last measured limit (owner writes it back after a paint). */
  bool cont, scrolling;
  helper_log log;
  int scroll, scroll0, pull, scroll_max;
  bool top_pull;         /* a press that started in the top band (possible top swipe) */
  /* Gesture: armed = finger on the mic, not yet recording; swiping = horizontal drag between bots.
   * slide = horizontal offset (px) of the page content; eases back to 0 (slide-in / spring-back). */
  bool touching, armed, swiping;
  int tx0, ty0, tdx;
  int64_t t0;
  float slide;
  /* Bot mood clock: seconds the page has shown the current state (bot_art.h: a happy face right after
   * a reply, sad X eyes right after an error, then calmer faces). */
  helper_state mood_state;
  float mood_t;
  helper_kotaro kot;  /* Kotaro's routines, nap and reactions (bot_art.h) */
} helper_view;

/* Where the bot graphic sits: body centre + drawing scale (big centred, or compact in the chat). */
static inline void helper_bot_place(const helper_view *h, float *cx, float *cy, float *scale) {
  *cx = (float)HELPER_BOT_X;
  *cy = (float)(h->chat ? HELPER_CHAT_BOT_Y : HELPER_BOT_Y);
  *scale = h->chat ? HELPER_CHAT_SCALE : HELPER_BOT_SCALE;
}
/* The graphic's drawn box (every bot pixel in every state lies inside it; tests/host/bot_graphic_test.c). */
typedef struct { int x0, y0, x1, y1; } helper_box;
static inline helper_box helper_bot_box(const helper_view *h) {
  float cx, cy, u;
  helper_bot_place(h, &cx, &cy, &u);
  float bottom = h->chat ? BOT_BOTTOM_COMPACT : BOT_BOTTOM, top = h->chat ? BOT_TOP_COMPACT : BOT_TOP;
  helper_box b = {(int)floorf(cx - BOT_HALF_W * u), (int)floorf(cy - top * u), (int)ceilf(cx + BOT_HALF_W * u),
                  (int)ceilf(cy + bottom * u)};
  return b;
}
/* Press-and-hold target = the graphic's box plus a small fingertip margin. */
static inline bool helper_bot_hit(const helper_view *h, int x, int y) {
  helper_box b = helper_bot_box(h);
  return x >= b.x0 - HELPER_BOT_SLOP && x < b.x1 + HELPER_BOT_SLOP && y >= b.y0 - HELPER_BOT_SLOP && y < b.y1 + HELPER_BOT_SLOP;
}
static inline bool helper_can_record(const helper_view *h) {
  return h->state == HV_IDLE || h->state == HV_DONE || h->state == HV_ERROR || h->state == HV_INTERRUPTED;
}
static inline bool helper_stop_visible(const helper_view *h) {
  return h->state == HV_RUNNING || (h->state == HV_TRANSCRIBING && h->id[0]);
}
/* While a command can be stopped, a tap on the (working) bot is Stop; it wears a small stop badge. */
static inline bool helper_stop_hit(const helper_view *h, int x, int y) { return helper_stop_visible(h) && helper_bot_hit(h, x, y); }
/* The bot takes a hold only when idle and nothing blocks the mic (loading counts as blocked). */
static inline bool helper_can_press_mic(const helper_view *h) { return helper_can_record(h) && !h->block; }
static inline void helper_note(helper_view *h, const char *text) {
  size_t n = strlen(text);
  if (n >= sizeof h->note) n = sizeof h->note - 1;
  memcpy(h->note, text, n);
  h->note[n] = 0;
}
/* Kotaro's reactions (helper_kotaro): a quick tap on him (not when it woke him), a command sent. */
static inline void helper_kotaro_tap(helper_view *h) {
  if (!h->kot.wake_s && helper_nap_depth(h->kot.idle_s) == 0) h->kot.tap_s = 1e-4f;
}
static inline void helper_kotaro_nod(helper_view *h) { h->kot.nod_s = 1e-4f; }

static inline void helper_log_push(helper_log *l, char role, const char *text) {
  size_t n = strlen(text);  /* callers pass NUL-terminated fields (<= VOICE_TEXT_MAX) */
  if (n > HELPER_LOG_MAX - 3) n = HELPER_LOG_MAX - 3;
  if (!n) return;
  size_t need = n + 2;
  while (l->used && l->used + need > HELPER_LOG_MAX) {  /* drop the oldest records */
    size_t first = strnlen(l->data, l->used) + 1;
    if (first > l->used) first = l->used;
    memmove(l->data, l->data + first, l->used - first);
    l->used = (uint16_t)(l->used - first);
  }
  l->data[l->used] = role;
  memcpy(l->data + l->used + 1, text, n);
  l->data[l->used + 1 + n] = 0;
  l->used = (uint16_t)(l->used + need);
}
/* Move the finished exchange on screen into the log (before a new turn replaces it). */
static inline void helper_archive(helper_view *h) {
  if (h->transcript[0]) helper_log_push(&h->log, 'U', h->transcript);
  if (h->reply[0]) helper_log_push(&h->log, 'B', h->reply);
  else if (h->state == HV_ERROR && h->transcript[0]) helper_log_push(&h->log, 'E', h->note[0] ? h->note : "Something went wrong");
}
static inline bool helper_has_history(const helper_view *h) { return h->log.used > 0; }

static inline void helper_press(helper_view *h, int64_t now) {
  if (!helper_can_record(h) || h->block) return;  /* no quota / sign-in / unavailable provider: mic disabled */
  helper_archive(h);  /* the last answer stays on screen as history; this turn continues the chat */
  h->scroll = h->pull = 0;
  h->scrolling = false;
  h->state = HV_LISTENING;
  h->holding = true;
  h->press_us = now;
  h->want_record = true;
  h->want_send = h->want_cancel = h->want_stop = false;
  h->level_milli = 0;
  h->id[0] = h->transcript[0] = h->reply[0] = h->note[0] = 0;
  h->user_in = h->bot_in = h->reveal = 0;
  memset(h->wave, 0, sizeof h->wave);
}

static inline void helper_begin_send(helper_view *h) {
  h->holding = false;
  h->level_milli = 0;
  h->state = HV_TRANSCRIBING;
  h->want_send = true;
  h->chat = true;
  helper_kotaro_nod(h);
}

static inline void helper_release(helper_view *h, int64_t now) {
  if (h->state != HV_LISTENING) { h->holding = false; return; }
  h->holding = false;
  h->level_milli = 0;
  if (now - h->press_us < HELPER_MIN_US) {
    h->state = HV_IDLE;
    h->want_cancel = true;
    h->chat = helper_has_history(h); /* nothing new to show: big mic only when the chat is empty */
    helper_note(h, "");
    return;
  }
  helper_begin_send(h);
}

static inline void helper_stop(helper_view *h) {
  if (!helper_stop_visible(h)) return;
  h->state = HV_STOPPING;
  h->want_stop = true;
}

static inline void helper_leave(helper_view *h) {
  h->touching = h->armed = h->swiping = h->scrolling = false;
  h->pull = 0;
  if (h->state == HV_LISTENING) {
    h->state = HV_IDLE;
    h->want_cancel = true;
    h->level_milli = 0;
  }
  h->holding = false;
}

static inline int helper_nbots(const helper_view *h) { return h->nbots >= 1 && h->nbots <= HELPER_BOTS ? h->nbots : HELPER_BOTS; }
/* Switching bots only between commands: never while listening, sending, running or stopping. */
static inline bool helper_can_switch(const helper_view *h) {
  return helper_can_record(h) && !h->holding && !h->net_busy && !h->want_record && !h->want_send;
}
static inline void helper_memo_save(helper_view *h) {
  if (h->bot < 0 || h->bot >= HELPER_BOTS) return;
  helper_memo *m = &h->memo[h->bot];
  m->state = h->state;
  m->chat = h->chat;
  m->cont = h->cont;
  m->log = h->log;
  memcpy(m->transcript, h->transcript, sizeof m->transcript);
  memcpy(m->reply, h->reply, sizeof m->reply);
  memcpy(m->note, h->note, sizeof m->note);
}
static inline void helper_memo_load(helper_view *h) {
  const helper_memo *m = &h->memo[h->bot];
  h->state = m->state == HV_DONE || m->state == HV_ERROR || m->state == HV_INTERRUPTED ? m->state : HV_IDLE;
  h->chat = m->chat;
  h->cont = m->cont;
  h->log = m->log;
  h->scroll = h->pull = 0;
  h->scrolling = false;
  memcpy(h->transcript, m->transcript, sizeof h->transcript);
  memcpy(h->reply, m->reply, sizeof h->reply);
  memcpy(h->note, m->note, sizeof h->note);
  h->id[0] = 0;
  h->reveal = (float)strlen(h->reply);  /* restored text is shown whole, never re-typed */
  h->user_in = h->transcript[0] ? 1.f : 0;
  h->bot_in = h->reply[0] || (h->state == HV_ERROR && h->transcript[0]) ? 1.f : 0;
  h->level_milli = 0;
  memset(h->wave, 0, sizeof h->wave);
}
/* Select bot `index` (clamped). slide = where the new content starts (0 = no animation). */
static inline bool helper_select(helper_view *h, int index, float slide) {
  int n = helper_nbots(h);
  index = index < 0 ? 0 : (index >= n ? n - 1 : index);
  if (index == h->bot || !helper_can_switch(h)) return false;
  helper_memo_save(h);
  h->bot = index;
  helper_memo_load(h);
  h->orbs.tint = index;
  h->slide = slide;
  h->bot_save = true;
  return true;
}
/* Restore at boot (NVS): no animation, nothing to persist. */
static inline void helper_restore_bot(helper_view *h, int index) {
  h->bot = index >= 0 && index < HELPER_BOTS ? index : 0;
  h->orbs.tint = h->bot;
}
/* One step left (next bot) or right (previous bot); at the ends only a rubber-band nudge. */
static inline bool helper_swipe(helper_view *h, unsigned dir, float from) {
  if (dir != HELPER_SWIPE_NEXT && dir != HELPER_SWIPE_PREV) return false;
  int to = h->bot + (dir == HELPER_SWIPE_NEXT ? 1 : -1);
  if (!helper_can_switch(h)) { h->slide = 0; return false; }
  if (to < 0 || to >= helper_nbots(h)) { h->slide = dir == HELPER_SWIPE_NEXT ? -HELPER_NUDGE_PX / 2 : HELPER_NUDGE_PX / 2; return false; }
  /* Next bot enters from the right (content moves left), previous from the left. */
  return helper_select(h, to, dir == HELPER_SWIPE_NEXT ? 368 + from : -368 + from);
}
/* Finger offset -> content offset: 1:1 towards a neighbour, rubber band (1/3, capped) at an end. */
static inline float helper_drag(const helper_view *h, int dx) {
  bool end = (dx < 0 && h->bot >= helper_nbots(h) - 1) || (dx > 0 && h->bot <= 0);
  if (!end) return (float)(dx > 368 ? 368 : (dx < -368 ? -368 : dx));
  float r = dx / 3.f;
  return r > HELPER_NUDGE_PX ? HELPER_NUDGE_PX : (r < -HELPER_NUDGE_PX ? -HELPER_NUDGE_PX : r);
}

/* Swipe up in the chat: forget this bot's conversation; the next hold starts a new Hermes session. */
static inline bool helper_new_session(helper_view *h) {
  if (!helper_can_switch(h)) return false;
  h->state = HV_IDLE;
  h->cont = false;
  h->chat = false;
  h->log.used = 0;
  h->id[0] = h->transcript[0] = h->reply[0] = h->note[0] = 0;
  h->reveal = h->user_in = h->bot_in = 0;
  h->scroll = h->pull = 0;
  helper_note(h, "New chat");
  return true;
}

static inline orbs_mode helper_orbs_mode(const helper_view *h) {
  switch (h->state) {
    case HV_LISTENING: return ORBS_LISTEN;
    case HV_TRANSCRIBING: case HV_RUNNING: case HV_STOPPING: return ORBS_RUN;
    case HV_DONE: case HV_ERROR: case HV_INTERRUPTED: return ORBS_DONE;
    default: return ORBS_IDLE;
  }
}

static inline bool helper_bot_visible(const helper_view *h) {
  return h->chat && (h->state == HV_RUNNING || h->state == HV_STOPPING || h->reply[0] ||
                     (h->state == HV_ERROR && h->transcript[0]));
}
static inline bool helper_revealing(const helper_view *h) { return h->reveal < (float)strlen(h->reply); }
static inline float helper_step(float v, float dt, float seconds) { v += dt / seconds; return v > 1 ? 1 : v; }

/* ---- Kotaro between commands (helper_kotaro) -------------------------------------------------- */
#define HELPER_AWAY_US (500LL * 1000)  /* no tick for this long: another page was shown */
static inline uint32_t helper_kotaro_hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
/* An event clock: counts up from its start, back to 0 (none) after `len` seconds. */
static inline void helper_kotaro_age(float *s, float dt, float len) {
  if (*s <= 0) return;
  *s += dt;
  if (*s >= len) *s = 0;
}
/* The next routine: a shuffled round of all of them (each once per round), never the same twice in a row. */
static inline unsigned helper_kotaro_pick(helper_kotaro *k, uint32_t r) {
  const unsigned all = ((1u << BOT_RT_N) - 1) & ~1u;
  if ((k->bag & all) == all) k->bag = 0;
  unsigned n = 0;
  for (unsigned i = 1; i < BOT_RT_N; i++) n += !(k->bag & (1u << i)) && i != k->last;
  if (!n) { k->bag = 0; return k->last % (BOT_RT_N - 1) + 1; }
  unsigned pick = r % n;
  for (unsigned i = 1; i < BOT_RT_N; i++)
    if (!(k->bag & (1u << i)) && i != k->last && !pick--) return i;
  return BOT_RT_STRETCH;
}
/* Every poller sample (helper_tick): mood easing, routines, the nap and the reaction clocks. `away` =
 * the page was not ticked for a while (another page was shown): he is awake when you come back. */
static inline void helper_kotaro_tick(helper_view *h, int64_t now, float dt, bool away) {
  helper_kotaro *k = &h->kot;
  bool id = h->id[0] != 0;
  bool active = away || h->touching || h->holding || h->armed || h->swiping || h->scrolling || h->slide != 0 ||
                !helper_can_record(h) || helper_revealing(h);
  helper_kotaro_age(&k->tap_s, dt, BOT_TAP_S);
  helper_kotaro_age(&k->nod_s, dt, BOT_NOD_S);
  helper_kotaro_age(&k->cheer_s, dt, BOT_CHEER_S);
  helper_kotaro_age(&k->wake_s, dt, BOT_WAKE_S);
  if (k->routine) {
    k->routine_s += dt;
    if (k->routine_s >= bot_routine_len[k->routine % BOT_RT_N]) k->routine = 0, k->routine_s = 0;
  }
  if (k->key_state != (unsigned char)h->state || k->key_block != h->block || k->key_id != id) {
    /* a reply arriving after a command (not a restored one on a bot swipe): celebrate */
    bool cheer = h->state == HV_DONE && (k->key_state == HV_TRANSCRIBING || k->key_state == HV_RUNNING || k->key_state == HV_STOPPING);
    k->from_state = k->key_state; k->from_block = k->key_block; k->from_id = k->key_id;
    k->from_t = h->mood_t;
    k->key_state = (unsigned char)h->state; k->key_block = h->block; k->key_id = id;
    k->switch_s = 0;
    k->cheer_s = cheer ? 1e-4f : 0;
    active = true;
  } else if (k->switch_s < 600) k->switch_s += dt;
  /* the hold arming: he leans in (and eases back once listening starts or the press is gone) */
  float lean = h->armed && h->touching ? 1.f : 0.f;
  k->lean = lean > k->lean ? fminf(lean, k->lean + dt / .25f) : fmaxf(lean, k->lean - dt / .3f);
  /* the nap: any touch or activity wakes him (with a stretch when he was lying down) */
  if (active) {
    float depth = helper_nap_depth(k->idle_s);
    if (depth > 0 && !away) { k->wake_s = 1e-4f; k->wake_from = depth; }
    k->idle_s = 0;
  } else if (k->idle_s < 3600) k->idle_s += dt;
  /* idle routines: only in calm moods, never while you touch the page, never into the nap */
  bool calm = !active && !h->block && k->switch_s >= BOT_ROUTINE_SETTLE_S && !k->wake_s && !k->tap_s && !k->cheer_s &&
              k->lean == 0 && (h->state == HV_IDLE || (h->state == HV_DONE && h->mood_t >= BOT_CALM_S));
  /* breathing only while nothing else moves him: a new breath starts from rest, a breath in finishes */
  bool still = !active && k->switch_s >= BOT_BLEND_S && !k->routine && !k->wake_s && !k->tap_s && !k->cheer_s && !k->nod_s &&
               k->lean == 0 && (h->state != HV_DONE || h->mood_t >= BOT_CALM_S);
  k->breath_s = (still || helper_breath_up(k->breath_s)) && k->breath_s < 3600 ? k->breath_s + dt : 0;
  if (!calm) { k->calm_s = 0; return; }
  if (k->routine) return;  /* the quiet gap counts from the end of the last one */
  uint32_t r = helper_kotaro_hash((uint32_t)(now / 1000) ^ 0x9e3779b9U);
  if (k->gap_s <= 0) k->gap_s = BOT_ROUTINE_GAP_MIN + (BOT_ROUTINE_GAP_MAX - BOT_ROUTINE_GAP_MIN) * (float)(r % 1001) / 1000.f;
  k->calm_s += dt;
  if (k->calm_s < k->gap_s) return;
  unsigned pick = helper_kotaro_pick(k, helper_kotaro_hash(r));
  if (k->idle_s + bot_routine_len[pick] + .5f >= BOT_NAP_S) return;  /* too close to the nap */
  k->routine = (unsigned char)pick; k->routine_s = 1e-4f;
  k->last = (unsigned char)pick; k->bag |= (unsigned char)(1u << pick);
  k->calm_s = 0;
  k->gap_s = BOT_ROUTINE_GAP_MIN + (BOT_ROUTINE_GAP_MAX - BOT_ROUTINE_GAP_MIN) * (float)(helper_kotaro_hash(r ^ 0x5bd1e995U) % 1001) / 1000.f;
}

/* 100 Hz poller: advance motion (wall clock), chat animations and the 15 s auto-stop. */
static inline void helper_tick(helper_view *h, int64_t now) {
  bool away = h->stamp_us && now - h->stamp_us > HELPER_AWAY_US;
  float dt = h->stamp_us && now > h->stamp_us ? (now - h->stamp_us) / 1000000.f : 0;
  if (dt > 1) dt = 1;
  h->stamp_us = now;
  helper_kotaro_tick(h, now, dt, away);
  if (h->mood_state != h->state) { h->mood_state = h->state; h->mood_t = 0; }
  else if (h->mood_t < 3600.f) h->mood_t += dt;
  size_t len = strlen(h->reply);
  if (h->reveal > (float)len) h->reveal = (float)len;
  else if (h->reveal < (float)len) {  /* typewriter; long replies type faster (done in <= ~4 s) */
    float cps = (float)len / 4.f > HELPER_REVEAL_CPS ? (float)len / 4.f : HELPER_REVEAL_CPS;
    h->reveal += dt * cps; if (h->reveal > (float)len) h->reveal = (float)len;
  }
  h->user_in = h->transcript[0] || h->state == HV_LISTENING || h->state == HV_TRANSCRIBING ? helper_step(h->user_in, dt, 0.35f) : 0;
  h->bot_in = helper_bot_visible(h) ? helper_step(h->bot_in, dt, 0.4f) : 0;
  for (h->wave_t += dt; h->wave_t >= 0.05f; h->wave_t -= 0.05f) {
    memmove(h->wave, h->wave + 1, sizeof h->wave - 1);
    h->wave[HELPER_WAVE - 1] = h->state == HV_LISTENING ? (unsigned char)(h->level_milli / 4) : 0;
  }
  /* Orbs: swell with the voice while listening; "talk" gently while the reply types out. */
  float level = h->state == HV_LISTENING ? h->level_milli / 1000.f : (helper_revealing(h) ? 0.35f : 0);
  if (dt > 0) orbs_advance(&h->orbs, dt, helper_orbs_mode(h), level);
  if (!h->swiping && h->slide != 0) {  /* slide-in / spring-back: ~90 ms time constant */
    h->slide *= expf(-dt * 11.f);
    if (h->slide > -0.75f && h->slide < 0.75f) h->slide = 0;
  }
  if (!h->scrolling && h->pull) { h->pull = (int)(h->pull * expf(-dt * 12.f)); if (h->pull < 2) h->pull = 0; }
  if (h->state == HV_LISTENING && now - h->press_us >= HELPER_MAX_US) helper_begin_send(h);
}


/* Touch on the Ask page (not the Home pull), called for every poller sample (a touch that started in
 * the Home pull zone arrives once it turned out not to be the pull, every sample at its own time).
 * Down on the working bot (Stop badge) = stop at once. Down on the bot arms it; staying within
 * HELPER_ARM_SLOP px for HELPER_ARM_US starts recording. Horizontal travel (from anywhere) drags between bots when a
 * switch is allowed; > HELPER_SWIPE_PX at release commits, otherwise it springs back. */
static inline void helper_touch(helper_view *h, int64_t now, bool down_edge, bool down, bool up_edge, int x, int y) {
  if (down_edge) {
    h->touching = true;
    h->armed = h->swiping = false;
    h->tx0 = x; h->ty0 = y; h->tdx = 0; h->t0 = now;
    h->top_pull = y < HELPER_TOP_BAND && helper_can_switch(h);
    if (helper_stop_hit(h, x, y)) { helper_stop(h); h->touching = false; h->top_pull = false; }
    else if (helper_bot_hit(h, x, y) && helper_can_record(h) && !h->block) h->armed = true;
  }
  if (down && h->touching) {
    int dx = x - h->tx0, dy = y - h->ty0, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    h->tdx = dx;
    if (h->top_pull) {
      /* mostly vertical, downward: never a bot swipe or a chat scroll; drifting sideways cancels */
      if (dy < -HELPER_ARM_SLOP || (ax > HELPER_ARM_SLOP && ax * 2 > ay)) h->top_pull = false;
      else { h->armed = false; return; }
    }
    if (h->armed && (ax > HELPER_ARM_SLOP || ay > HELPER_ARM_SLOP)) h->armed = false;
    if (h->armed && now - h->t0 >= HELPER_ARM_US) { h->armed = false; helper_press(h, now); }
    if (!h->swiping && !h->holding && ax > HELPER_ARM_SLOP && ax > ay && helper_can_switch(h)) h->swiping = true;
    if (h->swiping) h->slide = helper_drag(h, dx);
    /* Vertical drag in the chat: finger down = read older messages, finger up past the newest
     * = pull for a new chat (only between commands). */
    if (!h->swiping && !h->holding && !h->scrolling && h->chat && ay > HELPER_SCROLL_SLOP && ay > ax) {
      h->scrolling = true; h->armed = false; h->scroll0 = h->scroll;
    }
    if (h->scrolling) {
      int v = h->scroll0 + dy, max = h->scroll_max > 0 ? h->scroll_max : 0;
      if (v > max) v = max;
      if (v >= 0) { h->scroll = v; h->pull = 0; }
      /* Only a drag that STARTED at the newest message pulls for a new chat: scrolling back down
       * through history stops at the newest instead of resetting the conversation by accident. */
      else { h->scroll = 0; h->pull = helper_can_switch(h) && h->scroll0 == 0 ? (-v > HELPER_PULL_MAX ? HELPER_PULL_MAX : -v) : 0; }
    }
  }
  if (up_edge) {
    if (h->top_pull) {
      h->top_pull = false;
      if (y >= HELPER_TOP_NEW_Y) helper_new_session(h);
      h->touching = h->armed = h->swiping = false;
      return;
    }
    if (h->scrolling) {
      h->scrolling = false;
      if (h->pull >= HELPER_NEW_PX) helper_new_session(h);
    }
    else if (h->holding) helper_release(h, now);
    else if (h->swiping) {
      int dx = h->tdx;
      if (dx <= -HELPER_SWIPE_PX || dx >= HELPER_SWIPE_PX) {
        h->swiping = false;
        if (!helper_swipe(h, dx < 0 ? HELPER_SWIPE_NEXT : HELPER_SWIPE_PREV, h->slide)) h->slide = helper_drag(h, dx);
      }
    } else if (h->armed) { helper_note(h, ""); helper_kotaro_tap(h); }  /* a quick tap: nothing recorded, Kotaro reacts */
    h->touching = h->armed = h->swiping = false;
  }
}
/* The worker accepted the upload: later holds on this bot continue the same Hermes session. */
static inline void helper_sent(helper_view *h) { h->cont = true; }

/* USB test hook: same transitions as the button. Returns false for an unknown action. */
static inline bool helper_command(helper_view *h, int64_t now, unsigned action) {
  if (action == HELPER_CMD_PRESS) helper_press(h, now);
  else if (action == HELPER_CMD_RELEASE) helper_release(h, now);
  else if (action == HELPER_CMD_STOP) helper_stop(h);
  else return false;
  return true;
}

static inline bool helper_terminal(helper_state s) { return s == HV_DONE || s == HV_ERROR || s == HV_INTERRUPTED; }

/* Worker result for the current command. Terminal states are sticky; another id is ignored. */
static inline void helper_apply(helper_view *h, const voice_command *c) {
  if (helper_terminal(h->state) || h->state == HV_IDLE || h->state == HV_LISTENING) return;
  if (h->id[0] && strcmp(h->id, c->id)) return;
  memcpy(h->id, c->id, sizeof h->id);
  if (c->transcript[0]) memcpy(h->transcript, c->transcript, sizeof h->transcript);
  if (c->status != VOICE_ERROR && c->text[0]) memcpy(h->reply, c->text, sizeof h->reply);
  switch (c->status) {
    case VOICE_TRANSCRIBING: break;
    case VOICE_RUNNING: if (h->state != HV_STOPPING) h->state = HV_RUNNING; break;
    case VOICE_STOPPING: h->state = HV_STOPPING; break;
    case VOICE_DONE: h->state = HV_DONE; break;
    case VOICE_INTERRUPTED: h->state = HV_INTERRUPTED; break;
    default: h->state = HV_ERROR; helper_note(h, c->text[0] ? c->text : "Something went wrong"); break;
  }
}

static inline void helper_fail(helper_view *h, const char *why) {
  h->state = HV_ERROR;
  h->holding = false;
  h->level_milli = 0;
  helper_note(h, why);
}

/* Greedy word wrap into ASCII lines for the 9 px font. Non-ASCII UTF-8 sequences become '?'.
 * When text remains after max_lines, the last line ends in "...". Returns the line count. */
static inline int helper_wrap(const char *text, int cols, int max_lines, char lines[][HELPER_COLS + 1]) {
  if (cols > HELPER_COLS) cols = HELPER_COLS;
  if (max_lines > HELPER_WRAP_MAX) max_lines = HELPER_WRAP_MAX;
  static char clean[VOICE_TEXT_MAX + 1];  /* static: owner-task stack budget (renderer only) */
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p && n < sizeof clean - 1; p++) {
    if (*p >= 0x80) { if (*p >= 0xC0) clean[n++] = '?'; continue; }
    clean[n++] = (*p == '\n' || (*p >= 32 && *p < 127)) ? (char)*p : ' ';
  }
  clean[n] = 0;
  int count = 0;
  size_t i = 0;
  while (count < max_lines) {
    while (clean[i] == ' ') i++;
    if (!clean[i]) break;
    size_t start = i, end = i, last_space = 0;
    bool has_space = false;
    while (clean[end] && clean[end] != '\n' && (int)(end - start) < cols) {
      if (clean[end] == ' ') { last_space = end; has_space = true; }
      end++;
    }
    size_t cut = end, next = end;
    if (clean[end] && clean[end] != '\n' && clean[end] != ' ' && has_space) { cut = last_space; next = last_space + 1; }
    else if (clean[end] == '\n') next = end + 1;
    while (cut > start && clean[cut - 1] == ' ') cut--;
    size_t len = cut - start;
    memcpy(lines[count], clean + start, len);
    lines[count][len] = 0;
    count++;
    i = next;
    if (count == max_lines) {
      size_t rest = i;
      while (clean[rest] == ' ' || clean[rest] == '\n') rest++;
      if (clean[rest]) {
        char *line = lines[count - 1];
        size_t l = strlen(line);
        if ((int)l > cols - 3) l = (size_t)(cols - 3);
        memcpy(line + l, "...", 4);
      }
    }
  }
  return count;
}
