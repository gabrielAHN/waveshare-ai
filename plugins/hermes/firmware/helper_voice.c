/* Helper voice worker: board mic (ES8311 via BSP/esp_codec_dev) -> PSRAM PCM -> Hermes.
 * The stock Hermes Gadget SDK protocol to the selected bot's profile in the Hermes gateway,
 * through the bridge's gadget front (send_command_gateway).
 * Runs as its own low-priority task on CPU0; never touches display/touch/DMA resources.
 * Logs only states, sizes and mic LEVELS -- never audio, transcripts, replies, ids or tokens. */
#include "esp_timer.h"
#include "helper_voice.h"
#include "home_live.h"
#include "home_pair.h"
#include "power_main.h"
#include "pair_tls.h"
#include "voice_wire.h"
#include "gadget_wire.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "completion_click.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/task.h"

/* Narrow declaration instead of the BSP umbrella header (which drags in LVGL headers).
 * Implemented by waveshare/esp32_s3_touch_amoled_1_8 2.0.3; initializes shared I2C + I2S. */
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

#define TAG "helper_voice"
#define CHUNK_SAMPLES 512
#define MIC_GAIN_DB 30.0f
#define POLL_SLICE_MS 30
#define COMMAND_DEADLINE_US (330LL * 1000 * 1000)

static home_ui *state;
static SemaphoreHandle_t lock;
static unsigned char *audio;      /* VOICE_BUFFER_BYTES, PSRAM: PCM16 */
static esp_codec_dev_handle_t mic;
static esp_codec_dev_handle_t speaker;
static completion_once click_once;
static EXT_RAM_BSS_ATTR int16_t click_pcm[CLICK_SAMPLES]; /* 8.5 KB chime in PSRAM: never the stack or internal RAM */

static void set_busy(bool busy) {
  xSemaphoreTake(lock, portMAX_DELAY);
  state->helper.net_busy = busy;
  xSemaphoreGive(lock);
}

static void fail(const char *why) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!helper_terminal(state->helper.state)) helper_fail(&state->helper, why);
  xSemaphoreGive(lock);
  ESP_LOGW(TAG, "VOICE_ERROR reason=\"%s\"", why);
}

static void play_completion_click(void) {
  completion_click_make(click_pcm, CLICK_SAMPLES);
  if (!speaker) speaker = bsp_audio_codec_speaker_init();
  if (!speaker) { ESP_LOGW(TAG, "CLICK_INIT failed"); return; }
  esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 1, .sample_rate = CLICK_RATE};
  /* The Waveshare BSP creates separate ES8311 device handles over one shared
   * I2S data interface. This worker serializes mic and speaker use; recording
   * has closed the input handle before a terminal command can reach here. */
  if (esp_codec_dev_set_out_vol(speaker, CLICK_VOLUME) != ESP_CODEC_DEV_OK) { ESP_LOGW(TAG, "CLICK_VOLUME failed"); return; }
  if (esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGW(TAG, "CLICK_OPEN failed"); return; }
  int result = esp_codec_dev_write(speaker, click_pcm, sizeof click_pcm);
  int closed = esp_codec_dev_close(speaker);
  memset(click_pcm, 0, sizeof click_pcm);
  ESP_LOGI(TAG, "COMPLETION_CLICK played=%d samples=%u peak_max=%u voice_stack_free=%u",
           result==ESP_CODEC_DEV_OK&&closed==ESP_CODEC_DEV_OK,
           (unsigned)CLICK_SAMPLES,(unsigned)CLICK_PEAK_MAX,(unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void apply(const voice_command *c) {
  bool click;
  xSemaphoreTake(lock, portMAX_DELAY);
  helper_state before=state->helper.state;
  helper_apply(&state->helper, c);
  bool accepted_done=before!=HV_DONE&&state->helper.state==HV_DONE;
  click = completion_once_edge(&click_once, c->status, state->sound_off)&&accepted_done&&!power_sleeping();  /* asleep: amp stays off */
  xSemaphoreGive(lock);
  if (click) play_completion_click();
}

/* ---- Gadget SDK transport (Hermes gateway, one platform per bot profile) ------------------------- */

#define GW_MSG_MAX (24 * 1024)   /* a gateway reply (<= 4000 chars), JSON-escaped */
#define GW_AUDIO_CHUNK 4096      /* PCM bytes per binary frame (hub limit 8192, even) */
#define GW_HANDSHAKE_MS 10000

static EXT_RAM_BSS_ATTR char gw_msg[GW_MSG_MAX + 1];
static EXT_RAM_BSS_ATTR unsigned char gw_frame[4 + GW_AUDIO_CHUNK];

/* Next replay counter, persisted BEFORE it is used: the hub refuses any counter it has seen. */
static bool gw_next_counter(uint64_t *out) {
  nvs_handle_t h;
  uint64_t value = 0;
  if (nvs_open("home_wifi", NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = nvs_get_u64(h, "gw_ctr", &value);
  if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return false; }
  value++;
  bool ok = nvs_set_u64(h, "gw_ctr", value) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  *out = value;
  return ok && value;
}

/* esp_transport_ws masks the payload IN PLACE, so every text frame is sent from this writable copy
 * (a string literal lives in flash: masking it there is a cache-error panic). Wiped after each send
 * because auth frames pass through it. */
static char gw_out[1024];
static int64_t gw_last_tx;  /* last frame sent (keepalive, gadget_wire.h gw_keepalive_due) */
static bool gw_send_text(esp_transport_handle_t ws, const char *text) {
  size_t n = strlen(text);
  if (!n || n >= sizeof gw_out) return false;
  memcpy(gw_out, text, n);
  bool ok = esp_transport_ws_send_raw(ws, WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN, gw_out, (int)n, 5000) == (int)n;
  memset(gw_out, 0, n);
  if (ok) gw_last_tx = esp_timer_get_time();
  return ok;
}
/* While waiting on the gateway: our own ping when nothing was sent for a while (false = link down). */
static bool gw_keepalive(esp_transport_handle_t ws) {
  if (!gw_keepalive_due(esp_timer_get_time(), gw_last_tx)) return true;
  return gw_send_text(ws, "{\"type\":\"ping\"}");
}

/* One whole text message into gw_msg. >0 length, 0 nothing yet, -1 closed/error, -2 skipped (binary/oversize). */
static int gw_recv(esp_transport_handle_t ws, int timeout_ms) {
  int n = esp_transport_read(ws, gw_msg, GW_MSG_MAX, timeout_ms);
  if (n < 0) return -1;
  if (n == 0) return 0;
  ws_transport_opcodes_t op = esp_transport_ws_get_read_opcode(ws);
  int total = esp_transport_ws_get_read_payload_len(ws);
  if (op == WS_TRANSPORT_OPCODES_CLOSE) return -1;
  int got = n;
  bool oversize = total > GW_MSG_MAX;
  while (got < total) {
    int r = oversize ? esp_transport_read(ws, gw_msg, GW_MSG_MAX, 3000)
                     : esp_transport_read(ws, gw_msg + got, GW_MSG_MAX - got, 3000);
    if (r <= 0) return -1;
    got += r;
  }
  if (oversize || op != WS_TRANSPORT_OPCODES_TEXT) return -2;
  gw_msg[got] = 0;
  return got;
}

/* Ask: the stock Gadget SDK protocol to the selected bot's `gadget` platform in the Hermes gateway,
 * through the bridge's gadget front (same host and pinned certificate as the bridge; the front admits only this
 * enrolled board while it is signed in with the phone QR and approves its SDK id: no second pairing).
 * One connection per hold: hello -> challenge -> auth -> welcome, session.new after a swipe from the
 * top, audio.start + PCM frames + audio.end, then turn.start / transcript / reply.delta / reply /
 * turn.end. Stop = cancel (Hermes /stop). Logs states and sizes only. */
static bool gw_handshake(esp_transport_handle_t ws, const gw_identity *id, const char **why) {
  char out[512], nonce[64], kind[24];
  if (!gw_hello_gateway_json(id, out, sizeof out) || !gw_send_text(ws, out)) { *why = "Hermes connection lost"; return false; }
  for (int64_t until = esp_timer_get_time() + GW_HANDSHAKE_MS * 1000LL; esp_timer_get_time() < until;) {
    int n = gw_recv(ws, 200);
    if (n == -1) { *why = "Hermes refused the board"; return false; }
    if (n <= 0) continue;
    if (!gw_json_string(gw_msg, (size_t)n, "type", kind, sizeof kind)) continue;
    if (!strcmp(kind, "challenge")) {
      bool enrolled = false;
      bool ok = gw_json_string(gw_msg, (size_t)n, "nonce", nonce, sizeof nonce) && gw_json_bool(gw_msg, (size_t)n, "enrolled", &enrolled) &&
                (enrolled ? gw_auth_mac_json(id, nonce, out, sizeof out) : gw_auth_key_json(id, out, sizeof out)) && gw_send_text(ws, out);
      provision_wipe(out, sizeof out);
      if (!ok) { *why = "Hermes connection lost"; return false; }
    } else if (!strcmp(kind, "welcome")) {
      bool paired = false;
      if (gw_json_bool(gw_msg, (size_t)n, "paired", &paired) && paired) return true;
      *why = "Sign in on the board again";
      return false;
    } else if (!strcmp(kind, "error")) {
      *why = "Hermes refused the board";
      return false;
    }
  }
  *why = "Hermes not answering";
  return false;
}

static void send_command_gateway(size_t samples, const char *source) {
  static pair_record record;
  static EXT_RAM_BSS_ATTR voice_command command;
  static char notice[48];
  unsigned char token[32];
  char host[100], bearer[96], out[512], kind[24], rid[33], outcome[16], prompt_id[40];
  gw_identity id;
  if (!home_pair_load(&record, token)) { fail("Connect to Hermes in Settings"); return; }
  if (!gw_host_from_base(record.base, host, sizeof host)) { provision_wipe(token, sizeof token); fail("Connect to Hermes in Settings"); return; }
  gw_identity_from_token(token, &id);
  gw_bearer_header(token, bearer, sizeof bearer);
  provision_wipe(token, sizeof token);
  pair_pin_require(pair_tls_ctx(PAIR_TLS_VOICE), record.fp);
  set_busy(true);
  for (int waited = 0; waited < 130; waited++) {  /* let an in-flight live/bots TLS request finish first */
    xSemaphoreTake(lock, portMAX_DELAY);
    bool polling = state->bots_busy;
    xSemaphoreGive(lock);
    if (!polling) break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  bool cont;
  char bot[BOTS_TEXT], path[48];
  xSemaphoreTake(lock, portMAX_DELAY);
  cont = state->helper.cont;
  snprintf(bot, sizeof bot, "%s", bots_id(&state->bots, state->helper.bot));
  xSemaphoreGive(lock);
  uint64_t counter = 0;
  int64_t start = esp_timer_get_time();
  unsigned last = 0, updates = 0, stops = 0;
  const char *why = NULL;
  memset(&command, 0, sizeof command);
  if (!gw_gateway_path(bot, path, sizeof path)) { provision_wipe(bearer, sizeof bearer); set_busy(false); fail("Bot unavailable"); return; }
  esp_transport_handle_t ssl = esp_transport_ssl_init(), ws = ssl ? esp_transport_ws_init(ssl) : NULL;
  if (!ws) { why = "Out of memory"; goto done; }
  esp_transport_ssl_crt_bundle_attach(ssl, pair_tls_attach_voice);
  esp_transport_ssl_skip_common_name_check(ssl);  /* the pin is the trust anchor; host is an IP */
  esp_transport_ws_set_path(ws, path);
  esp_transport_ws_set_subprotocol(ws, GW_SUBPROTOCOL);
  esp_transport_ws_set_headers(ws, bearer);
  if (!gw_next_counter(&counter)) { why = "Storage error"; goto done; }
  gw_request_id(&id, counter, rid);
  if (esp_transport_connect(ws, host, CONFIG_WAVESHARE_AI_GATEWAY_PORT, 12000) < 0) {
    int http = esp_transport_ws_get_upgrade_request_status(ws);
    why = http == 401 || http == 403 ? "Sign in on the board again"
          : (http == 404 ? "Bot unavailable" : (http == 503 ? "Hermes gateway offline" : "Hermes unreachable"));
    ESP_LOGW(TAG, "GADGET_CONNECT ok=0 http=%d", http);
    goto done;
  }
  provision_wipe(bearer, sizeof bearer);
  ESP_LOGI(TAG, "GADGET_CONNECT ok=1 transport=gateway connect_us=%lld", (long long)(esp_timer_get_time() - start));
  if (!gw_handshake(ws, &id, &why)) goto done;
  if (!cont) {  /* a new chat (top pull or pull-up): Hermes /new first (the adapter confirms a device-asked /new) */
    if (!gw_send_text(ws, "{\"type\":\"session.new\"}")) { why = "Hermes connection lost"; goto done; }
    bool ended = false;
    for (int64_t until = esp_timer_get_time() + 10000000LL; esp_timer_get_time() < until;) {
      if (!gw_keepalive(ws)) { why = "Hermes connection lost"; goto done; }
      int n = gw_recv(ws, 200);
      if (n == -1) { why = "Hermes connection lost"; goto done; }
      if (n <= 0) continue;
      if (gw_type_is(gw_msg, (size_t)n, "ping")) {
        if (gw_pong_json(gw_msg, (size_t)n, out, sizeof out)) {
          gw_send_text(ws, out);
        }
        continue;
      }
      if (gw_type_is(gw_msg, (size_t)n, "error")) { why = "Hermes refused the board"; goto done; }
      if (gw_type_is(gw_msg, (size_t)n, "reply") && ended) break;  /* "New session started": not shown */
      if (gw_type_is(gw_msg, (size_t)n, "turn.end") && !ended) {
        ended = true;
        until = esp_timer_get_time() + 1500000LL;  /* its reply follows the turn.end */
      }
    }
    ESP_LOGI(TAG, "GADGET_NEW_SESSION done_us=%lld", (long long)(esp_timer_get_time() - start));
  }
  /* Upload: audio.start, PCM frames, audio.end. */
  if (!gw_gateway_audio_start_json(rid, out, sizeof out) || !gw_send_text(ws, out)) { why = "Hermes connection lost"; goto done; }
  const unsigned char *pcm = audio;
  size_t bytes = samples * 2;
  unsigned seq = 0;
  for (size_t off = 0; off < bytes; off += GW_AUDIO_CHUNK, seq++) {
    size_t n = bytes - off < GW_AUDIO_CHUNK ? bytes - off : GW_AUDIO_CHUNK;
    gw_audio_header(seq, gw_frame);
    memcpy(gw_frame + 4, pcm + off, n);
    if (esp_transport_ws_send_raw(ws, WS_TRANSPORT_OPCODES_BINARY | WS_TRANSPORT_OPCODES_FIN, (const char *)gw_frame, (int)(n + 4), 8000) != (int)(n + 4)) {
      why = "Upload failed";
      goto done;
    }
  }
  if (!gw_gateway_audio_end_json(rid, (unsigned)(samples * 1000 / VOICE_RATE), out, sizeof out) || !gw_send_text(ws, out)) {
    why = "Hermes connection lost";
    goto done;
  }
  ESP_LOGI(TAG, "VOICE_UPLOAD transport=gateway bytes=%u frames=%u request_us=%lld source=%s bot=%s thread=%s", (unsigned)bytes, seq,
           (long long)(esp_timer_get_time() - start), source, bot, cont ? "continue" : "new");
  memcpy(command.id, rid, sizeof command.id);
  command.status = VOICE_TRANSCRIBING;
  completion_once_seed(&click_once, VOICE_TRANSCRIBING);
  apply(&command);
  xSemaphoreTake(lock, portMAX_DELAY);
  helper_sent(&state->helper);  /* sent: the next hold continues this Hermes session */
  xSemaphoreGive(lock);
  bool started = false, got_reply = false;
  while (esp_timer_get_time() - start < COMMAND_DEADLINE_US) {
    bool stop, terminal, pressed;
    xSemaphoreTake(lock, portMAX_DELAY);
    stop = state->helper.want_stop;
    state->helper.want_stop = false;
    terminal = helper_terminal(state->helper.state);
    pressed = state->helper.state == HV_LISTENING || state->helper.want_record;
    xSemaphoreGive(lock);
    if ((terminal || pressed) && !stop) break;  /* the screen moved on (new hold) */
    if (stop) {
      if (gw_send_text(ws, "{\"type\":\"cancel\"}")) ++stops;
      command.status = VOICE_STOPPING;
      apply(&command);
      ESP_LOGI(TAG, "VOICE_STOP transport=gateway sent=%u", stops);
    }
    if (!gw_keepalive(ws)) { why = "Hermes connection lost"; break; }
    int n = gw_recv(ws, POLL_SLICE_MS * 3);
    if (n == -1) { why = "Hermes connection lost"; break; }
    if (n <= 0 || !gw_json_string(gw_msg, (size_t)n, "type", kind, sizeof kind)) continue;
    bool stopping = command.status == VOICE_STOPPING;
    if (!strcmp(kind, "ping")) {
      if (gw_pong_json(gw_msg, (size_t)n, out, sizeof out)) {
        gw_send_text(ws, out);
      }
      continue;
    } else if (!strcmp(kind, "turn.start")) {
      started = true;
      continue;
    } else if (!strcmp(kind, "status")) {  /* live working phrase ("Searching the web"); "" clears it */
      char phrase[48];
      if (!gw_json_text(gw_msg, (size_t)n, "text", phrase, sizeof phrase)) continue;
      xSemaphoreTake(lock, portMAX_DELAY);
      if (!helper_terminal(state->helper.state)) helper_note(&state->helper, phrase);
      xSemaphoreGive(lock);
      continue;
    } else if (!strcmp(kind, "transcript")) {
      if (!gw_json_text(gw_msg, (size_t)n, "text", command.transcript, sizeof command.transcript)) continue;
      if (!stopping) command.status = VOICE_RUNNING;
    } else if (!strcmp(kind, "reply.delta") || !strcmp(kind, "reply")) {
      bool interim = false;
      if (gw_json_bool(gw_msg, (size_t)n, "interim", &interim) && interim) continue;  /* progress commentary */
      if (!gw_json_text(gw_msg, (size_t)n, "text", command.text, sizeof command.text)) continue;
      got_reply = !strcmp(kind, "reply");  /* a delta after a reply is a new, partial message */
      if (!stopping) command.status = VOICE_RUNNING;
    } else if (!strcmp(kind, "turn.end")) {
      if (!gw_json_string(gw_msg, (size_t)n, "outcome", outcome, sizeof outcome)) outcome[0] = 0;
      gw_turn_end(outcome, got_reply, &command);
    } else if (!strcmp(kind, "notice")) {
      if (started || !gw_json_text(gw_msg, (size_t)n, "text", notice, sizeof notice)) continue;
      why = notice;  /* e.g. a too-short hold: dropped before any turn */
      break;
    } else if (!strcmp(kind, "error")) {
      why = "Hermes refused the request";
      break;
    } else if (!strcmp(kind, "unpaired") || !strcmp(kind, "pairing")) {
      why = "Sign in on the board again";
      break;
    } else if (!strcmp(kind, "prompt")) {  /* approvals are answered on the host, never from the board */
      if (gw_json_string(gw_msg, (size_t)n, "id", prompt_id, sizeof prompt_id) && gw_prompt_reply_json(prompt_id, false, out, sizeof out)) {
        gw_send_text(ws, out);
      }
      continue;
    } else {
      continue;
    }
    apply(&command);
    ++updates;
    if (command.status != last) {
      ESP_LOGI(TAG, "VOICE_STATE transport=gateway status=%u transcript_bytes=%u text_bytes=%u", command.status,
               (unsigned)strlen(command.transcript), (unsigned)strlen(command.text));
      last = command.status;
    }
    if (command.status == VOICE_DONE || command.status == VOICE_ERROR || command.status == VOICE_INTERRUPTED) break;
  }
  if (!why && esp_timer_get_time() - start >= COMMAND_DEADLINE_US) why = "Timed out";
done:
  provision_wipe(bearer, sizeof bearer);
  if (ws) {
    esp_transport_close(ws);
    esp_transport_destroy(ws);
  }
  if (ssl) esp_transport_destroy(ssl);
  set_busy(false);
  if (why) fail(why);
  ESP_LOGI(TAG, "VOICE_DONE transport=gateway status=%u updates=%u stops=%u elapsed_us=%lld voice_stack_free=%u internal_free=%u internal_min=%u psram_free=%u",
           last, updates, stops, (long long)(esp_timer_get_time() - start), (unsigned)uxTaskGetStackHighWaterMark(NULL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  memset(&command, 0, sizeof command);
  memset(gw_msg, 0, sizeof gw_msg);
  memset(&id, 0, sizeof id);
}

static bool mic_ready(void) {
  if (mic) return true;
  mic = bsp_audio_codec_microphone_init();
  ESP_LOGI(TAG, "MIC_INIT ok=%d", mic != NULL);
  return mic != NULL;
}

/* Record until the UI leaves LISTENING (release/auto-stop/cancel) or the buffer is full. */
static size_t record(unsigned *peak_milli, unsigned *mean_milli) {
  size_t samples = 0;
  unsigned chunks = 0, level_sum = 0;
  *peak_milli = *mean_milli = 0;
  if (!mic_ready()) return 0;
  esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 1, .sample_rate = VOICE_RATE};
  if (esp_codec_dev_open(mic, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGW(TAG, "MIC_OPEN failed"); return 0; }
  esp_codec_dev_set_in_gain(mic, MIC_GAIN_DB);
  int16_t *pcm = (int16_t *)(audio);
  size_t settle = 0;  /* codec pop window: read into the head of the buffer, then overwrite it */
  for (;;) {
    bool listening;
    xSemaphoreTake(lock, portMAX_DELAY);
    listening = state->helper.state == HV_LISTENING;
    xSemaphoreGive(lock);
    if (!listening || samples + CHUNK_SAMPLES > VOICE_MAX_SAMPLES) break;
    if (esp_codec_dev_read(mic, pcm + samples, CHUNK_SAMPLES * 2) != ESP_CODEC_DEV_OK) { ESP_LOGW(TAG, "MIC_READ failed"); break; }
    if (settle < VOICE_SETTLE_SAMPLES) { settle += CHUNK_SAMPLES; continue; }
    unsigned level = voice_level_milli(pcm + samples, CHUNK_SAMPLES);
    samples += CHUNK_SAMPLES;
    ++chunks;
    level_sum += level;
    if (level > *peak_milli) *peak_milli = level;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (state->helper.state == HV_LISTENING) state->helper.level_milli = level;
    xSemaphoreGive(lock);
  }
  esp_codec_dev_close(mic);
  *mean_milli = chunks ? level_sum / chunks : 0;
  return samples;
}

static void worker(void *unused) {
  (void)unused;
  ESP_LOGI(TAG, "HELPER_VOICE_READY buffer_bytes=%u psram=1 internal_free=%u", (unsigned)VOICE_BUFFER_BYTES,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  int64_t last_health=esp_timer_get_time();
  for (;;) {
    bool want_record, want_send, want_cancel, on_page;
    xSemaphoreTake(lock, portMAX_DELAY);
    want_record = state->helper.want_record;
    want_send = state->helper.want_send;
    want_cancel = state->helper.want_cancel;
    on_page = state->page == HELPER;
    state->helper.want_record = state->helper.want_cancel = false;
    xSemaphoreGive(lock);
    if (want_cancel && !want_record) ESP_LOGI(TAG, "VOICE_CANCEL");
    if (want_record && power_sleeping()) {  /* asleep (power_main.c): the mic and codec stay closed */
      xSemaphoreTake(lock, portMAX_DELAY);
      helper_leave(&state->helper);
      state->helper.want_send = false;
      xSemaphoreGive(lock);
      want_record = false;
      ESP_LOGI(TAG, "VOICE_REC refused=asleep");
    }
    if (want_record) {
      unsigned peak = 0, mean = 0;
      int64_t start = esp_timer_get_time();
      size_t samples = record(&peak, &mean);
      xSemaphoreTake(lock, portMAX_DELAY);
      want_send = state->helper.want_send;
      state->helper.want_send = false;
      bool cancelled = state->helper.want_cancel;
      state->helper.want_cancel = false;
      xSemaphoreGive(lock);
      /* Levels only: -60..0 dBFS mapped to 0..1000 (no audio leaves the board except to the bridge). */
      ESP_LOGI(TAG, "VOICE_REC samples=%u seconds_milli=%u peak_level_milli=%u mean_level_milli=%u elapsed_us=%lld send=%d cancelled=%d",
               (unsigned)samples, (unsigned)(samples * 1000 / VOICE_RATE), peak, mean, (long long)(esp_timer_get_time() - start),
               want_send, cancelled);
      if (want_send) {
        if (!voice_samples_ok(samples)) fail(samples ? "Recording too short" : "Microphone unavailable");
        else {
          voice_condition((int16_t *)(audio), samples);
          send_command_gateway(samples, "mic");
        }
      }
      continue;
    }
    if (want_send) {  /* release raced ahead of the record start: nothing captured */
      xSemaphoreTake(lock, portMAX_DELAY);
      state->helper.want_send = false;
      xSemaphoreGive(lock);
      fail("Recording too short");
    }
    int64_t now=esp_timer_get_time();
    if(now-last_health>=30LL*1000*1000){
      ESP_LOGI(TAG,"VOICE_HEALTH voice_stack_free=%u internal_free=%u",(unsigned)uxTaskGetStackHighWaterMark(NULL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));last_health=now;
    }
    vTaskDelay(pdMS_TO_TICKS(on_page ? 10 : 50));
  }
}

void helper_voice_start(home_ui *s, SemaphoreHandle_t mutex) {
  state = s;
  lock = mutex;
  audio = heap_caps_calloc(1, VOICE_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  assert(audio);
  assert(xTaskCreatePinnedToCore(worker, "helper_voice", 12288, NULL, 2, NULL, 0) == pdPASS);
}
