#include <assert.h>
#include <stdio.h>
#include "home_render.h"

static int64_t t;
static void sample(home_ui *s, bool down, int x, int y) { home_sample(s, t += 10000, down, x, y); }
static void tap(home_ui *s, int x, int y) { sample(s, true, x, y); sample(s, false, x, y); }
static void swipe(home_ui *s, int x0, int x1) { sample(s, true, x0, 200); sample(s, true, x1, 200); sample(s, false, x1, 200); if(s->page==HOME)for(int i=0;i<25;i++)sample(s,false,0,0); }
static void hold(home_ui *s, int x, int y, int ms) {
  sample(s, true, x, y);
  for (int i = 0; i < ms / 10; i++) sample(s, true, x, y);
  sample(s, false, x, y);
}
/* Finger down on the bot and held still until the arm delay starts the recording (0.4 s). */
static void arm(home_ui *s, int x, int y) { for (int i = 0; i <= 41; i++) sample(s, true, x, y); }
static void edge_home(home_ui *s) { sample(s, true, 180, 440); sample(s, true, 184, 224); sample(s, false, 184, 224); }  /* bottom-UP Home */
static home_ui runnable_home(void) {
  home_ui s = {.page = HELPER};
  s.bots.data.valid = true; s.bots.data.count = 3; s.bots.data.received_us = t;
  const char *ids[3] = {"helper", "atlas", "coding"};
  for (int i = 0; i < 3; i++) {
    strcpy(s.bots.data.b[i].id, ids[i]);
    s.bots.data.b[i].available = true;
  }
  return s;
}

static int differs(const uint16_t *a, const uint16_t *b, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) n += a[y * 368 + x] != b[y * 368 + x];
  return n;
}

int main(void) {
  /* Four tiles, swipe order Sparkles / Helper / Sensor / Settings; same tap region opens each. */
  home_ui s = (home_ui){.connected=true,.saved=true,.pair={.state=PAIR_ENROLLED_UNPAIRED},.phone={.st={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED}}};  /* Ask/Sensor ON: Wi-Fi, a linked host and a sign-in answer (loading_state_test) */
  assert(HOME_TILES == 4 && home_tiles[2].page == SENSORS && home_tiles[HOME_TILE_SETTINGS].page == SETTINGS);
  tap(&s, 180, 220);
  assert(s.page == SPARKLES);
  edge_home(&s);
  swipe(&s, 280, 80);
  assert(s.tile == 1);
  tap(&s, 30, 80);
  assert(s.page == HELPER);
  edge_home(&s);
  assert(s.page == HOME && s.tile == 1);
  swipe(&s, 280, 80);
  assert(s.tile == 2);
  tap(&s, 180, 220);
  assert(s.page == SENSORS && s.sensors.refresh == false);
  tap(&s, 180, 220);
  assert(s.page == SENSORS && s.sensors.refresh);  /* a tap on the Sensor page re-reads at once */
  edge_home(&s);
  assert(s.page == HOME && s.tile == 2);
  swipe(&s, 280, 80);
  assert(s.tile == 3);
  swipe(&s, 280, 80);
  assert(s.tile == 3);  /* clamped at the last tile */
  tap(&s, 339, 367);
  assert(s.page == SETTINGS);
  edge_home(&s);
  swipe(&s, 80, 280);
  swipe(&s, 80, 280);
  swipe(&s, 80, 280);
  swipe(&s, 80, 280);
  assert(s.tile == 0);
  tap(&s, 8, 220);
  tap(&s, 180, 430);
  assert(s.page == HOME);  /* outside the tap region */

  /* Helper: hold the bot to record, release to send; short holds are cancelled. */
  s = runnable_home();
  assert(s.helper.state == HV_IDLE);
  { helper_box bb = helper_bot_box(&s.helper);
    assert(helper_bot_hit(&s.helper, HELPER_BOT_X, HELPER_BOT_Y) && !helper_bot_hit(&s.helper, bb.x1 + HELPER_BOT_SLOP + 2, HELPER_BOT_Y)); }
  sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_IDLE && !s.helper.want_record);  /* armed, not yet recording */
  arm(&s, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_LISTENING && s.helper.want_record && !s.helper.want_send);
  for (int i = 0; i < 20; i++) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  sample(&s, false, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_IDLE && s.helper.want_cancel && !s.helper.want_send && !s.helper.note[0]);
  s.helper.want_record = s.helper.want_cancel = false;
  hold(&s, HELPER_BOT_X + 20, HELPER_BOT_Y - 20, 1200);
  assert(s.helper.state == HV_TRANSCRIBING && s.helper.want_send && !s.helper.want_cancel);
  assert(s.page == HELPER);  /* a long hold is not a tap and never navigates */
  /* Presses off the button, and the live palette/contact path, are untouched. */
  s = runnable_home();
  hold(&s, 30, 400, 800);
  assert(s.helper.state == HV_IDLE && !s.helper.want_record && s.input.scene.strength == 0);
  /* Auto-stop at 15 s of recording while still holding. */
  arm(&s, HELPER_BOT_X, HELPER_BOT_Y);
  for (int i = 0; i < 1498; i++) sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_LISTENING);
  sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  sample(&s, true, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_TRANSCRIBING && s.helper.want_send);
  sample(&s, false, HELPER_BOT_X, HELPER_BOT_Y);
  assert(s.helper.state == HV_TRANSCRIBING);
  s.helper.want_record = s.helper.want_send = false;  /* consumed by the voice worker */

  /* Worker updates: running shows transcript + reply; Stop is visible and targets the command. */
  voice_command c = {.status = VOICE_RUNNING};
  strcpy(c.id, "0123456789abcdef0123456789abcdef");
  strcpy(c.transcript, "what's on my calendar");
  strcpy(c.text, "You have two meetings.");
  helper_apply(&s.helper, &c);
  assert(s.helper.state == HV_RUNNING && !strcmp(s.helper.transcript, c.transcript) && !strcmp(s.helper.reply, c.text));
  assert(s.helper.chat);  /* a sent command switches to the chat layout: the bot moves down, compact */
  assert(helper_stop_visible(&s.helper) && helper_stop_hit(&s.helper, HELPER_BOT_X, HELPER_CHAT_BOT_Y));
  assert(!helper_stop_hit(&s.helper, HELPER_BOT_X, HELPER_BOT_Y));  /* old centre is now the chat */
  tap(&s, HELPER_BOT_X, HELPER_CHAT_BOT_Y);
  assert(s.helper.want_stop && s.helper.state == HV_STOPPING && !s.helper.want_record);
  s.helper.want_stop = false;
  tap(&s, HELPER_BOT_X, HELPER_CHAT_BOT_Y);
  assert(!s.helper.want_stop && !s.helper.want_record);  /* no repeat stop, no new record while stopping */
  c.status = VOICE_INTERRUPTED;
  helper_apply(&s.helper, &c);
  assert(s.helper.state == HV_INTERRUPTED && !helper_stop_visible(&s.helper));
  c.status = VOICE_DONE;
  helper_apply(&s.helper, &c);
  assert(s.helper.state == HV_INTERRUPTED);  /* terminal state is sticky for that command */
  c.status = VOICE_ERROR;
  strcpy(c.text, "local transcription unavailable");
  helper_apply(&s.helper, &c);
  assert(s.helper.state == HV_INTERRUPTED);
  s.bots.data.received_us = t; /* Fresh successful poll after the 15 s recording. */
  arm(&s, HELPER_BOT_X, HELPER_CHAT_BOT_Y);
  assert(s.helper.state == HV_LISTENING && !s.helper.transcript[0] && !s.helper.reply[0]);
  sample(&s, false, HELPER_BOT_X, HELPER_CHAT_BOT_Y);
  helper_fail(&s.helper, "Bridge unreachable");
  assert(s.helper.state == HV_ERROR && !strcmp(s.helper.note, "Bridge unreachable"));
  /* Error frames from the bridge surface the error text. */
  s.helper.state = HV_TRANSCRIBING;
  helper_apply(&s.helper, &c);
  assert(s.helper.state == HV_ERROR && !strcmp(s.helper.note, "local transcription unavailable"));

  /* Leaving the page while listening cancels the recording (never sends). */
  s = runnable_home();
  arm(&s, HELPER_BOT_X, HELPER_BOT_Y);
  s.helper.want_record = false;
  sample(&s, false, HELPER_BOT_X, HELPER_BOT_Y);
  s.helper.state = HV_LISTENING;
  s.helper.holding = false;
  edge_home(&s);
  assert(s.page == HOME && s.helper.state == HV_IDLE && s.helper.want_cancel);

  /* USB test hook drives the same state machine as the button (source recorded, not a touch). */
  s = runnable_home();
  assert(helper_command(&s.helper, t, HELPER_CMD_PRESS) && s.helper.state == HV_LISTENING);
  t += 900000;
  assert(helper_command(&s.helper, t, HELPER_CMD_RELEASE) && s.helper.state == HV_TRANSCRIBING);
  assert(!helper_command(&s.helper, t, 9));

  /* Text wrapping for the 368 px panel. */
  char lines[6][HELPER_COLS + 1];
  int n = helper_wrap("the quick brown fox jumps over the lazy dog and keeps running far away", HELPER_COLS, 6, lines);
  assert(n == 2);
  for (int i = 0; i < n; i++) assert(strlen(lines[i]) <= HELPER_COLS);
  n = helper_wrap("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 20, 2, lines);
  assert(n == 2 && strlen(lines[0]) == 20 && !strcmp(lines[1] + 17, "..."));
  n = helper_wrap("caf\xc3\xa9 ok", 20, 2, lines);
  assert(n == 1 && !strcmp(lines[0], "caf? ok"));
  n = helper_wrap("one\ntwo", 20, 3, lines);
  assert(n == 2 && !strcmp(lines[1], "two"));
  assert(helper_wrap("", 20, 3, lines) == 0);

  /* Rendering: all states render within bounds; stop button, transcript and reply are visible. */
  uint16_t *a = malloc((SPARKLES_PIXELS + 2) * 2), *b = malloc(SPARKLES_PIXELS * 2);
  assert(a && b);
  a[0] = 0x1111;
  a[SPARKLES_PIXELS + 1] = 0x2222;
  s = runnable_home();
  for (int st = HV_IDLE; st <= HV_INTERRUPTED; st++) {
    s.helper.state = (helper_state)st;
    s.helper.level_milli = 700;
    assert(home_render(&s, a + 1, SPARKLES_PIXELS));
    assert(a[0] == 0x1111 && a[SPARKLES_PIXELS + 1] == 0x2222);
  }
  /* Welcome layout: big bot in the centre. Chat layout: the compact working bot at the bottom. */
  s = runnable_home();
  assert(home_render(&s, b, SPARKLES_PIXELS));
  s.helper.chat = true;
  s.helper.state = HV_RUNNING;
  strcpy(s.helper.id, "0123456789abcdef0123456789abcdef");
  assert(home_render(&s, a + 1, SPARKLES_PIXELS));
  assert(differs(a + 1, b, HELPER_BOT_X - 14, HELPER_CHAT_BOT_Y - 14, HELPER_BOT_X + 14, HELPER_CHAT_BOT_Y + 14) > 150);
  /* Your words: right-aligned bubble that slides in. */
  strcpy(s.helper.transcript, "hello there");
  for (int i = 0; i < 60; i++) helper_tick(&s.helper, t += 10000);
  assert(s.helper.user_in == 1.0f && s.helper.bot_in == 1.0f);
  memcpy(b, a + 1, SPARKLES_PIXELS * 2);
  assert(home_render(&s, a + 1, SPARKLES_PIXELS));
  assert(differs(a + 1, b, 184, HELPER_BUBBLE_TOP, 368, HELPER_BUBBLE_BOTTOM) > 300);
  /* Reply types out over time instead of appearing at once, left-aligned. */
  strcpy(s.helper.reply, "General Kenobi, you are a bold one. The porch light is now on.");
  size_t rl = strlen(s.helper.reply);
  helper_tick(&s.helper, t += 10000);
  assert(s.helper.reveal > 0 && s.helper.reveal < 3);
  for (int i = 0; i < 40; i++) helper_tick(&s.helper, t += 10000);
  assert(s.helper.reveal > 20 && s.helper.reveal < (float)rl);
  assert(helper_revealing(&s.helper));
  for (int i = 0; i < 200; i++) helper_tick(&s.helper, t += 10000);
  assert(s.helper.reveal == (float)rl && !helper_revealing(&s.helper));
  memcpy(b, a + 1, SPARKLES_PIXELS * 2);
  assert(home_render(&s, a + 1, SPARKLES_PIXELS));
  assert(differs(a + 1, b, 0, HELPER_BUBBLE_TOP, 184, HELPER_BUBBLE_BOTTOM) > 300);
  /* A long reply keeps its newest lines visible and never draws over the button/gesture band. */
  memset(s.helper.reply, 'w', VOICE_TEXT_MAX);
  for (int i = 0; i < VOICE_TEXT_MAX; i += 6) s.helper.reply[i] = ' ';
  s.helper.reply[VOICE_TEXT_MAX] = 0;
  for (int i = 0; i < 1000; i++) helper_tick(&s.helper, t += 10000);
  assert(home_render(&s, a + 1, SPARKLES_PIXELS));
  assert(a[0] == 0x1111 && a[SPARKLES_PIXELS + 1] == 0x2222);
  { helper_view ch = {.chat = true}; helper_box cb = helper_bot_box(&ch);  /* bubbles end above the status line; the compact bot clears it and the gesture band */
    assert(HELPER_CHAT_CLIP_BOTTOM <= HELPER_STATUS_Y && HELPER_STATUS_Y + 14 <= cb.y0 && cb.y1 <= 420); }
  /* A too-short hold with nothing on screen goes back to the big centred bot. */
  s.helper = (helper_view){.chat = true};
  helper_press(&s.helper, 1000);
  helper_release(&s.helper, 2000);
  assert(!s.helper.chat && s.helper.state == HV_IDLE);
  /* No page title: the top band is the orbs plus only the small bot pill + dots (centre). */
  s = runnable_home();
  assert(home_render(&s, a + 1, SPARKLES_PIXELS));
  orbs_render(&s.helper.orbs, b);
  assert(!differs(a + 1, b, 0, 0, 120, HELPER_CHAT_CLIP) && !differs(a + 1, b, 248, 0, 368, HELPER_CHAT_CLIP));
  assert(differs(a + 1, b, 120, HELPER_PILL_Y, 248, HELPER_DOTS_Y + 8) > 100);
  /* Animated page: never treated as a still frame; session markers never overlay it. */
  home_ui same = s;
  assert(!home_visual_equal(&s, &same));
  s.live.valid = true;
  s.live.count = 3;
  s.live_now_us = s.live.received_us = 1;
  assert(home_live_points(&s) == 0);
  free(a);
  free(b);
  puts("4-tile Home, hold-to-talk (0.4 s arm) + auto-stop, chat layout (bubbles, typewriter reply, tap-the-bot stop), no title, wrap, render: PASS");
}
