#include <assert.h>
#include <stdio.h>
#include "wire_fields.h"
int main(void) {
  /* Countdown from reset_in_s using only the local monotonic clock since receipt. */
  char t[32];
  assert(wire_remaining(3900, 0, 0) == 3900);
  assert(wire_remaining(3900, 1000000, 61000000) == 3840);
  assert(wire_remaining(10, 0, 20000000) == 0);
  assert(wire_remaining(WIRE_NONE32, 0, 5) == WIRE_NONE32);
  assert(wire_remaining(100, 5000000, 1000000) == 100); /* clock never runs backwards */
  wire_duration(3900, t, sizeof t); assert(!strcmp(t, "1h 05m"));
  wire_duration(3599, t, sizeof t); assert(!strcmp(t, "59m"));
  wire_duration(59, t, sizeof t); assert(!strcmp(t, "<1m"));
  wire_duration(223160, t, sizeof t); assert(!strcmp(t, "2d 13h"));
  wire_duration(4000000000u, t, sizeof t); assert(!strcmp(t, "46296d 07h"));
  wire_reset_text(3900, t, sizeof t); assert(!strcmp(t, "resets in 1h 05m"));
  wire_reset_text(0, t, sizeof t); assert(!strcmp(t, "resetting now"));
  wire_reset_text(WIRE_NONE32, t, sizeof t); assert(!strcmp(t, "no reset"));
  char tiny[6]; wire_reset_text(3900, tiny, sizeof tiny); assert(strlen(tiny) == 5);
  puts("Relative reset countdown and bounded reset text: PASS");
  return 0;
}
