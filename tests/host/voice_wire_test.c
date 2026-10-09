#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "voice_wire.h"

int main(void) {
  assert(VOICE_BUFFER_BYTES == 15 * 16000 * 2 && VOICE_BUFFER_BYTES <= 512 * 1024);
  assert(voice_samples_ok(VOICE_MIN_SAMPLES) && !voice_samples_ok(VOICE_MIN_SAMPLES - 1));
  assert(voice_samples_ok(VOICE_MAX_SAMPLES) && !voice_samples_ok(VOICE_MAX_SAMPLES + 1));

  /* Conditioning: DC removed, quiet speech amplified (bounded gain), no wrap-around. */
  static int16_t pcm[16000];
  for (int i = 0; i < 16000; i++) pcm[i] = (int16_t)(3000 + 400 * sinf(i * 0.2f));
  voice_condition(pcm, 16000);
  double mean = 0, peak = 0;
  for (int i = 4000; i < 16000; i++) {
    mean += pcm[i];
    if (fabs((double)pcm[i]) > peak) peak = fabs((double)pcm[i]);
  }
  mean /= 12000;
  assert(fabs(mean) < 150);
  assert(peak > 400 * 3 && peak <= 400 * VOICE_MAX_GAIN + 400);
  for (int i = 0; i < 16000; i++) pcm[i] = (int16_t)(32000 * sinf(i * 0.05f));
  voice_condition(pcm, 16000);
  for (int i = 1; i < 16000; i++) assert(abs(pcm[i] - pcm[i - 1]) < 4000);  /* saturates, never wraps */
  memset(pcm, 0, sizeof pcm);
  voice_condition(pcm, 16000);
  for (int i = 0; i < 16000; i++) assert(pcm[i] == 0);

  /* The ES8311 emits a full-scale pop ~60 ms after open and a smaller one at ~250 ms.
   * The settle window and transient-resistant gain keep those pops from suppressing speech. */
  /* (1) the codec settle window is dropped from the head of every recording. */
  assert(VOICE_SETTLE_SAMPLES >= VOICE_RATE * 3 / 10 && VOICE_SETTLE_SAMPLES % 512 == 0);
  /* (2) a lone transient mid-recording (a tap on the glass) cannot collapse the gain. */
  for (int i = 0; i < 16000; i++) pcm[i] = (int16_t)(1500 * sinf(i * 0.2f));
  for (int i = 1000; i < 1016; i++) pcm[i] = (int16_t)(i & 1 ? 32000 : -32000);
  voice_condition(pcm, 16000);
  peak = 0;
  for (int i = 4000; i < 16000; i++) if (fabs((double)pcm[i]) > peak) peak = fabs((double)pcm[i]);
  assert(peak > VOICE_TARGET_PEAK * 0.6);  /* speech lands near the target, not ~1/20 of it */
  for (int i = 1; i < 16000; i++) assert(pcm[i] <= 32767 && pcm[i] >= -32768);
  memset(pcm, 0, sizeof pcm);

  /* Level meter: 0 for silence, monotonic, ~1000 for full scale. */
  assert(voice_level_milli(pcm, 16000) == 0);
  unsigned last = 0;
  for (int a = 30; a <= 30000; a *= 3) {
    for (int i = 0; i < 1600; i++) pcm[i] = (int16_t)(a * sinf(i * 0.3f));
    unsigned level = voice_level_milli(pcm, 1600);
    assert(level >= last && level <= 1000);
    last = level;
  }
  assert(last > 850);  /* 21870 peak sine = -6.5 dBFS */
  assert(voice_level_milli(NULL, 0) == 0);

  /* WVC1 button hook parser. */
  voice_cmd_parser vp = {0};
  unsigned char pkt[9] = {'W', 'V', 'C', '1', 2}, action = 0;
  uint32_t crc = provision_crc(pkt + 4, 1);
  for (int i = 0; i < 4; i++) pkt[5 + i] = (unsigned char)(crc >> (8 * i));
  int r = 0;
  for (int i = 0; i < 9; i++) r = voice_cmd_feed(&vp, pkt[i], &action);
  assert(r == 1 && action == 2);
  pkt[4] = 4;
  crc = provision_crc(pkt + 4, 1);
  for (int i = 0; i < 4; i++) pkt[5 + i] = (unsigned char)(crc >> (8 * i));
  for (int i = 0; i < 9; i++) r = voice_cmd_feed(&vp, pkt[i], &action);
  assert(r == -1 && action == 2);
  puts("voice PCM buffer, DC removal + bounded gain, level meter and WVC1 parser: PASS");
}
