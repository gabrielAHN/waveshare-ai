/* Phone sign-in (device grant) on the Settings page: WPH1 decode, QR encode/module accessor, taps,
 * state machine (waiting -> approved -> paired; denied/expired -> Try again), countdown repaint,
 * Ask-page mic gate, WPC1 USB hook, render (QR card pixels == QR modules). */
#include <assert.h>
#include <stdio.h>
#include "home_render.h"
#include "phone_qr.h"
#include "pair_usb.h"

static const char *URI = "https://auth.example.com/consent/openid/device-authorization?user_code=ABCD1234";
static uint16_t *A, *B;
static void tap(home_ui *s, int x, int y) {
  int64_t t = s->input.stamp_us + 20000;
  home_sample(s, t, true, x, y);
  home_sample(s, t + 40000, false, x, y);
  s->input.stamp_us = t + 60000;
}
static void tap_action(home_ui *s, int action) {
  settings_buttons b = home_settings_buttons(s, s->live_now_us);
  int k = home_settings_find(&b, action);
  assert(k >= 0);
  tap(s, b.t[k].x + b.t[k].w / 2, b.t[k].y + b.t[k].h / 2);
}
static size_t frame(unsigned char *f, unsigned state, unsigned flags, unsigned left, const char *code, const char *name, const char *uri) {
  memset(f, 0, PHONE_FRAME_SIZE);
  memcpy(f, "WPH1", 4); f[4] = 1; f[5] = (unsigned char)state; f[6] = (unsigned char)flags;
  f[8] = (unsigned char)(left & 255); f[9] = (unsigned char)(left >> 8);
  strcpy((char *)f + 12, code); strcpy((char *)f + 12 + 16, name); strcpy((char *)f + 12 + 16 + 33, uri);
  uint32_t crc = provision_crc(f, PHONE_FRAME_SIZE - 4);
  for (int i = 0; i < 4; i++) f[PHONE_FRAME_SIZE - 4 + i] = (unsigned char)(crc >> (8 * i));
  return PHONE_FRAME_SIZE;
}
static void render(const home_ui *s, uint16_t *p) { A[0] = 1; assert(home_render(s, p, SPARKLES_PIXELS)); }
static int diff(const home_ui *a, const home_ui *b) {
  render(a, A); render(b, B);
  int d = 0;
  for (int i = 0; i < SPARKLES_PIXELS; i++) d += A[i] != B[i];
  return d;
}

int main(void) {
  A = malloc(SPARKLES_PIXELS * sizeof *A); B = malloc(SPARKLES_PIXELS * sizeof *B); assert(A && B);
  assert(PHONE_FRAME_SIZE == 228);

  /* ---- WPH1 decode: golden, CRC, bounds; fail closed ---- */
  unsigned char f[PHONE_FRAME_SIZE];
  phone_status st = {0};
  frame(f, PH_PENDING, PHONE_FLAG_REQUIRED, 598, "ABCD1234", "", URI);
  assert(phone_decode(&st, f, sizeof f, 5000000));
  assert(st.valid && st.state == PH_PENDING && st.flags == PHONE_FLAG_REQUIRED && st.expires_in == 598);
  assert(!strcmp(st.user_code, "ABCD1234") && !strcmp(st.uri, URI) && st.received_us == 5000000);
  phone_status keep = st;
  f[20] ^= 1; assert(!phone_decode(&st, f, sizeof f, 1)); assert(!memcmp(&st, &keep, sizeof st));
  frame(f, 7, 0, 0, "", "", ""); assert(!phone_decode(&st, f, sizeof f, 1));          /* unknown state */
  frame(f, PH_NONE, 16, 0, "", "", ""); assert(!phone_decode(&st, f, sizeof f, 1));   /* unknown flag */
  frame(f, PH_AUTHORIZED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, 0, "", "sam", "");   /* 8 = shared sign-in */
  assert(PHONE_FLAG_SHARED == 8 && PHONE_FLAGS_KNOWN == 9 && phone_decode(&st, f, sizeof f, 1) && (st.flags & PHONE_FLAG_SHARED));
  phone_status unchanged = st;
  for (unsigned bit = 2; bit <= 4; bit *= 2) {
    frame(f, PH_AUTHORIZED, PHONE_FLAG_REQUIRED | bit, 0, "", "sam", "");
    assert(!phone_decode(&st, f, sizeof f, 1) && !memcmp(&st, &unchanged, sizeof st));
  }
  frame(f, PH_PENDING, 1, 10, "AB", "", "http://auth.example.com/x"); assert(!phone_decode(&st, f, sizeof f, 1)); /* not https */
  frame(f, PH_NONE, 1, 0, "", "", ""); assert(!phone_decode(&st, f, sizeof f - 1, 1));
  f[12 + 15] = 'X'; assert(!phone_decode(&st, f, sizeof f, 1));                    /* unterminated text */

  /* ---- QR: encode the verification URL, module accessor == qrcodegen_getModule ---- */
  phone_view v = {0};
  assert(phone_qr_encode(&v, URI));
  int size = qrcodegen_getSize(v.qr);
  assert(v.qr_ok && size >= 21 && size <= 57 && size == phone_qr_size(&v));
  for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) assert(phone_qr_module(&v, x, y) == qrcodegen_getModule(v.qr, x, y));
  assert(!phone_qr_module(&v, -1, 0) && !phone_qr_module(&v, size, 0));
  int scale = phone_qr_scale(size);
  assert(scale >= 5 && (size + 2 * PHONE_QR_QUIET) * scale <= PHONE_QR_BOX);
  printf("QR version size=%d scale=%d card=%dpx\n", size, scale, (size + 2 * PHONE_QR_QUIET) * scale);
  char toolong[400]; memset(toolong, 'a', sizeof toolong - 1); toolong[sizeof toolong - 1] = 0;
  phone_view big = {0}; assert(!phone_qr_encode(&big, toolong) && !big.qr_ok);

  /* ---- Settings page summary: enrolled, gate enforced, not signed in ---- */
  home_ui s = {0};
  s.page = SETTINGS; s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.step = PV_IDLE;
  strcpy(s.pair.bridge, "Studio host"); strcpy(s.pair.base, "https://192.0.2.10:8098"); s.pair.fp[0] = 1;
  frame(f, PH_NONE, PHONE_FLAG_REQUIRED, 0, "", "", ""); assert(phone_decode(&s.phone.st, f, sizeof f, 0));
  assert(phone_gate(&s.phone));
  render(&s, A);
  /* Ask page: mic disabled with "Sign in on Settings" (same disabled styling as No quota). */
  s.page = HELPER; s.input.stamp_us = 1000; home_bots_sync(&s, 1000);
  assert(s.helper.block == BR_PHONE && helper_mic_disabled(&s.helper));
  assert(!strcmp(bots_reason_text(BR_PHONE), "Sign in on Settings"));
  helper_block_text bt = helper_block_lines(&s.helper, "");
  assert(!strcmp(bt.line1, "Sign in") && !strcmp(bt.line2, "on Settings"));
  home_ui gated = s; home_ui open = s; open.helper.block = 0;
  gated.live_now_us = open.live_now_us = 0;
  assert(diff(&gated, &open) > 1000);
  /* Pressing the disabled mic never records. */
  s.helper.state = HV_IDLE; tap(&s, HELPER_BOT_X, HELPER_BOT_Y); assert(s.helper.state != HV_LISTENING);
  s.page = SETTINGS;

  /* Opening Settings on Wi-Fi with the gate enforced opens the QR by itself (no small buttons). */
  s.connected = true;
  tap(&s, 180, 10);
  assert(s.phone.showing && s.phone.want_start && !s.pair.want_scan);
  s.phone.want_start = false;
  /* Waiting: the QR card is drawn, module-exact, on a white card with a quiet zone. */
  frame(f, PH_PENDING, PHONE_FLAG_REQUIRED, 599, "ABCD1234", "", URI);
  assert(phone_decode(&s.phone.st, f, sizeof f, 10 * 1000000LL)); assert(phone_qr_encode(&s.phone, URI));
  s.live_now_us = 10 * 1000000LL;
  render(&s, A);
  int qx, qy, qs; phone_qr_geometry(&s.phone, &qx, &qy, &qs);
  int card = (size + 2 * PHONE_QR_QUIET) * qs;
  assert(qx - PHONE_QR_QUIET * qs >= 0 && qx - PHONE_QR_QUIET * qs + card <= 368 && qy + size * qs < 360);
  for (int my = -PHONE_QR_QUIET; my < size + PHONE_QR_QUIET; my++)
    for (int mx = -PHONE_QR_QUIET; mx < size + PHONE_QR_QUIET; mx++) {
      bool dark = phone_qr_module(&s.phone, mx, my);
      for (int k = 0; k < 4; k++) { /* sample 4 interior pixels of each module */
        int px = qx + mx * qs + 1 + (k & 1) * (qs - 3), py = qy + my * qs + 1 + (k >> 1) * (qs - 3);
        assert(A[py * 368 + px] == (dark ? PHONE_QR_INK : PHONE_QR_PAPER));
      }
    }
  /* The user code appears under the QR; the countdown repaints once per second only. */
  home_ui later = s; later.live_now_us += 400000; assert(home_visual_equal(&s, &later));
  later.live_now_us += 700000; assert(!home_visual_equal(&s, &later));
  char left[16]; phone_countdown(&s.phone, s.live_now_us + 61 * 1000000LL, left, sizeof left); assert(!strcmp(left, "8:58"));
  home_ui nocode = s; nocode.phone.st.user_code[0] = 0; assert(diff(&s, &nocode) > 300);
  assert(phone_display(&s.phone, s.live_now_us) == PH_PENDING);
  /* Local expiry: the countdown running out shows Expired + Try again even before the next poll. */
  assert(phone_display(&s.phone, s.live_now_us + 600 * 1000000LL) == PH_EXPIRED);
  /* Poll cadence: every ~3 s only while the QR is shown. */
  s.phone.poll_us = s.live_now_us;
  assert(!phone_poll_due(&s.phone, true, s.live_now_us + 2000000));
  assert(phone_poll_due(&s.phone, true, s.live_now_us + 3000000));
  s.phone.showing = false; assert(!phone_poll_due(&s.phone, false, s.live_now_us + 3000000));
  assert(phone_poll_due(&s.phone, true, s.live_now_us + PHONE_IDLE_POLL_US)); /* Settings/Connect visible: slow refresh */
  s.phone.showing = true;

  /* Bridge restarted while the QR waited: a polled "none" becomes a local Try-again, never a hang. */
  { home_ui r = s; phone_status none = {0}; frame(f, PH_NONE, PHONE_FLAG_REQUIRED, 0, "", "", "");
    assert(phone_decode(&none, f, sizeof f, r.live_now_us));
    assert(!phone_apply(&r.phone, &none, true) && r.phone.note[0] && !r.phone.qr_ok);
    tap_action(&r, SA_RETRY); assert(r.phone.want_start);
    /* ... but the start response itself (not a poll) leaves no note. */
    home_ui q = s; assert(!phone_apply(&q.phone, &none, false) && !q.phone.note[0]);
    phone_status pend = {0}; frame(f, PH_PENDING, 1, 500, "NEWC0DE1", "", "https://auth.example.com/consent/openid/device-authorization?user_code=NEWC0DE1"); assert(phone_decode(&pend, f, sizeof f, 0));
    assert(phone_apply(&q.phone, &pend, true)); }
  /* Only a large Back while waiting (it closes the QR); the Home pull also leaves it (Home). */
  { home_ui c = s; settings_buttons nb = home_settings_buttons(&c, c.live_now_us); assert(nb.count == 1 && nb.t[0].action == SA_BACK);
    home_ui k = c; tap_action(&k, SA_BACK); assert(!k.phone.showing && k.page == SETTINGS && !k.phone.want_start);
    int64_t t = c.input.stamp_us + 20000; home_sample(&c, t, true, 180, 440); home_sample(&c, t + 20000, true, 180, 428);
    home_sample(&c, t + 40000, true, 184, 224); home_sample(&c, t + 60000, false, 0, 0); assert(!c.phone.showing && c.page == HOME); }

  /* Denied / expired -> "Try again" restarts. */
  frame(f, PH_DENIED, PHONE_FLAG_REQUIRED, 0, "", "", ""); assert(phone_decode(&s.phone.st, f, sizeof f, s.live_now_us));
  render(&s, A);
  tap_action(&s, SA_RETRY); assert(s.phone.want_start); s.phone.want_start = false;
  frame(f, PH_EXPIRED, PHONE_FLAG_REQUIRED, 0, "", "", ""); assert(phone_decode(&s.phone.st, f, sizeof f, s.live_now_us));
  home_ui exp = s; frame(f, PH_REFUSED, PHONE_FLAG_REQUIRED, 0, "", "", ""); assert(phone_decode(&exp.phone.st, f, sizeof f, s.live_now_us));
  assert(!home_visual_equal(&s, &exp));
  tap_action(&s, SA_RETRY); assert(s.phone.want_start); s.phone.want_start = false;

  /* Approved as <name> -> paired. User rule: signed in + Wi-Fi opens Ask with the mic enabled before
   * the first /v1/bots frame; quota/route evidence still blocks (bots_entry_enabled_test). */
  frame(f, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, 0, "", "Sam Lee", ""); assert(phone_decode(&s.phone.st, f, sizeof f, s.live_now_us));
  assert(!phone_gate(&s.phone));
  render(&s, A);
  s.phone.showing = false; s.pair.state = PAIR_PAIRED;
  render(&s, A);
  s.page = HELPER; home_bots_sync(&s, s.live_now_us); assert(s.helper.block == BR_LOADING || s.helper.block == BR_NONE);
  assert(s.helper.block != BR_PHONE && s.helper.block != BR_UPSTREAM); s.page = SETTINGS;  /* never "disabled" once signed in */
  tap(&s, 180, 10); assert(!s.phone.showing && home_settings_screen(&s) == SS_STATUS); /* approved: QR closes */
  tap_action(&s, SA_SIGNOUT_ASK); assert(s.phone.confirm_signout && !s.phone.want_forget);   /* Sign out */
  assert(home_settings_screen(&s) == SS_SIGNOUT);
  tap_action(&s, SA_SIGNOUT_YES); assert(s.phone.want_forget && !s.phone.confirm_signout); s.phone.want_forget = false;
  /* Forgetting the bridge (USB/CLI only now) also signs the board out with the bridge. */
  s.pair.step = PV_IDLE; assert(pair_cmd_apply(&s, PAIR_CMD_FORGET)); assert(s.pair.want_forget && s.phone.want_forget);

  /* Unknown phone status is not an auth bypass and missing bot evidence stays closed. */
  home_ui u = {0}; u.page = HELPER; home_bots_sync(&u, 0); assert(!phone_gate(&u.phone) && u.helper.block == BR_UPSTREAM);

  /* ---- WPC1 USB hook: open the phone sign-in page without taps ---- */
  unsigned char w[9]; pair_cmd_encode(PAIR_CMD_PHONE, w);
  pair_cmd_parser pp = {0}; unsigned char action = 0; int r = 0;
  for (int i = 0; i < 9; i++) r = pair_cmd_feed(&pp, w[i], &action);
  assert(r == 1 && action == PAIR_CMD_PHONE);
  home_ui h = {0}; h.page = HOME; h.pair.state = PAIR_ENROLLED_UNPAIRED;
  assert(pair_cmd_apply(&h, action)); assert(h.page == SETTINGS && h.phone.showing && h.phone.want_start);
  home_ui none = {0}; assert(!pair_cmd_apply(&none, PAIR_CMD_PHONE)); /* not enrolled: nothing to sign in */

  /* Palette: the phone screens stay warm (no purple: R >= B and G + 8 >= B except the QR ink). */
  s.page = SETTINGS; s.phone.showing = true;
  frame(f, PH_PENDING, 1, 300, "ABCD1234", "", URI); phone_decode(&s.phone.st, f, sizeof f, s.live_now_us);
  render(&s, A);
  for (int i = 0; i < SPARKLES_PIXELS; i++) {
    int rr = (A[i] >> 11) * 8, gg = ((A[i] >> 5) & 63) * 4, bb = (A[i] & 31) * 8;
    assert(rr + 8 >= bb && gg + 8 >= bb);
  }
  puts("phone sign-in: WPH1 decode, QR encode + module-exact card, taps, waiting/approved/denied/expired/refused, countdown repaint, poll cadence, Ask gate, WPC1 hook, palette: PASS");
  free(A); free(B);
  return 0;
}
