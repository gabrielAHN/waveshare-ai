/* WBT1 bots frame decode/validation (golden bytes from the bridge), poll cadence, per-bot mic gate,
 * reset countdown text and NVS restore bounds (pure C core of the Ask-page bots). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "bots_view.h"

static size_t load_golden(unsigned char *out, size_t cap) {
  FILE *f = fopen("bridge/tests/fixtures/bots_frame.hex", "r");
  assert(f);
  size_t n = 0;
  int hi = -1, c;
  while ((c = fgetc(f)) != EOF) {
    int v = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1);
    if (v < 0) continue;
    if (hi < 0) hi = v;
    else { assert(n < cap); out[n++] = (unsigned char)(hi * 16 + v); hi = -1; }
  }
  fclose(f);
  return n;
}
static void recrc(unsigned char *f) {
  uint32_t crc = provision_crc(f, BOTS_FRAME_SIZE - 4);
  for (int i = 0; i < 4; i++) f[BOTS_FRAME_SIZE - 4 + i] = (unsigned char)(crc >> (8 * i));
}

int main(void) {
  assert(BOTS_FRAME_SIZE == 144);
  unsigned char g[BOTS_FRAME_SIZE + 8];
  size_t n = load_golden(g, sizeof g);
  assert(n == BOTS_FRAME_SIZE);
  bots_data d;
  assert(bots_decode(&d, g, n, 5000000));
  assert(d.valid && d.count == 2 && d.flags == BOTS_FLAG_PLUGIN && d.received_us == 5000000);
  assert(!strcmp(d.b[0].id, "helper") && !strcmp(d.b[0].name, "Helper") && !strcmp(d.b[0].provider, "OpenRouter"));
  assert(d.b[0].available && d.b[0].reason == BR_NONE && d.b[0].reset_s == BOTS_NONE32 && !d.b[0].stale);
  assert(!strcmp(d.b[1].id, "atlas") && !d.b[1].available && d.b[1].reason == BR_EXHAUSTED && d.b[1].reset_s == 7800);

  /* Fail closed: every corruption leaves `out` untouched. */
  bots_data keep = d, out = d;
  unsigned char f[BOTS_FRAME_SIZE];
  for (size_t i = 0; i < BOTS_FRAME_SIZE; i++) {  /* any single bit flip breaks the CRC */
    memcpy(f, g, sizeof f); f[i] ^= 0x10;
    assert(!bots_decode(&out, f, sizeof f, 1) && !memcmp(&out, &keep, sizeof out));
  }
  assert(!bots_decode(&out, g, BOTS_FRAME_SIZE - 1, 1) && !bots_decode(&out, NULL, BOTS_FRAME_SIZE, 1));
  struct { size_t at; unsigned char v; } bad[] = {
    {4, 2},            /* version */
    {5, 4},            /* count > 3 */
    {6, 8},            /* unknown flag */
    {7, 1},            /* reserved */
    {8 + 36, 2},       /* available not 0/1 */
    {8 + 37, 6},       /* reason out of range */
    {8 + 38, 2},       /* unknown bot flag */
    {8 + 39, 1},       /* pad */
    {8 + 11, 'x'},     /* id not NUL-terminated */
    {8 + 1, 0x7f},     /* non-printable */
    {8, 'H'},          /* id must be a lowercase profile name */
  };
  for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
    memcpy(f, g, sizeof f); f[bad[k].at] = bad[k].v; recrc(f);
    assert(!bots_decode(&out, f, sizeof f, 1) && !memcmp(&out, &keep, sizeof out));
  }
  memcpy(f, g, sizeof f); memcpy(f + 8 + 44, "helper\0\0\0\0\0\0", 12); recrc(f);  /* duplicate id */
  assert(!bots_decode(&out, f, sizeof f, 1));
  memcpy(f, g, sizeof f); memcpy(f + 8, "default\0\0\0\0\0", 12); recrc(f);     /* reserved profile */
  assert(!bots_decode(&out, f, sizeof f, 1));
  memcpy(f, g, sizeof f); f[5] = 0; recrc(f);  /* zero bots is valid (compiled-in list stays) */
  assert(bots_decode(&out, f, sizeof f, 1) && out.count == 0);

  /* Gate + labels. Before any evidence, every mic is fail-closed. */
  bots_view v = {0};
  assert(bots_count(&v) == 3 && !strcmp(bots_id(&v, 1), "atlas") && !strcmp(bots_name(&v, 2), "Coding"));
  assert(!strcmp(bots_id(&v, 9), "coding") && !strcmp(bots_id(&v, -1), "helper"));
  for (int i = 0; i < 3; i++) assert(bots_block(&v, i, 0) == BR_UPSTREAM);
  v.data = d;
  assert(bots_count(&v) == 2 && bots_block(&v, 0, 5000000) == BR_NONE && bots_block(&v, 1, 5000000) == BR_EXHAUSTED);
  char t[28];
  bots_reset_text(bots_reset_left(&v, 1, 5000000), t, sizeof t); assert(!strcmp(t, "resets in 2h 10m"));
  bots_reset_text(bots_reset_left(&v, 1, 5000000 + 3600LL * 1000000), t, sizeof t); assert(!strcmp(t, "resets in 1h 10m"));
  bots_reset_text(bots_reset_left(&v, 0, 5000000), t, sizeof t); assert(!t[0]);
  /* Old evidence never re-enables the mic merely because a quota countdown elapsed. */
  assert(bots_block(&v, 1, 5000000 + 7800LL * 1000000) == BR_UPSTREAM);
  v.data.received_us = 9e15;
  v.data.b[1].reset_s = BOTS_NONE32;  /* exhausted, reset unknown: stays blocked, no second line */
  assert(bots_block(&v, 1, 9e15) == BR_EXHAUSTED);
  v.data.b[1].reason = BR_SIGNIN; assert(bots_block(&v, 1, 0) == BR_SIGNIN);
  v.data.b[1].available = true; v.data.b[1].reason = BR_UNLABELLED; assert(bots_block(&v, 1, 0) == BR_UNLABELLED);
  assert(!strcmp(bots_reason_text(BR_EXHAUSTED), "No quota") && !strcmp(bots_reason_text(BR_SIGNIN), "Sign in on the host"));
  assert(!strcmp(bots_reason_text(BR_UNLABELLED), "Hermes unavailable") && !bots_reason_text(BR_NONE)[0]);
  assert(bots_block(&v, 5, 0) == BR_UPSTREAM);
  /* A once-good frame expires locally after bridge/poll outage, then a new frame recovers. */
  v.data.received_us = 5000000;
  assert(bots_block(&v, 0, 5000000 + BOTS_EVIDENCE_US) == BR_NONE);
  assert(bots_block(&v, 0, 5000001 + BOTS_EVIDENCE_US) == BR_UPSTREAM);
  assert(bots_decode(&v.data, g, n, 90000000));
  assert(bots_block(&v, 0, 90000000) == BR_NONE);

  /* Poll cadence: first open at once; not while hidden; 60 s steady; 15 s after a failure; a page
   * re-open or a refresh request (bridge 409) re-polls after >= 3 s. */
  bots_view p = {0};
  const int64_t S = 1000000;
  assert(!bots_poll_due(&p, false, 0, 0) && bots_poll_due(&p, true, 0, 0));
  p.attempts = 1; p.attempt_us = 100 * S;
  assert(!bots_poll_due(&p, true, 50 * S, 104 * S) && bots_poll_due(&p, true, 50 * S, 105 * S));
  assert(!bots_poll_due(&p, true, 101 * S, 102 * S) && bots_poll_due(&p, true, 101 * S, 104 * S));
  p.refresh = true; assert(bots_poll_due(&p, true, 0, 104 * S)); p.refresh = false;
  p.fails = 1; assert(!bots_poll_due(&p, true, 0, 104 * S) && bots_poll_due(&p, true, 0, 105 * S));

  /* NVS restore: 0..2 kept, anything else (absent -2, read error -1, corrupt 7) -> helper. */
  assert(bots_restore(2) == 2 && bots_restore(0) == 0 && bots_restore(-2) == 0 && bots_restore(-1) == 0 && bots_restore(7) == 0);
  puts("WBT1 decode (golden, CRC, bounds, unique ids), gate/reasons/countdown, poll cadence, NVS restore: PASS");
  return 0;
}
