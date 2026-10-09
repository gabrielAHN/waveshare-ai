/* Home cancellation geometry is C1 from the held finger through reversal to its real endpoint. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BOT_NO_LOCAL_OUTFITS 1
#include "home_ui.h"

static float field(home_blob b, int i) {
  switch (i) { case 0:return b.cx; case 1:return b.cy; case 2:return b.w;
    case 3:return b.h; case 4:return b.n; default:return b.alpha; }
}
static void zero_tangent(home_blob a, home_blob b, float dt, const float tolerance[6]) {
  for (int i = 0; i < 6; i++) assert(fabsf((field(b,i) - field(a,i)) / dt) <= tolerance[i]);
}
static void bounded(home_blob b) {
  assert(isfinite(b.cx) && isfinite(b.cy) && isfinite(b.w) && isfinite(b.h));
  assert(isfinite(b.n) && isfinite(b.alpha));
  assert(b.cx >= 0.f && b.cx <= 368.f && b.cy >= 0.f && b.cy <= 448.f);
  assert(b.w >= 0.f && b.w <= HOME_REVEAL_DIAMETER && b.h >= 0.f && b.h <= HOME_REVEAL_DIAMETER);
  assert(b.w == b.h && b.n == 2.f && b.alpha >= 0.f && b.alpha <= 1.f);
}
static home_ui released(int ax, int ay, int y1, int y2, int64_t dt1, int64_t dt2) {
  home_ui s = {0};
  s.page = SETTINGS; s.settings_tab = SETTINGS_SOUND;
  home_sample(&s, 1000000, true, ax, ay);
  home_sample(&s, 1000000 + dt1, true, ax, y1);
  home_sample(&s, 1000000 + dt1 + dt2, true, ax, y2);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_DRAG);
  home_sample(&s, 1010000 + dt1 + dt2, false, 0, 0);
  return s;
}

static void classification_and_invisible_tangents(void) {
  const float tol[6] = {.02f, .02f, .02f, .02f, .002f, .0002f};
  for (int ax = 24; ax <= 344; ax += 80) for (int ay = 420; ay <= 447; ay += 9) {
    home_blob start = home_blob_pull(ax, ay, 12.f, 0.f);
    home_blob classified = home_blob_pull(ax, ay, 12.01f, 0.f);
    home_blob near_cover = home_blob_pull(ax, ay, (float)(ay - HOME_PULL_CENTER_Y) - .01f, 0.f);
    home_blob cover = home_blob_pull(ax, ay, (float)(ay - HOME_PULL_CENTER_Y), 0.f);
    zero_tangent(start, classified, .01f, tol);
    zero_tangent(near_cover, cover, .01f, tol);
    assert(start.w == 0.f && start.h == 0.f && start.alpha == 0.f);
    assert(cover.cx == 184.f && cover.cy == 224.f);
    assert(cover.w == HOME_REVEAL_DIAMETER && cover.h == HOME_REVEAL_DIAMETER);
    assert(cover.n == 2.f && cover.alpha == 1.f);
  }
}

static void reversible_dense_geometry(void) {
  for (int ax = 40; ax <= 328; ax += 72) for (int ay = 420; ay <= 447; ay += 9) {
    float end = (float)(ay - HOME_PULL_CENTER_Y);
    home_blob previous = home_blob_pull(ax, ay, 12.f, 0.f);
    for (int i = 1; i <= 512; i++) {
      float f = 12.f + (end - 12.f) * i / 512.f;
      float dx = 18.f * sinf((float)i * .03125f);
      home_blob now = home_blob_pull(ax, ay, f, dx);
      bounded(now);
      assert(now.w >= previous.w - .001f && now.h >= previous.h - .001f);
      assert(now.n == previous.n && now.alpha >= previous.alpha - .0001f);
      home_blob again = home_blob_pull(ax, ay, f, dx);
      assert(!memcmp(&now, &again, sizeof now));
      previous = now;
    }
    for (int i = 512; i >= 0; i--) {
      float f = 12.f + (end - 12.f) * i / 512.f;
      bounded(home_blob_pull(ax, ay, f, 0.f));
    }
  }
}

static void actual_late_cancel(void) {
  home_ui s = released(80, 440, 428, 412, 100000, 100000);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK && s.slide_len <= 220.f);
  const float dt = .1f;
  home_blob held = s.blob;
  home_blob prior = home_blob_pull(s.blob_ax, s.blob_ay, s.blob_f0 - s.blob_vf * dt,
                                   s.blob_dx0 - s.blob_vx * dt);
  home_blob after = home_blob_back(&s, dt);
  for (int i = 0; i < 6; i++) {
    float incoming = (field(held,i) - field(prior,i)) / dt;
    float outgoing = (field(after,i) - field(held,i)) / dt;
    float tolerance = .18f * fabsf(incoming) + (i == 4 ? .003f : (i == 5 ? .002f : .025f));
    assert(fabsf(incoming - outgoing) <= tolerance);
  }

  float lo = 0.f, hi = s.slide_len;
  for (int i = 0; i < 32; i++) {
    float m = (lo + hi) * .5f, u = m / s.slide_len;
    float f = home_settle_at(s.blob_f0, s.blob_vf * s.slide_len, u);
    if (f > 12.f) lo = m; else hi = m;
  }
  float join = (lo + hi) * .5f;
  home_blob a = home_blob_back(&s, join - dt), b = home_blob_back(&s, join);
  home_blob c = home_blob_back(&s, join + dt);
  for (int i = 0; i < 6; i++) {
    float before = (field(b,i) - field(a,i)) / dt;
    float next = (field(c,i) - field(b,i)) / dt;
    float tolerance = i == 4 ? .002f : (i == 5 ? .0002f : .02f);
    assert(fabsf(before - next) <= tolerance);
  }
  home_blob almost = home_blob_back(&s, s.slide_len - 10.f);
  home_blob endpoint = home_blob_back(&s, s.slide_len);
  assert(memcmp(&almost, &endpoint, sizeof endpoint));
  bounded(almost); bounded(endpoint);
  assert(endpoint.cx == 184.f && endpoint.cy == 224.f);
  assert(endpoint.w == 0.f && endpoint.h == 0.f);
  assert(endpoint.n == 2.f && endpoint.alpha == 0.f);
  home_blob end_before = home_blob_back(&s, s.slide_len - dt);
  for (int i = 0; i < 6; i++) assert(fabsf((field(endpoint,i) - field(end_before,i)) / dt) < (i == 4 ? .002f : .02f));

  home_ui a_sample = s, b_sample = s, c_sample = s;
  int64_t release = 1210000, join_us = (int64_t)(join * 1000.f + .5f);
  home_sample(&a_sample, release + join_us - 100, false, 0, 0);
  home_sample(&b_sample, release + join_us, false, 0, 0);
  home_sample(&c_sample, release + join_us + 100, false, 0, 0);
  assert(a_sample.slide_kind == HOME_MOTION_BACK && b_sample.slide_kind == HOME_MOTION_BACK);
  assert(c_sample.slide_kind == HOME_MOTION_BACK);
  for (int i = 0; i < 6; i++) {
    float tolerance = i == 4 ? .002f : (i == 5 ? .0002f : .02f);
    float before = (field(b_sample.blob,i) - field(a_sample.blob,i)) / dt;
    float next = (field(c_sample.blob,i) - field(b_sample.blob,i)) / dt;
    assert(fabsf(before - next) <= tolerance);
  }
  home_sample(&s, release + (int64_t)(s.slide_len * 1000.f), false, 0, 0);
  assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_NONE);
  assert(s.note.done && !s.note.done_home && s.note.done_ms <= 220);
}

static void starts_speeds_and_release_controls(void) {
  const int starts[][2] = {{32,420},{80,430},{184,440},{288,447},{336,425}};
  for (size_t i = 0; i < sizeof starts / sizeof starts[0]; i++) {
    int x = starts[i][0], y = starts[i][1];
    home_ui slow = released(x, y, y - 12, y - 28, 100000, 100000);
    assert(slow.page == SETTINGS && slow.slide_kind == HOME_MOTION_BACK);
    home_blob target = home_blob_pull(x, y, (float)HOME_PULL_DECIDE_PX, 0.f);
    float previous[6]; bool returning[6] = {0};
    for (int field_index = 0; field_index < 6; field_index++)
      previous[field_index] = fabsf(field(slow.blob_from,field_index) - field(target,field_index));
    for (int k = 1; k <= 100; k++) {
      home_blob frame = home_blob_back(&slow, slow.slide_len * k / 100.f);
      bounded(frame);
      for (int field_index = 0; field_index < 6; field_index++) {
        float distance = fabsf(field(frame,field_index) - field(target,field_index));
        float epsilon = field_index == 4 ? .0005f : .005f;
        if (distance < previous[field_index] - epsilon) returning[field_index] = true;
        if (returning[field_index]) assert(distance <= previous[field_index] + epsilon);
        previous[field_index] = distance;
      }
    }
    home_blob restored = home_blob_back(&slow, slow.slide_len);
    assert(!memcmp(&restored, &target, sizeof target));
    home_ui full = released(x, y, y - 12, y - 12, 100000, 100000);
    assert(full.page == SETTINGS && full.slide_kind == HOME_MOTION_NONE && full.note.anim_ms == 0);

    home_ui fast = {0}; fast.page = SETTINGS; fast.settings_tab = SETTINGS_SOUND;
    home_sample(&fast, 2000000, true, x, y);
    home_sample(&fast, 2010000, true, x, y - 12);
    home_sample(&fast, 2040000, true, x, y - 36);
    assert(fast.page == SETTINGS && fast.slide_kind == HOME_MOTION_DRAG);
    home_sample(&fast, 2050000, false, 0, 0);
    assert(fast.page == HOME && fast.slide_kind == HOME_MOTION_OUT && fast.note.anim_ms <= 240);
    home_blob finish_previous = fast.blob_from;
    for (int k = 1; k <= 100; k++) {
      home_blob frame = home_blob_finish(&fast, fast.slide_len * k / 100.f);
      bounded(frame);
      assert(frame.w >= finish_previous.w - .01f && frame.h >= finish_previous.h - .01f);
      assert(frame.n == finish_previous.n && frame.alpha >= finish_previous.alpha - .001f);
      finish_previous = frame;
    }
    home_blob cover = home_blob_finish(&fast, fast.slide_len);
    assert(cover.cx == 184.f && cover.cy == 224.f);
    assert(cover.w == HOME_REVEAL_DIAMETER && cover.h == HOME_REVEAL_DIAMETER);
    assert(cover.alpha == 1.f && cover.n == 2.f);
  }
}

static void reversed_release_bounds_and_continuity(void) {
  static home_ui s;
  const int starts[][2] = {{32,420},{80,430},{184,440},{288,447},{335,425}};
  const int horizontal[] = {-28,0,24};
  const int reverse_y[] = {28,36,44};
  const float dt = .1f;
  unsigned cases = 0;
  for (size_t i = 0; i < sizeof starts / sizeof starts[0]; i++) {
    for (size_t j = 0; j < sizeof horizontal / sizeof horizontal[0]; j++) {
      int x = starts[i][0], y = starts[i][1], hx = horizontal[j];
      int last_x = x + hx;
      if (last_x < 0 || last_x > 367) continue;
      for (size_t k = 0; k < sizeof reverse_y / sizeof reverse_y[0]; k++) {
        memset(&s, 0, sizeof s);
        s.page = SETTINGS; s.settings_tab = SETTINGS_SOUND;
        home_sample(&s, 1000000, true, x, y);
        home_sample(&s, 1100000, true, x, y - 12);
        home_sample(&s, 1200000, true, x + hx / 2, y - 72);
        home_sample(&s, 1280000, true, last_x, y - reverse_y[k]);
        assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_DRAG);
        home_blob incoming = s.blob;
        home_sample(&s, 1290000, false, 0, 0);
        assert(s.page == SETTINGS && s.slide_kind == HOME_MOTION_BACK);
        assert(s.slide_len > 1.f && s.slide_len <= 220.f);
        assert(!memcmp(&incoming, &s.blob_from, sizeof incoming));

        home_blob prior = home_blob_pull(s.blob_ax, s.blob_ay,
          s.blob_f0 - s.blob_vf * dt, s.blob_dx0 - s.blob_vx * dt);
        home_blob after = home_blob_back(&s, dt);
        for (int field_index = 0; field_index < 6; field_index++) {
          float in_v = (field(incoming,field_index) - field(prior,field_index)) / dt;
          float out_v = (field(after,field_index) - field(incoming,field_index)) / dt;
          float tolerance = .18f * fabsf(in_v) +
            (field_index == 4 ? .003f : (field_index == 5 ? .002f : .025f));
          assert(fabsf(in_v - out_v) <= tolerance);
        }
        for (float ms = 0.f; ms <= s.slide_len; ms += .1f) bounded(home_blob_back(&s, ms));
        home_blob penultimate = home_blob_back(&s, s.slide_len - dt);
        home_blob endpoint = home_blob_back(&s, s.slide_len);
        bounded(penultimate); bounded(endpoint);
        assert(memcmp(&penultimate, &endpoint, sizeof endpoint));
        for (int field_index = 0; field_index < 6; field_index++) {
          float tolerance = field_index == 4 ? .002f :
            (field_index == 5 ? .0002f : .02f);
          assert(fabsf((field(endpoint,field_index) -
                        field(penultimate,field_index)) / dt) <= tolerance);
        }
        assert(endpoint.cx == 184.f && endpoint.cy == 224.f);
        assert(endpoint.w == 0.f && endpoint.h == 0.f);
        assert(endpoint.n == 2.f && endpoint.alpha == 0.f);
        cases++;
      }
    }
  }
  assert(cases == 45);
}

int main(void) {
  classification_and_invisible_tangents();
  reversible_dense_geometry();
  actual_late_cancel();
  starts_speeds_and_release_controls();
  reversed_release_bounds_and_continuity();
  puts("home cancel junction: all fields C1, bounded/reversible, real sample and release controls pass");
  return 0;
}
