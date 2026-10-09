/* Ask-page bot swipe + per-bot mic gate through the real touch path (home_sample) and renderer:
 * swipe vs hold disambiguation, clamp + rubber band, no switching while busy, per-bot history,
 * persisted selection, disabled-mic states block recording (touch and USB hook), layout bounds. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"

static int64_t t = 1;
static void sample(home_ui *s, bool down, int x, int y) { home_sample(s, t += 10000, down, x, y); }
static void idle(home_ui *s, int ms) { for (int i = 0; i < ms / 10; i++) sample(s, false, 0, 0); }
/* Horizontal drag in 8 px steps from (x0, y) to (x1, y), then release. */
static void drag(home_ui *s, int x0, int x1, int y) {
  int step = x1 > x0 ? 8 : -8;
  sample(s, true, x0, y);
  for (int x = x0; step > 0 ? x < x1 : x > x1; x += step) sample(s, true, x, y);
  sample(s, true, x1, y);
  sample(s, false, x1, y);
}
static void hold_mic(home_ui *s, int ms) {  /* hold the bot graphic */
  float fx, fy, u; helper_bot_place(&s->helper, &fx, &fy, &u); int x = (int)fx, y = (int)fy;
  for (int i = 0; i < ms / 10; i++) sample(s, true, x, y);
  sample(s, false, x, y);
}
static void done_command(home_ui *s, const char *said, const char *reply) {
  voice_command c = {.status = VOICE_DONE};
  memcpy(c.id, "0123456789abcdef0123456789abcdef", 33);
  snprintf(c.transcript, sizeof c.transcript, "%s", said);
  snprintf(c.text, sizeof c.text, "%s", reply);
  s->helper.want_record = s->helper.want_send = false;  /* consumed by the voice worker */
  helper_apply(&s->helper, &c);
  idle(s, 3000);
}
static bots_data frame3(bool a0, uint8_t r0, bool a1, uint8_t r1, bool a2, uint8_t r2, uint32_t reset1) {
  bots_data d = {.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", "atlas", "coding"}, *names[3] = {"Helper", "Atlas", "Coding"}, *prov[3] = {"OpenRouter", "Claude", "Claude"};
  bool av[3] = {a0, a1, a2}; uint8_t rs[3] = {r0, r1, r2};
  for (int i = 0; i < 3; i++) {
    strcpy(d.b[i].id, ids[i]); strcpy(d.b[i].name, names[i]); strcpy(d.b[i].provider, prov[i]);
    d.b[i].available = av[i]; d.b[i].reason = rs[i]; d.b[i].reset_s = i == 1 ? reset1 : BOTS_NONE32;
  }
  return d;
}
static home_ui runnable_home(void) {
  home_ui s = {.page = HELPER};
  s.bots.data = frame3(true, BR_NONE, true, BR_NONE, true, BR_NONE, BOTS_NONE32);
  return s;
}
static int differs(const uint16_t *a, const uint16_t *b, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) n += a[y * 368 + x] != b[y * 368 + x];
  return n;
}

int main(void) {
  /* --- Swipe left/right cycles helper -> atlas -> coding, clamped with a rubber band at the ends. */
  home_ui s = runnable_home();
  assert(s.helper.bot == 0);
  drag(&s, 300, 120, 150);
  assert(s.helper.bot == 1 && s.helper.state == HV_IDLE && !s.helper.want_record && s.helper.bot_save);
  assert(s.helper.slide > 100);  /* the new bot slides in from the right */
  idle(&s, 800);
  assert(s.helper.slide == 0);
  s.helper.bot_save = false;
  drag(&s, 300, 120, 150);
  assert(s.helper.bot == 2);
  s.helper.bot_save = false;
  /* Rubber band past the last bot: content follows at 1/3 (capped), springs back, no change. */
  sample(&s, true, 300, 150);
  for (int x = 300; x > 60; x -= 8) sample(&s, true, x, 150);
  assert(s.helper.swiping && s.helper.slide < 0 && s.helper.slide >= -HELPER_NUDGE_PX);
  sample(&s, false, 60, 150);
  assert(s.helper.bot == 2 && !s.helper.bot_save && s.helper.slide < 0);
  idle(&s, 800);
  assert(s.helper.slide == 0);
  drag(&s, 60, 300, 150); drag(&s, 60, 300, 150);
  assert(s.helper.bot == 0);
  drag(&s, 60, 300, 150);
  assert(s.helper.bot == 0);  /* clamped at the first bot */
  /* Under the swipe threshold: springs back. */
  drag(&s, 200, 170, 150);
  assert(s.helper.bot == 0);
  /* Mostly-vertical drags are not bot swipes. */
  sample(&s, true, 200, 100); for (int y = 100; y < 300; y += 8) sample(&s, true, 205, y); sample(&s, false, 205, 300);
  assert(s.helper.bot == 0);

  /* --- Swipe vs hold on the bot itself. */
  s = runnable_home();
  drag(&s, HELPER_BOT_X + 30, HELPER_BOT_X - 90, HELPER_BOT_Y);  /* starts ON the bot, quick horizontal */
  assert(s.helper.bot == 1 && s.helper.state == HV_IDLE && !s.helper.want_record && !s.helper.want_send);
  idle(&s, 600);
  /* A slow drift within the 12 px slop still records after 0.4 s. */
  sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  for (int i = 0; i < 45; i++) sample(&s, true, HELPER_BOT_X + i / 5, HELPER_BOT_Y);
  assert(s.helper.state == HV_LISTENING && s.helper.want_record && s.helper.bot == 1);
  /* While listening, horizontal travel never switches bots (and never slides the page). */
  for (int x = HELPER_BOT_X; x > HELPER_BOT_X - 120; x -= 8) sample(&s, true, x, HELPER_BOT_Y);
  for (int i = 0; i < 40; i++) sample(&s, true, HELPER_BOT_X - 120, HELPER_BOT_Y);
  assert(s.helper.bot == 1 && !s.helper.swiping && s.helper.slide == 0 && s.helper.state == HV_LISTENING);
  sample(&s, false, HELPER_BOT_X - 120, HELPER_BOT_Y);
  assert(s.helper.state == HV_TRANSCRIBING && s.helper.want_send);
  /* Moving > 12 px before the arm delay cancels the arm: no recording at all. */
  s = runnable_home();
  sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  for (int i = 0; i < 60; i++) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y + (i > 5 ? 20 : 0));
  sample(&s, false, HELPER_BOT_X, HELPER_BOT_Y + 20);
  assert(s.helper.state == HV_IDLE && !s.helper.want_record);
  /* A quick tap records nothing and hints. */
  sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y); sample(&s, false, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_IDLE && !s.helper.want_record && !s.helper.note[0]);
  /* Bottom-edge UP goes Home, and never counts as a bot swipe. */
  s = runnable_home();
  sample(&s, true, 180, 440); sample(&s, true, 184, 224); sample(&s, false, 184, 224);
  assert(s.page == HOME && s.helper.bot == 0);
  /* A horizontal drag in the retired lower third remains a bot swipe. */
  s = runnable_home();
  drag(&s, 300, 100, 400);
  assert(s.page == HELPER && s.helper.bot == 1);

  /* --- No switching while busy (listening / sending / running / stopping). */
  helper_state busy[] = {HV_LISTENING, HV_TRANSCRIBING, HV_RUNNING, HV_STOPPING};
  for (size_t k = 0; k < sizeof busy / sizeof *busy; k++) {
    s = runnable_home();
    s.helper.state = busy[k];
    s.helper.chat = true;
    drag(&s, 300, 60, 150);
    assert(s.helper.bot == 0 && s.helper.slide == 0 && !s.helper.bot_save);
    assert(!helper_swipe(&s.helper, HELPER_SWIPE_NEXT, 0) && s.helper.bot == 0);
  }
  s = runnable_home();
  s.helper.net_busy = true;  /* upload in flight right after release */
  drag(&s, 300, 60, 150);
  assert(s.helper.bot == 0);

  /* --- Each bot keeps its own last transcript/reply. */
  s = runnable_home();
  hold_mic(&s, 1200);
  done_command(&s, "helper question", "helper answer");
  drag(&s, 300, 100, 150);
  assert(s.helper.bot == 1 && !s.helper.transcript[0] && !s.helper.reply[0] && !s.helper.chat && s.helper.state == HV_IDLE);
  hold_mic(&s, 1200);
  done_command(&s, "atlas question", "atlas answer");
  drag(&s, 100, 300, 150);
  assert(s.helper.bot == 0 && !strcmp(s.helper.transcript, "helper question") && !strcmp(s.helper.reply, "helper answer"));
  assert(s.helper.chat && s.helper.state == HV_DONE && !helper_revealing(&s.helper) && s.helper.bot_in == 1.f);
  drag(&s, 300, 100, 150);
  assert(!strcmp(s.helper.reply, "atlas answer") && !s.helper.id[0]);
  /* A stale result for another command id never lands on the restored bot. */
  voice_command late = {.status = VOICE_RUNNING};
  memcpy(late.id, "ffffffffffffffffffffffffffffffff", 33);
  strcpy(late.text, "LATE");
  helper_apply(&s.helper, &late);
  assert(!strcmp(s.helper.reply, "atlas answer"));

  /* --- Persisted selection: restore sets the index without animation or a save request. */
  s = runnable_home();
  helper_restore_bot(&s.helper, bots_restore(2));
  assert(s.helper.bot == 2 && s.helper.slide == 0 && !s.helper.bot_save && s.helper.orbs.tint == 2);
  helper_restore_bot(&s.helper, bots_restore(9));
  assert(s.helper.bot == 0);
  /* The worker persists whenever a swipe commits (bot_save), exactly once per change. */
  drag(&s, 300, 100, 150);
  assert(s.helper.bot_save && s.helper.bot == 1);

  /* --- Disabled mic: no quota / sign-in / plugin update block recording (touch AND the USB hook). */
  uint8_t reasons[3] = {BR_EXHAUSTED, BR_SIGNIN, BR_UNLABELLED};
  for (int k = 0; k < 3; k++) {
    s = runnable_home();
    bool avail = reasons[k] == BR_UNLABELLED;
    s.bots.data = frame3(true, BR_NONE, avail, reasons[k], true, BR_NONE, 7800);
    drag(&s, 300, 100, 150);
    assert(s.helper.bot == 1 && s.helper.block == reasons[k]);
    hold_mic(&s, 1500);
    assert(s.helper.state == HV_IDLE && !s.helper.want_record && !s.helper.want_send);
    assert(helper_command(&s.helper, t, HELPER_CMD_PRESS) && s.helper.state == HV_IDLE && !s.helper.want_record);
    /* Swiping to an available bot re-enables the mic. */
    drag(&s, 300, 100, 150);
    assert(s.helper.bot == 2 && s.helper.block == BR_NONE);
    hold_mic(&s, 1200);
    assert(s.helper.state == HV_TRANSCRIBING && s.helper.want_send);
  }
  /* A quota countdown cannot grant permission: wait for a fresh available frame. */
  s = runnable_home();
  s.bots.data = frame3(true, BR_NONE, false, BR_EXHAUSTED, true, BR_NONE, 1);
  drag(&s, 300, 100, 150);
  idle(&s, 1100);
  assert(s.helper.block == BR_EXHAUSTED);
  /* Fewer bots from the bridge clamp the selection and the dots. */
  s = runnable_home();
  s.helper.bot = 2;
  s.bots.data = frame3(true, 0, true, 0, true, 0, BOTS_NONE32);
  s.bots.data.count = 2;
  idle(&s, 20);
  assert(s.helper.bot == 1 && helper_nbots(&s.helper) == 2);

  /* --- Rendering + layout. */
  uint16_t *a = malloc((SPARKLES_PIXELS + 2) * 2), *b = malloc(SPARKLES_PIXELS * 2), *c = malloc(SPARKLES_PIXELS * 2);
  assert(a && b && c);
  a[0] = 0x1111; a[SPARKLES_PIXELS + 1] = 0x2222;
  /* Pill + dots fit above the chat clip and never reach the bubbles/mic/bottom band. */
  assert(HELPER_PILL_Y + HELPER_PILL_H <= HELPER_DOTS_Y && HELPER_DOTS_Y + 8 < HELPER_CHAT_CLIP);
  { helper_view wv = {0}, cv = {.chat = true}; helper_box wb = helper_bot_box(&wv), cb = helper_bot_box(&cv);
    assert(HELPER_CHAT_CLIP <= HELPER_BUBBLE_TOP + 44 && HELPER_DOTS_Y + 8 < wb.y0);
    assert(HELPER_CHAT_CLIP_BOTTOM <= HELPER_STATUS_Y && HELPER_STATUS_Y + 14 <= cb.y0 && cb.y1 <= 420);
    /* Welcome disabled texts fit on screen, below the bot and above the gesture band. */
    assert(wb.y1 < HELPER_BOT_TEXT_Y && HELPER_BOT_TEXT_Y + 40 + 17 < 420 && (int)strlen("Not ready") * 18 <= 300 && (int)strlen("on the host to use this bot") * 9 <= 300); }
  for (int bot = 0; bot < 3; bot++)
    for (int st = HV_IDLE; st <= HV_INTERRUPTED; st++)
      for (int blk = 0; blk <= BR_UNLABELLED; blk++)
        for (int chat = 0; chat < 2; chat++) {
          s = runnable_home();
          helper_restore_bot(&s.helper, bot);
          s.helper.state = (helper_state)st; s.helper.chat = chat; s.helper.block = (unsigned char)blk;
          s.helper.slide = (float)((bot - 1) * 150);
          strcpy(s.helper.transcript, "a question"); strcpy(s.helper.reply, "an answer that wraps onto more than one line of the bubble");
          s.helper.reveal = 20;
          assert(home_render(&s, a + 1, SPARKLES_PIXELS));
          assert(a[0] == 0x1111 && a[SPARKLES_PIXELS + 1] == 0x2222);
        }
  /* The chat never paints over the pill/dots band: long chat vs empty chat differ only below the clip. */
  s = runnable_home();
  s.helper.chat = true; s.helper.state = HV_DONE;
  assert(home_render(&s, b, SPARKLES_PIXELS));
  memset(s.helper.reply, 'w', VOICE_TEXT_MAX);
  for (int i = 0; i < VOICE_TEXT_MAX; i += 6) s.helper.reply[i] = ' ';
  s.helper.reply[VOICE_TEXT_MAX] = 0; strcpy(s.helper.transcript, "tell me everything"); s.helper.reveal = VOICE_TEXT_MAX;
  s.helper.user_in = s.helper.bot_in = 1;
  assert(home_render(&s, c, SPARKLES_PIXELS));
  assert(!differs(b, c, 0, 0, 368, HELPER_CHAT_CLIP) && differs(b, c, 0, HELPER_CHAT_CLIP, 368, HELPER_BUBBLE_BOTTOM) > 1000);
  assert(!differs(b, c, 0, 420, 368, 448));
  /* Disabled mic looks different: a greyed sleeping bot, no orange anywhere on it, reason text visible. */
  s = runnable_home();
  assert(home_render(&s, b, SPARKLES_PIXELS));
  s.helper.block = BR_EXHAUSTED;
  assert(home_render(&s, c, SPARKLES_PIXELS));
  helper_box bx = helper_bot_box(&s.helper);
  assert(differs(b, c, bx.x0, bx.y0, bx.x1, bx.y1) > 2000);
  assert(differs(b, c, 60, HELPER_BOT_TEXT_Y - 2, 308, HELPER_BOT_TEXT_Y + 40) > 200);
  int orange = 0;
  for (int yy = bx.y0; yy < bx.y1; yy++) for (int xx = bx.x0; xx < bx.x1; xx++) orange += c[yy * 368 + xx] == HH_ORANGE || c[yy * 368 + xx] == BOT_STOP;
  assert(orange == 0);
  /* Each bot's pill shows its own name (header differs between bots). */
  s = runnable_home();
  assert(home_render(&s, b, SPARKLES_PIXELS));
  helper_restore_bot(&s.helper, 1);
  s.helper.orbs = (orbs_motion){.tint = 1};
  assert(home_render(&s, c, SPARKLES_PIXELS));
  assert(differs(b, c, 120, HELPER_PILL_Y, 248, HELPER_PILL_Y + HELPER_PILL_H) > 50);
  /* Accent tint stays in the white/yellow/orange family: red >= green >= blue in every palette entry. */
  for (int tint = 0; tint < 3; tint++) {
    orbs_build_lut(tint);
    for (int w = 0; w < ORBS_WARM; w++)
      for (int i = 0; i < ORBS_LEVELS; i++) {
        uint16_t v = orbs_lut[0][w][i];
        int R = (v >> 11) * 255 / 31, G = ((v >> 5) & 63) * 255 / 63, B = (v & 31) * 255 / 31;
        assert(R >= G - 10 && G >= B - 10 && R >= 200);
      }
  }
  /* Slide transition shifts the content; the fixed header does not move. */
  s = runnable_home();
  assert(home_render(&s, b, SPARKLES_PIXELS));
  s.helper.slide = 120;
  assert(home_render(&s, c, SPARKLES_PIXELS));
  assert(differs(b, c, 0, 100, 368, 400) > 5000);
  free(a); free(b); free(c);
  puts("bot swipe (clamp, rubber band, slide), swipe-vs-hold, busy lock, per-bot memory, NVS restore, mic gate (no quota/sign-in/plugin), layout: PASS");
  return 0;
}
