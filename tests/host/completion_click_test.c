#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "completion_click.h"

int main(void) {
  int16_t pcm[CLICK_SAMPLES];
  completion_click_make(pcm, CLICK_SAMPLES);
  int peak = 0;
  for (size_t i = 0; i < CLICK_SAMPLES; i++) {
    int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
    if (v > peak) peak = v;
  }
  assert(CLICK_SAMPLES <= CLICK_RATE * 3 / 10); /* <= 300 ms, bounded */
  /* audible: the old click peaked at 192 at volume 1 and the user could not hear it */
  assert(peak >= CLICK_PEAK_MAX * 9 / 10 && peak <= CLICK_PEAK_MAX && CLICK_PEAK_MAX >= 6000);
  assert(CLICK_VOLUME >= 50 && CLICK_VOLUME <= 80);
  assert(pcm[0] == 0 && pcm[CLICK_SAMPLES - 1] == 0);
  assert(pcm[1] < CLICK_PEAK_MAX / 40 && pcm[1] > -CLICK_PEAK_MAX / 40); /* ramp, no hard edge */
  { int tail = 0; for (int i = CLICK_SOUND_SAMPLES - 8; i < CLICK_SOUND_SAMPLES; i++) tail += pcm[i] < 0 ? -pcm[i] : pcm[i];
    assert(tail < 200); }                                                  /* fades out before the silence */
  { int mid = 0; for (int i = CLICK_SOUND_SAMPLES / 2 - 4; i < CLICK_SOUND_SAMPLES / 2 + 4; i++) mid += pcm[i] < 0 ? -pcm[i] : pcm[i];
    assert(mid < 400); }                                                   /* two notes, not one tone */
  for (size_t i = CLICK_SOUND_SAMPLES; i < CLICK_SAMPLES; i++) assert(pcm[i] == 0);

  completion_once once = {0};
  assert(!completion_once_edge(&once, VOICE_DONE, false));
  completion_once_seed(&once, VOICE_TRANSCRIBING);
  assert(completion_once_edge(&once, VOICE_DONE, false)); /* accepted command may finish immediately */
  assert(!completion_once_edge(&once, VOICE_RUNNING, false));
  assert(completion_once_edge(&once, VOICE_DONE, false));
  assert(!completion_once_edge(&once, VOICE_DONE, false));
  assert(!completion_once_edge(&once, VOICE_ERROR, false));
  assert(!completion_once_edge(&once, VOICE_DONE, true)); /* muted edge consumed */
  assert(!completion_once_edge(&once, VOICE_DONE, false));
  assert(!completion_once_edge(&once, VOICE_RUNNING, false));
  assert(completion_once_edge(&once, VOICE_DONE, false));
  puts("completion_click_test ok");
}
