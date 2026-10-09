/* The WGS1 gesture replays in home_wifi.c must behave like a finger: drag up at the newest message =
 * new chat, drag down = older, the Home pull (bottom-edge UP to centre) = Home, and the Home tile
 * swipe + tap reopens Ask with the SAME conversation. Mirrors gesture_replay.h sample-for-sample. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
#include "bots_command.h"
static int64_t t = 1000000;
static uint16_t px[SPARKLES_PIXELS];
static void replay(home_ui *ui, int g) {  /* copy of feed_gesture() touch sequences */
  if (ui->down) { home_sample(ui, t, false, 0, 0); t += 10000; }
  if (g == GESTURE_NEW || g == GESTURE_OLDER) {
    int y0 = g == GESTURE_NEW ? 300 : 140, y1 = g == GESTURE_NEW ? 140 : 300;
    for (int k = 0; k <= 8; k++) { home_sample(ui, t, true, 184, y0 + (y1 - y0) * k / 8); t += 10000; }
    home_sample(ui, t, false, 184, y1);
  } else if (g == GESTURE_HOME) {
    for (int k = 0; k <= 8; k++) { home_sample(ui, t, true, 184, 440 - 27 * k); t += 10000; }
    home_sample(ui, t, false, 0, 0);  /* released with no point, like the panel (gesture_replay.h) */
  } else if (g == GESTURE_OPEN_ASK && ui->page == HOME) {
    for (int guard = 0; guard < HOME_TILES && ui->tile != 1; guard++) {
      int dir = ui->tile < 1 ? 1 : -1;
      for (int k = 0; k <= 8; k++) { home_sample(ui, t, true, 184 - dir * 12 * k, 220); t += 10000; }
      home_sample(ui, t, false, 184 - dir * 96, 220); t += 10000;
      for (int k = 0; k < 40; k++) { home_sample(ui, t, false, 0, 0); t += 10000; }
    }
    home_sample(ui, t, true, 184, 220); t += 30000; home_sample(ui, t, true, 184, 220); t += 30000; home_sample(ui, t, false, 184, 220);
  }
  t += 10000;
}
static void idle(home_ui *s, int ms) { for (int i = 0; i < ms / 10; i++) { t += 10000; home_sample(s, t, false, 0, 0); } }
static void paint(home_ui *s) { s->live_now_us = t; assert(home_render(s, px, SPARKLES_PIXELS)); s->helper.scroll_max = helper_scroll_extent; }

int main(void) {
  home_ui s = (home_ui){.connected=true,.saved=true,.pair={.state=PAIR_ENROLLED_UNPAIRED},.phone={.st={.valid=true,.state=PH_AUTHORIZED,.flags=PHONE_FLAG_REQUIRED}}}; s.page = HELPER; s.tile = 1;  /* Ask ON: Wi-Fi, linked host, signed in */
  s.bots.data = (bots_data){.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", "atlas", "coding"};
  for (int i = 0; i < 3; i++) { strcpy(s.bots.data.b[i].id, ids[i]); strcpy(s.bots.data.b[i].name, ids[i]); s.bots.data.b[i].available = true; s.bots.data.b[i].reset_s = BOTS_NONE32; }
  /* a finished, long conversation */
  helper_view *h = &s.helper;
  static char longtxt[1200];
  for (size_t i = 0; i + 1 < sizeof longtxt; i++) longtxt[i] = i % 9 == 8 ? ' ' : 'a' + (char)(i % 7);
  for (int turn = 0; turn < 3; turn++) {
    helper_log_push(&h->log, 'U', "a question from me"); helper_log_push(&h->log, 'B', longtxt);
  }
  h->chat = true; h->cont = true; h->state = HV_DONE;
  idle(&s, 100); paint(&s);
  assert(h->scroll_max > 0);
  unsigned used = h->log.used;
  replay(&s, GESTURE_OLDER); idle(&s, 50);
  assert(s.page == HELPER && h->scroll > 0 && h->cont && h->log.used == used);  /* read older, nothing lost */
  replay(&s, GESTURE_HOME); idle(&s, 400);
  assert(s.page == HOME);
  replay(&s, GESTURE_OPEN_ASK); idle(&s, 400);
  assert(s.page == HELPER && h->cont && h->chat && h->log.used == used);         /* back to the same chat */
  paint(&s);
  s.tile = 0; replay(&s, GESTURE_HOME); idle(&s, 400); assert(s.page == HOME);
  replay(&s, GESTURE_OPEN_ASK); idle(&s, 400); assert(s.page == HELPER && s.tile == 1 && h->log.used == used);
  replay(&s, GESTURE_HOME); idle(&s, 400); assert(s.page == HOME);
  s.tile = 2; replay(&s, GESTURE_OPEN_ASK); idle(&s, 400); assert(s.page == HELPER && s.tile == 1 && h->log.used == used);
  /* Drag up from the newest message = new chat. */
  h->scroll = 0; paint(&s);
  replay(&s, GESTURE_NEW); idle(&s, 50);
  assert(!h->cont && h->log.used == 0 && !h->chat);
  puts("WGS1 gesture replays (older, Home, reopen Ask keeps chat, new chat): PASS");
  return 0;
}
