#pragma once
/* Phone sign-in (OAuth 2.0 device authorization grant, via the bridge), one view per provider (plugins.h):
 * home_ui.phone = the Hermes provider (Ask), home_ui.phone_ha = the Home Assistant provider (Sensor).
 * Pure C, host-tested (tests/host/phone_view_test.c, tests/host/provider_signin_test.c, tests/host/settings_ui_test.c).
 * No IDF, network or storage: the home_live worker talks to the bridge (header X-Provider: hermes |
 * home_assistant) and mirrors each result into its phone_view under the UI lock.
 *
 * WPH1 (little-endian, PHONE_FRAME_SIZE = 228 bytes; mirrors bridge/waveshare_bridge/phone_pair.py):
 *   'WPH1' u8 version(1) u8 state u8 flags u8 0 u16 expires_in_s u16 0
 *   char user_code[16] char name[33] char uri[163]   (NUL-terminated printable ASCII)
 *   u32 crc32(all previous bytes)
 * state (per provider): 0 none, 1 pending (QR shown), 2 authorized (name; signed in at that provider's
 * gateway by a member of ITS groups), 3 denied, 4 expired, 5 refused (signed in, but not in that
 * provider's groups), 6 error. flags: 1 = this board must sign in before using the provider (gate
 * enforced), 8 = shared (this provider's sign-in gateway is shared with another provider on the bridge: one QR
 * signs in to both, signing out signs out both). A provider the bridge does not have answers HTTP 404:
 * phone_absent() marks the view "not set up". The board never sees an OAuth token: only the public
 * user code, the verification URL it renders as a QR, and a display name.
 * Unknown status (no frame yet) never disables anything: the bridge refuses server-side. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "provision.h"
#include "../components/qrcodegen/qrcodegen.h"

#define PHONE_CODE_LEN 16
#define PHONE_NAME_LEN 33
#define PHONE_URI_LEN 163
#define PHONE_FRAME_SIZE (12 + PHONE_CODE_LEN + PHONE_NAME_LEN + PHONE_URI_LEN + 4)
#define PHONE_FLAG_REQUIRED 1u
#define PHONE_FLAG_SHARED 8u
#define PHONE_FLAGS_KNOWN 9u
#define PHONE_QR_MAX_VERSION 10
#define PHONE_QR_BUF qrcodegen_BUFFER_LEN_FOR_VERSION(PHONE_QR_MAX_VERSION)
#define PHONE_QR_QUIET 4               /* modules of white quiet zone around the symbol (spec: 4) */
#define PHONE_QR_BOX 270               /* max card edge in px (symbol + 4-module quiet zone) */
#define PHONE_QR_CARD_Y 40             /* card top: below the title line, inside the rounded safe area */
#define PHONE_QR_ROW_Y 322             /* code + Back row under the card (card ends <= 310) */
#define PHONE_POLL_US (3LL * 1000 * 1000)          /* while the QR is on screen */
#define PHONE_IDLE_POLL_US (60LL * 1000 * 1000)    /* Settings / Connect visible */

typedef enum { PH_NONE, PH_PENDING, PH_AUTHORIZED, PH_DENIED, PH_EXPIRED, PH_REFUSED, PH_ERROR } phone_state;
typedef struct {
  bool valid;
  uint8_t state, flags;
  uint16_t expires_in;
  int64_t received_us;
  char user_code[PHONE_CODE_LEN], name[PHONE_NAME_LEN], uri[PHONE_URI_LEN];
} phone_status;
typedef struct {
  phone_status st;
  uint8_t qr[PHONE_QR_BUF];
  bool qr_ok;
  bool showing;          /* the Settings screen shows the phone sign-in QR */
  bool want_start, want_forget, confirm_signout, refresh;
  bool absent;           /* the bridge answered 404: this provider is not set up there (cleared by any frame) */
  int64_t poll_us;       /* last status request (0 = never) */
  int http;              /* last bridge status for start/status (0 none) */
  char note[32];         /* short local error ("Portal unreachable") */
} phone_view;

static inline uint32_t phone_u32(const unsigned char *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline bool phone_text(char *out, const unsigned char *p, size_t n) {
  if (p[n - 1]) return false;
  size_t len = 0;
  while (len < n && p[len]) len++;
  for (size_t i = 0; i < len; i++)
    if (p[i] < 32 || p[i] > 126) return false;
  memcpy(out, p, len);
  memset(out + len, 0, n - len);
  return true;
}
/* Fail-closed: `out` is written only when the whole frame validates. */
static inline bool phone_decode(phone_status *out, const unsigned char *f, size_t n, int64_t now_us) {
  if (!out || !f || n != PHONE_FRAME_SIZE) return false;
  if (phone_u32(f + n - 4) != provision_crc(f, n - 4)) return false;
  if (memcmp(f, "WPH1", 4) || f[4] != 1 || f[5] > PH_ERROR || (f[6] & ~PHONE_FLAGS_KNOWN) || f[7] || f[10] || f[11]) return false;
  phone_status s;
  memset(&s, 0, sizeof s);
  s.state = f[5];
  s.flags = f[6];
  s.expires_in = (uint16_t)(f[8] | (f[9] << 8));
  if (!phone_text(s.user_code, f + 12, PHONE_CODE_LEN) || !phone_text(s.name, f + 12 + PHONE_CODE_LEN, PHONE_NAME_LEN) ||
      !phone_text(s.uri, f + 12 + PHONE_CODE_LEN + PHONE_NAME_LEN, PHONE_URI_LEN))
    return false;
  if (s.uri[0] && strncmp(s.uri, "https://", 8)) return false;          /* only an https portal URL is ever drawn */
  if (s.state == PH_PENDING && (!s.uri[0] || !s.user_code[0] || !s.expires_in)) return false;
  s.valid = true;
  s.received_us = now_us;
  *out = s;
  return true;
}
/* Encode the verification URL (byte mode, ECC M boosted, version <= 10). */
static inline bool phone_qr_encode(phone_view *v, const char *uri) {
  uint8_t temp[PHONE_QR_BUF];
  v->qr_ok = uri && uri[0] && strlen(uri) < PHONE_URI_LEN &&
             qrcodegen_encodeText(uri, temp, v->qr, qrcodegen_Ecc_MEDIUM, 1, PHONE_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
  if (!v->qr_ok) memset(v->qr, 0, sizeof v->qr);
  return v->qr_ok;
}
static inline int phone_qr_size(const phone_view *v) { return v->qr_ok ? qrcodegen_getSize(v->qr) : 0; }
static inline bool phone_qr_module(const phone_view *v, int x, int y) { return v->qr_ok && qrcodegen_getModule(v->qr, x, y); }
static inline int phone_qr_scale(int size) {
  int s = size > 0 ? PHONE_QR_BOX / (size + 2 * PHONE_QR_QUIET) : 0;
  return s > 8 ? 8 : s;
}
/* Top-left pixel of module (0,0) and module size; the card spans the quiet zone around it. */
static inline void phone_qr_geometry(const phone_view *v, int *x, int *y, int *scale) {
  int size = phone_qr_size(v), s = phone_qr_scale(size), card = (size + 2 * PHONE_QR_QUIET) * s;
  *scale = s;
  *x = (368 - card) / 2 + PHONE_QR_QUIET * s;
  *y = PHONE_QR_CARD_Y + PHONE_QR_QUIET * s;
}
/* May this board use Hermes? Gate only on an explicit bridge answer. */
static inline bool phone_gate(const phone_view *v) {
  return v->st.valid && (v->st.flags & PHONE_FLAG_REQUIRED) && v->st.state != PH_AUTHORIZED;
}
static inline bool phone_signed_in(const phone_view *v) { return v->st.valid && v->st.state == PH_AUTHORIZED; }
/* This provider's sign-in is shared with another provider on the bridge (flag 8). */
static inline bool phone_shared(const phone_view *v) { return v->st.valid && (v->st.flags & PHONE_FLAG_SHARED); }
/* The bridge answered 404 for this provider (start, forget or status): it is not set up there. Drops
 * the last status and closes its QR / sign-out question; the next valid frame clears it. */
static inline void phone_absent(phone_view *v) {
  memset(&v->st, 0, sizeof v->st);
  v->absent = true;
  v->qr_ok = v->showing = v->confirm_signout = false;
  memset(v->qr, 0, sizeof v->qr);
}
/* Seconds left on the current code (counted down locally since the frame arrived). */
static inline int phone_left(const phone_view *v, int64_t now_us) {
  if (!v->st.valid || v->st.state != PH_PENDING) return 0;
  int64_t elapsed = now_us > v->st.received_us ? (now_us - v->st.received_us) / 1000000 : 0;
  int left = (int)v->st.expires_in - (int)elapsed;
  return left > 0 ? left : 0;
}
static inline phone_state phone_display(const phone_view *v, int64_t now_us) {
  if (!v->st.valid) return PH_NONE;
  if (v->st.state == PH_PENDING && !phone_left(v, now_us)) return PH_EXPIRED;
  return (phone_state)v->st.state;
}
static inline void phone_countdown(const phone_view *v, int64_t now_us, char *out, size_t cap) {
  if (!out || !cap) return;
  int left = phone_left(v, now_us);
  snprintf(out, cap, "%d:%02d", left / 60, left % 60);
}
/* Poll the bridge every 3 s while the QR is shown, every 60 s while Settings/Connect is visible
 * (and at once the first time or on request), never otherwise. */
static inline bool phone_poll_due(const phone_view *v, bool visible, int64_t now_us) {
  if (!visible && !v->showing) return false;
  if (!v->poll_us || v->refresh) return true;
  int64_t since = now_us - v->poll_us;
  return since >= (v->showing ? PHONE_POLL_US : PHONE_IDLE_POLL_US);
}
/* Open the phone screen and ask the worker for a fresh code (clears any previous result). */
static inline void phone_request_start(phone_view *v) {
  v->showing = true;
  v->want_start = true;
  v->confirm_signout = false;
  v->note[0] = 0;
  if (v->st.state != PH_AUTHORIZED) {
    v->st.state = PH_NONE;
    v->st.user_code[0] = v->st.uri[0] = 0;
    v->qr_ok = false;
  }
}
/* Apply a verified WPH1 answer from the worker (under the UI lock). A status poll that finds no flow
 * while the QR screen waits (bridge restarted, code dropped) turns into a local "Try again" note, so
 * the screen can never hang on "Getting a code...". Returns whether the QR must be (re)encoded. */
static inline bool phone_apply(phone_view *v, const phone_status *st, bool polled) {
  bool was_waiting = v->showing && (v->st.state == PH_PENDING || v->st.state == PH_NONE);
  bool new_uri = strcmp(v->st.uri, st->uri) != 0 || !v->qr_ok;
  v->st = *st;
  v->absent = false;
  v->note[0] = 0;
  if (st->state != PH_PENDING) v->qr_ok = false;
  if (polled && was_waiting && st->state == PH_NONE && !v->want_start) snprintf(v->note, sizeof v->note, "Code lost - try again");
  return st->state == PH_PENDING && new_uri;
}
