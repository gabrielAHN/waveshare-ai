/* Host preview of the phone sign-in (device grant) screens -> evidence/phone-*.ppm.
 * Build paths are owned by tools/render_docs_previews.sh.
 * Then:  /usr/bin/python3 tests/preview/phone_sheet.py   (sheet + QR decode check of the waiting panel).
 * Simulation only: these are the renderer's pixels, not a photo of the AMOLED. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"
static uint16_t px[SPARKLES_PIXELS];
static const char *URI = "https://auth.example.com/consent/openid/device-authorization?user_code=QWRT7KXM";
static void save(const char *name, const home_ui *s) {
  const char *dir = getenv("PREVIEW_DIR");
  char path[512];
  snprintf(path, sizeof path, "%s/phone-%s.ppm", dir ? dir : "evidence", name);
  assert(home_render(s, px, SPARKLES_PIXELS));
  FILE *f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n368 448\n255\n");
  for (int i = 0; i < SPARKLES_PIXELS; i++) {
    unsigned p = px[i];
    fputc(((p >> 11) & 31) * 255 / 31, f); fputc(((p >> 5) & 63) * 255 / 63, f); fputc((p & 31) * 255 / 31, f);
  }
  fclose(f);
  printf("%s\n", path);
}
static void status(home_ui *s, unsigned state, unsigned flags, unsigned left, const char *code, const char *name, const char *uri) {
  phone_status *st = &s->phone.st;
  memset(st, 0, sizeof *st);
  st->valid = true; st->state = (uint8_t)state; st->flags = (uint8_t)flags; st->expires_in = (uint16_t)left;
  st->received_us = s->live_now_us;
  snprintf(st->user_code, sizeof st->user_code, "%s", code);
  snprintf(st->name, sizeof st->name, "%s", name);
  snprintf(st->uri, sizeof st->uri, "%s", uri);
  if (state == PH_PENDING) assert(phone_qr_encode(&s->phone, uri)); else s->phone.qr_ok = false;
}
int main(void) {
  home_ui s = {0};
  s.live_now_us = 100LL * 1000 * 1000;
  s.page = SETTINGS; s.saved = s.connected = true; strcpy(s.credentials.ssid, "home-net");
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.step = PV_IDLE; s.pair.live_http = 200;
  strcpy(s.pair.bridge, "Studio host"); strcpy(s.pair.base, "https://192.0.2.10:8098");
  pair_hex32("479ad1dc62451c82f1cb229bf5bf629f" "1e2eb88b79d0307b6dcf1898a0cce5f7", s.pair.fp);
  status(&s, PH_NONE, PHONE_FLAG_REQUIRED, 0, "", "", "");
  save("1-settings-signin", &s);
  s.phone.showing = true; save("2-getting-code", &s);
  status(&s, PH_PENDING, PHONE_FLAG_REQUIRED, 599, "QWRT7KXM", "", URI);
  save("3-waiting-qr", &s);
  status(&s, PH_DENIED, PHONE_FLAG_REQUIRED, 0, "", "", ""); save("5-denied", &s);
  status(&s, PH_EXPIRED, PHONE_FLAG_REQUIRED, 0, "", "", ""); save("6-expired", &s);
  status(&s, PH_REFUSED, PHONE_FLAG_REQUIRED, 0, "", "", ""); save("7-refused", &s);
  s.phone.showing = false;
  status(&s, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, 0, "", "Sam Lee", "");
  save("8-settings-signed-in", &s);
  s.phone.confirm_signout = true; save("9-settings-signout-confirm", &s); s.phone.confirm_signout = false;
  /* Ask page with the gate enforced: mic disabled, "Sign in / on Settings". */
  status(&s, PH_NONE, PHONE_FLAG_REQUIRED, 0, "", "", "");
  s.page = HELPER; { int64_t t = 0; for (int i = 0; i < 150; i++) helper_tick(&s.helper, t += 10000); } home_bots_sync(&s, s.live_now_us);
  save("c-ask-gated", &s);
  return 0;
}
