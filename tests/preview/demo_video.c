/* Waveshare AI demo video (SPEC7 W15): the board's own UI, driven and drawn exactly as on the device,
 * inside a device-shaped frame. Host simulation, not a screen recording.
 *
 *  - Input: synthetic finger samples go through the same path as direct_main.c touch_poll at 100 Hz
 *    (touch_debounce_step, then home_sample on the one home_ui), so every page change below is a real
 *    gesture through the real state machine (carousel swipes, tile taps, hold-to-talk, bot swipes,
 *    Sparkles painting, Settings tab swipes and buttons, the Home gesture).
 *  - Drawing: direct_main.c owner loop at a fixed 30 fps scene clock: copy the state, skip the frame
 *    when home_visual_equal says nothing changed, else home_compose (real Home inside the centered
 *    reveal circle, untransformed outgoing page outside) into one of two frames.
 *  - Scripted data only where the board gets it from the network or a chip, delivered through the
 *    firmware's own decoders and setters like the workers do (home_live.c, helper_voice.c,
 *    direct_main.c battery_poll), answering at once: the WLS4 session feed (4 open sessions, 2
 *    working), the WBT1 bots frame (repo default bots helper + atlas), the WPH1 phone sign-in status,
 *    the WHS1 Home Assistant readings, the voice turn (mic level, transcribing, running with a status
 *    phrase, the reply streaming in, done) and the PMU battery samples. Placeholder identities only:
 *    account label "Hermes" (the default), phone user "Sam", Wi-Fi "Home Wi-Fi".
 *
 *   tools/demo_video.sh              builds this, draws the device frame + captions (ImageMagick) and
 *                                    pipes the frames into ffmpeg -> docs/media/waveshare-ai-demo.mp4 and
 *                                    the README cut docs/images/demo.gif
 *   demo_video --layout              prints the frame geometry and captions (shell assignments)
 *   demo_video ASSET_DIR             reads ASSET_DIR/frame-<k>.ppm (one device frame per caption) and
 *                                    ASSET_DIR/mask.pgm (the panel's rounded corners), writes the
 *                                    1080x1080 frames at 30 fps to stdout as raw RGB24 (the P6 payload,
 *                                    for ffmpeg -f rawvideo), and ASSET_DIR/storyboard.txt (what
 *                                    happens when) + ASSET_DIR/gif.txt (the README cut, MP4 seconds)
 *   DEMO_TOUCH=0                     no fingertip marker (drawn by the compositor, outside the renderer)
 *   DEMO_FRAMES=DIR                  also keep every frame as DIR/NNNNN.ppm (~3.5 MB each)
 *
 * The Home gesture lives in ONE function, demo_go_home(): only its touch path changes when the
 * gesture changes (SPEC7 W14). */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Public demo art is reproducible and never includes a device's private outfit header. */
#define BOT_NO_LOCAL_OUTFITS 1
#include "home_render.h"

/* ---- Output layout (tools/demo_video.sh reads it with --layout) ---- */
#define OUT_W 1080
#define OUT_H 1080
#define SCALE 2                    /* pixel-exact: integer scale, nearest neighbour */
#define SCR_W (368 * SCALE)
#define SCR_H (448 * SCALE)
#define SCR_X ((OUT_W - SCR_W) / 2)
#define SCR_Y 36
#define SCR_R (40 * SCALE)         /* the panel's rounded corners (~40 px) */
#define BEZEL 22
#define CAP_Y 1000                 /* caption line centre */
#define FPS 30
#define CAP_FADE_US 400000LL       /* captions cross-fade between sections */
static const char *const captions[] = {
  "Home: swipe through the tiles",
  "Sparkles: a glow per open Hermes session",
  "Ask: hold Kotaro and talk",
  "Sensor: your room from Home Assistant",
  "Settings: battery and themes",
};
#define CAPTIONS (int)(sizeof captions / sizeof captions[0])
enum { CAP_HOME, CAP_SPARKLES, CAP_ASK, CAP_SENSOR, CAP_SETTINGS };

/* ---- Scripted example data (placeholders only) ---- */
#define DEMO_WIFI "Home Wi-Fi"
#define DEMO_PHONE_USER "Sam"
#define DEMO_ASK "What's the weather for my run tonight?"
/* The 9 px font is ASCII only (a degree sign would draw as '?'), so the reply spells it out. */
#define DEMO_REPLY "Light rain until 7, then clear and 14 degrees. Head out around 7:30."
static const char *const demo_reply_parts[] = {"Light rain until 7,", "Light rain until 7, then clear and 14 degrees.", DEMO_REPLY};
#define DEMO_STATUS "Checking the forecast"
static const struct { const char *id, *name, *provider; } demo_bots[] = {{"helper", "Helper", "anthropic"}, {"atlas", "Atlas", "openai"}};
#define DEMO_BOTS (int)(sizeof demo_bots / sizeof demo_bots[0])
/* Four open Hermes sessions (WLS4, ascending ids), two of them running a turn; token level 3 of 5. */
static const struct { uint64_t id; uint8_t provider, working; } demo_sessions[] = {
  {0x1d5a0c33e2f14b07ULL, LIVE_PROVIDER_ANTHROPIC, 1},
  {0x4b19e7d2a0c86f15ULL, LIVE_PROVIDER_OPENAI_CODEX, 0},
  {0x8e02f4b6c1d93a2cULL, LIVE_PROVIDER_ANTHROPIC, 0},
  {0xc7a3519e04b2d86fULL, LIVE_PROVIDER_OPENROUTER, 1},
};
#define DEMO_SESSIONS (int)(sizeof demo_sessions / sizeof demo_sessions[0])
#define DEMO_LEVEL 3
/* Indoor readings (x10: C, %, hPa, ug/m3) with the bridge's verdicts: PM2.5 is "Okay", the rest good. */
static const int16_t demo_x10[SENSORS_COUNT] = {221, 440, 10126, 40, 110, 150};
static const uint8_t demo_quality[SENSORS_COUNT] = {SQ_GOOD, SQ_GOOD, SQ_GOOD, SQ_GOOD, SQ_MEDIUM, SQ_GOOD};

/* ---- The board ---- */
static home_ui ui;                 /* the one state the poller and the workers share */
static int64_t now_us;             /* esp_timer: the 100 Hz poller's clock */
static const int64_t T0 = 700LL * 1000 * 1000;
static void need(bool ok, const char *what) {
  if (ok) return;
  fprintf(stderr, "demo_video: script out of step with the UI at %.2f s: %s\n", (now_us - T0) / 1e6, what);
  exit(1);
}

/* stdio can accept buffered writes and fail only on flush / close (disk full, broken pipe).
 * Never report a completed demo unless every output sink has completed successfully. */
static void output_error(const char *sink) {
  fprintf(stderr, "demo_video: output %s: %s\n", sink, strerror(errno ? errno : EIO));
  exit(1);
}
static void output_finish(FILE *f, const char *sink) {
  int error = 0;
  if (fflush(f) == EOF) error = errno ? errno : EIO;
  if (fclose(f) == EOF && !error) error = errno ? errno : EIO;
  if (error) { errno = error; output_error(sink); }
}

/* Wire frames, as the bridge sends them (decoded by the firmware's own decoders below). */
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static void put64(unsigned char *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static void field(unsigned char *p, const char *s, size_t n) { memset(p, 0, n); memcpy(p, s, strlen(s) < n ? strlen(s) : n - 1); }
static size_t wire_live(unsigned char *f) {
  memcpy(f, "WLS4", 4); put16(f + 4, DEMO_SESSIONS); f[6] = DEMO_LEVEL; f[7] = LIVE_FLAG_MEASURED;
  for (int i = 0; i < DEMO_SESSIONS; i++) {
    put64(f + 8 + i * 10, demo_sessions[i].id); f[16 + i * 10] = demo_sessions[i].provider; f[17 + i * 10] = demo_sessions[i].working;
  }
  return 8 + DEMO_SESSIONS * 10;
}
static void wire_bots(unsigned char f[BOTS_FRAME_SIZE]) {
  memset(f, 0, BOTS_FRAME_SIZE);
  memcpy(f, "WBT1", 4); f[4] = 1; f[5] = DEMO_BOTS; f[6] = BOTS_FLAG_PLUGIN;
  for (int i = 0; i < DEMO_BOTS; i++) {
    unsigned char *p = f + 8 + i * BOTS_ENTRY;
    field(p, demo_bots[i].id, BOTS_TEXT); field(p + 12, demo_bots[i].name, BOTS_TEXT); field(p + 24, demo_bots[i].provider, BOTS_TEXT);
    p[36] = 1; put32(p + 40, BOTS_NONE32);
  }
  put32(f + BOTS_FRAME_SIZE - 4, provision_crc(f, BOTS_FRAME_SIZE - 4));
}
static void wire_sensors(unsigned char f[SENSORS_FRAME_SIZE]) {
  memset(f, 0, SENSORS_FRAME_SIZE);
  memcpy(f, "WHS1", 4); f[4] = 1; f[5] = SS_OK; f[6] = SENSORS_COUNT;
  for (int i = 0; i < SENSORS_COUNT; i++) { put16(f + 8 + i * 6, (uint16_t)demo_x10[i]); f[10 + i * 6] = demo_quality[i]; put16(f + 12 + i * 6, 4); }
  put32(f + SENSORS_FRAME_SIZE - 4, provision_crc(f, SENSORS_FRAME_SIZE - 4));
}
static void wire_phone(unsigned char f[PHONE_FRAME_SIZE], unsigned flags) {
  memset(f, 0, PHONE_FRAME_SIZE);
  memcpy(f, "WPH1", 4); f[4] = 1; f[5] = PH_AUTHORIZED; f[6] = (unsigned char)flags;
  field(f + 12 + PHONE_CODE_LEN, DEMO_PHONE_USER, PHONE_NAME_LEN);
  put32(f + PHONE_FRAME_SIZE - 4, provision_crc(f, PHONE_FRAME_SIZE - 4));
}

/* ---- The workers (home_live.c, helper_voice.c, battery_poll), answering at once ---- */
static int64_t live_due_us, battery_due_us, bots_opened_us;
static int last_page = -1;
static battery_estimator battery_est;
static int battery_step;
static void battery_sample(int64_t at) {  /* plugged in and charging, ~1 % every 90 s (like docs_preview) */
  pmu_sample m = {.ok = true, .vbus = true, .battery = true, .charging = true, .vbat_mv = 3920 + battery_step / 3, .percent = 52 + battery_step / 6};
  ui.battery = battery_update(&battery_est, &m, at);
  battery_step++;
}
static void live_worker(void) {
  if (now_us < live_due_us) return;
  bool busy = ui.page == HELPER || ui.helper.net_busy;
  if (ui.session_off || busy) memset(&ui.live, 0, sizeof ui.live);  /* phase 5/6: no poll, the feed goes stale */
  else {
    unsigned char f[8 + LIVE_MAX * 10];size_t n = wire_live(f);
    need(live_decode(&ui.live, f, n, now_us), "WLS4 frame");
    pair_live_result(&ui.pair, 200, now_us);
  }
  live_due_us = now_us + (busy ? 500000 : 1500000);
}
static void phone_worker(int tab) {
  phone_view *v = home_tab_phone(&ui, tab);
  if (!v || ui.helper.net_busy || ui.helper.state == HV_LISTENING || home_bots_paused(&ui)) return;
  if (!phone_poll_due(v, home_phone_visible(&ui, tab), now_us)) return;
  v->refresh = false;
  unsigned char f[PHONE_FRAME_SIZE];phone_status st;
  wire_phone(f, tab == SETTINGS_HERMES ? PHONE_FLAG_REQUIRED : PHONE_FLAG_REQUIRED);  /* Home Assistant: its own sign-in */
  need(phone_decode(&st, f, sizeof f, now_us), "WPH1 frame");
  v->http = 200; v->poll_us = now_us;
  home_provider_apply(&ui, tab, &st, true);
  home_bots_sync(&ui, now_us);
}
static void bots_worker(void) {
  if (home_bots_paused(&ui) || !bots_poll_due(&ui.bots, home_bots_surface(&ui), bots_opened_us, now_us)) return;
  ui.bots.refresh = false;
  unsigned char f[BOTS_FRAME_SIZE];bots_data d;wire_bots(f);
  need(bots_decode(&d, f, sizeof f, now_us), "WBT1 frame");
  ui.bots.http = 200; ui.bots.attempts++; ui.bots.attempt_us = now_us; ui.bots.data = d; ui.bots.fails = 0;
  home_bots_sync(&ui, now_us);
}
static void sensors_worker(void) {
  if (home_bots_paused(&ui) || home_ha_off(&ui) || !sensors_poll_due(&ui.sensors, ui.page == SENSORS, bots_opened_us, now_us)) return;
  ui.sensors.refresh = false;
  unsigned char f[SENSORS_FRAME_SIZE];sensors_data d;wire_sensors(f);
  need(sensors_decode(&d, f, sizeof f, now_us), "WHS1 frame");
  ui.sensors.http = 200; ui.sensors.attempts++; ui.sensors.attempt_us = now_us; ui.sensors.data = d; ui.sensors.fails = 0;
}
/* helper_voice.c: records while listening (the mic level every 32 ms), then uploads and follows the
 * gateway: transcribing, the transcript (running), Hermes' status phrase, reply deltas, the reply. */
static struct { bool recording; int64_t level_us, sent_us; int step; } voice;
static unsigned speech_level(int64_t since_us) {  /* a talking voice: syllables, a short pause, a little noise */
  float t = since_us / 1e6f, env = t < .15f ? t / .15f : 1.f;
  float syll = fabsf(sinf(t * 12.6f)) * (.62f + .38f * sinf(t * 2.9f + .7f));
  if (t > 1.05f && t < 1.3f) syll *= .15f;
  float noise = (sp_hash((uint32_t)(since_us / 32000) * 2654435761u) & 255) / 255.f;
  float v = env * (140.f + 720.f * syll + 90.f * noise);
  return v > 1000 ? 1000u : (unsigned)v;
}
static void voice_apply(unsigned status, const char *transcript, const char *text) {
  voice_command c;memset(&c, 0, sizeof c);c.status = status;
  strcpy(c.id, "5f0c2a9e7b13d46a8e21c0f97a3b5d64");
  if (transcript) snprintf(c.transcript, sizeof c.transcript, "%s", transcript);
  if (text) snprintf(c.text, sizeof c.text, "%s", text);
  helper_apply(&ui.helper, &c);
}
static void voice_worker(void) {
  helper_view *h = &ui.helper;
  if (h->want_record) { h->want_record = false; voice.recording = true; voice.level_us = 0; }
  if (voice.recording) {
    if (h->state == HV_LISTENING) {
      if (now_us - voice.level_us >= 32000) { voice.level_us = now_us; h->level_milli = speech_level(now_us - h->press_us); }
      return;
    }
    voice.recording = false;
    bool send = h->want_send;
    h->want_send = h->want_cancel = false;
    if (!send) return;
    h->net_busy = true; voice.sent_us = now_us; voice.step = 0;
  }
  if (!voice.sent_us) return;
  static const int ms[] = {400, 900, 1100, 2500, 3000, 3500};
  int64_t dt = now_us - voice.sent_us;
  while (voice.step < 6 && dt >= ms[voice.step] * 1000LL) {
    switch (voice.step) {
      case 0: voice_apply(VOICE_TRANSCRIBING, NULL, NULL); helper_sent(h); break;  /* uploaded */
      case 1: voice_apply(VOICE_RUNNING, DEMO_ASK, NULL); break;                    /* transcript */
      case 2: if (!helper_terminal(h->state)) helper_note(h, DEMO_STATUS); break;  /* status phrase */
      case 3: voice_apply(VOICE_RUNNING, DEMO_ASK, demo_reply_parts[0]); break;     /* reply.delta */
      case 4: voice_apply(VOICE_RUNNING, DEMO_ASK, demo_reply_parts[1]); break;
      case 5: voice_apply(VOICE_DONE, DEMO_ASK, demo_reply_parts[2]); h->net_busy = false; voice.sent_us = 0; break;  /* reply + turn.end */
    }
    voice.step++;
  }
}
static void workers(void) {
  if ((int)ui.page != last_page) { bots_opened_us = now_us; last_page = (int)ui.page; }
  voice_worker();
  bots_worker();
  sensors_worker();
  phone_worker(SETTINGS_HERMES);
  phone_worker(SETTINGS_HOME_ASSISTANT);
  live_worker();
  if (now_us >= battery_due_us) { battery_sample(now_us); battery_due_us = now_us + BATTERY_POLL_US; }
}

/* ---- The owner loop (direct_main.c): 30 fps, home_visual_equal skip, home_compose ---- */
static uint16_t frames[2][SPARKLES_PIXELS], snap_px[SPARKLES_PIXELS], home_px[SPARKLES_PIXELS];
static home_snapshot page_snap = {.px = snap_px, .home = home_px};
static const uint16_t *last_frame;
static int last_frame_page = -1, back;
static home_ui painted, screen;
static bool has_painted;
static long frame_no;
static int64_t frame_at(long k) { return T0 + (int64_t)llround(k * 1e6 / FPS); }

/* ---- The compositor: device frame, panel corners, captions, fingertip ---- */
static unsigned char *tmpl[CAPTIONS], *mask, out[OUT_W * OUT_H * 3];
static int caption = CAP_HOME, caption_from = CAP_HOME;
static int64_t caption_us;
static bool touch_marker = true;
static const char *frames_dir;
static struct { bool down; float x, y; int64_t down_us, up_us; } finger;
static FILE *story;
static void note(const char *fmt, ...) {
  double s = (now_us - T0) / 1e6;
  char line[160];va_list ap;va_start(ap, fmt);vsnprintf(line, sizeof line, fmt, ap);va_end(ap);
  if (fprintf(story, "%02d:%04.1f  %s\n", (int)(s / 60), fmod(s, 60), line) < 0) output_error("storyboard");
}
static void compose_out(const uint16_t *px) {
  /* the device frame of this section (cross-fading the caption after a change) */
  memcpy(out, tmpl[caption], sizeof out);
  int64_t age = now_us - caption_us;
  if (caption_from != caption && age < CAP_FADE_US) {
    unsigned a = (unsigned)(age * 256 / CAP_FADE_US);
    const unsigned char *from = tmpl[caption_from], *to = tmpl[caption];
    for (size_t i = (size_t)(SCR_Y + SCR_H) * OUT_W * 3; i < sizeof out; i++) out[i] = (unsigned char)((from[i] * (256 - a) + to[i] * a) >> 8);
  }
  /* the panel, pixel-exact: RGB565 -> RGB888 as the docs previews, SCALE x SCALE nearest neighbour */
  for (int y = 0; y < 448; y++)
    for (int x = 0; x < 368; x++) {
      uint16_t v = px[y * 368 + x];
      unsigned c[3] = {(v >> 11) * 255u / 31, ((v >> 5) & 63) * 255u / 63, (v & 31) * 255u / 31};
      for (int dy = 0; dy < SCALE; dy++)
        for (int dx = 0; dx < SCALE; dx++) {
          int mx = x * SCALE + dx, my = y * SCALE + dy;
          unsigned m = mask[my * SCR_W + mx];
          if (!m) continue;
          unsigned char *o = out + ((size_t)(SCR_Y + my) * OUT_W + SCR_X + mx) * 3;
          for (int k = 0; k < 3; k++) o[k] = (unsigned char)(m == 255 ? c[k] : (c[k] * m + o[k] * (255 - m) + 127) / 255);
        }
    }
  /* fingertip: a soft light disc with a thin dark ring (reads on Light and Dark), fading in and out */
  float alpha = 0;
  if (finger.down) alpha = fminf(1.f, (now_us - finger.down_us) / 60000.f);
  else if (finger.up_us && now_us - finger.up_us < 180000) alpha = 1.f - (now_us - finger.up_us) / 180000.f;
  if (touch_marker && alpha > 0) {
    float cx = SCR_X + (finger.x + .5f) * SCALE, cy = SCR_Y + (finger.y + .5f) * SCALE, r = 25.f;
    for (int y = (int)(cy - r - 2); y <= (int)(cy + r + 2); y++)
      for (int x = (int)(cx - r - 2); x <= (int)(cx + r + 2); x++) {
        if (x < SCR_X || y < SCR_Y || x >= SCR_X + SCR_W || y >= SCR_Y + SCR_H) continue;
        float d = sqrtf((x + .5f - cx) * (x + .5f - cx) + (y + .5f - cy) * (y + .5f - cy));
        float fill = fminf(1.f, fmaxf(0.f, r - 2.5f - d)), ring = fminf(1.f, fmaxf(0.f, r - d)) - fill;
        float m = mask[(y - SCR_Y) * SCR_W + (x - SCR_X)] / 255.f;
        unsigned char *o = out + ((size_t)y * OUT_W + x) * 3;
        for (int k = 0; k < 3; k++) {
          float v = o[k];
          v += (255.f - v) * .34f * fill * alpha * m;
          v += (36.f - v) * .45f * ring * alpha * m;
          o[k] = (unsigned char)(v + .5f);
        }
      }
  }
  /* stdout: raw RGB24 frames (the P6 payload; ffmpeg -f rawvideo). DEMO_FRAMES=DIR also keeps each
   * frame as DIR/NNNNN.ppm (~3.5 MB each: opt-in). */
  if (fwrite(out, 1, sizeof out, stdout) != sizeof out) output_error("stdout");
  if (frames_dir) {
    char path[1024];snprintf(path, sizeof path, "%s/%05ld.ppm", frames_dir, frame_no);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    if (fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H) < 0) output_error("frame header");
    if (fwrite(out, 1, sizeof out, f) != sizeof out) output_error("frame pixels");
    output_finish(f, "frame");
  }
}
static void emit_frame(void) {
  screen = ui; screen.live_now_us = now_us;
  if (!(has_painted && home_visual_equal(&screen, &painted))) {
    uint16_t *shadow = frames[back];
    need(home_compose(&screen, shadow, SPARKLES_PIXELS, &page_snap, last_frame, last_frame_page) != 0, "home_compose");
    back ^= 1;
    last_frame = shadow;
    last_frame_page = home_page_offset(&screen) == 0 && !home_blob_shown(&screen) && !screen.power.countdown ? (int)screen.page : -1;
    painted = screen; has_painted = true;
    if (screen.page == HELPER) ui.helper.scroll_max = helper_scroll_extent;
  }
  compose_out(last_frame);
  frame_no++;
}

/* ---- The poller (direct_main.c touch_poll) and the finger ---- */
static touch_debounce debounce;
static void advance(int ms) {
  for (int i = 0; i < ms / 10; i++) {
    now_us += 10000;
    bool contact = finger.down;
    unsigned sx = contact ? (unsigned)lrintf(finger.x) : 0, sy = contact ? (unsigned)lrintf(finger.y) : 0;  /* release: no point */
    int dx, dy;
    contact = touch_debounce_step(&debounce, contact, (int)sx, (int)sy, &dx, &dy);
    if (contact) { sx = (unsigned)dx; sy = (unsigned)dy; }
    home_sample(&ui, now_us, contact, (int)sx, (int)sy);
    memset(&ui.note, 0, sizeof ui.note);
    workers();
    while (frame_at(frame_no) <= now_us) emit_frame();
  }
}
static void wait_s(float s) { advance((int)lrintf(s * 100) * 10); }
static void touch_down(float x, float y) { finger.down = true; finger.x = x; finger.y = y; finger.down_us = now_us; }
static void touch_up(void) { finger.down = false; finger.up_us = now_us; }
static float ease_io(float u) { return .5f - .5f * cosf(u * 3.14159265f); }
static void move_to(float x1, float y1, int ms, float (*ease)(float)) {
  float x0 = finger.x, y0 = finger.y;int n = ms / 10;
  for (int k = 1; k <= n; k++) { float u = ease((float)k / n); finger.x = x0 + (x1 - x0) * u; finger.y = y0 + (y1 - y0) * u; advance(10); }
}
static void swipe(float x0, float y0, float x1, float y1, int ms) { touch_down(x0, y0); advance(20); move_to(x1, y1, ms, ease_io); touch_up(); }
static void tap(float x, float y) { touch_down(x, y); advance(70); touch_up(); advance(40); }
/* Home carousel: one tile to the left (next) or right (previous); waits for the settle. */
static void carousel(int dir) {
  int to = ui.tile + dir;
  if (dir > 0) swipe(300, 236, 64, 228, 240); else swipe(64, 236, 300, 228, 240);
  for (int i = 0; i < 60 && (ui.home_slide_from || ui.down); i++) advance(10);
  need(ui.page == HOME && ui.tile == to, "carousel swipe");
}
static void open_tile(home_page page) {
  need(ui.page == HOME && home_tiles[ui.tile].page == page, "tile to open");
  tap(184, 220);
  need(ui.page == page, "tile tap opens its page");
}
static void section(int k) { caption_from = caption; caption = k; caption_us = now_us; note("-- %s", captions[k]); }
/* README GIF cut: [start, end) seconds of the MP4 */
static FILE *gif;
static double gif_start = -1;
static void gif_in(void) { gif_start = (now_us - T0) / 1e6; }
static void gif_out(void) {
  if (fprintf(gif, "%.3f %.3f\n", gif_start, (now_us - T0) / 1e6) < 0) output_error("gif selection");
  gif_start = -1;
}

/* Go Home: pull UP from the bottom edge. A fixed-center circle reveals Home and finishes expanding
 * from a partial flick; the current theme accent is confined to its narrow edge. */
static void demo_go_home(void) {
  note("Home gesture: centered circle expands to reveal Home");
  touch_down(184, 440);
  advance(30);
  move_to(184, 400, 60, ease_io);
  touch_up();
  for (int i = 0; i < 300 && !(ui.page == HOME && !ui.down && !home_motion_moving(&ui)); i++) advance(10);
  need(ui.page == HOME, "demo_go_home reaches Home");
}

/* ---- Assets ---- */
static unsigned char *read_pnm(const char *path, const char *magic, int w, int h, int channels) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(1); }
  char m[3] = {0};int vals[3], n = 0;
  if (fscanf(f, "%2s", m) != 1 || strcmp(m, magic)) { fprintf(stderr, "%s: not %s\n", path, magic); exit(1); }
  while (n < 3) {
    int c = fgetc(f);
    if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); continue; }
    if (c == EOF) break;
    if (c >= '0' && c <= '9') { ungetc(c, f); if (fscanf(f, "%d", &vals[n++]) != 1) break; }
  }
  fgetc(f);
  if (n != 3 || vals[0] != w || vals[1] != h || vals[2] != 255) { fprintf(stderr, "%s: want %dx%d 8-bit\n", path, w, h); exit(1); }
  size_t size = (size_t)w * h * channels;unsigned char *p = malloc(size);
  if (!p || fread(p, 1, size, f) != size) { fprintf(stderr, "%s: short\n", path); exit(1); }
  if (fclose(f) == EOF) { perror(path); exit(1); }
  return p;
}

/* ---- The storyboard ---- */
static void boot(void) {
  memset(&ui, 0, sizeof ui);
  now_us = T0;
  ui.input.stamp_us = T0;
  direct_set_usage(&ui.input, SP_USAGE_DEFAULT);       /* owner(): calm mid default */
  ui.connected = ui.saved = true;                       /* Wi-Fi from the flash, joined */
  snprintf(ui.credentials.ssid, sizeof ui.credentials.ssid, "%s", DEMO_WIFI);
  snprintf(ui.status, sizeof ui.status, "Connected to Wi-Fi");  /* home_wifi.c on joining */
  ui.pair.state = PAIR_ENROLLED_UNPAIRED;               /* enrolled with the host bridge */
  helper_restore_bot(&ui.helper, 0);
  /* The battery poller has been running for 10 minutes on the charger (the time-to-full estimate). */
  battery_estimate_reset(&battery_est);
  for (int i = 0; i < 40; i++) battery_sample(T0 - (40 - i) * BATTERY_POLL_US);
  battery_due_us = T0;
}
static void storyboard(void) {
  /* 1. Home: the carousel shows each tile (real swipes), then back to the first one. */
  section(CAP_HOME);
  note("Home: the Sparkles tile (session glows inside it)");
  wait_s(.5f);
  gif_in();
  wait_s(1.0f);
  carousel(+1); note("swipe: Ask tile"); wait_s(1.0f);
  carousel(+1); note("swipe: Sensor tile"); wait_s(.4f);
  gif_out();
  wait_s(.5f);
  carousel(+1); note("swipe: Settings tile"); wait_s(1.0f);
  for (int k = 0; k < 3; k++) { carousel(-1); wait_s(k < 2 ? .3f : .6f); }
  note("swipes back: the Sparkles tile");

  /* 2. Sparkles: 4 open sessions (2 working), a finger paints a trail; go Home. */
  section(CAP_SPARKLES);
  gif_in();
  open_tile(SPARKLES); note("tap: Sparkles opens (4 sessions, 2 working)");
  wait_s(1.0f);
  note("paint a trail");
  touch_down(96, 150); advance(30);
  for (int k = 1; k <= 340; k++) {  /* 3.4 s, a slow looping stroke clear of the bottom edge */
    float t = k / 100.f;
    finger.x = 184 + 112 * sinf(1.9f * t - 1.4f + .1f * t * t);
    finger.y = 224 + 118 * sinf(1.27f * t - .63f);
    advance(10);
    if (k == 150) gif_out();
  }
  touch_up(); wait_s(1.0f);
  need(ui.page == SPARKLES, "painting stays on Sparkles");
  demo_go_home(); wait_s(.5f);

  /* 3. Ask: Kotaro idle, hold to talk, the bot runs, the reply streams in; swipe bots; go Home. */
  section(CAP_ASK);
  carousel(+1); wait_s(.4f);
  open_tile(HELPER); note("tap: Ask opens, Kotaro idle (Helper)");
  wait_s(1.6f);
  note("hold Kotaro: listening");
  touch_down(184, 214);
  advance(1500);
  gif_in();
  advance(1200);
  need(ui.helper.state == HV_LISTENING, "holding Kotaro listens");
  touch_up(); note("let go: sending");
  advance(50);
  need(ui.helper.state == HV_TRANSCRIBING, "release sends");
  for (int i = 0; i < 600 && ui.helper.state != HV_DONE; i++) {
    helper_state before = ui.helper.state;advance(10);
    if (ui.helper.state == HV_RUNNING && before != HV_RUNNING) note("running: Kotaro works, \"%s\"", DEMO_STATUS);
  }
  need(ui.helper.state == HV_DONE, "reply done");
  note("done: the reply types out, Kotaro cheers");
  wait_s(.7f);
  gif_out();
  wait_s(1.7f);
  swipe(300, 170, 70, 166, 260); wait_s(.2f);
  need(ui.helper.bot == 1, "swipe to the second bot");
  note("swipe: Atlas"); wait_s(1.3f);
  swipe(70, 170, 300, 166, 260); wait_s(.2f);
  need(ui.helper.bot == 0 && ui.helper.state == HV_DONE, "swipe back keeps the chat");
  note("swipe back: Helper, the chat kept"); wait_s(1.3f);
  demo_go_home(); wait_s(.5f);

  /* 4. Sensor: indoor readings from Home Assistant; go Home. */
  section(CAP_SENSOR);
  carousel(+1); wait_s(.4f);
  gif_in();
  open_tile(SENSORS); note("tap: Sensor opens");
  wait_s(2.0f);
  gif_out();
  wait_s(.8f);
  demo_go_home(); wait_s(.5f);

  /* 5. Settings: Battery (charging), then Display: Light -> Dark and a couple of accents; go Home
   * in the new theme, and a look at Ask in it (the accent colours your chat bubbles). */
  section(CAP_SETTINGS);
  carousel(+1); wait_s(.4f);
  open_tile(SETTINGS); note("tap: Settings opens on Wi-Fi");
  need(ui.settings_tab == SETTINGS_WIFI, "Settings opens on the Wi-Fi tab");
  wait_s(.8f);
  for (int k = 0; k < 3; k++) { swipe(290, 250, 76, 246, 220); wait_s(k < 2 ? .4f : .2f); }
  need(ui.settings_tab == SETTINGS_BATTERY, "three swipes to Battery");
  note("swipe: Battery (charging)"); wait_s(2.2f);
  for (int k = 0; k < 2; k++) { swipe(76, 250, 290, 246, 220); wait_s(.4f); }
  need(ui.settings_tab == SETTINGS_DISPLAY, "back to Display");
  note("swipe: Display"); wait_s(.6f);
  gif_in();
  tap(184, 118); need(ui.theme_mode == THEME_DARK, "Theme: Dark"); note("tap Theme: Dark"); wait_s(.9f);
  tap(184, 226); need(ui.accent == 1, "Accent: Blue"); note("tap Accent: Blue"); wait_s(.8f);
  tap(184, 226); need(ui.accent == 2, "Accent: Green"); note("tap Accent: Green"); wait_s(.9f);
  demo_go_home();  /* Home in Dark + Green */
  wait_s(.3f);
  gif_out();
  wait_s(.2f);
  carousel(-1); wait_s(.3f);
  carousel(-1); wait_s(.4f);
  open_tile(HELPER); note("tap: Ask in Dark + Green, the chat kept");
  wait_s(2.2f);
  demo_go_home();
  wait_s(1.2f);
  note("end");
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--layout")) {
    if (printf("OUT_W=%d OUT_H=%d SCALE=%d SCR_X=%d SCR_Y=%d SCR_W=%d SCR_H=%d SCR_R=%d BEZEL=%d CAP_Y=%d FPS=%d CAPTIONS=%d\n",
               OUT_W, OUT_H, SCALE, SCR_X, SCR_Y, SCR_W, SCR_H, SCR_R, BEZEL, CAP_Y, FPS, CAPTIONS) < 0) output_error("layout");
    for (int k = 0; k < CAPTIONS; k++) if (printf("CAPTION_%d='%s'\n", k, captions[k]) < 0) output_error("layout");
    output_finish(stdout, "layout");
    return 0;
  }
  if (argc != 2) { fprintf(stderr, "usage: demo_video --layout | demo_video ASSET_DIR > frames.rgb24\n"); return 2; }
  const char *dir = argv[1];char path[1024];
  for (int k = 0; k < CAPTIONS; k++) { snprintf(path, sizeof path, "%s/frame-%d.ppm", dir, k); tmpl[k] = read_pnm(path, "P6", OUT_W, OUT_H, 3); }
  snprintf(path, sizeof path, "%s/mask.pgm", dir); mask = read_pnm(path, "P5", SCR_W, SCR_H, 1);
  const char *tm = getenv("DEMO_TOUCH");touch_marker = !tm || strcmp(tm, "0");
  frames_dir = getenv("DEMO_FRAMES");
  if (frames_dir && !frames_dir[0]) frames_dir = NULL;
  snprintf(path, sizeof path, "%s/storyboard.txt", dir); story = fopen(path, "w");
  snprintf(path, sizeof path, "%s/gif.txt", dir); gif = fopen(path, "w");
  if (!story || !gif) { perror(path); return 1; }
  static char buf[1 << 22];setvbuf(stdout, buf, _IOFBF, sizeof buf);
  boot();
  storyboard();
  if (fprintf(story, "frames=%ld fps=%d duration_s=%.2f\n", frame_no, FPS, frame_no / (double)FPS) < 0) output_error("storyboard");
  output_finish(story, "storyboard"); output_finish(gif, "gif selection");
  output_finish(stdout, "stdout");
  if (fprintf(stderr, "demo_video: %ld frames (%.1f s)\n", frame_no, frame_no / (double)FPS) < 0 || fflush(stderr) == EOF) return 1;
  return 0;
}
