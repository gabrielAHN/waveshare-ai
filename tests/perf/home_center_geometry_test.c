#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"

static uint16_t home_pixels[SPARKLES_PIXELS];
static uint16_t a_pixels[SPARKLES_PIXELS];
static uint16_t b_pixels[SPARKLES_PIXELS];
static uint16_t direct_pixels[SPARKLES_PIXELS];
static uint16_t cached_pixels[SPARKLES_PIXELS];

static float blob_alpha(const home_blob *b) {
#ifdef HOME_BLOB_HAS_ALPHA
  return b->alpha;
#else
  (void)b;
  return 1.f;
#endif
}

int main(void) {
  home_blob classify = home_blob_pull(80, 440, 12.f, 0.f);
  assert(classify.w == 0.f && classify.h == 0.f);
  assert(classify.cx == 184.f && classify.cy == 224.f && classify.n == 2.f);
  assert(blob_alpha(&classify) == 0.f);

  home_blob left64 = home_blob_pull(80, 440, 64.f, 0.f);
  home_blob left140 = home_blob_pull(80, 440, 140.f, 0.f);
  home_blob right64 = home_blob_pull(288, 440, 64.f, 0.f);
  assert(left64.w < left140.w && left64.h < left140.h);
  assert(left64.n == 2.f && left140.n == 2.f && blob_alpha(&left64) < blob_alpha(&left140));
  assert(left64.cx == 184.f && right64.cx == 184.f);
  assert(left64.cy == 224.f && left140.cy == 224.f);
  home_blob reversed = home_blob_pull(80, 440, 64.f, 0.f);
  assert(!memcmp(&left64, &reversed, sizeof left64));

  home_blob cover = home_blob_pull(80, 440, 216.f, 0.f);
  assert(cover.w == HOME_REVEAL_DIAMETER && cover.h == HOME_REVEAL_DIAMETER && blob_alpha(&cover) == 1.f);
  assert(cover.cx == 184.f && cover.cy == 224.f && cover.n == 2.f);

  for (int i = 0; i < SPARKLES_PIXELS; i++) {
    home_pixels[i] = (uint16_t)(0x2104u + (unsigned)i * 17u);
    a_pixels[i] = b_pixels[i] = 0x1234u;
  }
  home_accent_draw(a_pixels, home_pixels, &left64, 0xf980u);
  home_accent_draw(b_pixels, home_pixels, &left64, 0xf980u);
  assert(!memcmp(a_pixels, b_pixels, sizeof a_pixels));
  int center = home_round(left64.cy) * 368 + home_round(left64.cx);
  assert(a_pixels[center] == home_pixels[center]);
  assert(a_pixels[0] == 0x1234u);

  static home_ui s;
  memset(&s, 0, sizeof s);
  s.page = SETTINGS;
  s.slide_page = SETTINGS;
  s.slide_kind = HOME_MOTION_DRAG;
  s.motion_id = 7;
  s.tile = home_tile_index(HELPER);
  s.blob = left64;
  home_snapshot snap;
  memset(&snap, 0, sizeof snap);
  snap.home = cached_pixels;
  snap.home_valid = false;
  assert(home_compose(&s, direct_pixels, SPARKLES_PIXELS, NULL, NULL, -1));
  assert(home_compose(&s, b_pixels, SPARKLES_PIXELS, &snap, NULL, -1));
  assert(!memcmp(direct_pixels, b_pixels, sizeof direct_pixels));

  puts("home center geometry: fixed circle, expand, reverse, cover, source and first frame pass");
  return 0;
}
