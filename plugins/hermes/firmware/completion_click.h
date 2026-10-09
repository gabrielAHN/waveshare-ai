#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "voice_wire.h"

/* The user could not hear the old click (peak 192/32767 at codec volume 1 of 100: ~-45 dBFS into a
 * tiny speaker). Now a soft two-note chime, ~250 ms, peak ~-12 dBFS at codec volume CLICK_VOLUME. */
enum {
  CLICK_RATE = 16000,
  CLICK_SOUND_SAMPLES = 4000,
  CLICK_SILENCE_SAMPLES = 256,
  CLICK_SAMPLES = CLICK_SOUND_SAMPLES + CLICK_SILENCE_SAMPLES,
  CLICK_PEAK_MAX = 8000,
  CLICK_VOLUME = 70,
};

typedef struct {
  unsigned last_status;
  bool initialized;
} completion_once;

static inline void completion_once_seed(completion_once *o, unsigned status) {
  o->last_status = status;
  o->initialized = true;
}

/* Observes protocol state edges. Initial state, repeats, errors and muted DONE
 * edges are consumed without sound, so boot/random memory cannot emit audio. */
static inline bool completion_once_edge(completion_once *o, unsigned status, bool sound_off) {
  if (!o->initialized) {
    o->initialized = true;
    o->last_status = status;
    return false;
  }
  bool done_edge = status == VOICE_DONE && o->last_status != VOICE_DONE;
  o->last_status = status;
  return done_edge && !sound_off;
}

/* Two-note chime (E6 then A6), each with a soft attack and an exponential-ish decay, reaching exact zero
 * before the explicit silence so opening/closing the codec never creates an edge. Integer-only and
 * deterministic for host tests: a 64-step sine table, phase accumulators in 16.16 fixed point. */
static inline int click_sine(uint32_t phase) {
  static const int16_t t[64] = {0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170, 25329, 27245, 28898, 30273,
                                31356, 32137, 32609, 32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329, 23170,
                                20787, 18204, 15446, 12539, 9512, 6393, 3212, 0, -3212, -6393, -9512, -12539, -15446,
                                -18204, -20787, -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609, -32767,
                                -32609, -32137, -31356, -30273, -28898, -27245, -25329, -23170, -20787, -18204, -15446,
                                -12539, -9512, -6393, -3212};
  return t[(phase >> 26) & 63];
}
static inline void completion_click_make(int16_t *out, size_t count) {
  enum { NOTE = CLICK_SOUND_SAMPLES / 2, ATTACK = 80 };
  static const uint32_t step[2] = {(uint32_t)(1318.5 * 4294967296.0 / CLICK_RATE),   /* E6 */
                                   (uint32_t)(1760.0 * 4294967296.0 / CLICK_RATE)};  /* A6 */
  uint32_t phase = 0;
  for (size_t i = 0; i < count; i++) {
    if (i >= CLICK_SOUND_SAMPLES) { out[i] = 0; continue; }
    int note = i < NOTE ? 0 : 1, k = (int)(i % NOTE);
    if (k == 0) phase = 0;
    phase += step[note];
    int env = k < ATTACK ? k * 256 / ATTACK : 256 * (NOTE - k) / (NOTE - ATTACK);  /* 0..256 */
    env = env * env / 256;                                                        /* softer tail */
    out[i] = (int16_t)((long)click_sine(phase) * env / 256 * CLICK_PEAK_MAX / 32767);
  }
  if (count) out[count - 1] = 0;
}
