/* Host preview of the Ask conversation from the real renderer (continue / scroll / swipe-up pull).
 * Host preview; compile against the named-device core and shared plugin headers. */
#include <stdio.h>
#include "home_render.h"

static void save(const char *dir, const char *name, const uint16_t *p) {
  char path[512];
  snprintf(path, sizeof path, "%s-%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return; }
  fprintf(f, "P6\n368 448\n255\n");
  for (int i = 0; i < SPARKLES_PIXELS; i++) {
    unsigned char rgb[3] = {(unsigned char)((p[i] >> 11) * 255 / 31), (unsigned char)(((p[i] >> 5) & 63) * 255 / 63),
                            (unsigned char)((p[i] & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("%s\n", path);
}
static int64_t now = 1;
static void tick(home_ui *s, float seconds) { for (int i = 0; i < (int)(seconds * 100); i++) helper_tick(&s->helper, now += 10000); }
static void turn(home_ui *s, const char *said, const char *reply) {
  helper_press(&s->helper, now); tick(s, 1.2f); helper_release(&s->helper, now);
  s->helper.want_record = s->helper.want_send = false; helper_sent(&s->helper);
  voice_command c = {.status = VOICE_DONE};
  strcpy(c.id, "0123456789abcdef0123456789abcdef");
  snprintf(c.transcript, sizeof c.transcript, "%s", said);
  snprintf(c.text, sizeof c.text, "%s", reply);
  helper_apply(&s->helper, &c); tick(s, 5);
}
int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "convo";
  static uint16_t p[SPARKLES_PIXELS];
  static home_ui s = {.page = HELPER};
  tick(&s, 2);
  turn(&s, "What's on my calendar tomorrow?", "You have a dentist appointment at 9:30 and a team sync at 2pm.");
  turn(&s, "Move the sync to 3", "Done - the team sync is now at 3pm tomorrow.");
  turn(&s, "Summarise the plan for the week in detail",
       "Monday: finish the board firmware and flash it. Tuesday: test voice on battery, check the reset log on the host. "
       "Wednesday: review the sparkle animation speed with several Hermes sessions open. Thursday: dentist at 9:30, "
       "then the team sync at 3pm. Friday: write up what worked and what did not, and plan the next round of fixes. "
       "Weekend: nothing scheduled - enjoy it.");
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "1-newest", p);
  s.helper.scroll_max = helper_scroll_extent;
  s.helper.scroll = s.helper.scroll_max / 2;
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "2-scrolled", p);
  s.helper.scroll = s.helper.scroll_max;
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "3-oldest", p);
  s.helper.scroll = 0; s.helper.pull = 90; s.helper.scrolling = true;
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "4-pull-new", p);
  s.helper.scrolling = false; helper_new_session(&s.helper); tick(&s, 1);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "5-new-chat", p);
  return 0;
}
