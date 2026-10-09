/* Velocity-continuous circular reveal finish and cancellation. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BOT_NO_LOCAL_OUTFITS 1
#include "home_ui.h"

static home_ui s;
static void reset(void) { memset(&s, 0, sizeof s); s.page = SETTINGS; s.settings_tab = SETTINGS_SOUND; }
static float value(home_blob b, int field) { return field ? b.alpha : b.w; }
static float field_value(home_blob b, int field) {
  const float values[] = {b.w, b.h, b.cx, b.cy, b.n, b.alpha};
  return values[field];
}

static void release_derivative(bool finish) {
  const float dt = .1f;
  home_blob held = s.blob;
  float vf = -home_track_speed(&s.track, 1);
  int up = s.start_y - s.last_y, dx = s.last_x - s.start_x;
  home_blob prior = home_blob_pull(s.start_x, s.start_y, (float)up - vf * dt, (float)dx);
  int64_t release = s.input.stamp_us + 10000;
  home_sample(&s, release, false, 0, 0);
  assert(!memcmp(&held, &s.blob_from, sizeof held));
  home_blob after = finish ? home_blob_finish(&s, dt) : home_blob_back(&s, dt);
  for (int field = 0; field < 2; field++) {
    float incoming = (value(held,field) - value(prior,field)) / dt;
    float outgoing = (value(after,field) - value(held,field)) / dt;
    float tolerance = .18f * fabsf(incoming) + (field ? .002f : .04f);
    assert(fabsf(incoming - outgoing) <= tolerance);
  }
}

static void finish(void) {
  reset();
  home_sample(&s, 1000000, true, 184, 440);
  home_sample(&s, 1010000, true, 184, 428);
  home_sample(&s, 1020000, true, 184, 416);
  float held = s.blob.w;
  release_derivative(true);
  assert(s.page == HOME && s.slide_kind == HOME_MOTION_OUT && s.slide_len <= 240.f);
  float previous = held;
  for (int i = 1; i <= 1000; i++) {
    home_blob b = home_blob_finish(&s, s.slide_len * (float)i / 1000.f);
    assert(b.cx == 184.f && b.cy == 224.f && b.w == b.h && b.n == 2.f);
    assert(b.w >= previous - .01f && b.w <= HOME_REVEAL_DIAMETER);
    assert(b.alpha >= 0.f && b.alpha <= 1.f);
    previous = b.w;
  }
  home_blob endpoint = home_blob_finish(&s, s.slide_len);
  assert(endpoint.w == HOME_REVEAL_DIAMETER && endpoint.alpha == 1.f);
  puts("circle finish: release derivative continuous, bounded expansion, exact cover endpoint <=240ms");
}

static void cancel_upward(void) {
  reset();
  home_sample(&s, 2000000, true, 80, 440);
  home_sample(&s, 2100000, true, 80, 428);
  home_sample(&s, 2200000, true, 80, 412);
  float held = s.blob.w;
  release_derivative(false);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK && s.slide_len <= 220.f);
  bool reversed = false;
  float previous = held;
  for (int i = 1; i <= 1000; i++) {
    home_blob b = home_blob_back(&s, s.slide_len * (float)i / 1000.f);
    assert(b.cx == 184.f && b.cy == 224.f && b.w == b.h && b.n == 2.f);
    assert(b.w >= 0.f && b.w <= HOME_REVEAL_DIAMETER);
    assert(b.alpha >= 0.f && b.alpha <= 1.f);
    if (b.w < previous) reversed = true;
    if (reversed) assert(b.w <= previous + .01f);
    previous = b.w;
  }
  home_blob penultimate = home_blob_back(&s, s.slide_len - .1f);
  home_blob endpoint = home_blob_back(&s, s.slide_len);
  assert(reversed && penultimate.w > 0.f && endpoint.w == 0.f && endpoint.alpha == 0.f);
  puts("circle cancel upward: incoming expansion continues then reverses, no early zero plateau <=220ms");
}

static void cancel_downward(void) {
  reset();
  home_sample(&s, 3000000, true, 335, 447);
  home_sample(&s, 3100000, true, 335, 435);
  home_sample(&s, 3200000, true, 260, 375);
  home_sample(&s, 3280000, true, 180, 419);
  float held = s.blob.w;
  release_derivative(false);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK && s.slide_len <= 220.f);
  float previous = held;
  for (int i = 1; i <= 1000; i++) {
    home_blob b = home_blob_back(&s, s.slide_len * (float)i / 1000.f);
    if (!(b.w >= 0.f && b.w <= HOME_REVEAL_DIAMETER && b.w <= previous + .01f)) {
      fprintf(stderr, "cancel-down i=%d len=%g held=%g previous=%g w=%g vf=%g vx=%g\n",
              i, (double)s.slide_len, (double)held, (double)previous, (double)b.w,
              (double)s.blob_vf, (double)s.blob_vx);
    }
    assert(b.w >= 0.f && b.w <= HOME_REVEAL_DIAMETER && b.w <= previous + .01f);
    assert(b.w == b.h && b.cx == 184.f && b.cy == 224.f && b.n == 2.f);
    assert(b.alpha >= 0.f && b.alpha <= 1.f);
    previous = b.w;
  }
  home_blob penultimate = home_blob_back(&s, s.slide_len - .1f);
  home_blob endpoint = home_blob_back(&s, s.slide_len);
  assert(penultimate.w > 0.f && endpoint.w == 0.f && endpoint.alpha == 0.f);
  puts("circle cancel downward: reversed release stays bounded and converges without undershoot/plateau");
}

static void cancel_domain_controls(void) {
  const int starts_x[] = {32, 184, 335};
  const int starts_y[] = {420, 440, 447};
  int cases = 0;
  for (int sign = -1; sign <= 1; sign += 2) {
    for (int horizontal = -1; horizontal <= 1; horizontal += 2) {
      for (int which = 0; which < 3; which++) {
        int x = starts_x[which], y = starts_y[which], final_y = HOME_PULL_CENTER_Y + 4;
        int prior_x = x - horizontal * 24, prior_y = final_y + sign * 8;
        if (prior_x < 0 || prior_x >= 368) prior_x = x + horizontal * 24;
        reset();
        int64_t t = 5000000 + (int64_t)cases * 1000000;
        home_sample(&s, t, true, x, y);
        home_sample(&s, t += 40000, true, x, y - HOME_PULL_DECIDE_PX);
        home_sample(&s, t += 80000, true, prior_x, prior_y);
        home_sample(&s, t += 80000, true, x, final_y);
        assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_DRAG);
        home_blob held = s.blob;
        const float dt = .1f;
        float vf = -home_track_speed(&s.track, 1), vx = home_track_speed(&s.track, 0);
        home_blob before = home_blob_pull(s.start_x, s.start_y,
                                          (float)(s.start_y - s.last_y) - vf * dt,
                                          (float)(s.last_x - s.start_x) - vx * dt);
        home_sample(&s, t += 1000, false, 0, 0);
        assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK && s.slide_len <= 220.f);
        assert(sign > 0 ? s.blob_vf > 0.f : s.blob_vf < 0.f);
        assert(horizontal * s.blob_vx > 0.f);
        assert(!memcmp(&held, &s.blob_from, sizeof held));
        home_blob after = home_blob_back(&s, dt);
        for (int field = 0; field < 6; field++) {
          float incoming = (field_value(held, field) - field_value(before, field)) / dt;
          float outgoing = (field_value(after, field) - field_value(held, field)) / dt;
          float tolerance = .20f * fabsf(incoming) + (field == 0 || field == 1 ? .05f : .003f);
          assert(fabsf(incoming - outgoing) <= tolerance);
        }
        for (int k = 0; k <= 1000; k++) {
          home_blob b = home_blob_back(&s, s.slide_len * (float)k / 1000.f);
          assert(isfinite(b.w) && isfinite(b.h) && isfinite(b.cx) && isfinite(b.cy));
          assert(isfinite(b.n) && isfinite(b.alpha));
          assert(b.w >= 0.f && b.w <= HOME_REVEAL_DIAMETER && b.h >= 0.f && b.h <= HOME_REVEAL_DIAMETER);
          assert(b.cx >= 0.f && b.cx <= 368.f && b.cy >= 0.f && b.cy <= 448.f);
          assert(b.n >= 2.f && b.n <= HOME_BLOB_N0 && b.alpha >= 0.f && b.alpha <= 1.f);
        }
        home_sample(&s, t + (int64_t)(s.slide_len * 1000.f) + 1, false, 0, 0);
        assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_NONE);
        assert(s.blob.w == 0.f && s.blob.h == 0.f && s.blob.alpha == 0.f);
        cases++;
      }
    }
  }
  assert(cases == 12);
  puts("circle cancel domains: signed slow-UP/down release, horizontal velocity, all-field tangents/bounds pass");
}

int main(int argc, char **argv) {
  const char *section=argc>1?argv[1]:NULL;
  if(!section||!strcmp(section,"finish"))finish();
  if(!section||!strcmp(section,"cancel-up"))cancel_upward();
  if(!section||!strcmp(section,"cancel-down"))cancel_downward();
  if(!section||!strcmp(section,"domains"))cancel_domain_controls();
  if(section&&strcmp(section,"finish")&&strcmp(section,"cancel-up")&&strcmp(section,"cancel-down")&&
     strcmp(section,"domains"))
    assert(!"unknown section");
  return 0;
}
