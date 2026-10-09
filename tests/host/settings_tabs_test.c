/* Settings pages: Wi-Fi, Display, Sound, Battery (the device core), then one tab per provider -- Hermes (titled
 * WAVESHARE_AI_HERMES_ACCOUNT_NAME), Home Assistant (titled WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME) -- in that swipe
 * order. Battery is core (every build, with or without Wi-Fi), has no targets, and repaints only when the
 * battery reading visibly changes. Through the real swipe path (home_sample) and renderer. */
#include <assert.h>
#include <stdio.h>
#include "home_render.h"

static int64_t t = 1000000;
static void swipe(home_ui *s, int dir) {  /* dir +1 = next page (finger moves left) */
  for (int j = 0; j <= 8; j++) { t += 10000; home_sample(s, t, true, 184 - dir * 12 * j, 220); }
  t += 10000; home_sample(s, t, false, 0, 0);
}
static home_ui board(bool wifi) {
  home_ui s; memset(&s, 0, sizeof s);
  s.page = SETTINGS; s.connected = s.saved = wifi;
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200;
  s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED; s.phone.st.flags = PHONE_FLAG_REQUIRED;
  s.phone_ha.st = s.phone.st;
  s.input.stamp_us = s.live_now_us = t;
  return s;
}
int main(void) {
  /* Order and names. */
  assert(SETTINGS_TABS == 6 && SETTINGS_WIFI == 0 && SETTINGS_DISPLAY == 1 && SETTINGS_SOUND == 2 && SETTINGS_BATTERY == 3 && SETTINGS_HERMES == 4 &&
         SETTINGS_HOME_ASSISTANT == 5);
  static const char *names[SETTINGS_TABS] = {"wifi", "display", "sound", "battery", "hermes", "home_assistant"};
  for (int k = 0; k < SETTINGS_TABS; k++) assert(!strcmp(settings_tab_name(k), names[k]));
  assert(!strcmp(settings_tab_name(SETTINGS_TABS), "?"));
  assert(home_settings_tab_enabled(SETTINGS_BATTERY) && home_settings_tab_enabled(SETTINGS_WIFI) && home_settings_tab_enabled(SETTINGS_DISPLAY));
  /* With Wi-Fi: 6 pages, swiped in order, clamped at both ends. */
  home_ui s = board(true);
  assert(home_settings_tabs(&s) == 6);
  settings_page seen[8]; int n = 0;
  seen[n++] = s.settings_tab;
  for (int k = 0; k < 7; k++) { swipe(&s, 1); seen[n++] = s.settings_tab; }
  assert(seen[0] == SETTINGS_WIFI && seen[1] == SETTINGS_DISPLAY && seen[2] == SETTINGS_SOUND && seen[3] == SETTINGS_BATTERY && seen[4] == SETTINGS_HERMES &&
         seen[5] == SETTINGS_HOME_ASSISTANT && seen[6] == SETTINGS_HOME_ASSISTANT && seen[7] == SETTINGS_HOME_ASSISTANT);
  swipe(&s, -1); assert(s.settings_tab == SETTINGS_HERMES);
  swipe(&s, -1); assert(s.settings_tab == SETTINGS_BATTERY);
  /* The sign-in QR keeps the sign-in page: a swipe there never lands on Battery (or Sound). */
  home_ui q = board(true); q.settings_tab = SETTINGS_HERMES; q.phone.showing = true; q.phone.st.state = PH_PENDING;
  swipe(&q, 1); assert(q.settings_tab == SETTINGS_HERMES);
  swipe(&q, -1); assert(q.settings_tab == SETTINGS_HERMES);
  /* ... and the Home Assistant QR keeps the Home Assistant page. */
  home_ui qa = board(true); qa.settings_tab = SETTINGS_HOME_ASSISTANT; qa.phone_ha.showing = true; qa.phone_ha.st.state = PH_PENDING;
  swipe(&qa, -1); assert(qa.settings_tab == SETTINGS_HOME_ASSISTANT);
  swipe(&qa, 1); assert(qa.settings_tab == SETTINGS_HOME_ASSISTANT);
  /* A closed Ask tile (signed out) still opens the phone sign-in QR, never the new Battery page,
   * and the QR stays up through swipes until the sign-in ends. */
  home_ui h = board(true); h.page = HOME; h.phone.st.state = PH_NONE; h.phone.st.flags = PHONE_FLAG_REQUIRED;
  int ask = -1;
  for (int i = 0; i < HOME_TILES; i++) if (home_tiles[i].page == HELPER) ask = i;
  assert(ask >= 0);
  h.tile = ask;
  assert(home_tile_disabled(&h, ask));
  home_open_signin(&h);
  assert(h.page == SETTINGS && h.settings_tab == SETTINGS_HERMES && h.auto_qr && h.phone.showing);
  h.phone.st.state = PH_PENDING; h.phone.st.valid = true;
  assert(home_settings_screen(&h) == SS_QR);
  for (int k = 0; k < 3; k++) { swipe(&h, 1); assert(h.settings_tab == SETTINGS_HERMES && home_settings_screen(&h) == SS_QR); }
  swipe(&h, -1); assert(h.settings_tab == SETTINGS_HERMES && home_settings_screen(&h) == SS_QR);
  /* A closed Sensor tile opens the Home Assistant page and its QR, never the Hermes one. */
  home_ui e = board(true); e.page = HOME; e.phone_ha.st.state = PH_NONE;
  e.tile = home_tile_index(SENSORS); assert(e.tile >= 0 && home_tile_disabled(&e, e.tile));
  home_open_signin(&e);
  assert(e.page == SETTINGS && e.settings_tab == SETTINGS_HOME_ASSISTANT && e.phone_ha.showing && !e.phone.showing);
  /* Without Wi-Fi: no sign-in page; Display, then Sound, then straight on to Battery. */
  home_ui o = board(false);
  assert(home_settings_tabs(&o) == 4 && home_settings_tab_reachable(&o, SETTINGS_BATTERY) && home_settings_tab_reachable(&o, SETTINGS_DISPLAY) &&
         !home_settings_tab_reachable(&o, SETTINGS_HERMES));
  swipe(&o, 1); assert(o.settings_tab == SETTINGS_DISPLAY);
  swipe(&o, 1); assert(o.settings_tab == SETTINGS_SOUND);
  swipe(&o, 1); assert(o.settings_tab == SETTINGS_BATTERY);
  swipe(&o, -1); assert(o.settings_tab == SETTINGS_SOUND);
  /* Battery page: status only (no targets), a tap does nothing, renders one tab dot per reachable tab (6). */
  s = board(true); s.settings_tab = SETTINGS_BATTERY;
  s.battery = (battery_view){.known = true, .ok = true, .present = true, .vbus = true, .charging = true, .percent = 58, .vbat_mv = 3924, .eta_min = 80};
  assert(home_settings_screen(&s) == SS_STATUS && home_settings_buttons(&s, t).count == 0);
  home_ui before = s; home_tap(&s, 184, 150);
  assert(s.settings_tab == SETTINGS_BATTERY && s.page == SETTINGS && s.sound_off == before.sound_off && s.session_off == before.session_off);
  static uint16_t a[SPARKLES_PIXELS], b[SPARKLES_PIXELS];
  settings_record rec; memset(&rec, 0, sizeof rec); set_rec = &rec;
  assert(home_render(&s, a, SPARKLES_PIXELS)); set_rec = NULL;
  bool title = false, pct = false, line = false, detail = false;
  for (int k = 0; k < rec.count; k++) {
    title |= !strcmp(rec.t[k].text, "Battery");
    pct |= !strcmp(rec.t[k].text, "58%") && rec.t[k].scale == 4;
    line |= !strcmp(rec.t[k].text, "Charging") && rec.t[k].scale == 2;
    detail |= !strcmp(rec.t[k].text, "about 1 h 20 min to full");
  }
  assert(title && pct && line && detail);
  int dots = 0;  /* tab dots row y=56: count runs of non-background pixels */
  for (int x = 1; x < 368; x++) dots += a[59 * 368 + x] != PHONE_BG && a[59 * 368 + x - 1] == PHONE_BG;
  assert(dots == home_settings_tabs(&s) && dots == 6);
  /* A shared sign-in (flag 8 from either provider, SPEC3 Contract S) folds the two account tabs into one:
   * 5 pages, 5 dots, and the last swipe ends on the shared (Hermes) tab. */
  {home_ui sh = s; sh.phone_ha.st.flags |= PHONE_FLAG_SHARED;
   assert(home_settings_tabs(&sh) == 5 && !home_settings_tab_reachable(&sh, SETTINGS_HOME_ASSISTANT) && home_render(&sh, b, SPARKLES_PIXELS));
   int n5 = 0; for (int x = 1; x < 368; x++) n5 += b[59 * 368 + x] != PHONE_BG && b[59 * 368 + x - 1] == PHONE_BG;
   assert(n5 == 5);
   swipe(&sh, 1); assert(sh.settings_tab == SETTINGS_HERMES); swipe(&sh, 1); assert(sh.settings_tab == SETTINGS_HERMES);
   swipe(&sh, -1); assert(sh.settings_tab == SETTINGS_BATTERY);}
  /* Repaints when the reading changes on the Battery page, never because of it elsewhere. */
  home_ui c = s; assert(home_visual_equal(&s, &c));
  c.battery.percent = 59; assert(!home_visual_equal(&s, &c));
  c = s; c.battery.eta_min = 75; assert(!home_visual_equal(&s, &c));
  home_ui w = s, w2 = s; w.settings_tab = w2.settings_tab = SETTINGS_SOUND; w2.battery.percent = 12;
  assert(home_visual_equal(&w, &w2));
  /* Charging shows the bolt: the battery differs from the same percent on battery. */
  c = s; c.battery.vbus = c.battery.charging = false; c.battery.eta_min = -1;
  assert(home_render(&c, b, SPARKLES_PIXELS));
  int diff = 0; for (int y = BATT_Y0; y < BATT_Y0 + BATT_H; y++) for (int x = BATT_X0; x < BATT_X0 + BATT_W; x++) diff += a[y * 368 + x] != b[y * 368 + x];
  assert(diff > 400);
  /* The core-only firmware keeps Battery too (home_settings_tab_enabled has no plugin switch for it). */
  puts("settings_tabs: Wi-Fi, Display, Sound, Battery, Hermes, Home Assistant in swipe order (4 without Wi-Fi, 5 when the sign-in is shared), Battery = status page, repaint on change: PASS");
  return 0;
}
