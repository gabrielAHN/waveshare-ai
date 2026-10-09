/* Circular Home-reveal renderer and compositor pixel contracts:
 * K bounds/guards; L exact AA edges; M RGB565 scalar blend; N Home/outgoing aperture pixels;
 * O real touch/compositor cache and source selection; P interrupted tile OPEN contacts. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_render.h"

#define N SPARKLES_PIXELS
#define G (368 * 8)
static uint16_t home[N], ref[N], f1[N], f2[N], snapbuf[N], homebuf[N], poison[N];
static uint16_t guarded[N + 2 * G];
static uint32_t rng = 12345;
static int64_t now_us = 1000000;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xffff) / 65535.f; }
static float level(const home_blob *b, int x, int y) {
  float ux = fabsf(((float)x + .5f - b->cx) / (b->w * .5f));
  float uy = fabsf(((float)y + .5f - b->cy) / (b->h * .5f));
  return powf(ux, b->n) + powf(uy, b->n);
}

static void bounds(void) {
  for (int i = 0; i < N; i++) home[i] = (uint16_t)rnd();
  float nan = NAN, inf = INFINITY;
  home_blob odd[] = {{184,224,368,448,12,1},{-500,224,368,448,12,1},{900,224,368,448,2,1},{184,-400,368,448,2,1},
    {184,900,368,448,2,1},{184,224,4000,4000,2,1},{184,224,1,1,2,1},{184,224,.5f,300,2,1},{0,0,2,2,64,1},
    {367.9f,447.9f,3,3,1,1},{184,224,368,448,64,1},{nan,224,368,448,2,1},{184,nan,368,448,2,1},
    {184,224,nan,448,2,1},{184,224,368,inf,2,1},{184,224,368,448,nan,1},{184,224,-10,448,2,1},
    {184,224,368,448,0,1},{1e30f,1e30f,1e30f,1e30f,2,1},{184,224,0,0,2,0}};
  int shapes = 0;
  for (int k = 0; k < 2000; k++) {
    home_blob b = k < (int)(sizeof odd / sizeof odd[0]) ? odd[k]
      : (home_blob){frand(-400,800),frand(-400,900),frand(1,1200),frand(1,1200),frand(1,40),frand(.01f,1)};
    for (int with_home = 0; with_home < 2; with_home++) {
      for (int i = 0; i < N + 2 * G; i++) guarded[i] = 0xA5A5;
      uint16_t *p = guarded + G; memcpy(p, home, sizeof home);
      home_accent_draw(p, with_home ? home : NULL, &b, 0xF81F);
      for (int i = 0; i < G; i++) assert(guarded[i] == 0xA5A5 && guarded[G + N + i] == 0xA5A5);
      if (!home_blob_sane(&b)) assert(!memcmp(p, home, sizeof home));
      shapes++;
    }
  }
  printf("K bounds: %d reveal shapes (off-panel, huge, tiny, NaN, inf, zero) stay in frame\n", shapes);
}

static float cov(uint16_t v) { return ((v >> 5) & 63) / 63.f; }
static float half_exact(const home_blob *b, float y) {
  float v = fabsf(y - b->cy) / (b->h * .5f);
  return v >= 1 ? 0 : b->w * .5f * powf(1 - powf(v, b->n), 1 / b->n);
}
static void aa_edge(void) {
  for(int i=0;i<N;i++)home[i]=0xffff;
  const home_blob shapes[] = {{184,224,300,380,12,1},{170.3f,210.7f,260,340,5.6f,1},{190.5f,240.25f,200,300,3.1f,1},
    {184.2f,230.6f,156,156,2,1},{160.7f,250.1f,180,130,2,1},{200,200,90,240,8,1}};
  for (size_t k = 0; k < sizeof shapes / sizeof shapes[0]; k++) {
    const home_blob *b = &shapes[k];home_blob coverage=*b;coverage.alpha=1e-6f;
    memset(f1,0,sizeof f1);home_accent_draw(f1,home,&coverage,0xffff);
    double sum = 0; int partial = 0, worst_side = 0; float worst_row = 0, worst_col = 0;
    for (int y = 0; y < 448; y++) {
      float rs = 0; int lp = 0, rp = 0;
      for (int x = 0; x < 368; x++) {
        uint16_t v = f1[y * 368 + x]; unsigned r = v >> 11, g = (v >> 5) & 63, bl = v & 31;
        assert(abs((int)r * 63 - (int)g * 31) <= 63 && r == bl);
        float c = cov(v); rs += c; sum += c;
        if (c > 0 && c < 1) { partial++; if (x < b->cx) lp++; else rp++; }
      }
      float vr = fabsf(y + .5f - b->cy) / (b->h * .5f);
      if (vr < .9f) { float e = fabsf(rs - 2 * half_exact(b, y + .5f)); if (e > worst_row) worst_row = e; }
      if (vr < .7f) { if (lp > worst_side) worst_side = lp; if (rp > worst_side) worst_side = rp; }
    }
    for (int x = 0; x < 368; x++) {
      float u = fabsf(x + .5f - b->cx) / (b->w * .5f); if (u >= .9f) continue;
      float cs = 0; for (int y = 0; y < 448; y++) cs += cov(f1[y * 368 + x]);
      float exact = b->h * powf(1 - powf(u, b->n), 1 / b->n), e = fabsf(cs - exact);
      if (e > worst_col) worst_col = e;
    }
    float g = tgammaf(1 + 1 / b->n), area = b->w * b->h * g * g / tgammaf(1 + 2 / b->n);
    assert(fabs(sum - area) / area < .005);
    assert(worst_row < .25f && worst_col < .3f && worst_side <= 3 && partial > 100);
  }
  puts("L anti-aliased edge: rows/columns within 0.3 px, area within 0.5 percent");
}

static void helpers565(void) {
  int sh[3] = {11,5,0}, mask[3] = {31,63,31};
  for (int k = 0; k < 40000; k++) {
    uint16_t bg = (uint16_t)rnd(), fg = (uint16_t)rnd();
    if (k < 16) { bg = k & 1 ? 0xffff : 0; fg = k & 2 ? 0xffff : 0; }
    for (unsigned a = 0; a <= 32; a++) {
      uint16_t got = home_mix565(bg, fg, a);
      for (int ch = 0; ch < 3; ch++) {
        float b = (float)((bg >> sh[ch]) & mask[ch]), f = (float)((fg >> sh[ch]) & mask[ch]);
        float ideal = b + (f - b) * (float)a / 32.f;
        assert(fabsf((float)((got >> sh[ch]) & mask[ch]) - ideal) < 1.f);
      }
      if (!a) assert(got == bg);
      if (a == 32) assert(got == fg);
    }
  }
  puts("M RGB565: coverage blend within one channel LSB, exact at 0 and 32");
}

static void solid(void) {
  for (int i = 0; i < N; i++){home[i]=(uint16_t)rnd();ref[i]=(uint16_t)rnd();}
  const home_blob shapes[] = {{184,224,368,448,12,1},{150,300,250,250,2,1},{220,180,156,156,2,1},{184,380,40,40,2.5f,1}};
  for (size_t k = 0; k < sizeof shapes / sizeof shapes[0]; k++) {
    uint16_t accent = (uint16_t)(0x1234 + k * 0x1111);
    memcpy(f1,ref,sizeof ref);home_accent_draw(f1,home,&shapes[k],accent);
    memcpy(f2,ref,sizeof ref);home_reveal_draw(f2,ref,home,&shapes[k],accent);
    assert(!memcmp(f1, f2, sizeof f1)); int in = 0, out = 0;
    for (int y = 0; y < 448; y++) for (int x = 0; x < 368; x++) {
      float q = level(&shapes[k], x, y);
      if (q <= .75f) { assert(f1[y * 368 + x] == home[y * 368 + x]); in++; }
      if (q >= 1.25f) { assert(f1[y * 368 + x] == ref[y * 368 + x]); out++; }
    }
    assert(in > 100 && out > 100);
  }
  puts("N aperture: safe interior is exact Home, safe exterior exact outgoing; paths match");
}

static home_ui ready(home_page page, int mode, int accent) {
  home_ui s; memset(&s, 0, sizeof s);
  s.connected = s.saved = true; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; s.pair.live_ok = true;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED; s.phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s.phone_ha.st = s.phone.st;
#endif
  s.page = page; s.tile = 1; s.theme_mode = (uint8_t)mode; s.accent = (uint8_t)accent; return s;
}
static void at(home_ui *s, int dt, bool down, int x, int y) { now_us += dt; home_sample(s, now_us, down, x, y); }

static void compose(void) {
  for (int mode = 0; mode < THEME_MODES; mode++) for (int accent = 0; accent < THEME_ACCENTS; accent++) {
    home_ui s = ready(SETTINGS, mode, accent), rest = s; assert(home_render(&rest, ref, N));
    home_snapshot cache = {.px = snapbuf, .home = homebuf};
    assert(home_compose(&s, f1, N, &cache, NULL, -1) && !memcmp(f1, ref, sizeof ref));
    at(&s,10000,true,150,440); at(&s,10000,true,154,408);
    memcpy(snapbuf,ref,sizeof ref);for(int i=0;i<N;i++)poison[i]=(uint16_t)(i*29u);
    cache.motion = s.motion_id; cache.page = s.slide_page; cache.valid = true;
    home_ui h = s; h.page = HOME; h.slide_kind = HOME_MOTION_NONE; assert(home_render(&h, home, N));
    assert(home_compose(&s, f1, N, &cache, poison, SETTINGS));
    memcpy(f2,snapbuf,sizeof f2);home_reveal_draw(f2,snapbuf,home,&s.blob,theme_color_of(mode,accent,TA_FILL));
    assert(!memcmp(f1,f2,sizeof f1)&&cache.home_valid&&!memcmp(homebuf,home,sizeof home));
    home_snapshot none = {0};
    assert(home_compose(&s,f2,N,&none,snapbuf,SETTINGS)&&!memcmp(f1,f2,sizeof f1));
    assert(home_compose(&s,f2,N,NULL,snapbuf,SETTINGS)&&!memcmp(f1,f2,sizeof f1));
  }
  puts("O compose: 2 modes x 5 accents, real Home cache and outgoing source selection");
}

static void interrupted_open(void) {
  for (int render_open = 1; render_open >= 0; render_open--) {
    home_ui s = ready(HOME, THEME_DARK, 1); home_snapshot cache = {.px = snapbuf, .home = homebuf};
    at(&s,10000,true,184,220); at(&s,10000,false,0,0);
    assert(s.page == HELPER && s.slide_kind == HOME_MOTION_OPEN);
    const uint16_t *last = NULL;
    if (render_open) { at(&s,30000,false,0,0); assert(home_compose(&s,f2,N,&cache,NULL,-1)); last = f2; }
    at(&s,10000,true,184,440); at(&s,10000,true,184,428); at(&s,10000,true,184,416); at(&s,10000,false,0,0);
    assert(s.page == HOME && s.slide_kind == HOME_MOTION_OUT && s.note.offset == 24);
    for (int i = 0; i < N; i++) snapbuf[i] = (uint16_t)(0xA55Au ^ i);
    cache.motion = s.motion_id; cache.page = s.slide_page; cache.valid = false; cache.home_valid = false;
    assert(home_compose(&s,f1,N,&cache,last,-1));
    home_ui h = s; h.slide_kind = HOME_MOTION_NONE; assert(home_render(&h,home,N));
    home_ui o=s;o.page=s.slide_page;o.slide_kind=HOME_MOTION_NONE;o.page_y=0;assert(home_render(&o,ref,N));
    memcpy(f2,ref,sizeof f2);home_reveal_draw(f2,ref,home,&s.blob,theme_color_of(s.theme_mode,s.accent,TA_FILL));
    assert(!memcmp(f1,f2,sizeof f1));
  }
  puts("P interrupted OPEN: first compose reconstructs untransformed outgoing pixels");
}

int main(int argc, char **argv) {
  static const struct { const char *name; void (*run)(void); } sections[] = {
    {"K",bounds},{"L",aa_edge},{"M",helpers565},{"N",solid},{"O",compose},{"P",interrupted_open},
  };
  int ran = 0;
  for (size_t k = 0; k < sizeof sections / sizeof sections[0]; k++) {
    if (argc > 1 && strcmp(argv[1], sections[k].name)) continue;
    sections[k].run(); ran++;
  }
  assert(ran > 0); printf("home accent renderer: %d sections PASS\n", ran); return 0;
}
