/* Host preview of the Helper page (orbs + UI) from the real renderer. Writes PPM files.
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


static void tick(home_ui *s, float seconds) {
  static int64_t now = 1;
  for (int i = 0; i < (int)(seconds * 100); i++) helper_tick(&s->helper, now += 10000);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "helper-preview";
  static uint16_t p[SPARKLES_PIXELS];
  home_ui s = {.page = HELPER};
  tick(&s, 4);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "1-welcome", p);
  helper_press(&s.helper, s.helper.stamp_us);
  for (int i = 0; i < 150; i++) { s.helper.level_milli = 300 + (unsigned)(600 * fabsf(sinf(i * 0.21f))); tick(&s, 0.01f); }
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "2-listening", p);
  helper_release(&s.helper, s.helper.stamp_us);
  tick(&s, 0.6f);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "3-sending", p);
  voice_command c = {.status = VOICE_RUNNING};
  strcpy(c.id, "0123456789abcdef0123456789abcdef");
  strcpy(c.transcript, "What's on my calendar tomorrow morning?");
  helper_apply(&s.helper, &c);
  tick(&s, 1.0f);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "4-thinking", p);
  strcpy(c.text, "You have two meetings tomorrow morning: a design review at 9:30 and a 1:1 with Sam at 11. Want me to block focus time after lunch as well?");
  helper_apply(&s.helper, &c);
  tick(&s, 0.9f);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "5-typing", p);
  c.status = VOICE_DONE;
  helper_apply(&s.helper, &c);
  tick(&s, 3);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "6-done", p);
  s.helper = (helper_view){.orbs = s.helper.orbs, .chat = true, .state = HV_ERROR, .stamp_us = s.helper.stamp_us};
  strcpy(s.helper.transcript, "Turn on the porch light");
  strcpy(s.helper.note, "Hermes offline - try later");
  tick(&s, 1);
  home_render(&s, p, SPARKLES_PIXELS); save(dir, "7-error", p);
  home_ui h = {0};
  h.tile = 1;
  home_render(&h, p, SPARKLES_PIXELS); save(dir, "8-home-tile", p);
  return 0;
}
