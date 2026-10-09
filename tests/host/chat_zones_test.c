/* Continued talk: the chat bubbles and the controls (status row, the compact bot with its listening
 * pulse, working orbit and stop badge, and the listening level bars) must never paint the same pixel, in any state or
 * animation phase, with a full conversation on screen. Renders each part alone over the same orbs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
static uint16_t base[SPARKLES_PIXELS], chat[SPARKLES_PIXELS], ctl[SPARKLES_PIXELS], head[SPARKLES_PIXELS];
static int shared(helper_view *h, int *row) {
  orbs_render(&h->orbs, base);
  memcpy(chat, base, sizeof base); helper_chat(chat, h, HELPER_CHAT_CLIP);
  memcpy(ctl, base, sizeof base); helper_chat_controls(h, ctl, BOT_LOOK_CODING, "");
  memcpy(head, base, sizeof base); helper_bot_header(head, h, "Helper");
  int n = 0;
  for (int i = 0; i < SPARKLES_PIXELS; i++) {
    int c = chat[i] != base[i], k = ctl[i] != base[i], t = head[i] != base[i];
    if (c + k + t > 1) { if (!n) *row = i / 368; n++; }
  }
  return n;
}
int main(void) {
  static helper_view h;
  h.chat = true; h.cont = true; h.nbots = 3;
  static char longtxt[900];
  for (size_t i = 0; i + 1 < sizeof longtxt; i++) longtxt[i] = i % 9 == 8 ? ' ' : (char)('a' + i % 7);
  for (int t = 0; t < 3; t++) { helper_log_push(&h.log, 'U', "a question from me about tomorrow"); helper_log_push(&h.log, 'B', longtxt); }
  struct { helper_state st; bool id; bool block; const char *name; } cases[] = {
    {HV_DONE, 0, 0, "done"}, {HV_LISTENING, 0, 0, "listening"}, {HV_TRANSCRIBING, 0, 0, "sending"},
    {HV_TRANSCRIBING, 1, 0, "sent"}, {HV_RUNNING, 1, 0, "running"}, {HV_STOPPING, 1, 0, "stopping"},
    {HV_ERROR, 0, 0, "error"}, {HV_INTERRUPTED, 0, 0, "stopped"}, {HV_DONE, 0, 1, "no-quota"},
  };
  int checked = 0;
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    h.state = cases[c].st; h.block = cases[c].block ? BR_EXHAUSTED : 0;
    strcpy(h.id, cases[c].id ? "0123456789abcdef0123456789abcdef" : "");
    strcpy(h.transcript, cases[c].st == HV_LISTENING ? "" : "and what about the day after tomorrow?");
    strcpy(h.reply, cases[c].st == HV_DONE ? "The day after is clear except for a 4pm call." : "");
    helper_note(&h, cases[c].st == HV_ERROR ? "Could not reach Hermes" : "");
    h.reveal = (float)strlen(h.reply); h.user_in = h.bot_in = 1;
    h.level_milli = 1000;
    for (int i = 0; i < HELPER_WAVE; i++) h.wave[i] = 250;  /* loudest bars */
    for (int pull = 0; pull <= 90; pull += 90)
      for (int scroll = 0; scroll <= 400; scroll += 200)
        for (int f = 0; f < 24; f++) {  /* orbit / halo / dots phases */
          h.orbs.time = f * 0.137f; h.pull = pull; h.scroll = scroll;
          int row = -1, n = shared(&h, &row);
          if (n) { fprintf(stderr, "%s pull=%d scroll=%d f=%d: %d shared px, first at y=%d\n", cases[c].name, pull, scroll, f, n, row); return 1; }
          checked++;
        }
  }
  printf("chat zones: bubbles, controls and header never share a pixel (%d frames): PASS\n", checked);
  return 0;
}
