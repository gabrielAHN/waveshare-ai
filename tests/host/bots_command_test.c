/* WBS1 test-only bot-swipe USB frame: parser accepts exactly next/prev with a valid CRC,
 * resynchronises after stray bytes and rejects everything else (tools/swipe_bot.py builds it). */
#include <assert.h>
#include <stdio.h>
#include "bots_command.h"

static int feed_all(bots_cmd_parser *p, const unsigned char *b, size_t n, unsigned char *dir) {
  int last = 0;
  for (size_t i = 0; i < n; i++) { int r = bots_cmd_feed(p, b[i], dir); if (r) last = r; }
  return last;
}
static void make(unsigned char f[9], unsigned char v) {
  memcpy(f, "WBS1", 4); f[4] = v;
  uint32_t crc = provision_crc(&v, 1);
  for (int i = 0; i < 4; i++) f[5 + i] = (unsigned char)(crc >> (8 * i));
}

int main(void) {
  bots_cmd_parser p = {0};
  unsigned char f[9], dir = 0;
  make(f, 1); assert(feed_all(&p, f, 9, &dir) == 1 && dir == 1);
  make(f, 2); dir = 0; assert(feed_all(&p, f, 9, &dir) == 1 && dir == 2);
  /* Python zlib.crc32(b'\x01') little-endian == firmware provision_crc (tools/swipe_bot.py). */
  make(f, 1); assert(f[5] == 0x1b && f[6] == 0xdf && f[7] == 0x05 && f[8] == 0xa5);
  make(f, 0); dir = 9; assert(feed_all(&p, f, 9, &dir) == -1 && dir == 9);
  make(f, 3); assert(feed_all(&p, f, 9, &dir) == -1);
  make(f, 1); f[6] ^= 1; assert(feed_all(&p, f, 9, &dir) == -1);
  unsigned char noisy[20] = {'x', 'W', 'B', 'W', 'B', 'S', 'W'};
  make(noisy + 7, 2); dir = 0; assert(feed_all(&p, noisy, 16, &dir) == 1 && dir == 2);
  const unsigned char other[9] = {'W', 'S', 'E', 'S', 1, 0, 0, 0, 0};
  assert(feed_all(&p, other, 9, &dir) == 0);  /* another frame family is not ours */
  /* WGS1: 1..5 chat/Home, 13/14 Sparkles style, 15 top pull; all others rejected. */
  gesture_cmd_parser g = {0};
  for (unsigned char v = 1; v <= GESTURE_LAST; v++) {
    make(f, v); f[1] = 'G'; unsigned char got = 0; int last = 0;
    for (int i = 0; i < 9; i++) { int r = gesture_cmd_feed(&g, f[i], &got); if (r) last = r; }
    if (v >= 6 && v <= 12) assert(last == -1 && got == 0);
    else assert(last == 1 && got == v);
  }
  make(f, GESTURE_LAST + 1); f[1] = 'G'; { int last = 0; unsigned char got = 0; for (int i = 0; i < 9; i++) { int r = gesture_cmd_feed(&g, f[i], &got); if (r) last = r; } assert(last == -1); }
  make(f, 1); { int last = 0; unsigned char got = 0; for (int i = 0; i < 9; i++) { int r = gesture_cmd_feed(&g, f[i], &got); if (r) last = r; } assert(last == 0); }  /* WBS1 is not WGS1 */
  puts("WBS1 parser (next/prev, CRC, resync, rejects): PASS");
  return 0;
}
