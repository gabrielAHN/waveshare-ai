/* Host preview: every stage of a CONTINUED turn on top of an existing conversation (real renderer).
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
  const char *dir = argc > 1 ? argv[1] : "cont";
  static uint16_t p[SPARKLES_PIXELS];
  static home_ui s = {.page = HELPER};
  helper_view *h = &s.helper;
  tick(&s, 2);
  turn(&s, "What's on my calendar tomorrow?", "You have a dentist appointment at 9:30 and a team sync at 2pm.");
  turn(&s, "Move the sync to 3", "Done - the team sync is now at 3pm tomorrow, and I told the team.");
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "0-done", p);
  /* continue: hold the mic */
  helper_press(h, now);
  for (int i = 0; i < HELPER_WAVE; i++) h->wave[i] = (uint8_t)(120 + 120 * ((i * 7) % 5) / 4);
  h->level_milli = 900; tick(&s, .6f);
  for (int i = 0; i < HELPER_WAVE; i++) h->wave[i] = (uint8_t)(120 + 120 * ((i * 7) % 5) / 4);
  h->level_milli = 900;
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "1-listening", p);
  helper_release(h, now); h->want_record = h->want_send = false; tick(&s, .3f);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "2-sending", p);
  helper_sent(h);
  voice_command c = {.status = VOICE_RUNNING};
  strcpy(c.id, "0123456789abcdef0123456789abcdef");
  snprintf(c.transcript, sizeof c.transcript, "%s", "And what about the day after tomorrow?");
  helper_apply(h, &c); tick(&s, 1.f);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "3-thinking", p);
  c.status = VOICE_DONE;
  snprintf(c.text, sizeof c.text, "%s", "The day after is clear except for a 4pm reminder to call the plumber about the kitchen sink.");
  helper_apply(h, &c); tick(&s, .5f);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "4-typing", p);
  tick(&s, 5);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "5-done", p);
  return 0;
}
