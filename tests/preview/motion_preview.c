/* Host preview of the motion contracts from the real touch path and compositor (the board's
 * home_compose with its two PSRAM frames: the page snapshot and the Home cache):
 *  (1) ask-drop: a fixed-center circle reveals real Home over Ask (Light), expanding with travel;
 *      a partial flick finishes continuously within 240 ms;
 *  (2) sparkles-drop: the same real compositor path on Sparkles in Dark / Blue;
 *  (3) back: a short pull, held still and released: the circle contracts to restore the page;
 *  (4) carousel: a Home carousel settle after a slow drag; (5) open: a tile's page slides up over Home.
 * Writes P6 PPMs to OUT_DIR/motion-<strip>-<n>.ppm and one label per frame to OUT_DIR/motion-labels.txt.
 * Host simulation only.
 *   cc -O1 -Idevices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main -Iplugins/hermes/firmware -Iplugins/home_assistant/firmware tests/preview/motion_preview.c devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/components/qrcodegen/qrcodegen.c -lm -o build/motion_preview
 *   build/motion_preview OUT_DIR */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_render.h"
static uint16_t frame[SPARKLES_PIXELS], last[SPARKLES_PIXELS], snapbuf[SPARKLES_PIXELS], homebuf[SPARKLES_PIXELS];
static home_snapshot snap = {.px = snapbuf, .home = homebuf};
static int last_page = -1;
static int64_t t = 1000000;
static const char *out;
static FILE *labels;
static void output_error(const char *sink) {
  fprintf(stderr, "motion_preview: output %s: %s\n", sink, strerror(errno ? errno : EIO));
  exit(1);
}
/* Buffered writes can fail only at finalization; close even when flushing fails. */
static void output_finish(FILE *f, const char *sink) {
  int error = 0;
  if (fflush(f) == EOF) error = errno ? errno : EIO;
  if (fclose(f) == EOF && !error) error = errno ? errno : EIO;
  if (error) { errno = error; output_error(sink); }
}
/* One board frame: compose, and keep it as the last presented frame (direct_main.c's rule). */
static void present(home_ui *s) {
  if (!home_compose(s, frame, SPARKLES_PIXELS, &snap, last_page >= 0 ? last : NULL, last_page)) exit(1);
  memcpy(last, frame, sizeof frame);
  last_page = home_layer_rest(s) ? (int)s->page : -1;
}
static void save(home_ui *s, const char *strip, int n, const char *label) {
  present(s);
  char path[512];
  snprintf(path, sizeof path, "%s/motion-%s-%d.ppm", out, strip, n);
  FILE *f = fopen(path, "wb");
  if (!f) output_error("frame");
  if (fprintf(f, "P6\n368 448\n255\n") < 0) output_error("frame");
  for (int i = 0; i < SPARKLES_PIXELS; i++) {
    uint16_t v = frame[i];
    unsigned char rgb[3] = {(unsigned char)((v >> 11) * 255 / 31), (unsigned char)(((v >> 5) & 63) * 255 / 63), (unsigned char)((v & 31) * 255 / 31)};
    if (fwrite(rgb, 1, 3, f) != 3) output_error("frame");
  }
  output_finish(f, "frame");
  if (fprintf(labels, "%s %d %s\n", strip, n, label) < 0) output_error("labels");
  if (printf("%s page=%d kind=%d blob=%.0fx%.0f@%.0f,%.0f n=%.1f alpha=%.3f tile=%d drag_offset=%d page_y=%d\n", path, s->page, s->slide_kind,
             s->blob.w, s->blob.h, s->blob.cx, s->blob.cy, s->blob.n, s->blob.alpha, s->tile, s->drag_offset, s->page_y) < 0) output_error("stdout");
}
static void at(home_ui *s, int dt_ms, bool down, int x, int y) { t += dt_ms * 1000LL; home_sample(s, t, down, x, y); }
static home_ui ready(void) {
  home_ui s;
  memset(&s, 0, sizeof s);
  s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; s.pair.live_ok = true;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED; s.phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s.phone_ha.st = s.phone.st;
#endif
  s.bots.data = (bots_data){.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", "atlas", "coding"};
  for (int i = 0; i < 3; i++) { strcpy(s.bots.data.b[i].id, ids[i]); strcpy(s.bots.data.b[i].name, ids[i]); s.bots.data.b[i].available = true; s.bots.data.b[i].reset_s = BOTS_NONE32; }
  return s;
}
static home_ui ask_page(void) {
  home_ui s = ready(); s.page = HELPER; s.tile = 1;
  helper_log_push(&s.helper.log, 'U', "what is on today");
  helper_log_push(&s.helper.log, 'B', "Two meetings and a walk at five. Want a reminder before the walk?");
  s.helper.chat = s.helper.cont = true; s.helper.state = HV_DONE; s.helper.user_in = s.helper.bot_in = 1; s.helper.reveal = 999;
  at(&s, 10, false, 0, 0);
  return s;
}
static void fresh_sources(void) {
  snap.valid = snap.home_valid = false;
  last_page = -1;
}
/* Keep the public 12-frame strip names/count: low/mid/high held travel, partial release, six finish
 * samples and Home. The filenames remain ask-drop/sparkles-drop for preview compatibility. */
static void pull_and_drop(home_ui *s, const char *strip) {
  save(s, strip, 0, "rest");
  int n = 1, y = 440, x = 184;
  at(s, 10, true, x, y);
  present(s);
  static const int shown[] = {15, 30, 50};
  for (int d = 5, k = 0; d <= 50; d += 5) {
    at(s, 10, true, x + d / 10, y - d);
    if (k < 3 && d == shown[k]) { char l[48]; snprintf(l, sizeof l, "held %d px", d); save(s, strip, n++, l); k++; }
    else present(s);
  }
  at(s, 10, false, 0, 0);
  save(s, strip, n++, "release");
  int64_t r = t;
  static const int marks[] = {20, 40, 60, 80, 110, 140};
  for (int k = 0; k < 6; k++) {
    while ((t - r) / 1000 + 20 <= marks[k]) { at(s, 20, false, 0, 0); present(s); }
    at(s, (int)(marks[k] - (t - r) / 1000), false, 0, 0);
    char l[48]; snprintf(l, sizeof l, "+%d ms", marks[k]); save(s, strip, n++, l);
  }
  while (s->slide_kind != HOME_MOTION_NONE) at(s, 20, false, 0, 0);
  save(s, strip, n++, "Home");
}
/* Optional supervisor storyboard: real touch/compositor hooks beyond the fixed public 45 frames.
 * One held level per each of ten themes, then stationary/reverse/cancel, held theme change, and a
 * renderer-first-frame tile-OPEN interruption. Invoke with a third argument `extended`. */
static void extended_storyboard(void) {
  for (int mode = 0; mode < THEME_MODES; mode++) for (int accent = 0; accent < THEME_ACCENTS; accent++) {
    home_ui s = ready(); s.page = SETTINGS; s.tile = 3; s.theme_mode = (uint8_t)mode; s.accent = (uint8_t)accent;
    int d = (int[]){15,30,50}[(mode * THEME_ACCENTS + accent) % 3];
    at(&s, 10, true, 150, 440); at(&s, 30, true, 150 + d / 8, 440 - d);
    char strip[48], label[64];
    snprintf(strip, sizeof strip, "extended-theme-%d-%d", mode, accent);
    snprintf(label, sizeof label, "%s %s held %d px", theme_mode_name(mode), theme_accent_name(accent), d);
    save(&s, strip, 0, label);
  }
  home_ui r = ask_page();
  at(&r,10,true,150,440); at(&r,40,true,154,408); save(&r,"extended-reverse",0,"held 32 px");
  at(&r,140,true,154,408); save(&r,"extended-reverse",1,"stationary 140 ms");
  at(&r,20,true,152,420); save(&r,"extended-reverse",2,"reversed to 20 px");
  at(&r,10,false,0,0); save(&r,"extended-reverse",3,"cancel release");
  while (r.slide_kind != HOME_MOTION_NONE) at(&r,20,false,0,0);
  save(&r,"extended-reverse",4,"restored Ask");

  for (int mode = 0; mode < THEME_MODES; mode++) {
    home_ui k = ask_page(); k.theme_mode = (uint8_t)mode; k.accent = 1;
    fresh_sources();
    char strip[48], label[64];
    snprintf(strip, sizeof strip, "extended-blue-cancel-%d", mode);
    save(&k,strip,0,"Blue rest");
    at(&k,10,true,184,440); at(&k,50,true,184,400);
    snprintf(label, sizeof label, "Blue %s held 40 px", theme_mode_name(mode));
    save(&k,strip,1,label);
    at(&k,80,true,184,400); at(&k,10,false,0,0);
    save(&k,strip,2,"cancel release");
    at(&k,90,false,0,0); save(&k,strip,3,"cancel midpoint");
    while (k.slide_kind != HOME_MOTION_NONE) at(&k,20,false,0,0);
    save(&k,strip,4,"restored Ask");
  }

  home_ui th = ready(); th.page = SETTINGS; th.theme_mode = THEME_LIGHT; th.accent = 0;
  at(&th,10,true,150,440); at(&th,30,true,154,408); save(&th,"extended-theme-change",0,"Light Orange held");
  th.theme_mode = THEME_DARK; th.accent = 4; save(&th,"extended-theme-change",1,"Dark Purple same geometry");

  home_ui q = ready(); q.page = HOME; q.tile = 1;
  at(&q,10,true,184,220); at(&q,10,false,0,0);  /* OPEN exists but has not been composed */
  at(&q,10,true,184,440); at(&q,10,true,184,428); at(&q,10,true,184,416); at(&q,10,false,0,0);
  save(&q,"extended-interrupted-open",0,"quick first-frame partial flick");
}
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: motion_preview OUT_DIR\n"); return 2; }
  out = argv[1];
  char lp[512]; snprintf(lp, sizeof lp, "%s/motion-labels.txt", out);
  labels = fopen(lp, "w");
  if (!labels) output_error("labels");
  /* 1. Ask (Light): circular Home reveal and partial-flick finish -> Home (tile 1). */
  home_ui s = ask_page();
  s.theme_mode = THEME_LIGHT; s.accent = 1;
  pull_and_drop(&s, "ask-drop");
  /* 2. Sparkles in Dark / Blue: the circular aperture reveals real Home beneath the page. */
  home_ui p = ready(); p.page = SPARKLES; p.tile = 0; p.theme_mode = THEME_DARK; p.accent = 1;
  p.input.scene.time = 3;
  for (int i = 0; i < 60; i++) at(&p, 10, true, 90 + 3 * i, 180 + (i % 20));
  at(&p, 10, false, 0, 0);
  for (int i = 0; i < 10; i++) { at(&p, 30, false, 0, 0); present(&p); }
  pull_and_drop(&p, "sparkles-drop");
  /* 3. A short pull (44 px), held still, released: the aperture contracts to restore the page. */
  home_ui b = ask_page();
  b.theme_mode = THEME_LIGHT; b.accent = 1;
  save(&b, "back", 0, "rest");
  at(&b, 10, true, 184, 440);
  for (int k = 1; k <= 11; k++) { at(&b, 30, true, 184, 440 - 4 * k); present(&b); }
  for (int k = 0; k < 10; k++) { at(&b, 10, true, 184, 396); present(&b); }
  save(&b, "back", 1, "held 44 px");
  at(&b, 10, false, 0, 0);
  save(&b, "back", 2, "release");
  static const int back_ms[] = {30, 60, 90, 120, 150, 180};
  int64_t r = t;
  for (int k = 0; k < 6; k++) { at(&b, (int)(back_ms[k] - (t - r) / 1000), false, 0, 0); char l[32]; snprintf(l, sizeof l, "+%d ms", back_ms[k]); save(&b, "back", 3 + k, l); }
  /* 4. Home carousel: a slow drag 210 px left from tile 0, held still, released -> settles on tile 1. */
  home_ui c = ready(); c.page = HOME; c.tile = 0;
  for (int k = 0; k <= 21; k++) at(&c, 20, true, 300 - 10 * k, 220);
  for (int k = 0; k < 10; k++) at(&c, 10, true, 90, 220);
  save(&c, "carousel", 0, "held 210 px");
  at(&c, 10, false, 0, 0);
  for (int k = 1; k < 6; k++) { at(&c, 40, false, 0, 0); char l[32]; snprintf(l, sizeof l, "+%d ms", 40 * k); save(&c, "carousel", k, l); }
  /* 5. A tap on the Sensor tile: its page slides up over Home. */
  home_ui o = ready(); o.page = HOME; o.tile = 2;
  at(&o, 10, true, 184, 220); at(&o, 30, true, 184, 220); at(&o, 30, false, 0, 0);
  save(&o, "open", 0, "tap");
  for (int k = 1; k < 6; k++) { at(&o, 40, false, 0, 0); char l[32]; snprintf(l, sizeof l, "+%d ms", 40 * k); save(&o, "open", k, l); }
  if (argc > 2 && !strcmp(argv[2], "extended")) extended_storyboard();
  output_finish(labels, "labels");
  output_finish(stdout, "stdout");
  return 0;
}
