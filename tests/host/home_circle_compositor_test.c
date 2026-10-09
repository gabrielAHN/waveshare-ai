/* Real Home inside a centered circular aperture; real outgoing page outside. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"

#define N SPARKLES_PIXELS
static home_ui s, page, before;
static uint16_t frame[N], home[N], outgoing[N], snap_px[N], home_px[N], last[N];

static void ready(home_page which, int mode, int accent) {
  memset(&s, 0, sizeof s);
  s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED;
  s.pair.live_http = 200; s.pair.live_ok = true;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED;
  s.phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s.phone_ha.st = s.phone.st;
#endif
  s.page = which; s.settings_tab = SETTINGS_SOUND;
  s.tile = home_tile_index(SETTINGS);
  s.theme_mode = (uint8_t)mode; s.accent = (uint8_t)accent;
}

static void references(void) {
  page = s; page.slide_kind = HOME_MOTION_NONE;
  assert(home_render(&page, outgoing, N));
  page.page = HOME;
  assert(home_render(&page, home, N));
}

static void begin_partial(int x, int y, int travel) {
  home_sample(&s, 1000000, true, x, y);
  home_sample(&s, 1100000, true, x, y - 12);
  home_sample(&s, 1200000, true, x < 184 ? x + 90 : x - 90, y - travel);
  assert(s.edge && s.slide_kind == HOME_MOTION_DRAG);
}

static void pixels_and_edge(void) {
  ready(SETTINGS, THEME_LIGHT, 1);
  references();
  begin_partial(60, 440, 108);
  home_snapshot snap = {.px = snap_px, .home = home_px};
  assert(home_compose(&s, frame, N, &snap, outgoing, SETTINGS));
  assert(frame[224 * 368 + 184] == home[224 * 368 + 184]);
  assert(frame[0] == outgoing[0]);
  int radius = (int)(s.blob.w * .5f);
  int pure_home = 0, pure_outgoing = 0, edge = 0;
  for (int x = 0; x < 368; x++) {
    uint16_t got = frame[224 * 368 + x];
    int d = abs(x - 184);
    if (d + 3 < radius) { assert(got == home[224 * 368 + x]); pure_home++; }
    else if (d > radius + 3) { assert(got == outgoing[224 * 368 + x]); pure_outgoing++; }
    else if (got != home[224 * 368 + x] && got != outgoing[224 * 368 + x]) edge++;
  }
  assert(pure_home > 20 && pure_outgoing > 20 && edge > 0 && edge <= 12);
  puts("circle compositor pixels: Home inside, outgoing outside, narrow accent AA edge");
}

static void source_selection(void) {
  ready(HELPER, THEME_DARK, 1);
  references();
  memcpy(last, outgoing, sizeof last);
  begin_partial(300, 447, 80);
  home_sample(&s, 1210000, false, 0, 0);
  assert(s.page == HOME && s.slide_kind == HOME_MOTION_OUT);

  home_snapshot reconstructed = {.px = snap_px, .home = home_px};
  before = s;
  assert(home_compose(&s, frame, N, &reconstructed, NULL, -1));
  assert(!memcmp(&s, &before, sizeof s));
  assert(reconstructed.valid && reconstructed.page == HELPER);
  assert(frame[224 * 368 + 184] == home[224 * 368 + 184]);
  assert(frame[0] == outgoing[0]);

  home_snapshot last_only = {.home = home_px};
  before = s;
  assert(home_compose(&s, frame, N, &last_only, last, HELPER));
  assert(!memcmp(&s, &before, sizeof s));
  assert(frame[224 * 368 + 184] == home[224 * 368 + 184]);
  assert(frame[0] == outgoing[0]);

  home_snapshot no_buffers = {0};
  before = s;
  assert(home_compose(&s, frame, N, &no_buffers, NULL, -1));
  assert(!memcmp(&s, &before, sizeof s));
  assert(frame[224 * 368 + 184] == home[224 * 368 + 184]);
  assert(frame[0] == outgoing[0]);

  before = s;
  assert(home_compose(&s, frame, N, NULL, NULL, -1));
  assert(!memcmp(&s, &before, sizeof s));
  assert(frame[224 * 368 + 184] == home[224 * 368 + 184]);
  assert(frame[0] == outgoing[0]);
  puts("circle source: reconstruction, last-frame, missing-buffer and null-snapshot dual-source paths preserve state");
}

static void endpoints_and_cancel(void) {
  ready(SETTINGS, THEME_LIGHT, 1);
  references();
  begin_partial(32, 440, 216);
  home_snapshot snap = {.px = snap_px, .home = home_px};
  assert(home_compose(&s, frame, N, &snap, outgoing, SETTINGS));
  assert(!memcmp(frame, home, sizeof frame));
  home_sample(&s, 1210000, false, 0, 0);
  home_sample(&s, 1211000, false, 0, 0);
  assert(s.page == HOME && s.slide_kind == HOME_MOTION_NONE);
  assert(home_compose(&s, frame, N, &snap, outgoing, SETTINGS));
  assert(!memcmp(frame, home, sizeof frame));

  ready(SETTINGS, THEME_DARK, 1);
  references();
  begin_partial(335, 447, 36);
  home_sample(&s, 1210000, false, 0, 0);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK);
  snap = (home_snapshot){.px = snap_px, .home = home_px};
  assert(home_compose(&s, frame, N, &snap, outgoing, SETTINGS));
  assert(frame[224 * 368 + 184] == home[224 * 368 + 184]);
  assert(frame[0] == outgoing[0]);
  home_sample(&s, 1430000, false, 0, 0);
  assert(s.slide_kind == HOME_MOTION_NONE && s.page == SETTINGS);
  assert(home_compose(&s, frame, N, &snap, outgoing, SETTINGS));
  assert(!memcmp(frame, outgoing, sizeof frame));
  puts("circle endpoints: full cover is exact Home; completed cancel is exact outgoing page");
}

int main(int argc, char **argv) {
  const char *section=argc>1?argv[1]:NULL;
  if(!section||!strcmp(section,"pixels"))pixels_and_edge();
  if(!section||!strcmp(section,"source"))source_selection();
  if(!section||!strcmp(section,"endpoints"))endpoints_and_cancel();
  if(section&&strcmp(section,"pixels")&&strcmp(section,"source")&&strcmp(section,"endpoints"))
    assert(!"unknown section");
  return 0;
}
