#pragma once
/* Board side of the Hermes Gadget SDK protocol (hermes-gadget.v1), used by the Ask voice turn when
 * the board talks through the bridge gadget front to its bot's Hermes gateway profile. Pure C, host-tested (tests/host/gadget_wire_test.c); no IDF,
 * network or storage here.
 *
 * Identity: the SDK device key is HMAC-SHA256(board token, "waveshare-ai-gadget-key-v1"), so the
 * board's existing enrolled identity (and its phone sign-in) is the only credential; there is no
 * second pairing. device_id = "hg-" + hex(sha256(key))[0:16]; auth = base64(HMAC-SHA256(key,
 * "hermes-gadget/v1|" + device_id + "|" + nonce)). Request ids = the 16-hex id suffix + a 16-hex
 * counter that only ever grows (the hub refuses any counter it has already seen).
 *
 * Replies arrive as transcript, status, reply.delta, reply and turn.end messages. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256_tiny.h"
#include "voice_wire.h"

#define GW_PATH "/gadget"
#define GW_SUBPROTOCOL "hermes-gadget.v1"
#define GW_KEY_CONTEXT "waveshare-ai-gadget-key-v1"
#define GW_AUTH_CONTEXT "hermes-gadget/v1|"

typedef struct {
  unsigned char key[32];
  char device_id[20]; /* "hg-" + 16 hex + NUL */
} gw_identity;

static inline void gw_hmac_sha256(const unsigned char *key, size_t key_len, const void *msg, size_t msg_len,
                                  unsigned char out[32]) {
  unsigned char block[64] = {0}, inner[32];
  if (key_len > 64) {
    sha256_tiny(key, key_len, block);
  } else {
    memcpy(block, key, key_len);
  }
  unsigned char pad[64];
  sha256_ctx c;
  for (int i = 0; i < 64; i++) pad[i] = block[i] ^ 0x36;
  sha256_init(&c);
  sha256_update(&c, pad, 64);
  sha256_update(&c, msg, msg_len);
  sha256_final(&c, inner);
  for (int i = 0; i < 64; i++) pad[i] = block[i] ^ 0x5c;
  sha256_init(&c);
  sha256_update(&c, pad, 64);
  sha256_update(&c, inner, 32);
  sha256_final(&c, out);
  memset(block, 0, sizeof block);
  memset(pad, 0, sizeof pad);
  memset(inner, 0, sizeof inner);
}

static inline void gw_hex(const unsigned char *in, size_t n, char *out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 15];
  }
  out[2 * n] = 0;
}

static inline void gw_identity_from_token(const unsigned char token[32], gw_identity *id) {
  unsigned char digest[32];
  gw_hmac_sha256(token, 32, GW_KEY_CONTEXT, strlen(GW_KEY_CONTEXT), id->key);
  sha256_tiny(id->key, 32, digest);
  memcpy(id->device_id, "hg-", 3);
  gw_hex(digest, 8, id->device_id + 3);
}

/* Standard base64 with padding. Returns the encoded length, 0 when `cap` is too small. */
static inline size_t gw_b64(const unsigned char *in, size_t n, char *out, size_t cap) {
  static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t need = 4 * ((n + 2) / 3);
  if (cap < need + 1) return 0;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = a[(v >> 18) & 63];
    out[o++] = a[(v >> 12) & 63];
    out[o++] = i + 1 < n ? a[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? a[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}

static inline bool gw_auth_mac(const gw_identity *id, const char *nonce, char *out, size_t cap) {
  char msg[160];
  int n = snprintf(msg, sizeof msg, "%s%s|%s", GW_AUTH_CONTEXT, id->device_id, nonce ? nonce : "");
  if (!nonce || n <= 0 || (size_t)n >= sizeof msg) return false;
  unsigned char mac[32];
  gw_hmac_sha256(id->key, 32, msg, (size_t)n, mac);
  return gw_b64(mac, 32, out, cap) == 44;
}

/* "Authorization: Bearer <64 hex>\r\n" for the WebSocket upgrade. Wipe `out` after use. */
static inline bool gw_bearer_header(const unsigned char token[32], char *out, size_t cap) {
  if (cap < 22 + 64 + 3) return false;
  memcpy(out, "Authorization: Bearer ", 22);
  gw_hex(token, 32, out + 22);
  memcpy(out + 86, "\r\n", 3);
  return true;
}

static inline void gw_request_id(const gw_identity *id, uint64_t counter, char out[33]) {
  memcpy(out, id->device_id + 3, 16);
  for (int i = 0; i < 16; i++) out[16 + i] = "0123456789abcdef"[(counter >> (60 - 4 * i)) & 15];
  out[32] = 0;
}

/* Binary audio frame header: channel 1 (audio), stream 1, u16 little-endian sequence. */
static inline void gw_audio_header(unsigned seq, unsigned char out[4]) {
  out[0] = 1;
  out[1] = 1;
  out[2] = (unsigned char)(seq & 0xff);
  out[3] = (unsigned char)((seq >> 8) & 0xff);
}

static inline bool gw_plain(const char *s, size_t max) {
  if (!s) return false;
  size_t n = strlen(s);
  if (!n || n > max) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

static inline bool gw_hex32(const char *s) {
  if (!s || strlen(s) != 32) return false;
  for (int i = 0; i < 32; i++) {
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  }
  return true;
}

static inline bool gw_fits(int n, size_t cap) { return n > 0 && (size_t)n < cap; }

static inline bool gw_auth_key_json(const gw_identity *id, char *out, size_t cap) {
  char b64[48];
  if (gw_b64(id->key, 32, b64, sizeof b64) != 44) return false;
  bool ok = gw_fits(snprintf(out, cap, "{\"type\":\"auth\",\"key\":\"%s\"}", b64), cap);
  memset(b64, 0, sizeof b64);
  return ok;
}

static inline bool gw_auth_mac_json(const gw_identity *id, const char *nonce, char *out, size_t cap) {
  char mac[48];
  if (!gw_auth_mac(id, nonce, mac, sizeof mac)) return false;
  return gw_fits(snprintf(out, cap, "{\"type\":\"auth\",\"mac\":\"%s\"}", mac), cap);
}

/* ---- Hermes gateway (stock Gadget SDK platform per bot profile, via the bridge's gadget front) ---- */

static inline bool gw_hello_gateway_json(const gw_identity *id, char *out, size_t cap) {
  return gw_fits(snprintf(out, cap,
                          "{\"type\":\"hello\",\"proto\":1,\"device_id\":\"%s\",\"name\":\"Waveshare AI\","
                          "\"board\":\"waveshare-esp32s3-amoled-1.8\",\"firmware\":\"waveshare-ai\","
                          "\"caps\":{\"display\":{\"width\":368,\"height\":448,\"color\":true,\"charset\":\"ascii\","
                          "\"text_cols\":34,\"text_rows\":14},\"mic\":{\"rate\":16000,\"format\":\"pcm16\"},"
                          "\"inputs\":[\"talk\",\"cancel\"],\"talk_mode\":\"hold\"}}",
                          id->device_id), cap);
}

/* The bridge front routes /gadget/<bot> to that bot profile's gadget platform in the Hermes gateway. */
static inline bool gw_gateway_path(const char *bot, char *out, size_t cap) {
  if (!gw_plain(bot, 32)) return false;
  return gw_fits(snprintf(out, cap, GW_PATH "/%s", bot), cap);
}

static inline bool gw_gateway_audio_start_json(const char *rid, char *out, size_t cap) {
  if (!gw_hex32(rid)) return false;
  return gw_fits(snprintf(out, cap,
                          "{\"type\":\"audio.start\",\"id\":\"%s\",\"stream\":1,\"rate\":16000,\"format\":\"pcm16\","
                          "\"mode\":\"hold\"}", rid), cap);
}

static inline bool gw_gateway_audio_end_json(const char *rid, unsigned duration_ms, char *out, size_t cap) {
  if (!gw_hex32(rid)) return false;
  return gw_fits(snprintf(out, cap, "{\"type\":\"audio.end\",\"id\":\"%s\",\"stream\":1,\"duration_ms\":%u}", rid,
                          duration_ms), cap);
}

static inline bool gw_prompt_reply_json(const char *prompt_id, bool yes, char *out, size_t cap) {
  if (!gw_plain(prompt_id, 32)) return false;
  return gw_fits(snprintf(out, cap, "{\"type\":\"prompt.reply\",\"id\":\"%s\",\"answer\":\"%s\"}", prompt_id,
                          yes ? "yes" : "no"), cap);
}

/* Answer a heartbeat ping, echoing its numeric ts when present. */
static inline bool gw_pong_json(const char *ping, size_t n, char *out, size_t cap);

/* Host of the pinned bridge base URL: the hub listens on the same host with the same certificate. */
static inline bool gw_host_from_base(const char *base, char *out, size_t cap) {
  if (!base || strncmp(base, "https://", 8)) return false;
  const char *host = base + 8, *end = host;
  if (*host == '[') {
    end = strchr(host, ']');
    if (!end) return false;
    end++;
  } else {
    while (*end && *end != ':' && *end != '/') end++;
  }
  size_t n = (size_t)(end - host);
  if (!n || n > 96) return false;
  return gw_fits(snprintf(out, cap, "%.*s", (int)n, host), cap);
}


/* ---- minimal JSON reader: top-level members of one object, strict syntax ---------------------- */

static inline const char *gw_ws(const char *p, const char *e) {
  while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

/* Skip one JSON string starting at '"'; returns the char after the closing quote or NULL. */
static inline const char *gw_skip_string(const char *p, const char *e) {
  if (p >= e || *p != '"') return NULL;
  for (p++; p < e; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '"') return p + 1;
    if (c < 0x20) return NULL;
    if (c == '\\') {
      if (++p >= e) return NULL;
      if (*p == 'u') {
        for (int i = 0; i < 4; i++) {
          if (++p >= e) return NULL;
          char h = *p;
          if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return NULL;
        }
      } else if (!strchr("\"\\/bfnrt", *p)) {
        return NULL;
      }
    }
  }
  return NULL;
}

static inline const char *gw_skip_value(const char *p, const char *e, int depth) {
  p = gw_ws(p, e);
  if (p >= e || depth > 8) return NULL;
  if (*p == '"') return gw_skip_string(p, e);
  if (*p == '{' || *p == '[') {
    char close = *p == '{' ? '}' : ']';
    bool object = *p == '{';
    p = gw_ws(p + 1, e);
    if (p < e && *p == close) return p + 1;
    for (;;) {
      if (object) {
        p = gw_skip_string(gw_ws(p, e), e);
        if (!p) return NULL;
        p = gw_ws(p, e);
        if (p >= e || *p != ':') return NULL;
        p++;
      }
      p = gw_skip_value(p, e, depth + 1);
      if (!p) return NULL;
      p = gw_ws(p, e);
      if (p >= e) return NULL;
      if (*p == close) return p + 1;
      if (*p != ',') return NULL;
      p++;
    }
  }
  const char *s = p;
  while (p < e && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' ||
                   (*p >= 'a' && *p <= 'z'))) p++;
  return p > s ? p : NULL;
}

/* Locate top-level member `key`: [*vs, *ve) is its raw value. False on syntax error or absence. */
static inline bool gw_json_find(const char *j, size_t n, const char *key, const char **vs, const char **ve) {
  const char *p = j, *e = j + n, *found_s = NULL, *found_e = NULL;
  if (!j || !key) return false;
  p = gw_ws(p, e);
  if (p >= e || *p != '{') return false;
  p = gw_ws(p + 1, e);
  if (p < e && *p == '}') return false; /* empty object: no member to find */
  size_t klen = strlen(key);
  for (;;) {
    const char *ks = gw_ws(p, e), *kend = gw_skip_string(ks, e);
    if (!kend) return false;
    p = gw_ws(kend, e);
    if (p >= e || *p != ':') return false;
    const char *v = gw_ws(p + 1, e), *vend = gw_skip_value(v, e, 0);
    if (!vend) return false;
    if ((size_t)(kend - ks - 2) == klen && !memcmp(ks + 1, key, klen)) {
      found_s = v;
      found_e = vend;
    }
    p = gw_ws(vend, e);
    if (p >= e) return false;
    if (*p == '}') break;
    if (*p != ',') return false;
    p++;
  }
  if (gw_ws(p + 1, e) != e || !found_s) return false;
  *vs = found_s;
  *ve = found_e;
  return true;
}

static inline size_t gw_utf8(uint32_t cp, char *o) {
  if (cp < 0x80) {
    o[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    o[0] = (char)(0xc0 | (cp >> 6));
    o[1] = (char)(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp < 0x10000) {
    o[0] = (char)(0xe0 | (cp >> 12));
    o[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    o[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  o[0] = (char)(0xf0 | (cp >> 18));
  o[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  o[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
  o[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

static inline uint32_t gw_hex4(const char *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    char h = p[i];
    v = v * 16 + (uint32_t)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
  }
  return v;
}

/* Decode the JSON string [s, e) (already syntax-checked) into out; false when it does not fit. */
/* 1 = the whole string fit, 2 = cut short at `cap` (only with trunc), 0 = invalid or too long. */
static inline int gw_decode(const char *s, const char *e, char *out, size_t cap, bool trunc) {
  if (e - s < 2 || *s != '"' || !cap) return 0;
  size_t o = 0;
  for (const char *p = s + 1; p < e - 1; p++) {
    char tmp[4];
    size_t len = 1;
    if (*p != '\\') {
      tmp[0] = *p;
    } else {
      p++;
      switch (*p) {
        case 'n': tmp[0] = '\n'; break;
        case 't': tmp[0] = '\t'; break;
        case 'r': tmp[0] = '\r'; break;
        case 'b': tmp[0] = '\b'; break;
        case 'f': tmp[0] = '\f'; break;
        case 'u': {
          uint32_t cp = gw_hex4(p + 1);
          p += 4;
          if (cp >= 0xd800 && cp < 0xdc00 && p + 6 < e && p[1] == '\\' && p[2] == 'u') {
            uint32_t lo = gw_hex4(p + 3);
            if (lo >= 0xdc00 && lo < 0xe000) {
              cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
              p += 6;
            }
          }
          if (!cp) return 0; /* embedded NUL */
          len = gw_utf8(cp, tmp);
          break;
        }
        default: tmp[0] = *p; break;
      }
    }
    if (o + len >= cap) {
      if (!trunc) return 0;
      out[o] = 0;
      return 2;
    }
    memcpy(out + o, tmp, len);
    o += len;
  }
  out[o] = 0;
  return 1;
}

static inline bool gw_decode_string(const char *s, const char *e, char *out, size_t cap) {
  return gw_decode(s, e, out, cap, false) == 1;
}

static inline bool gw_json_string(const char *j, size_t n, const char *key, char *out, size_t cap) {
  const char *s, *e;
  return gw_json_find(j, n, key, &s, &e) && *s == '"' && gw_decode_string(s, e, out, cap);
}

/* Display text: a string longer than `cap` is cut at a character boundary and ends in "...". */
static inline bool gw_json_text(const char *j, size_t n, const char *key, char *out, size_t cap) {
  const char *s, *e;
  if (!gw_json_find(j, n, key, &s, &e) || *s != '"') return false;
  if (gw_decode(s, e, out, cap, false) == 1) return true;
  if (cap < 8 || gw_decode(s, e, out, cap - 3, true) != 2) return false;
  strcat(out, "...");
  return true;
}

static inline bool gw_json_bool(const char *j, size_t n, const char *key, bool *out) {
  const char *s, *e;
  if (!gw_json_find(j, n, key, &s, &e)) return false;
  if (e - s == 4 && !memcmp(s, "true", 4)) {
    *out = true;
    return true;
  }
  if (e - s == 5 && !memcmp(s, "false", 5)) {
    *out = false;
    return true;
  }
  return false;
}

/* Plain non-negative integer only (no sign, fraction or exponent). */
static inline bool gw_type_is(const char *j, size_t n, const char *type) {
  char t[24];
  return gw_json_string(j, n, "type", t, sizeof t) && !strcmp(t, type);
}

static inline bool gw_pong_json(const char *ping, size_t n, char *out, size_t cap) {
  const char *s, *e;
  if (gw_json_find(ping, n, "ts", &s, &e) && e > s && e - s <= 20) {
    for (const char *p = s; p < e; p++) {
      if (*p < '0' || *p > '9') return gw_fits(snprintf(out, cap, "{\"type\":\"pong\"}"), cap);
    }
    return gw_fits(snprintf(out, cap, "{\"type\":\"pong\",\"ts\":%.*s}", (int)(e - s), s), cap);
  }
  return gw_fits(snprintf(out, cap, "{\"type\":\"pong\"}"), cap);
}

/* turn.end -> the board's command state. A failed turn shows Hermes's own message when a full
 * `reply` arrived in this turn (e.g. a provider/quota error Hermes sent to the chat), never a
 * partial reply.delta; otherwise a generic line. */
static inline void gw_turn_end(const char *outcome, bool got_reply, voice_command *c) {
  if (outcome && !strcmp(outcome, "success")) {
    c->status = VOICE_DONE;
    if (!c->text[0]) snprintf(c->text, sizeof c->text, "Done.");
  } else if (outcome && !strcmp(outcome, "cancelled")) {
    c->status = VOICE_INTERRUPTED;
  } else {
    c->status = VOICE_ERROR;
    if (!got_reply || !c->text[0]) snprintf(c->text, sizeof c->text, "Hermes could not finish");
  }
}

/* Keepalive: the board pings the gateway itself every GW_KEEPALIVE_US while it waits. The hub closes a
 * device after 3 x heartbeat_s with no inbound frame, and when the host stalls (swap) it sends no pings
 * to answer; after the stall it reads the buffered pings before its heartbeat check, so the turn lives. */
#define GW_KEEPALIVE_US (5LL * 1000 * 1000)
static inline bool gw_keepalive_due(int64_t now_us, int64_t last_tx_us) { return now_us - last_tx_us >= GW_KEEPALIVE_US; }
