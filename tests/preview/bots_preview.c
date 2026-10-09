/* Host-only simulation of the Ask-page bots with the actual native renderer: each bot idle, mid-swipe
 * transition, rubber band, no-quota with reset, sign-in, plugin-update (welcome + chat), done chat per
 * bot, listening. Writes evidence/bots-*.ppm. Texts/numbers are illustrative, NOT account data; NOT
 * physical screenshots.
 *   cc -O2 -Idevices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main -Iplugins/hermes/firmware -Iplugins/home_assistant/firmware tests/preview/bots_preview.c devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/components/qrcodegen/qrcodegen.c -lm -o build/bots_preview
 *   build/bots_preview
 *   /usr/bin/python3 tests/preview/bots_sheet.py   -> evidence/bots-sheet.png */
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"

static int64_t now = 1;
static void save(home_ui *s, const char *name) {
  static uint16_t p[SPARKLES_PIXELS];
  s->live_now_us = now;
  s->bots.data.received_us = now; /* synthetic current response, not an expired fixture */
  home_bots_sync(s, now);
  assert(home_render(s, p, SPARKLES_PIXELS));
  char path[160];
  snprintf(path, sizeof path, "evidence/bots-%s.ppm", name);
  FILE *f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n368 448\n255\n");
  for (int i = 0; i < SPARKLES_PIXELS; i++) { unsigned v = p[i]; fputc((v >> 11) * 255 / 31, f); fputc(((v >> 5) & 63) * 255 / 63, f); fputc((v & 31) * 255 / 31, f); }
  fclose(f);
  printf("%s\n", path);
}
static void tick(home_ui *s, float seconds) {
  for (int i = 0; i < (int)(seconds * 100); i++) { helper_tick(&s->helper, now += 10000); home_bots_sync(s, now); }
}
static bots_data frame(uint8_t r1, bool a1, uint8_t r2, bool a2, uint32_t reset) {
  bots_data d = {.valid = true, .count = 3, .flags = BOTS_FLAG_PLUGIN, .received_us = now};
  const char *ids[3] = {"helper", "atlas", "coding"}, *names[3] = {"Helper", "Atlas", "Coding"}, *prov[3] = {"OpenRouter", "Claude", "Claude"};
  for (int i = 0; i < 3; i++) { strcpy(d.b[i].id, ids[i]); strcpy(d.b[i].name, names[i]); strcpy(d.b[i].provider, prov[i]); d.b[i].available = true; d.b[i].reset_s = BOTS_NONE32; }
  d.b[1].available = a1; d.b[1].reason = r1; d.b[1].reset_s = reset;
  d.b[2].available = a2; d.b[2].reason = r2; d.b[2].reset_s = reset;
  return d;
}
static void done(home_ui *s, const char *said, const char *reply) {
  helper_press(&s->helper, now);
  tick(s, 1.0f);
  helper_release(&s->helper, now);
  voice_command c = {.status = VOICE_DONE};
  strcpy(c.id, "0123456789abcdef0123456789abcdef");
  strcpy(c.transcript, said);
  strcpy(c.text, reply);
  s->helper.want_record = s->helper.want_send = false;
  helper_apply(&s->helper, &c);
  tick(s, 5);
}
int main(void) {
  home_ui s = {.page = HELPER};
  s.bots.http = 200; s.bots.attempts = 1; s.bots.data = frame(0, true, 0, true, BOTS_NONE32);
  s.input.stamp_us = now;
  tick(&s, 3);
  save(&s, "01-helper-idle");
  helper_swipe(&s.helper, HELPER_SWIPE_NEXT, 0); s.helper.slide = 150; tick(&s, 0.02f);
  save(&s, "02-mid-swipe-helper-to-atlas");
  tick(&s, 1); save(&s, "03-atlas-idle");
  helper_swipe(&s.helper, HELPER_SWIPE_NEXT, 0); tick(&s, 1); save(&s, "04-coding-idle");
  s.helper.slide = helper_drag(&s.helper, -200); save(&s, "05-coding-rubber-band");
  s.helper.slide = 0;
  /* No quota on the Claude bots (with a reset), sign-in, plugin update. */
  s.bots.data = frame(BR_EXHAUSTED, false, BR_EXHAUSTED, false, 2 * 3600 + 600);
  tick(&s, 0.5f); save(&s, "06-coding-no-quota-reset");
  s.bots.data = frame(BR_EXHAUSTED, false, BR_EXHAUSTED, false, BOTS_NONE32);
  tick(&s, 0.1f); save(&s, "07-coding-no-quota-unknown-reset");
  s.bots.data = frame(BR_SIGNIN, false, BR_SIGNIN, false, BOTS_NONE32);
  tick(&s, 0.1f); save(&s, "08-coding-sign-in");
  s.bots.data = frame(BR_PLUGIN, true, BR_PLUGIN, true, BOTS_NONE32);
  tick(&s, 0.1f); save(&s, "09-coding-plugin-update");
  helper_swipe(&s.helper, HELPER_SWIPE_PREV, 0); helper_swipe(&s.helper, HELPER_SWIPE_PREV, 0); tick(&s, 1);
  save(&s, "10-helper-enabled-while-others-blocked");
  /* Done chat per bot (all available). */
  s.bots.data = frame(0, true, 0, true, BOTS_NONE32);
  done(&s, "What's on my calendar?", "You have a design review at 3 and nothing after 5, so the evening is free.");
  save(&s, "11-helper-done-chat");
  helper_swipe(&s.helper, HELPER_SWIPE_NEXT, 0); tick(&s, 1);
  done(&s, "How far is the ferry terminal?", "About 2.4 km: a 30 minute walk along the waterfront, or 9 minutes by bike.");
  save(&s, "12-atlas-done-chat");
  helper_swipe(&s.helper, HELPER_SWIPE_NEXT, 0); tick(&s, 1);
  done(&s, "Did the build pass?", "Yes. All 48 host suites and the firmware build passed; nothing was flashed.");
  save(&s, "13-coding-done-chat");
  s.bots.data = frame(BR_EXHAUSTED, false, BR_EXHAUSTED, false, 47 * 60);
  tick(&s, 0.1f); save(&s, "14-coding-chat-no-quota");
  s.bots.data = frame(0, true, 0, true, BOTS_NONE32); tick(&s, 0.1f);
  helper_swipe(&s.helper, HELPER_SWIPE_PREV, 0); tick(&s, 1);
  save(&s, "15-atlas-history-restored");
  helper_press(&s.helper, now);
  for (int i = 0; i < 120; i++) { s.helper.level_milli = 300 + (unsigned)(600 * fabsf(sinf(i * 0.21f))); tick(&s, 0.01f); }
  save(&s, "16-atlas-listening");
  puts("Ask-page bot host framebuffer simulations saved to evidence/bots-*.ppm; NOT physical screenshots");
  return 0;
}
