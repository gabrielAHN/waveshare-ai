/* One Settings account tab when Hermes and Home Assistant share a sign-in (SPEC3 Contract S).
 * When both providers' sign-ins are built (Ask and Sensor) and either provider's phone status carries
 * PHONE_FLAG_SHARED (8), Settings shows ONE account tab: the Hermes tab, titled with the Hermes account
 * label, says who is signed in and that it covers Hermes and Home Assistant; its QR signs in both, its
 * sign-out signs out both (the board re-polls both statuses), and both statuses are polled while it is
 * visible. The Home Assistant tab is then unreachable: tab dots, swipes, home_open_signin (the Sensor
 * tile's "Sign in" fix), WPC1 all land on the shared tab. Until a status with flag 8 arrives the tabs stay
 * separate; once seen it is kept in RAM for the boot. Single-provider builds: tests/host/provider_signin_test.c.
 * Through the real paths (home_sample swipes, home_tap, pair_cmd_apply, home_render). RED first. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
#include "pair_usb.h"

static uint16_t px[SPARKLES_PIXELS];
static const int64_t NOW = 100LL * 1000 * 1000;
static int64_t t = 100LL * 1000 * 1000;

static phone_status status_of(unsigned state, unsigned flags, const char *name) {
  phone_status st; memset(&st, 0, sizeof st);
  st.valid = true; st.state = (uint8_t)state; st.flags = (uint8_t)flags; st.received_us = NOW;
  snprintf(st.name, sizeof st.name, "%s", name);
  return st;
}
/* On Wi-Fi, paired, on Settings > Wi-Fi; no sign-in status yet. */
static home_ui board(void) {
  home_ui s; memset(&s, 0, sizeof s);
  s.page = SETTINGS; s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; strcpy(s.pair.bridge, "Studio host");
  pair_live_result(&s.pair, 200, NOW);
  s.input.stamp_us = s.live_now_us = t;
  return s;
}
/* The live worker's path for a status answer (home_live.c phone_service_one -> home_provider_apply). */
static void answer(home_ui *s, int tab, unsigned state, unsigned flags, const char *name) {
  phone_status st = status_of(state, flags, name);
  home_provider_apply(s, tab, &st, true);
}
/* The real board: Hermes answers flags 13 (required | home | shared), Home Assistant 9 (required | shared). */
static home_ui shared_board(unsigned state) {
  home_ui s = board();
  answer(&s, SETTINGS_HERMES, state, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, state == PH_AUTHORIZED ? "sam" : "");
  answer(&s, SETTINGS_HOME_ASSISTANT, state, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, state == PH_AUTHORIZED ? "sam" : "");
  s.phone.refresh = s.phone_ha.refresh = s.bots.refresh = false;
  return s;
}
static void swipe(home_ui *s, int dir) {  /* dir +1 = next page (finger moves left) */
  for (int j = 0; j <= 8; j++) { t += 10000; home_sample(s, t, true, 184 - dir * 12 * j, 220); }
  t += 10000; home_sample(s, t, false, 0, 0);
  s->live_now_us = t;
}
/* One 10 ms poller sample with no touch (home_settings_tick runs at every sample). */
static void tick(home_ui *s) { t += 10000; home_sample(s, t, false, 0, 0); s->live_now_us = t; }
static void tap_target(home_ui *s, int action) {
  settings_buttons b = home_settings_buttons(s, s->live_now_us);
  int k = home_settings_find(&b, action); assert(k >= 0);
  t += 20000; home_sample(s, t, true, b.t[k].x + b.t[k].w / 2, b.t[k].y + b.t[k].h / 2);
  t += 40000; home_sample(s, t, false, b.t[k].x + b.t[k].w / 2, b.t[k].y + b.t[k].h / 2);
  s->live_now_us = t;
}
/* Every drawn text, joined with spaces; *title = the big text in the title line. */
static void screen_text(const home_ui *s, char *out, size_t cap, char *title, size_t tcap) {
  settings_record rec; memset(&rec, 0, sizeof rec); set_rec = &rec; assert(home_render(s, px, SPARKLES_PIXELS)); set_rec = NULL;
  out[0] = 0; if (title) title[0] = 0;
  for (int j = 0; j < rec.count; j++) {
    if (title && rec.t[j].y < 56 && rec.t[j].scale == 2) snprintf(title, tcap, "%s", rec.t[j].text);
    if (strlen(out) + strlen(rec.t[j].text) + 2 >= cap) break;
    if (out[0]) strcat(out, " ");
    strcat(out, rec.t[j].text);
  }
}
/* Tab dots: runs of non-background pixels on the dot row (y 56..61). */
static int dots(const home_ui *s) {
  assert(home_render(s, px, SPARKLES_PIXELS));
  int n = 0;
  for (int x = 1; x < 368; x++) n += px[59 * 368 + x] != PHONE_BG && px[59 * 368 + x - 1] == PHONE_BG;
  return n;
}

/* 1. Separate until a status with flag 8 has been received (no guessing); then one tab, kept for the boot. */
static void latch(void) {
  home_ui s = board();
  assert(home_settings_tabs(&s) == 6 && home_settings_tab_reachable(&s, SETTINGS_HOME_ASSISTANT));
  answer(&s, SETTINGS_HERMES, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");  /* not shared */
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");
  assert(home_settings_tabs(&s) == 6 && home_settings_tab_reachable(&s, SETTINGS_HOME_ASSISTANT));
  s.settings_tab = SETTINGS_HOME_ASSISTANT; assert(dots(&s) == 6);
  /* The Home Assistant status arrives with flag 8 (the real board: flags=9): one account tab. */
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_AUTHORIZED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam");
  assert(home_settings_tabs(&s) == 5 && !home_settings_tab_reachable(&s, SETTINGS_HOME_ASSISTANT) &&
         home_settings_tab_reachable(&s, SETTINGS_HERMES));
  /* The open Home Assistant tab folds into the shared tab at the next sample (no dot for a hidden tab). */
  tick(&s); assert(s.settings_tab == SETTINGS_HERMES && dots(&s) == 5);
  /* Kept in RAM for the boot: a later answer without flag 8, or a 404, does not split the tabs again. */
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");
  answer(&s, SETTINGS_HERMES, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");
  assert(home_settings_tabs(&s) == 5 && !home_settings_tab_reachable(&s, SETTINGS_HOME_ASSISTANT));
  phone_absent(&s.phone_ha); assert(home_settings_tabs(&s) == 5);
  /* Hermes' answer alone carries flag 8 (flags=13): shared too. */
  home_ui h = board();
  answer(&h, SETTINGS_HERMES, PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "");
  assert(home_settings_tabs(&h) == 5);
  /* No Wi-Fi: no account tab at all. */
  home_ui o = shared_board(PH_AUTHORIZED); o.connected = false; assert(home_settings_tabs(&o) == 4);
}
/* 2. Tab count, order, swipes and dots: Wi-Fi, Display, Sound, Battery, the shared tab; the end clamps. */
static void tabs(void) {
  home_ui s = shared_board(PH_AUTHORIZED);
  settings_page seen[8]; int n = 0; seen[n++] = s.settings_tab;
  for (int k = 0; k < 6; k++) { swipe(&s, 1); seen[n++] = s.settings_tab; }
  assert(seen[0] == SETTINGS_WIFI && seen[1] == SETTINGS_DISPLAY && seen[2] == SETTINGS_SOUND && seen[3] == SETTINGS_BATTERY &&
         seen[4] == SETTINGS_HERMES && seen[5] == SETTINGS_HERMES && seen[6] == SETTINGS_HERMES);
  assert(home_settings_next_tab(&s, -1) == SETTINGS_HERMES && home_settings_next_tab(&s, 1) == SETTINGS_BATTERY);
  swipe(&s, -1); assert(s.settings_tab == SETTINGS_BATTERY);
  for (int k = 0; k < SETTINGS_TABS; k++) { s.settings_tab = (settings_page)k; if (home_settings_tab_reachable(&s, k)) assert(dots(&s) == 5); }
  s.settings_tab = SETTINGS_HERMES;
  /* The active dot (wide, accent) is the last one, after 4 inactive ones. */
  assert(home_render(&s, px, SPARKLES_PIXELS));
  int x0[8], w[8], n5 = 0;
  for (int x = 1; x < 368; x++) {
    if (px[59 * 368 + x] != PHONE_BG && px[59 * 368 + x - 1] == PHONE_BG) { assert(n5 < 8); x0[n5] = x; w[n5] = 0; n5++; }
    if (px[59 * 368 + x] != PHONE_BG && n5) w[n5 - 1]++;
  }
  assert(n5 == 5);
  for (int k = 0; k < 4; k++) assert(w[k] < w[4] && px[59 * 368 + x0[k] + w[k] / 2] != HH_ORANGE);
  assert(px[59 * 368 + x0[4] + w[4] / 2] == HH_ORANGE);
}
/* 3. What the shared tab says: the Hermes account label as its title, who is signed in, one line that it
 *    covers Hermes and Home Assistant, each provider's own state; the sign-out warning says both. */
static void text(void) {
  char all[600], title[48];
  home_ui s = shared_board(PH_AUTHORIZED); s.settings_tab = SETTINGS_HERMES;
  screen_text(&s, all, sizeof all, title, sizeof title);
  assert(!strcmp(title, WAVESHARE_AI_HERMES_ACCOUNT_NAME));
  settings_buttons b = home_settings_buttons(&s, s.live_now_us);
  assert(b.count == 1 && b.t[0].action == SA_SIGNOUT_ASK && !strcmp(b.t[0].label, "sam") && !strcmp(b.t[0].caption, WAVESHARE_AI_HERMES_SIGNED_IN));
  assert(strstr(all, "Covers Hermes and Home Assistant") && strstr(all, "Hermes: ready") && strstr(all, "Home Assistant: ready"));
  assert(!strstr(all, "Shared with"));
  /* Home Assistant refused (not in its groups) while Hermes is fine: the tab says which one. */
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_REFUSED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam");
  screen_text(&s, all, sizeof all, NULL, 0);
  assert(strstr(all, "Hermes: ready") && strstr(all, "Home Assistant: not for this account") && strstr(all, "Covers Hermes and Home Assistant"));
  /* Signed out: one "Scan to sign in" (Hermes caption), both providers say sign in. */
  home_ui o = shared_board(PH_NONE); o.settings_tab = SETTINGS_HERMES; o.auto_qr = true;
  screen_text(&o, all, sizeof all, title, sizeof title);
  b = home_settings_buttons(&o, o.live_now_us);
  assert(b.count == 1 && b.t[0].action == SA_SIGNIN && b.t[0].primary && !strcmp(b.t[0].caption, WAVESHARE_AI_HERMES_SIGNED_OUT));
  assert(!strcmp(title, WAVESHARE_AI_HERMES_ACCOUNT_NAME) && strstr(all, "Covers Hermes and Home Assistant") &&
         strstr(all, "Hermes: sign in to use Ask") && strstr(all, "Home Assistant: sign in to use Sensor"));
  /* The sign-out question names both. */
  s = shared_board(PH_AUTHORIZED); s.settings_tab = SETTINGS_HERMES; s.phone.confirm_signout = true;
  screen_text(&s, all, sizeof all, NULL, 0); assert(strstr(all, WAVESHARE_AI_SHARED_SIGNOUT_NOTE));
  /* A change of the OTHER provider's status repaints the shared tab. */
  home_ui a = shared_board(PH_AUTHORIZED); a.settings_tab = SETTINGS_HERMES; home_ui c = a;
  c.phone_ha.st.state = PH_REFUSED; assert(!home_visual_equal(&a, &c));
}
/* 4. Its QR signs in both, its sign-out signs out both (the board refreshes both statuses). */
static void act_on_both(void) {
  home_ui s = shared_board(PH_NONE); s.settings_tab = SETTINGS_HERMES; s.auto_qr = true;
  tap_target(&s, SA_SIGNIN);
  assert(s.phone.want_start && s.phone.showing && !s.phone_ha.want_start && !s.phone_ha.showing);   /* ONE flow */
  assert(s.settings_tab == SETTINGS_HERMES && home_settings_screen(&s) == SS_QR);
  assert(home_phone_visible(&s, SETTINGS_HERMES) && home_phone_visible(&s, SETTINGS_HOME_ASSISTANT));
  /* Approved on the phone: the Hermes answer signs in, the Home Assistant status is re-polled ... */
  s.phone.want_start = false;
  answer(&s, SETTINGS_HERMES, PH_AUTHORIZED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam");
  assert(phone_signed_in(&s.phone) && s.phone_ha.refresh && phone_poll_due(&s.phone_ha, home_phone_visible(&s, SETTINGS_HOME_ASSISTANT), NOW + 1));
  /* ... and says signed in too: both tiles on. */
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_AUTHORIZED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam");
  tick(&s);
  assert(!s.phone.showing && home_settings_screen(&s) == SS_STATUS);
  assert(home_tile_state(&s, home_tile_index(HELPER)) == TILE_ON && home_tile_state(&s, home_tile_index(SENSORS)) == TILE_ON);
  /* Sign out: one confirm, the Hermes forget, and the Home Assistant status re-polled at once. */
  s.phone_ha.refresh = false;
  tap_target(&s, SA_SIGNOUT_ASK); assert(s.phone.confirm_signout && home_settings_screen(&s) == SS_SIGNOUT);
  tap_target(&s, SA_SIGNOUT_YES);
  assert(s.phone.want_forget && s.phone_ha.refresh && !s.phone.confirm_signout);
  /* The bridge signs out both: both tiles close. */
  s.phone.want_forget = false;
  answer(&s, SETTINGS_HERMES, PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "");
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "");
  assert(home_tile_disabled(&s, home_tile_index(HELPER)) && home_tile_disabled(&s, home_tile_index(SENSORS)));
}
/* 5. Both statuses are polled while the shared tab is visible (60 s, at once on refresh); not elsewhere. */
static void polling(void) {
  home_ui s = shared_board(PH_AUTHORIZED);
  static const struct {home_page page; int tab; bool hermes, ha;} rows[] = {
    {SETTINGS, SETTINGS_HERMES, true, true}, {SETTINGS, SETTINGS_WIFI, true, false},
    {SETTINGS, SETTINGS_BATTERY, true, false}, {HOME, SETTINGS_WIFI, true, true}, {HELPER, SETTINGS_WIFI, true, false},
    {SENSORS, SETTINGS_WIFI, false, true}, {SPARKLES, SETTINGS_WIFI, false, false},
  };
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    s.page = rows[i].page; s.settings_tab = (settings_page)rows[i].tab;
    assert(home_phone_visible(&s, SETTINGS_HERMES) == rows[i].hermes && home_phone_visible(&s, SETTINGS_HOME_ASSISTANT) == rows[i].ha);
  }
  s.page = SETTINGS; s.settings_tab = SETTINGS_HERMES; s.phone.poll_us = s.phone_ha.poll_us = NOW;
  assert(!phone_poll_due(&s.phone_ha, home_phone_visible(&s, SETTINGS_HOME_ASSISTANT), NOW + PHONE_POLL_US));
  assert(phone_poll_due(&s.phone_ha, home_phone_visible(&s, SETTINGS_HOME_ASSISTANT), NOW + PHONE_IDLE_POLL_US));
  assert(phone_poll_due(&s.phone, home_phone_visible(&s, SETTINGS_HERMES), NOW + PHONE_IDLE_POLL_US));
  /* Not shared: the Hermes tab polls only Hermes (today's rule). */
  home_ui u = board(); u.settings_tab = SETTINGS_HERMES;
  answer(&u, SETTINGS_HERMES, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");
  assert(home_phone_visible(&u, SETTINGS_HERMES) && !home_phone_visible(&u, SETTINGS_HOME_ASSISTANT));
}
/* 6. Every route to the Home Assistant tab lands on the shared tab: the Sensor tile's "Sign in" fix,
 *    home_open_signin, WPC1 4 / 5 / 6, an open tab when flag 8 arrives, the auto QR of a Settings visit. */
static void routes(void) {
  int sensor = home_tile_index(SENSORS), ask = home_tile_index(HELPER);
  /* Signed out: the closed Sensor tile opens the shared tab's QR (the Hermes flow), never a second one. */
  home_ui s = shared_board(PH_NONE); s.page = HOME; s.tile = sensor;
  assert(home_tile_disabled(&s, sensor) && !strcmp(home_tile_off_label(&s, sensor), "Sign in"));
  t += 20000; home_sample(&s, t, true, 184, 224); t += 40000; home_sample(&s, t, false, 184, 224);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HERMES && s.phone.want_start && s.phone.showing && !s.phone_ha.want_start && !s.phone_ha.showing);
  /* The Ask tile goes to the same tab. */
  home_ui a = shared_board(PH_NONE); a.page = HOME; a.tile = ask; home_open_signin(&a);
  assert(a.page == SETTINGS && a.settings_tab == SETTINGS_HERMES && a.phone.want_start);
  /* Signed in to Hermes but Home Assistant refused: Sensor's fix shows the shared tab (no new flow). */
  home_ui r = shared_board(PH_AUTHORIZED); r.page = HOME; r.tile = sensor;
  answer(&r, SETTINGS_HOME_ASSISTANT, PH_REFUSED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam");
  assert(home_tile_disabled(&r, sensor)); home_open_signin(&r);
  assert(r.page == SETTINGS && r.settings_tab == SETTINGS_HERMES && !r.phone.want_start && !r.phone_ha.want_start);
  /* WPC1 4 (phone QR) from anywhere, even with a stale Home Assistant tab selected: the shared QR. */
  home_ui w = shared_board(PH_NONE); w.page = HOME; w.settings_tab = SETTINGS_HOME_ASSISTANT;
  assert(pair_cmd_apply(&w, PAIR_CMD_PHONE) && w.settings_tab == SETTINGS_HERMES && w.phone.want_start && !w.phone_ha.want_start);
  home_ui w2 = shared_board(PH_NONE); w2.settings_tab = SETTINGS_HOME_ASSISTANT; w2.auto_qr = true;
  assert(pair_cmd_apply(&w2, PAIR_CMD_PHONE) && w2.settings_tab == SETTINGS_HERMES && w2.phone.want_start && !w2.phone_ha.want_start);
  /* WPC1 5 presses the shared tab's first target. */
  home_ui p5 = shared_board(PH_AUTHORIZED); p5.page = HOME; p5.settings_tab = SETTINGS_HOME_ASSISTANT;
  assert(pair_cmd_apply(&p5, PAIR_CMD_BUTTON_A) && p5.settings_tab == SETTINGS_HERMES && p5.phone.confirm_signout && !p5.phone_ha.confirm_signout);
  /* A Home Assistant QR / sign-out question that was open when flag 8 arrived closes; the shared tab shows. */
  home_ui q = board(); q.settings_tab = SETTINGS_HOME_ASSISTANT;
  answer(&q, SETTINGS_HERMES, PH_NONE, PHONE_FLAG_REQUIRED, ""); answer(&q, SETTINGS_HOME_ASSISTANT, PH_NONE, PHONE_FLAG_REQUIRED, "");
  q.auto_qr = true; home_phone_start(&q, SETTINGS_HOME_ASSISTANT); assert(q.phone_ha.showing);
  answer(&q, SETTINGS_HOME_ASSISTANT, PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "");
  tick(&q); assert(q.settings_tab == SETTINGS_HERMES && !q.phone_ha.showing && !q.phone_ha.confirm_signout);
  /* Settings visit, both signed out: the QR that opens by itself is the shared one, on the shared tab. */
  home_ui v = shared_board(PH_NONE); v.settings_tab = SETTINGS_WIFI; tick(&v);
  assert(v.settings_tab == SETTINGS_HERMES && v.phone.showing && v.phone.want_start && !v.phone_ha.showing && v.auto_qr);
  /* Hermes signed in, the Home Assistant answer still says signed out (its re-poll is on its way): no
   * Home Assistant QR on a hidden tab. */
  home_ui x = shared_board(PH_AUTHORIZED); x.settings_tab = SETTINGS_WIFI;
  answer(&x, SETTINGS_HOME_ASSISTANT, PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "");
  tick(&x); assert(x.settings_tab == SETTINGS_WIFI && !x.phone_ha.showing && !x.phone_ha.want_start && !x.phone.showing);
}
/* 7. Not shared (both providers): exactly today's two tabs, each acting on its own view. */
static void not_shared(void) {
  home_ui s = board();
  answer(&s, SETTINGS_HERMES, PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");
  answer(&s, SETTINGS_HOME_ASSISTANT, PH_NONE, PHONE_FLAG_REQUIRED, "");
  s.page = HOME; s.tile = home_tile_index(SENSORS);
  t += 20000; home_sample(&s, t, true, 184, 224); t += 40000; home_sample(&s, t, false, 184, 224);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && s.phone_ha.want_start && !s.phone.want_start);
  assert(home_settings_tabs(&s) == 6 && dots(&s) == 6);
  char all[600], title[48]; s.phone_ha.showing = false; screen_text(&s, all, sizeof all, title, sizeof title);
  assert(!strcmp(title, WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME) && !strstr(all, "Covers"));
}

int main(void) {
  assert(WAVESHARE_AI_PROVIDER_HERMES && WAVESHARE_AI_PROVIDER_HOME_ASSISTANT && WAVESHARE_AI_PLUGIN_AI && WAVESHARE_AI_PLUGIN_HOME_ASSISTANT);
  latch(); tabs(); text(); act_on_both(); polling(); routes(); not_shared();
  puts("shared_signin: flag 8 -> one account tab (5 tabs, dots, swipes), Hermes title + covers line, one QR / one sign-out "
       "for both (both re-polled), both polled on it, separate until flag 8, kept for the boot, every route to it: PASS");
  return 0;
}
