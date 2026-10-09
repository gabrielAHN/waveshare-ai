#pragma once
/* Ask-page bots: WBT1 frame from the bridge (GET /v1/bots), per-bot mic gating and swipe selection.
 * Pure C, host-tested (tests/host/bots_view_test.c, tests/host/bots_ui_test.c). No IDF, network or storage.
 *
 * WBT1 (little-endian, BOTS_FRAME_SIZE = 144 bytes; mirrors bridge/waveshare_bridge/bots.py):
 *   'WBT1' u8 version(1) u8 count(0..3) u8 flags u8 0
 *   3 x { id[12] name[12] provider[12] (NUL-terminated printable ASCII)
 *         u8 available(0/1) u8 reason(0 none,1 exhausted,2 signin,3 unlabelled,4 phone_auth,
 *                                    5 upstream) u8 bflags(1 stale) u8 0
 *         u32 reset_in_s (0xFFFFFFFF unknown; bridge-relative, counted down locally) }
 *   u32 crc32(all previous bytes)
 * The mic is enabled only with a fresh frame saying available && reason == none. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "provision.h"
#include "wire_fields.h"

#define BOTS_MAX 3
#define BOTS_TEXT 12
#define BOTS_ENTRY (3 * BOTS_TEXT + 8)
#define BOTS_FRAME_SIZE (8 + BOTS_MAX * BOTS_ENTRY + 4)
#define BOTS_NONE32 0xFFFFFFFFu
#define BOTS_FLAG_PLUGIN 1u          /* the live Hermes plugin routes per bot */
#define BOTS_FLAG_STALE 2u           /* quota cache stale/missing on the host */
#define BOTS_FLAG_PLUGIN_UNKNOWN 4u
#define BOTS_FLAGS_KNOWN 7u
#define BOTS_POLL_US (5LL * 1000 * 1000)    /* bounded reachability refresh while Ask is visible */
#define BOTS_RETRY_US (5LL * 1000 * 1000)   /* recover promptly after a failed poll */
#define BOTS_REOPEN_US (3LL * 1000 * 1000)
#define BOTS_EVIDENCE_US (12LL * 1000 * 1000) /* poll + bounded TLS timeout + scheduling margin */
#define BOTS_HTTP_UNPAIRED (-1)

/* BR_PHONE: the board is not signed in with the phone (phone_qr.h); the bridge marks every bot. */
/* BR_LOADING (board-side only, never on the wire): still finding out (joining Wi-Fi, first /v1/bots
 * answer on its way, or reconnecting after a dropped poll). Drawn as a spinner, not as "disabled". */
typedef enum { BR_NONE, BR_EXHAUSTED, BR_SIGNIN, BR_UNLABELLED=3, BR_PHONE, BR_UPSTREAM, BR_LOADING } bots_reason;
/* Failed /v1/bots polls in a row before "Hermes unavailable" (5 s apart: ~30 s of no answer). */
#define BOTS_GIVE_UP_FAILS 6
typedef struct { char id[BOTS_TEXT], name[BOTS_TEXT], provider[BOTS_TEXT]; bool available, stale; uint8_t reason; uint32_t reset_s; } bots_entry;
typedef struct { bool valid; uint8_t flags, count; int64_t received_us; bots_entry b[BOTS_MAX]; } bots_data;
/* refresh = poll as soon as possible (e.g. the bridge just refused a command with 409). */
typedef struct { bots_data data; int http; unsigned attempts, fails; int64_t attempt_us; bool refresh; } bots_view;

/* Compiled-in swipe order, used until the first frame (and for its display names). */
static const char *const bots_default_id[BOTS_MAX] = {"helper", "atlas", "coding"};
static const char *const bots_default_name[BOTS_MAX] = {"Helper", "Atlas", "Coding"};

static inline bool bots_id_ok(const char *id) {
  if (!id[0] || !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9'))) return false;
  for (const char *c = id; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return false;
  return strcmp(id, "default") != 0;
}

/* Fail-closed: `out` is written only when the whole frame validates. */
static inline bool bots_decode(bots_data *out, const unsigned char *f, size_t n, int64_t now_us) {
  if (!out || !f || n != BOTS_FRAME_SIZE) return false;
  if (wire_u32(f + n - 4) != provision_crc(f, n - 4)) return false;
  if (memcmp(f, "WBT1", 4) || f[4] != 1 || f[5] > BOTS_MAX || (f[6] & ~BOTS_FLAGS_KNOWN) || f[7]) return false;
  bots_data d;
  memset(&d, 0, sizeof d);
  d.flags = f[6];
  d.count = f[5];
  const unsigned char *p = f + 8;
  for (int i = 0; i < BOTS_MAX; i++, p += BOTS_ENTRY) {
    if (i >= d.count) continue;
    bots_entry *e = &d.b[i];
    if (!wire_text(e->id, p, BOTS_TEXT) || !wire_text(e->name, p + 12, BOTS_TEXT) || !wire_text(e->provider, p + 24, BOTS_TEXT)) return false;
    if (!bots_id_ok(e->id) || p[36] > 1 || p[37] > BR_UPSTREAM || (p[38] & ~1u) || p[39]) return false;
    for (int k = 0; k < i; k++) if (!strcmp(d.b[k].id, e->id)) return false;  /* ids are unique */
    e->available = p[36];
    e->reason = p[37];
    e->stale = p[38] & 1u;
    e->reset_s = wire_u32(p + 40);
  }
  d.valid = true;
  d.received_us = now_us;
  *out = d;
  return true;
}

/* Poll only while the Ask page is visible: at once the first time, when the page (re)opens (unless a
 * poll finished < 3 s ago), on request, after a failure every 15 s, otherwise every 60 s. */
static inline bool bots_poll_due(const bots_view *v, bool visible, int64_t opened_us, int64_t now_us) {
  if (!visible) return false;
  if (!v->attempts) return true;
  int64_t since = now_us - v->attempt_us;
  if ((v->refresh || v->attempt_us < opened_us) && since >= BOTS_REOPEN_US) return true;
  if (v->fails && since >= BOTS_RETRY_US) return true;
  return since >= BOTS_POLL_US;
}

static inline int bots_count(const bots_view *v) { return v->data.valid && v->data.count ? v->data.count : BOTS_MAX; }
static inline int bots_clamp(const bots_view *v, int i) { int n = bots_count(v); return i < 0 ? 0 : (i >= n ? n - 1 : i); }
static inline const char *bots_id(const bots_view *v, int i) {
  i = bots_clamp(v, i);
  return v->data.valid && v->data.count ? v->data.b[i].id : bots_default_id[i];
}
static inline const char *bots_name(const bots_view *v, int i) {
  i = bots_clamp(v, i);
  return v->data.valid && v->data.count ? v->data.b[i].name : bots_default_name[i];
}
static inline const char *bots_provider(const bots_view *v, int i) {
  i = bots_clamp(v, i);
  return v->data.valid && v->data.count ? v->data.b[i].provider : "";
}
/* Seconds until the blocking window resets (BOTS_NONE32 unknown), counted down since arrival. */
static inline uint32_t bots_reset_left(const bots_view *v, int i, int64_t now_us) {
  if (!v->data.valid || i < 0 || i >= v->data.count) return BOTS_NONE32;
  return wire_remaining(v->data.b[i].reset_s, v->data.received_us, now_us);
}
/* Only fresh affirmative evidence enables a mic. Countdown expiry is not proof of recovery. */
static inline bots_reason bots_block(const bots_view *v, int i, int64_t now_us) {
  if (i < 0 || i >= bots_count(v)) return BR_UPSTREAM;
  if (!v->data.valid || !v->data.count || v->fails || now_us - v->data.received_us > BOTS_EVIDENCE_US) return BR_UPSTREAM;
  const bots_entry *e = &v->data.b[i];
  if (e->reason != BR_NONE) return (bots_reason)e->reason;
  return e->available ? BR_NONE : BR_UPSTREAM; /* quota staleness is telemetry, not route health */
}
static inline const char*bots_reason_name(unsigned r){return r==BR_EXHAUSTED?"exhausted":(r==BR_SIGNIN?"signin":(r==BR_UNLABELLED?"unavailable":(r==BR_PHONE?"phone_auth":(r==BR_UPSTREAM?"upstream":(r==BR_LOADING?"loading":"none")))));}
static inline const char *bots_reason_text(bots_reason r) {
  switch (r) {
    case BR_EXHAUSTED: return "No quota";
    case BR_SIGNIN: return "Sign in on the host";
    case BR_UNLABELLED: return "Hermes unavailable";
    case BR_PHONE: return "Sign in on Settings";
    case BR_UPSTREAM: return "Hermes unavailable";
    case BR_LOADING: return "Connecting to Hermes";
    default: return "";
  }
}
/* Second line for an exhausted bot: "resets in 2h 10m" (empty when unknown). */
static inline void bots_reset_text(uint32_t left, char *out, size_t cap) {
  if (!out || !cap) return;
  out[0] = 0;
  if (left == BOTS_NONE32 || !left) return;
  wire_reset_text(left, out, cap);
}
/* NVS restore of the selected bot: absent/corrupt -> 0 (helper). */
static inline int bots_restore(int stored) { return stored >= 0 && stored < BOTS_MAX ? stored : 0; }
