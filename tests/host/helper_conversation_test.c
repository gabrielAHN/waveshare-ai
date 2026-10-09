#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
/* Conversation on the Ask page:
 *  - after a reply, the next mic hold CONTINUES the same Hermes session (cont=1 -> X-Thread continue)
 *    and earlier turns stay on screen (log);
 *  - a vertical drag scrolls back through the conversation (full long replies readable);
 *  - pulling the newest message up past HELPER_NEW_PX and releasing starts a NEW session;
 *  - switching bots and leaving Home/returning keep each bot's conversation until that swipe-up. */
static int64_t t = 1000000;
static uint16_t px[SPARKLES_PIXELS];
static void sample(home_ui *s, bool down, int x, int y) { t += 10000; home_sample(s, t, down, x, y); }
static void idle(home_ui *s, int ms) { for (int i = 0; i < ms / 10; i++) sample(s, false, 0, 0); }
static void drag(home_ui *s, int x0, int y0, int x1, int y1) {
  for (int i = 0; i <= 20; i++) sample(s, true, x0 + (x1 - x0) * i / 20, y0 + (y1 - y0) * i / 20);
  sample(s, false, x1, y1);
}
static void hold_mic(home_ui *s, int ms) {  /* hold the bot graphic */
  float fx, fy, u; helper_bot_place(&s->helper, &fx, &fy, &u); int x = (int)fx, y = (int)fy;
  for (int i = 0; i < ms / 10; i++) sample(s, true, x, y);
  sample(s, false, x, y);
}
static void turn(home_ui *s, const char *said, const char *reply) {
  s->bots.data.received_us = t;  /* a fresh /v1/bots poll (fixture) */
  idle(s, 20);
  hold_mic(s, 1200);
  if (!(s->helper.state == HV_TRANSCRIBING && s->helper.want_send)) fprintf(stderr, "turn '%s': state=%d block=%u bot=%d page=%d chat=%d\n", said, s->helper.state, s->helper.block, s->helper.bot, s->page, s->helper.chat);
  assert(s->helper.state == HV_TRANSCRIBING && s->helper.want_send);
  s->helper.want_record = s->helper.want_send = false;  /* the voice worker took it */
  helper_sent(&s->helper);                               /* upload accepted (202) */
  voice_command c = {.status = VOICE_DONE};
  memcpy(c.id, "0123456789abcdef0123456789abcdef", 33);
  snprintf(c.transcript, sizeof c.transcript, "%s", said);
  snprintf(c.text, sizeof c.text, "%s", reply);
  helper_apply(&s->helper, &c);
  idle(s, 4500);
}
static home_ui runnable(void) {
  home_ui s = {.page = HELPER};
  s.bots.data = (bots_data){.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", "atlas", "coding"};
  for (int i = 0; i < 3; i++) { strcpy(s.bots.data.b[i].id, ids[i]); strcpy(s.bots.data.b[i].name, ids[i]); s.bots.data.b[i].available = true; s.bots.data.b[i].reset_s = BOTS_NONE32; }
  return s;
}
static int log_records(const helper_log *l, char role) {
  int n = 0;
  for (size_t at = 0; at < l->used;) { size_t len = strnlen(l->data + at, l->used - at); if (len > 1 && l->data[at] == role) n++; at += len + 1; }
  return n;
}
static void paint(home_ui *s) { s->live_now_us = t; assert(home_render(s, px, SPARKLES_PIXELS)); s->helper.scroll_max = helper_scroll_extent; }

int main(void) {
  home_ui s = runnable();
  assert(!s.helper.cont);                 /* first turn after boot: a new session */
  turn(&s, "first question", "first answer");
  assert(s.helper.cont && s.helper.state == HV_DONE);
  /* Finished responding -> hold again = continue; the finished exchange moves into the history. */
  hold_mic(&s, 1200);
  assert(s.helper.state == HV_TRANSCRIBING && s.helper.cont);
  assert(log_records(&s.helper.log, 'U') == 1 && log_records(&s.helper.log, 'B') == 1);
  s.helper.want_record = s.helper.want_send = false;
  voice_command c = {.status = VOICE_DONE};
  memcpy(c.id, "0123456789abcdef0123456789abcdef", 33);
  strcpy(c.transcript, "second question"); strcpy(c.text, "second answer");
  helper_apply(&s.helper, &c); idle(&s, 3000);
  assert(s.helper.cont && !strcmp(s.helper.reply, "second answer"));
  /* A short tap never wipes the conversation or the chat layout. */
  { float fx, fy, u; helper_bot_place(&s.helper, &fx, &fy, &u); int x = (int)fx, y = (int)fy; for (int i = 0; i < 45; i++) sample(&s, true, x, y); sample(&s, false, x, y); }
  assert(s.helper.chat && s.helper.cont);

  /* Scroll: a long reply taller than the window can be scrolled back to its first line. */
  static char longr[VOICE_TEXT_MAX + 1];
  for (int i = 0; i < VOICE_TEXT_MAX; i++) longr[i] = i % 7 == 6 ? ' ' : 'a' + i % 26;
  longr[VOICE_TEXT_MAX] = 0;
  turn(&s, "tell me everything", longr);
  paint(&s);
  assert(s.helper.scroll_max > 400);      /* far more than one screen of text */
  int before = s.helper.scroll;
  drag(&s, 184, 90, 184, 290);            /* finger down = older text comes into view */
  assert(s.helper.scroll > before + 150 && s.helper.cont && s.helper.log.used);
  drag(&s, 184, 90, 184, 290); drag(&s, 184, 90, 184, 290); drag(&s, 184, 90, 184, 290);
  drag(&s, 184, 90, 184, 290); drag(&s, 184, 90, 184, 290);
  assert(s.helper.scroll == s.helper.scroll_max);  /* clamped at the oldest message */
  paint(&s);
  /* scrolling never changed the session or started a recording */
  assert(s.helper.state == HV_DONE && !s.helper.want_record && s.helper.cont);
  /* A vertical scroll that starts on the bot is a scroll, not a recording. */
  { helper_box bb = helper_bot_box(&s.helper); drag(&s, HELPER_BOT_X, bb.y0 + 4, HELPER_BOT_X, bb.y0 - 96); }
  assert(!s.helper.want_record && s.helper.state == HV_DONE);
  for (int i = 0; i < 20 && s.helper.scroll; i++) drag(&s, 184, 290, 184, 90);
  assert(s.helper.scroll == 0 && s.helper.cont);  /* over-scrolling down never resets the chat */
  drag(&s, 184, 200, 184, 160);                   /* a small pull at the newest springs back */
  assert(s.helper.cont && s.helper.log.used);
  idle(&s, 600);
  assert(s.helper.pull == 0 && s.helper.cont && s.helper.log.used);

  /* Switch bot: atlas has its own (empty) conversation; helper's is kept. */
  size_t helper_used = s.helper.log.used;
  drag(&s, 300, 150, 120, 150);
  assert(s.helper.bot == 1 && !s.helper.cont && !s.helper.log.used && !s.helper.reply[0]);
  turn(&s, "atlas q", "atlas a");
  assert(s.helper.cont);
  drag(&s, 80, 150, 280, 150);
  assert(s.helper.bot == 0 && s.helper.cont && s.helper.log.used == helper_used && s.helper.reply[0]);
  /* Home and back: still the same conversation. */
  for (int i = 0; i <= 12; i++) sample(&s, true, 184, 440 - i * 18);   /* bottom-UP Home pull to centre */
  sample(&s, false, 0, 0);
  assert(s.page == HOME);
  s.page = HELPER; s.bots.data.received_us = t;
  assert(s.helper.bot == 0 && s.helper.cont && s.helper.log.used == helper_used);

  /* User rule (2026-09-29): a swipe from the TOP edge down to the centre = new session on the SAME
   * bot. It starts above the chat (the pill/dots band), so a scroll from inside the chat still reads
   * older messages. It works with or without a chat on screen, and never swaps the bot. */
  {
    int bot = s.helper.bot;
    size_t used = s.helper.log.used;
    for (int k = 0; k <= 12; k++) sample(&s, true, 184, 20 + 16 * k);   /* 20 -> 212 */
    assert(s.helper.log.used == used);                                  /* nothing until release */
    sample(&s, false, 0, 0);
    assert(s.helper.bot == bot && !s.helper.cont && !s.helper.log.used && !s.helper.chat);
    assert(!strcmp(s.helper.note, "New chat") && s.page == HELPER);
    turn(&s, "again", "again answer");
    assert(s.helper.cont && s.helper.bot == bot);
    /* a short top pull (not to the centre) springs back and keeps the chat */
    used = s.helper.log.used;
    for (int k = 0; k <= 6; k++) sample(&s, true, 184, 20 + 10 * k);
    sample(&s, false, 0, 0);
    assert(s.helper.cont && s.helper.log.used == used);
    /* a top swipe that drifts sideways is not a new chat and not a bot swap */
    for (int k = 0; k <= 12; k++) sample(&s, true, 184 + 14 * k, 20 + 8 * k);
    sample(&s, false, 0, 0);
    assert(s.helper.cont && s.helper.log.used == used);
  }
  /* Swipe up (pull the newest message up and release) = new session. */
  drag(&s, 184, 280, 184, 150);
  assert(!s.helper.cont && !s.helper.log.used && !s.helper.transcript[0] && !s.helper.reply[0] && !s.helper.chat);
  assert(!strcmp(s.helper.note, "New chat"));
  paint(&s);
  turn(&s, "fresh", "fresh answer");
  assert(s.helper.cont && !log_records(&s.helper.log, 'U'));
  /* atlas kept its own conversation through helper's reset. */
  drag(&s, 300, 150, 120, 150);
  assert(s.helper.bot == 1 && s.helper.cont && !strcmp(s.helper.reply, "atlas a"));

  /* Not while a reply is running: no new chat, no scroll-driven session change. */
  s = runnable(); turn(&s, "q", "a");
  hold_mic(&s, 1200); s.helper.want_record = s.helper.want_send = false; s.helper.net_busy = true;
  drag(&s, 184, 280, 184, 120);
  assert(s.helper.cont && s.helper.state == HV_TRANSCRIBING);
  s.helper.net_busy = false;

  /* The history keeps the newest turns when full; it never overflows. */
  s = runnable();
  for (int i = 0; i < 60; i++) { char q[40], a[200]; snprintf(q, sizeof q, "question %d", i); memset(a, 'x', 180); a[180] = 0; turn(&s, q, a); }
  assert(s.helper.log.used <= HELPER_LOG_MAX && s.helper.log.used > HELPER_LOG_MAX - 400);
  assert(strstr(s.helper.log.data + s.helper.log.used - 200, "x"));
  paint(&s);
  puts("conversation: continue on hold, history, scroll full reply, swipe-up new chat, per-bot + Home keep: PASS");
  return 0;
}
