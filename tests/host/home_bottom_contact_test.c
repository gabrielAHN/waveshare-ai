/* A bottom-strip contact stays reserved for its full lifetime and can qualify later. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"

static home_ui ui;
static int64_t clock_us;

static void ready(home_page page) {
  memset(&ui, 0, sizeof ui);
  ui.connected = ui.saved = true;
  ui.pair.state = PAIR_ENROLLED_UNPAIRED;
  ui.pair.live_ok = true;
  ui.pair.live_http = 200;
  ui.phone.st.valid = true;
  ui.phone.st.state = PH_AUTHORIZED;
  ui.phone.st.flags = PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  ui.phone_ha.st = ui.phone.st;
#endif
  ui.page = page;
  ui.settings_tab = SETTINGS_SOUND;
  ui.tile = home_tile_index(page);
  clock_us = 1000000;
}

static void at(int64_t dt_us, bool down, int x, int y) {
  clock_us += dt_us;
  home_sample(&ui, clock_us, down, x, y);
}

static void deadline(void) {
  ready(SETTINGS);
  at(10000, true, 184, 440);
  at(160000, true, 184, 440);
  assert(ui.pull_wait && !ui.edge && !ui.consumed && ui.pull_n == 2);
  at(10000, true, 184, 400);
  assert(ui.edge && !ui.pull_wait && !ui.consumed);
}

static void buffer_limit(void) {
  ready(SETTINGS);
  at(10000, true, 184, 440);
  for (int i = 1; i < HOME_PULL_BUF + 8; i++) {
    at(4000, true, 184 + (i & 1), 440);
    assert(ui.pull_wait && !ui.edge && !ui.consumed);
  }
  assert(ui.pull_n == HOME_PULL_BUF);
  at(4000, true, 184, 400);
  assert(ui.edge && !ui.pull_wait && !ui.consumed);
}

static void direction(void) {
  ready(SETTINGS);
  at(10000, true, 184, 440);
  at(10000, true, 196, 439);
  assert(ui.pull_wait && !ui.edge && !ui.consumed);
  at(10000, true, 190, 400);
  assert(ui.edge && !ui.pull_wait && !ui.consumed);
  home_blob expected = home_blob_pull(184, 440, 40.f, 6.f);
  assert(fabsf(ui.blob.w - expected.w) < .001f);

  ready(SETTINGS);
  at(10000, true, 184, 440);
  at(10000, true, 192, 428);
  assert(ui.pull_wait && !ui.edge && !ui.consumed);
  at(10000, true, 184, 400);
  assert(ui.edge && !ui.consumed);
}

static void full_contact(void) {
  ready(SPARKLES);
  at(10000, true, 184, 447);
  at(10000, true, 196, 446);
  for (int i = 0; i < 40; i++) at(10000, true, 184, 447);
  assert(ui.pull_wait && ui.pull_n == HOME_PULL_BUF);
  assert(ui.input.scene.trail_count == 0 && ui.input.scene.strength == 0);
  for (int y = 446; y >= HOME_PULL_CENTER_Y; y--) at(10000, true, 184, y);
  for (int i = 0; i < 12; i++) at(10000, true, 184, HOME_PULL_CENTER_Y);
  assert(ui.page == SPARKLES && ui.edge && home_blob_shown(&ui));
  assert(ui.input.scene.trail_count == 0 && ui.input.scene.strength == 0);
  at(10000, false, 0, 0);
  assert(ui.page == HOME);

  ready(SENSORS);
  at(10000, true, 184, 440);
  at(10000, true, 230, 438);
  at(10000, true, 184, 447);
  at(10000, false, 0, 0);
  assert(ui.page == SENSORS && !ui.edge && !ui.sensors.refresh);
}

int main(int argc, char **argv) {
  static const struct { const char *name; void (*run)(void); } cases[] = {
    {"deadline", deadline}, {"buffer", buffer_limit},
    {"direction", direction}, {"full", full_contact},
  };
  int ran = 0;
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    if (argc > 1 && strcmp(argv[1], cases[i].name)) continue;
    cases[i].run();
    ran++;
  }
  assert(ran > 0);
  printf("bottom contact: %d cases PASS\n", ran);
  return 0;
}
