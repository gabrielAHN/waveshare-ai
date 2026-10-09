/* Centered expanding Home-aperture geometry through the real touch path. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"

static home_ui s, reference;
static uint16_t frame[SPARKLES_PIXELS], home_pixels[SPARKLES_PIXELS];
static uint16_t outgoing_pixels[SPARKLES_PIXELS], snapshot_pixels[SPARKLES_PIXELS];

static void reset(void) {
  memset(&s, 0, sizeof s);
  s.page = SETTINGS;
  s.settings_tab = SETTINGS_SOUND;
}

static void touch(int64_t us, bool down, int x, int y) {
  home_sample(&s, us, down, x, y);
}

static void circle(home_blob b) {
  assert(fabsf(b.cx - 184.f) < .001f);
  assert(fabsf(b.cy - 224.f) < .001f);
  assert(fabsf(b.w - b.h) < .001f);
  assert(fabsf(b.n - 2.f) < .001f);
  assert(b.w >= 0.f && b.alpha >= 0.f && b.alpha <= 1.f);
}

static void first_active(void) {
  reset();
  touch(1000000, true, 32, 447);
  touch(1100000, true, 32, 435);
  assert(s.edge && s.slide_kind == HOME_MOTION_DRAG && s.page == SETTINGS);
  circle(s.blob);
  assert(s.blob.w <= 2.f);
  puts("circle first active: fixed physical center, n2, equal diameter, zero/small start");
}

static void held_reverse(void) {
  reset();
  touch(2000000, true, 40, 440);
  touch(2100000, true, 40, 428);
  home_blob at_start = s.blob;
  touch(2200000, true, 300, 360);
  home_blob forward = s.blob;
  touch(2300000, true, 12, 320);
  home_blob farther = s.blob;
  assert(forward.w > at_start.w && farther.w > forward.w);
  circle(at_start); circle(forward); circle(farther);
  touch(2400000, true, 335, 360);
  circle(s.blob);
  assert(!memcmp(&forward, &s.blob, sizeof forward));
  home_blob held = s.blob;
  touch(2500000, true, 335, 360);
  assert(!memcmp(&held, &s.blob, sizeof held));
  puts("circle held: upward distance expands, x cannot move it, reversal is exact, hold is still");
}

static void corner_cover(void) {
  reset();
  touch(3000000, true, 335, 447);
  touch(3100000, true, 335, 435);
  touch(3200000, true, 10, HOME_PULL_CENTER_Y);
  circle(s.blob);
  float farthest = hypotf(183.5f, 223.5f);
  assert(s.blob.w * .5f >= farthest + 1.f);
  const float corners[][2] = {{.5f,.5f},{367.5f,.5f},{.5f,447.5f},{367.5f,447.5f}};
  for (size_t i = 0; i < sizeof corners / sizeof corners[0]; i++) {
    float dx = corners[i][0] - s.blob.cx, dy = corners[i][1] - s.blob.cy;
    assert(dx * dx + dy * dy < (s.blob.w * .5f) * (s.blob.w * .5f));
  }
  puts("circle endpoint: diagonal radius covers every panel corner plus AA margin");
}

static void render_ready(int theme, int accent) {
  reset();
  s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED;
  s.pair.live_http = 200; s.pair.live_ok = true;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED;
  s.phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s.phone_ha.st = s.phone.st;
#endif
  s.theme_mode = (uint8_t)theme; s.accent = (uint8_t)accent;
  reference = s;
  assert(home_render_page(&reference, outgoing_pixels, SPARKLES_PIXELS));
  reference.page = HOME;
  assert(home_render_page(&reference, home_pixels, SPARKLES_PIXELS));
}

static void reverse_full_cover(void) {
  const int starts_y[] = {420, 440, 447};
  const int starts_x[] = {32, 80, 335};
  int cases = 0;
  for (int theme = THEME_LIGHT; theme <= THEME_DARK; theme++) {
    for (int accent = 0; accent < 5; accent++) {
      int y = starts_y[cases % 3], x = starts_x[(cases / 3) % 3];
      int beyond = cases & 1 ? 4 : 0;
      int64_t t = 4000000 + (int64_t)cases * 1000000;
      render_ready(theme, accent);
      touch(t, true, x, y);
      touch(t += 40000, true, x, y - HOME_PULL_DECIDE_PX);
      touch(t += 40000, true, 368 - x, 100);
      touch(t += 80000, true, x, HOME_PULL_CENTER_Y - beyond);
      assert(s.slide_kind == HOME_MOTION_DRAG && s.blob.w == HOME_REVEAL_DIAMETER);
      home_snapshot snap = {.px = snapshot_pixels, .home = home_pixels};
      assert(home_compose(&s, frame, SPARKLES_PIXELS, &snap, outgoing_pixels, SETTINGS));
      assert(!memcmp(frame, home_pixels, sizeof frame));

      touch(t += 1000, false, 0, 0);
      assert(s.page == HOME &&
             (s.slide_kind == HOME_MOTION_NONE || s.blob.w == HOME_REVEAL_DIAMETER));
      const int after_ms[] = {0, 1, 60, 120, 240, 400};
      for (size_t k = 0; k < sizeof after_ms / sizeof after_ms[0]; k++) {
        touch(t + (int64_t)after_ms[k] * 1000, false, 0, 0);
        assert(home_compose(&s, frame, SPARKLES_PIXELS, &snap, outgoing_pixels, SETTINGS));
        assert(!memcmp(frame, home_pixels, sizeof frame));
      }
      touch(t + 500000, true, 184, 440);
      assert(home_compose(&s, frame, SPARKLES_PIXELS, &snap, outgoing_pixels, SETTINGS));
      assert(!memcmp(frame, home_pixels, sizeof frame));
      cases++;
    }
  }
  assert(cases == 10);
  puts("circle reverse-full: exact/beyond center stays plain Home for 10 theme/accent cases and new touch");
}

int main(int argc, char **argv) {
  const char *section=argc>1?argv[1]:NULL;
  if (!section||!strcmp(section,"first")) first_active();
  if (!section||!strcmp(section,"held")) held_reverse();
  if (!section||!strcmp(section,"corner")) corner_cover();
  if (!section||!strcmp(section,"reverse-full")) reverse_full_cover();
  if(section&&strcmp(section,"first")&&strcmp(section,"held")&&strcmp(section,"corner")&&
     strcmp(section,"reverse-full"))
    assert(!"unknown section");
  return 0;
}
