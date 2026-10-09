#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned powf_calls;
static float counted_powf(float x, float y) {
  powf_calls++;
  return powf(x, y);
}
#define powf counted_powf
#include "home_render.h"
#undef powf

int main(void) {
  const home_blob circle = {184.f, 224.f, 220.f, 220.f, 2.f, .6f};
  float sum = 0;
  powf_calls = 0;
  for (int y = 114; y <= 334; y++) sum += home_blob_half(&circle, (float)y + .5f);
  assert(sum > 1000.f);
  assert(powf_calls == 0);  /* specialised circle path: no two-powf row cost */

  for (int y = 114; y <= 334; y++) {
    float v = fabsf(((float)y + .5f - circle.cy) / (circle.h * .5f));
    float reference = v >= 1.f ? 0.f : circle.w * .5f * sqrtf(1.f - v * v);
    assert(fabsf(home_blob_half(&circle, (float)y + .5f) - reference) < .0001f);
  }
  puts("home renderer cost: n=2 uses zero powf calls and matches scalar circle oracle");
  return 0;
}
