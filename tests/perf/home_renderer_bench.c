/* Shared old/current renderer microbenchmark. The build selects HOME_SOURCE_ROOT through include
 * paths, so this exact fixture can compile against immutable b589 or the candidate. Host timings are
 * only a smoke/proxy; ESP_PLATFORM logs esp_timer measurements from identical render stages. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif
#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#ifndef CONFIG_SPIRAM
#error "home_renderer_bench requires PSRAM"
#endif
#define BENCH_REPEATS 40
static int64_t bench_now(void) { return esp_timer_get_time(); }
static void *bench_alloc(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void bench_free(void *p) { heap_caps_free(p); }
static void bench_yield(void) { vTaskDelay(1); }
#else
#include <time.h>
#define BENCH_REPEATS 10
static int64_t bench_now(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { perror("clock_gettime"); abort(); }
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
static void *bench_alloc(size_t bytes) { return malloc(bytes); }
static void bench_free(void *p) { free(p); }
static void bench_yield(void) {}
#endif

static void bench_require(int ok, const char *what) {
  if (ok) return;
  fprintf(stderr, "HOME_RENDER_BENCH error=%s\n", what);
  abort();
}

typedef struct { int64_t sum, min, max; uint32_t hash; } bench_result;
static uint32_t frame_hash(const uint16_t *p) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < SPARKLES_PIXELS; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}
static void fixture(home_ui *s) {
  memset(s, 0, sizeof *s);
  s->connected = s->saved = true;
  s->pair.state = PAIR_ENROLLED_UNPAIRED;
  s->pair.live_http = 200;
  s->pair.live_ok = true;
  s->phone.st.valid = true;
  s->phone.st.state = PH_AUTHORIZED;
  s->phone.st.flags = PHONE_FLAG_REQUIRED;
#if WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
  s->phone_ha.st = s->phone.st;
#endif
  s->page = SETTINGS;
  s->settings_tab = SETTINGS_SOUND;
  s->tile = home_tile_index(SETTINGS);
  s->theme_mode = THEME_DARK;
  s->accent = 3;
  s->slide_kind = HOME_MOTION_DRAG;
  s->slide_page = SETTINGS;
  s->motion_id = 7;
}
static const float shapes[][3] = {{368.f,448.f,12.f},{260.f,280.f,4.f},{160.f,160.f,2.f},{32.f,32.f,2.f}};
static void select_shape(home_ui *s,int y) {
  int i = y == 428 ? 0 : (y == 360 ? 1 : (y == 300 ? 2 : 3));
  s->blob.cx = 184.f;
  s->blob.cy = 224.f;
  s->blob.w = shapes[i][0];
  s->blob.h = shapes[i][1];
  s->blob.n = shapes[i][2];
#ifdef HOME_BLOB_HAS_ALPHA
  s->blob.alpha = 1.f;
#endif
}
static bench_result measure_home(home_ui *s,uint16_t *home) {
  bench_result r = {.min = INT64_MAX};
  for (int k = 0; k < BENCH_REPEATS; k++) {
    home_page keep=s->page;s->page=HOME;
    int64_t a = bench_now();
    bool rendered=home_render_page(s,home,SPARKLES_PIXELS);
    int64_t us = bench_now() - a;
    s->page=keep;
    bench_require(rendered,"home_render_page");
    r.sum += us;
    if (us < r.min) r.min = us;
    if (us > r.max) r.max = us;
    bench_yield();
  }
  r.hash = frame_hash(home);
  return r;
}
static bench_result measure_accent(home_ui *s,uint16_t *frame,const uint16_t *home) {
  bench_result r = {.min = INT64_MAX};
  theme_use(s->theme_mode,s->accent);
  for(int k=0;k<BENCH_REPEATS;k++){
    int64_t a=bench_now();
    home_accent_draw(frame,home,&s->blob,theme_c(TA_FILL));
    int64_t us=bench_now()-a;r.sum+=us;
    if(us<r.min)r.min=us;
    if(us>r.max)r.max=us;
    bench_yield();
  }
  r.hash=frame_hash(frame);return r;
}
static bench_result measure_compose(home_ui*s,uint16_t*frame,home_snapshot*snap){
  bench_result r={.min=INT64_MAX};
  for(int k=0;k<BENCH_REPEATS;k++){
    snap->home_valid=false;
    int64_t a=bench_now();
    bool composed=home_compose(s,frame,SPARKLES_PIXELS,snap,NULL,-1);
    int64_t us=bench_now()-a;r.sum+=us;
    bench_require(composed,"home_compose");
    if(us<r.min)r.min=us;
    if(us>r.max)r.max=us;
    bench_yield();
  }
  r.hash=frame_hash(frame);return r;
}
static void print_result(int y,const char*stage,bench_result r){
  printf("HOME_RENDER_STAGE y=%d stage=%s avg_us=%" PRId64 " min_us=%" PRId64 " max_us=%" PRId64 " hash=%08" PRIx32 "\n",
         y,stage,r.sum/BENCH_REPEATS,r.min,r.max,r.hash);
}
static void run_bench(void) {
  const size_t bytes = SPARKLES_PIXELS * sizeof(uint16_t);
  uint16_t *frame = bench_alloc(bytes), *home = bench_alloc(bytes);
  home_ui *s=bench_alloc(sizeof *s);
  bench_require(frame && home && s,"PSRAM allocation");
  home_snapshot snap = {.home = home};
  fixture(s);
  const int ys[] = {428, 360, 300, 240};
#ifdef ESP_PLATFORM
  printf("HOME_RENDER_BENCH platform=esp repeats=%d cpu_mhz=%d bytes_per_buffer=%u buffers=2 psram=1\n",
         BENCH_REPEATS, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, (unsigned)bytes);
#else
  printf("HOME_RENDER_BENCH platform=host repeats=%d bytes_per_buffer=%u buffers=2\n", BENCH_REPEATS, (unsigned)bytes);
#endif
  for (size_t k = 0; k < sizeof ys / sizeof ys[0]; k++) {
    select_shape(s,ys[k]);
    bench_result home_r=measure_home(s,home);
    bench_result accent_r=measure_accent(s,frame,home);
    bench_result compose_r=measure_compose(s,frame,&snap);
    print_result(ys[k],"home",home_r);
    print_result(ys[k],"accent",accent_r);
    print_result(ys[k],"compose_miss",compose_r);
  }
  bench_free(frame);
  bench_free(home);
  bench_free(s);
}
#ifdef ESP_PLATFORM
void app_main(void) { run_bench(); }
#else
int main(void) { run_bench(); return 0; }
#endif
