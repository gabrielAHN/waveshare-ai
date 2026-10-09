/* Waveshare AI pairing worker: on-device identity, mDNS discovery, pinned TLS enrollment, NVS record.
 * Runs on the home_live task (see home_live.c). Logs states, counts, short ids and the 6-digit
 * comparison code (shown on screen anyway); never the device token, Wi-Fi data or key timing. */
#include "esp_timer.h"
#include "home_pair.h"
#include "pair_tls.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "mdns.h"
#include "nvs.h"
#include "freertos/task.h"
#include <strings.h>
#define TAG "home_pair"
#define NS "home_wifi"            /* shared namespace for device configuration */
#define KEY_ID "device_id"
#define KEY_PAIR "pair"
#define ENROLL_POLL_MS 1500
#define ENROLL_DEADLINE_US (150LL * 1000 * 1000)
static home_ui *ui;
static SemaphoreHandle_t lock;
static bool mdns_ready;
/* The live worker can start before the Wi-Fi worker has run home_pair_boot (and never runs it when
 * NVS is unusable, e.g. right after a full-image flash erased it): until then there is no pairing. */
static bool pair_booted(void) { return __atomic_load_n(&lock, __ATOMIC_ACQUIRE) != NULL; }

/* ---------- NVS ---------- */
static bool nvs_rw(nvs_handle_t *h) { return nvs_open(NS, NVS_READWRITE, h) == ESP_OK; }
static int id_get(void *ctx, unsigned char out[32]) {
  (void)ctx;
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0; /* namespace absent == nothing stored */
  size_t n = 32;
  esp_err_t e = nvs_get_blob(h, KEY_ID, out, &n);
  nvs_close(h);
  if (e == ESP_ERR_NVS_NOT_FOUND) return 0;
  return e == ESP_OK && n == 32 ? 1 : -1;
}
static bool id_put(void *ctx, const unsigned char in[32]) {
  (void)ctx;
  nvs_handle_t h;
  if (!nvs_rw(&h)) return false;
  bool ok = nvs_set_blob(h, KEY_ID, in, 32) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
static const pair_id_store id_store = {id_get, id_put, NULL};
static void rng(void *out, size_t n) { esp_fill_random(out, n); }
static int record_get(pair_record *r) {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
  size_t n = sizeof *r;
  esp_err_t e = nvs_get_blob(h, KEY_PAIR, r, &n);
  nvs_close(h);
  if (e == ESP_ERR_NVS_NOT_FOUND) return 0;
  return e == ESP_OK && n == sizeof *r && pair_record_valid(r) ? 1 : -1;
}
static bool record_put(const pair_record *r) {
  nvs_handle_t h;
  pair_record check;
  if (!pair_record_valid(r) || !nvs_rw(&h)) return false;
  bool ok = nvs_set_blob(h, KEY_PAIR, r, sizeof *r) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok && record_get(&check) == 1 && !memcmp(&check, r, sizeof *r);
}
static bool key_erase(const char *key) {
  nvs_handle_t h;
  if (!nvs_rw(&h)) return false;
  esp_err_t e = nvs_erase_key(h, key);
  bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
/* ---------- UI mirror ---------- */
static void mirror(const pair_record *r, pair_step step, const char *note) {
  xSemaphoreTake(lock, portMAX_DELAY);
  pair_view *v = &ui->pair;
  v->state = r->state;
  v->step = step;
  snprintf(v->bridge, sizeof v->bridge, "%s", r->name);
  snprintf(v->base, sizeof v->base, "%s", r->base);
  memcpy(v->fp, r->fp, 32);
  snprintf(v->note, sizeof v->note, "%s", note ? note : "");
  if (step != PV_ENROLLING) { v->code_shown = false; v->code = 0; }
  xSemaphoreGive(lock);
}
static void note(pair_step step, const char *text) {
  xSemaphoreTake(lock, portMAX_DELAY);
  ui->pair.step = step;
  snprintf(ui->pair.note, sizeof ui->pair.note, "%s", text);
  if (step != PV_ENROLLING) { ui->pair.code_shown = false; ui->pair.code = 0; }
  xSemaphoreGive(lock);
}

void home_pair_boot(home_ui *state, SemaphoreHandle_t mutex) {
  ui = state;
  __atomic_store_n(&lock, mutex, __ATOMIC_RELEASE);
  pair_record r;
  int got = record_get(&r); /* NVS was initialised by the Wi-Fi worker (never erased on failure) */
  const char *origin = "present";
  if (got == 0) {
    pair_record_init(&r);
    origin = "absent";
  } else if (got < 0) {
    pair_record_init(&r); /* never overwrite an unreadable record automatically */
    origin = "read-error";
  }
  /* First boot: generate the device identity now. The RF subsystem is not running yet, so enable
   * the SAR-ADC entropy source for esp_fill_random() and disable it before Wi-Fi/ADC/I2S start. */
  unsigned char token[32];
  int id = id_get(NULL, token);
  const char *made = "present";
  if (id == 0) {
    bootloader_random_enable();
    pair_id_result g = pair_identity_ensure(&id_store, rng, token);
    bootloader_random_disable();
    id = g == PAIR_ID_ERROR ? -1 : 1;
    made = g == PAIR_ID_GENERATED ? "generated-first-boot" : "error";
  } else if (id < 0) made = "read-error";
  char sid[9] = "-";
  if (id == 1) pair_short_id(token, sid);
  provision_wipe(token, sizeof token);
  mirror(&r, PV_IDLE, NULL);
  ESP_LOGI(TAG, "PAIR_BOOT state=%u record=%s device_id=%s identity=%s source=%s", r.state, origin,
           sid, made, id == 1 ? "nvs" : "-");
}

bool home_pair_load(pair_record *r, unsigned char token[32]) {
  bool ok = record_get(r) == 1 && pair_record_live(r) && id_get(NULL, token) == 1 && pair_identity_valid(token);
  if (!ok) provision_wipe(token, 32);
  return ok;
}
void home_pair_live_status(int http) {
  if (!pair_booted()) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  pair_live_result(&ui->pair, http, esp_timer_get_time());
  xSemaphoreGive(lock);
}
void home_pair_live_forget(void) {
  if (!pair_booted()) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  pair_live_forget(&ui->pair);
  xSemaphoreGive(lock);
}

/* ---------- identity ---------- */
static bool identity(unsigned char token[32]) {
  pair_id_result r = pair_identity_ensure(&id_store, rng, token);
  char sid[9] = "-";
  if (r != PAIR_ID_ERROR) pair_short_id(token, sid);
  if (r == PAIR_ID_GENERATED) ESP_LOGI(TAG, "DEVICE_ID generated=1 short=%s source=esp_fill_random-rf-on", sid);
  if (r == PAIR_ID_ERROR) ESP_LOGW(TAG, "DEVICE_ID error=1");
  return r != PAIR_ID_ERROR;
}
static void rotate(const char *why) {
  unsigned char token[32];
  bool ok = pair_identity_rotate(&id_store, rng, token);
  char sid[9] = "-";
  if (ok) pair_short_id(token, sid);
  provision_wipe(token, sizeof token);
  ESP_LOGI(TAG, "DEVICE_ID rotated=%d short=%s reason=%s", ok, sid, why);
}

/* ---------- mDNS discovery ---------- */
static bool mdns_up(void) {
  if (mdns_ready) return true;
  unsigned char mac[32];
  char host[24];
  esp_fill_random(mac, 2);
  snprintf(host, sizeof host, "waveshare-ai-%02x%02x", mac[0], mac[1]);
  mdns_ready = mdns_init() == ESP_OK && mdns_hostname_set(host) == ESP_OK;
  ESP_LOGI(TAG, "MDNS_INIT ok=%d", mdns_ready);
  return mdns_ready;
}
static const char *txt(const mdns_result_t *r, const char *key) {
  for (size_t i = 0; i < r->txt_count; i++)
    if (r->txt[i].key && !strcasecmp(r->txt[i].key, key)) return r->txt[i].value;
  return NULL;
}
static void scan(void) {
  pair_list found = {0};
  int rejected = 0;
  if (!mdns_up()) { note(PV_ERROR, "mDNS unavailable"); return; }
  mdns_result_t *results = NULL;
  esp_err_t e = mdns_query_ptr("_waveshare-ai", "_tcp", 3000, PAIR_LIST_MAX, &results);
  for (mdns_result_t *r = results; e == ESP_OK && r; r = r->next) {
    char ip[16] = "";
    for (mdns_ip_addr_t *a = r->addr; a && !ip[0]; a = a->next)
      if (a->addr.type == ESP_IPADDR_TYPE_V4) snprintf(ip, sizeof ip, IPSTR, IP2STR(&a->addr.u_addr.ip4));
    if (!ip[0] && r->hostname) {
      esp_ip4_addr_t a4 = {0};
      if (mdns_query_a(r->hostname, 1500, &a4) == ESP_OK) snprintf(ip, sizeof ip, IPSTR, IP2STR(&a4));
    }
    const char *name = txt(r, "name");
    if (pair_list_add(&found, name && *name ? name : r->instance_name, ip, r->port, txt(r, "fp")) < 0) ++rejected;
  }
  if (results) mdns_query_results_free(results);
  xSemaphoreTake(lock, portMAX_DELAY);
  ui->pair.list = found;
  ui->pair.step = PV_LIST;
  snprintf(ui->pair.note, sizeof ui->pair.note, "%s", found.count ? "" : "No bridge found");
  xSemaphoreGive(lock);
  ESP_LOGI(TAG, "PAIR_SCAN code=%d found=%d rejected_no_pin_or_addr=%d", e, found.count, rejected);
  for (int i = 0; i < found.count; i++) {
    char fp[24];
    pair_fp_short(found.items[i].fp, fp);
    ESP_LOGI(TAG, "PAIR_SCAN_ITEM index=%d base=%s fp=%s", i, found.items[i].base, fp);
  }
}

/* ---------- enrollment ---------- */
typedef struct {unsigned char data[64]; size_t length; bool overflow;} small_body;
static esp_err_t on_body(esp_http_client_event_t *event) {
  small_body *b = event->user_data;
  if (event->event_id == HTTP_EVENT_ON_DATA) {
    if (event->data_len < 0 || (size_t)event->data_len > sizeof b->data - b->length) { b->overflow = true; return ESP_OK; }
    memcpy(b->data + b->length, event->data, (size_t)event->data_len);
    b->length += (size_t)event->data_len;
  }
  return ESP_OK;
}
static bool cancelled(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool c = ui->pair.want_cancel;
  ui->pair.want_cancel = false;
  xSemaphoreGive(lock);
  return c;
}
static void enroll(pair_bridge target, bool tofu) {
  unsigned char token[32], body[PAIR_ENROLL_BODY];
  char url[PAIR_URL_MAX + 16];
  pair_record current;
  if (record_get(&current) != 1) pair_record_init(&current);
  if (!identity(token)) { note(PV_ERROR, "Device identity unavailable"); return; }
  pair_pin_ctx *pin = pair_tls_ctx(PAIR_TLS_ENROLL);
  if (tofu) pair_pin_capture(pin);
  else pair_pin_require(pin, target.fp);
  char sid[9];
  pair_short_id(token, sid);
  pair_enroll_body(token, "Waveshare AI", body);
  pair_url(url, sizeof url, target.base, "/v1/enroll");
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(ui->pair.bridge, sizeof ui->pair.bridge, "%s", target.name);
  ui->pair.state = PAIR_ENROLLING;
  xSemaphoreGive(lock);
  small_body reply = {0};
  esp_http_client_config_t cfg = {.url = url, .crt_bundle_attach = pair_tls_attach_enroll, .timeout_ms = 6000,
                                  .disable_auto_redirect = true, .keep_alive_enable = true, .buffer_size = 1024,
                                  .buffer_size_tx = 512, .event_handler = on_body, .user_data = &reply,
                                  .method = HTTP_METHOD_POST};
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) { provision_wipe(token, sizeof token); note(PV_ERROR, "Out of memory"); return; }
  esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
  esp_http_client_set_post_field(client, (const char *)body, sizeof body);
  ESP_LOGI(TAG, "ENROLL_START base=%s mode=%s device_id=%s", target.base, tofu ? "tofu-manual" : "pinned-mdns", sid);
  int64_t start = esp_timer_get_time();
  bool sent = false, shown = false;
  const char *outcome = "timeout";
  int last = -1;
  while (esp_timer_get_time() - start < ENROLL_DEADLINE_US) {
    if (cancelled()) { outcome = "cancelled"; break; }
    memset(&reply, 0, sizeof reply);
    esp_err_t e = esp_http_client_perform(client);
    int status = e == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    if (e == ESP_OK) sent = true; /* token left the device inside a pinned (or captured) TLS session */
    if (!pin->seen || !pin->matched) status = 0;
    if (status != last) ESP_LOGI(TAG, "ENROLL_HTTP status=%d pin_seen=%d pin_matched=%d", status, pin->seen, pin->matched);
    last = status;
    if (tofu && pin->seen && pin->matched && !pair_fp_set(target.fp)) memcpy(target.fp, pin->observed, 32);
    if (!shown && pin->matched && (status == 202 || status == 200)) {
      uint32_t code = pair_code(target.fp, token);
      char text[8], fp[24];
      pair_code_text(code, text);
      pair_fp_short(target.fp, fp);
      xSemaphoreTake(lock, portMAX_DELAY);
      ui->pair.code = code;
      ui->pair.code_shown = true;
      xSemaphoreGive(lock);
      ESP_LOGI(TAG, "ENROLL_CODE code=%s fp=%s note=compare-with-host", text, fp);
      shown = true;
    }
    pair_enroll_result r = pair_enroll_status(status);
    if (r == PAIR_ENROLL_ACCEPTED) { outcome = "accepted"; break; }
    if (r == PAIR_ENROLL_DENIED) { outcome = "denied"; break; }
    if (r == PAIR_ENROLL_EXPIRED) { outcome = "expired"; break; }
    if (r == PAIR_ENROLL_CLOSED) { outcome = "closed"; break; }
    if (r == PAIR_ENROLL_UNREACHABLE && pin->seen && !pin->matched) { outcome = "pin-mismatch"; break; }
    vTaskDelay(pdMS_TO_TICKS(r == PAIR_ENROLL_RETRY ? 3000 : ENROLL_POLL_MS));
    if (!pin->matched) {
      /* An unanswered connection is retried with a fresh TLS session (same pin). */
      esp_http_client_close(client);
    }
  }
  esp_http_client_cleanup(client);
  provision_wipe(body, sizeof body);
  ESP_LOGI(TAG, "ENROLL_RESULT outcome=%s elapsed_ms=%lld", outcome, (long long)((esp_timer_get_time() - start) / 1000));
  if (!strcmp(outcome, "accepted")) {
    pair_record r;
    pair_record_init(&r);
    r.state = PAIR_ENROLLED_UNPAIRED;
    r.flags = tofu ? PAIR_FLAG_TOFU : 0;
    snprintf(r.base, sizeof r.base, "%s", target.base);
    snprintf(r.name, sizeof r.name, "%s", target.name);
    memcpy(r.fp, target.fp, 32);
    bool ok = record_put(&r);
    ESP_LOGI(TAG, "PAIR_SAVED ok=%d state=%u", ok, r.state);
    mirror(ok ? &r : &current, ok ? PV_DONE : PV_ERROR, ok ? "Connected. Live feed starting" : "Save failed - retry");
  } else {
    if (sent) rotate(outcome); /* the token reached a peer that was not confirmed: never reuse it */
    const char *msg = !strcmp(outcome, "denied") ? "Declined on the host" : !strcmp(outcome, "expired") ? "Code expired - try again"
                    : !strcmp(outcome, "closed") ? "Run waveshare-bridge enroll" : !strcmp(outcome, "pin-mismatch") ? "Certificate mismatch!"
                    : !strcmp(outcome, "cancelled") ? "Cancelled" : "Bridge unreachable";
    mirror(&current, strcmp(outcome, "cancelled") ? PV_ERROR : PV_IDLE, msg);
  }
  provision_wipe(token, sizeof token);
}
static void forget(void) {
  bool ok = key_erase(KEY_PAIR);
  rotate("forget");
  pair_record r;
  pair_record_init(&r);
  mirror(&r, PV_IDLE, ok ? "Bridge forgotten" : "Forget failed - retry");
  ESP_LOGI(TAG, "PAIR_FORGET ok=%d", ok);
}

bool home_pair_service(bool connected) {
  bool scan_req, forget_req, enroll_req;
  int index;
  pair_bridge target = {0};
  if (!pair_booted()) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  pair_view *v = &ui->pair;
  scan_req = v->want_scan;
  forget_req = v->want_forget;
  enroll_req = v->enroll_requested;
  index = v->want_enroll;
  v->want_scan = v->want_forget = v->enroll_requested = false;
  if (enroll_req && index >= 0 && index < v->list.count) target = v->list.items[index];
  else enroll_req = false;
  if (!scan_req && !enroll_req) v->want_cancel = false;
  xSemaphoreGive(lock);
  if (!scan_req && !forget_req && !enroll_req) return false;
  if (forget_req) forget();
  if (!connected) { if (scan_req || enroll_req) note(PV_ERROR, "Connect Wi-Fi first"); return true; }
  if (scan_req) scan();
  if (enroll_req) enroll(target, false);
  return true;
}
