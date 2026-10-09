#pragma once
/* Ask audio: 16 kHz mono PCM16, conditioning, level meter and gateway result fields.
 * Pure C, host-tested; no IDF, network or storage here. */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "provision.h"

#define VOICE_RATE 16000
#define VOICE_MIN_SAMPLES (VOICE_RATE * 2 / 5)   /* 0.4 s */
#define VOICE_MAX_SAMPLES (VOICE_RATE * 15)      /* 15 s auto-stop */
#define VOICE_BUFFER_BYTES (VOICE_MAX_SAMPLES * 2)
#define VOICE_MAX_GAIN 8
#define VOICE_TARGET_PEAK 20000
/* ES8311 settle: the codec emits a full-scale pop ~60 ms after esp_codec_dev_open and a smaller one
 * near 250 ms; these first 10 chunks (320 ms) are recorded but never kept. */
#define VOICE_SETTLE_SAMPLES (512 * 10)
/* Gain reference ignores the loudest 3 % of 16 ms blocks, so one tap/click can't set the gain. */
#define VOICE_GAIN_BLOCK 256
#define VOICE_GAIN_SKIP_PERCENT 3
#define VOICE_TRANSCRIPT_MAX 240
#define VOICE_TEXT_MAX 1600  /* the chat scrolls the whole reply */

enum { VOICE_TRANSCRIBING = 1, VOICE_RUNNING, VOICE_STOPPING, VOICE_DONE, VOICE_ERROR, VOICE_INTERRUPTED };

typedef struct {
  unsigned status;
  char id[33];
  char transcript[VOICE_TRANSCRIPT_MAX + 1];
  char text[VOICE_TEXT_MAX + 1];
} voice_command;

static inline bool voice_samples_ok(size_t samples) { return samples >= VOICE_MIN_SAMPLES && samples <= VOICE_MAX_SAMPLES; }

/* Remove DC (mean, then a gentle one-pole high-pass for drift) and apply a bounded gain that brings
 * the speech peak (loudest 16 ms blocks minus the top 3 %, i.e. clicks) to VOICE_TARGET_PEAK.
 * Saturates instead of wrapping. In place; n may be 0. */
static inline void voice_condition(int16_t *pcm, size_t n) {
  if (!pcm || !n) return;
  int64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += pcm[i];
  int32_t mean = (int32_t)(sum / (int64_t)n);
  float prev_x = 0, prev_y = 0;
  static uint16_t hist[512];  /* block peak >> 6; single worker caller */
  memset(hist, 0, sizeof hist);
  size_t blocks = 0;
  int32_t block_peak = 0;
  for (size_t i = 0; i < n; i++) {
    float x = (float)(pcm[i] - mean);
    float y = x - prev_x + 0.995f * prev_y;
    prev_x = x; prev_y = y;
    int32_t v = (int32_t)lrintf(y);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    pcm[i] = (int16_t)v;
    int32_t a = v < 0 ? -v : v;
    if (a > block_peak) block_peak = a;
    if ((i + 1) % VOICE_GAIN_BLOCK == 0 || i + 1 == n) {
      hist[block_peak >> 6 > 511 ? 511 : block_peak >> 6]++;
      ++blocks;
      block_peak = 0;
    }
  }
  size_t skip = blocks * VOICE_GAIN_SKIP_PERCENT / 100;
  if (!skip && blocks >= 16) skip = 1;
  int32_t peak = 0;
  for (int b = 511, seen = 0; b >= 0; b--) {
    seen += hist[b];
    if ((size_t)seen > skip) { peak = (b + 1) << 6; break; }
  }
  if (peak <= 64) return;  /* silence (bottom histogram bin): leave it alone */
  float gain = (float)VOICE_TARGET_PEAK / (float)peak;
  if (gain > VOICE_MAX_GAIN) gain = VOICE_MAX_GAIN;
  for (size_t i = 0; i < n; i++) {
    int32_t v = (int32_t)lrintf(pcm[i] * gain);
    pcm[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
  }
}

/* RMS level as 0..1000 over a -60..0 dBFS scale. */
static inline unsigned voice_level_milli(const int16_t *pcm, size_t n) {
  if (!pcm || !n) return 0;
  double acc = 0;
  for (size_t i = 0; i < n; i++) acc += (double)pcm[i] * pcm[i];
  double rms = sqrt(acc / (double)n);
  if (rms < 1) return 0;
  double db = 20 * log10(rms / 32768.0);
  double level = (db + 60) / 60;
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  return (unsigned)lrint(level * 1000);
}

/* USB test hook (same CRC-checked family as WLV1; local, physical USB only):
 *   WVC1 <action:1> <crc32(action):4>  -- 1 press mic, 2 release/send, 3 stop (drives the button's
 *                                         state machine; logged as usb_serial, never as touch) */
typedef struct {unsigned char data[9]; size_t used;} voice_cmd_parser;
static inline int voice_cmd_feed(voice_cmd_parser *p, unsigned char byte, unsigned char *action) {
  if (p->used < 4 && byte != (unsigned char)"WVC1"[p->used]) { p->used = byte == 'W' ? 1 : 0; if (p->used) p->data[0] = byte; return 0; }
  p->data[p->used++] = byte;
  if (p->used < sizeof p->data) return 0;
  uint32_t crc = 0;
  for (int i = 0; i < 4; i++) crc |= (uint32_t)p->data[5 + i] << (8 * i);
  bool ok = p->data[4] >= 1 && p->data[4] <= 3 && crc == provision_crc(p->data + 4, 1);
  if (ok) *action = p->data[4];
  provision_wipe(p, sizeof *p);
  return ok ? 1 : -1;
}
