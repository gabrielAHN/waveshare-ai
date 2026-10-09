#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "ui_easing.h"

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static float quad_in_out_reference(float t, float b, float c, float d) {
  float u = t / (d * .5f);
  if (u < 1.f) return c * .5f * u * u + b;
  u -= 1.f;
  return -c * .5f * (u * (u - 2.f) - 1.f) + b;
}

static float cubic_out_reference(float t, float b, float c, float d) {
  float u = t / d - 1.f;
  return c * (u * u * u + 1.f) + b;
}

int main(void) {
  const float starts[] = {0.f, 7.25f, -13.f};
  const float changes[] = {1.f, -4.5f, 19.f};
  const float durations[] = {.125f, 1.f, 240.f};
  for (unsigned k = 0; k < sizeof starts / sizeof starts[0]; k++) {
    float b = starts[k], c = changes[k], d = durations[k];
    assert(ui_ease_quad_in_out(-d, b, c, d) == b);
    assert(ui_ease_cubic_out(-d, b, c, d) == b);
    assert(ui_ease_quad_in_out(d * 2.f, b, c, d) == b + c);
    assert(ui_ease_cubic_out(d * 2.f, b, c, d) == b + c);
    for (int i = 0; i <= 1000; i++) {
      float t = d * (float)i / 1000.f;
      float quad = ui_ease_quad_in_out(t, b, c, d);
      float cubic = ui_ease_cubic_out(t, b, c, d);
      assert(isfinite(quad) && isfinite(cubic));
      assert(near(quad, quad_in_out_reference(t, b, c, d), 2e-5f));
      assert(near(cubic, cubic_out_reference(t, b, c, d), 2e-5f));
      float lo = fminf(b, b + c), hi = fmaxf(b, b + c);
      assert(quad >= lo - 2e-5f && quad <= hi + 2e-5f);
      assert(cubic >= lo - 2e-5f && cubic <= hi + 2e-5f);
    }
  }
  assert(ui_ease_quad_in_out(0.f, 3.f, -8.f, 0.f) == -5.f);
  assert(ui_ease_cubic_out(0.f, 3.f, -8.f, -1.f) == -5.f);
  assert(ui_ease_quad_in_out(NAN, 2.f, 3.f, 1.f) == 2.f);
  assert(ui_ease_cubic_out(INFINITY, 2.f, 3.f, 1.f) == 5.f);

  const float h = 1e-3f;
  float quad_start = (ui_ease_quad_in_out(h, 0.f, 1.f, 1.f) - ui_ease_quad_in_out(0.f, 0.f, 1.f, 1.f)) / h;
  float quad_end = (ui_ease_quad_in_out(1.f, 0.f, 1.f, 1.f) - ui_ease_quad_in_out(1.f - h, 0.f, 1.f, 1.f)) / h;
  float cubic_end = (ui_ease_cubic_out(1.f, 0.f, 1.f, 1.f) - ui_ease_cubic_out(1.f - h, 0.f, 1.f, 1.f)) / h;
  assert(fabsf(quad_start) < .01f && fabsf(quad_end) < .01f && fabsf(cubic_end) < .01f);
  puts("ui easing: dense equations, guards, bounds and seams pass");
  return 0;
}
