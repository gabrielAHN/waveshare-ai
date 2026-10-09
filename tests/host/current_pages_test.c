/* Stable USB page and gesture IDs; unsupported entries fail closed. */
#include <assert.h>
#include <stdio.h>
#include "home_ui.h"
#include "bots_command.h"

static int gesture(unsigned value, unsigned char *out) {
  gesture_cmd_parser parser = {0};
  unsigned char frame[9] = {'W', 'G', 'S', '1', (unsigned char)value};
  uint32_t crc = provision_crc(frame + 4, 1);
  for (int i = 0; i < 4; i++) frame[5 + i] = (unsigned char)(crc >> (8 * i));
  int result = 0;
  for (int i = 0; i < 9; i++) result = gesture_cmd_feed(&parser, frame[i], out);
  return result;
}

int main(void) {
  assert(HOME == 0 && SPARKLES == 1 && SETTINGS == 2 && HELPER == 3 && SENSORS == 6);
  assert(!home_page_enabled(4) && !home_page_enabled(5));
  for (unsigned i = 6; i <= 12; i++) {
    unsigned char out = 200;
    assert(gesture(i, &out) == -1 && out == 200);
  }
  for (unsigned i = 1; i <= 15; i++) {
    if (i >= 6 && i <= 12) continue;
    unsigned char out = 200;
    assert(gesture(i, &out) == 1 && out == i);
  }
  puts("Stable page IDs and supported gesture allowlist: PASS");
  return 0;
}
