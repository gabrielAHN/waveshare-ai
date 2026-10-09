/* Providers (the named device's firmware/main/plugins.h): the Hermes provider (Sparkles + Ask, its sign-in) and the opt-in
 * Home Assistant provider (Sensor, its OWN sign-in view and Settings tab). Two phone_views, the shared
 * sign-in flag 8, the gate of each tile, HTTP 404 = "Not set up", the Settings tab of each provider, which
 * pages poll which status, and the X-Provider value of each. tests/run_host_tests.sh also builds this suite
 * with -DWAVESHARE_AI_PROVIDER_HOME_ASSISTANT=0 and with -DWAVESHARE_AI_PROVIDER_HERMES=0 (PROVIDER_BUILDS).
 * RED first. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "home_render.h"
#include "pair_usb.h"

#define H WAVESHARE_AI_PROVIDER_HERMES
#define HA WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
static uint16_t px[SPARKLES_PIXELS];
static const int64_t NOW = 100LL * 1000 * 1000;

static phone_status status_of(unsigned state, unsigned flags, const char *name) {
  phone_status st; memset(&st, 0, sizeof st);
  st.valid = true; st.state = (uint8_t)state; st.flags = (uint8_t)flags; st.received_us = NOW;
  snprintf(st.name, sizeof st.name, "%s", name);
  return st;
}
/* On Wi-Fi, paired with the bridge, both statuses unknown yet. */
static home_ui board(void) {
  home_ui s; memset(&s, 0, sizeof s);
  s.page = HOME; s.connected = s.saved = true;
  s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; strcpy(s.pair.bridge, "Studio host");
  s.input.stamp_us = s.live_now_us = NOW;
  return s;
}
static void set(home_ui *s, int tab, phone_status st) { phone_view *v = home_tab_phone(s, tab); assert(v); v->st = st; v->absent = false; }
static int tile_of(home_page page) { return home_tile_index(page); }
/* Every drawn text of the current screen, joined with spaces. */
static void screen_text(const home_ui *s, char *out, size_t cap) {
  settings_record rec; memset(&rec, 0, sizeof rec); set_rec = &rec; assert(home_render(s, px, SPARKLES_PIXELS)); set_rec = NULL;
  out[0] = 0;
  for (int j = 0; j < rec.count; j++) {
    if (strlen(out) + strlen(rec.t[j].text) + 2 >= cap) break;
    if (out[0]) strcat(out, " ");
    strcat(out, rec.t[j].text);
  }
}

/* 1. The build: which provider tabs, views and tiles exist. */
static void build(void) {
  assert(home_settings_tab_enabled(SETTINGS_HERMES) == H && home_settings_tab_enabled(SETTINGS_HOME_ASSISTANT) == HA);
  assert(WAVESHARE_AI_PLUGIN_SPARKLES == H && WAVESHARE_AI_PLUGIN_AI == H && WAVESHARE_AI_PLUGIN_HOME_ASSISTANT == HA);
  assert((tile_of(SPARKLES) >= 0) == H && (tile_of(HELPER) >= 0) == H && (tile_of(SENSORS) >= 0) == HA);
  assert(HOME_TILES == 1 + 2 * H + HA && home_tiles[HOME_TILES - 1].page == SETTINGS);
  home_ui s = board();
  assert((home_tab_phone(&s, SETTINGS_HERMES) != NULL) == H && (home_tab_phone(&s, SETTINGS_HOME_ASSISTANT) != NULL) == HA);
  assert(!home_tab_phone(&s, SETTINGS_WIFI) && !home_tab_phone(&s, SETTINGS_SOUND) && !home_tab_phone(&s, SETTINGS_BATTERY));
#if H && HA
  assert(home_tab_phone(&s, SETTINGS_HERMES) == &s.phone && home_tab_phone(&s, SETTINGS_HOME_ASSISTANT) == &s.phone_ha);
#endif
  /* The X-Provider header value of each provider's sign-in requests = its tab name (Contract B / D). */
  assert(!strcmp(settings_tab_name(SETTINGS_HERMES), "hermes") && !strcmp(settings_tab_name(SETTINGS_HOME_ASSISTANT), "home_assistant"));
  assert(home_provider_signin(SETTINGS_HERMES) == H && home_provider_signin(SETTINGS_HOME_ASSISTANT) == HA);
  assert(!strcmp(home_provider_label(SETTINGS_HERMES), WAVESHARE_AI_HERMES_ACCOUNT_NAME) &&
         !strcmp(home_provider_label(SETTINGS_HOME_ASSISTANT), WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME));
  /* Tab reachability: Wi-Fi, Display and Battery always, Sound with a Hermes tile, each built provider's
   * tab once Wi-Fi is up. */
  assert(home_settings_tab_enabled(SETTINGS_SOUND) == H && home_settings_tab_enabled(SETTINGS_DISPLAY) && home_settings_tabs(&s) == 3 + H + H + HA);
  s.connected = false;
  assert(home_settings_tabs(&s) == 3 + H && !home_settings_tab_reachable(&s, SETTINGS_HERMES) && !home_settings_tab_reachable(&s, SETTINGS_HOME_ASSISTANT));
  /* Swipes from Battery reach the provider tabs in order, then stop. */
  s.connected = true; s.page = SETTINGS; s.settings_tab = SETTINGS_BATTERY;
  int next = home_settings_next_tab(&s, -1);
  assert(next == (H ? SETTINGS_HERMES : HA ? SETTINGS_HOME_ASSISTANT : SETTINGS_BATTERY));
  if (H) { s.settings_tab = SETTINGS_HERMES; assert(home_settings_next_tab(&s, -1) == (HA ? SETTINGS_HOME_ASSISTANT : SETTINGS_HERMES)); }
  /* A provider tab that is not built is never kept. */
  s.settings_tab = H ? SETTINGS_HOME_ASSISTANT : SETTINGS_HERMES;
  if (!(H && HA)) { home_settings_tick(&s); assert(s.settings_tab == SETTINGS_WIFI); }
}

#if H && HA
/* 2. Two sign-in views: each tile reads its own provider's view, never the other. */
static void gates(void) {
  int ask = tile_of(HELPER), sensor = tile_of(SENSORS), sp = tile_of(SPARKLES);
  home_ui s = board();
  assert(home_tile_status(&s, ask).state == TILE_LOADING && home_tile_status(&s, sensor).state == TILE_LOADING);
  /* Hermes signed in: Ask on, Sensor still waits for its own answer. */
  set(&s, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  assert(home_tile_state(&s, ask) == TILE_ON && home_tile_state(&s, sensor) == TILE_LOADING);
  assert(!strcmp(home_tile_off_label(&s, sensor), "Checking sign-in"));
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  tile_status st = home_tile_status(&s, sensor);
  assert(st.state == TILE_OFF && st.blocks && !strcmp(st.label, "Sign in") && home_tile_state(&s, ask) == TILE_ON);
  assert(home_ha_off(&s) && !home_ha_absent(&s));
  /* Home Assistant signed in, Hermes signed out: the other way round. */
  set(&s, SETTINGS_HERMES, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  assert(home_tile_state(&s, ask) == TILE_OFF && home_tile_state(&s, sensor) == TILE_ON && !home_ha_off(&s));
  /* Sensor: authorized = signed in (a refused account is off). */
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_REFUSED, PHONE_FLAG_REQUIRED, "sam"));
  assert(home_tile_state(&s, sensor) == TILE_OFF);
  /* Sparkles (Hermes, optional) never reads a sign-in. */
  pair_live_result(&s.pair, 200, NOW); assert(home_tile_state(&s, sp) == TILE_ON);
  /* Ask's mic gate reads only the Hermes view. */
  home_ui a = board(); a.page = HELPER;
  set(&a, SETTINGS_HERMES, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  set(&a, SETTINGS_HOME_ASSISTANT, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  home_bots_sync(&a, NOW); assert(a.helper.block == BR_PHONE);
}
/* 3. A tap on a grey tile opens ITS provider's tab: the QR when signed out there. */
static void taps(void) {
  home_ui s = board();
  set(&s, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  s.tile = tile_of(SENSORS); home_tap(&s, 184, 224);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && s.phone_ha.showing && s.phone_ha.want_start);
  assert(!s.phone.showing && !s.phone.want_start && home_settings_screen(&s) == SS_QR);
  /* Only one QR at a time: opening the Hermes one closes the Home Assistant one. */
  home_phone_start(&s, SETTINGS_HERMES); assert(s.phone.showing && !s.phone_ha.showing);
  /* Settings targets act on the open tab's view: Sign out on the Home Assistant tab. */
  home_ui o = board(); o.page = SETTINGS; o.settings_tab = SETTINGS_HOME_ASSISTANT;
  set(&o, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  set(&o, SETTINGS_HOME_ASSISTANT, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "ha-user"));
  settings_buttons b = home_settings_buttons(&o, NOW);
  assert(b.count == 1 && b.t[0].action == SA_SIGNOUT_ASK && !strcmp(b.t[0].label, "ha-user") && !strcmp(b.t[0].caption, WAVESHARE_AI_HOME_ASSISTANT_SIGNED_IN));
  assert(home_settings_press(&o, 0) && o.phone_ha.confirm_signout && !o.phone.confirm_signout);
  assert(home_settings_do(&o, SA_SIGNOUT_YES) && o.phone_ha.want_forget && !o.phone.want_forget);
  /* WPC1 6 on the Hermes tab presses the Hermes row only. */
  o.settings_tab = SETTINGS_HERMES; o.phone_ha.want_forget = false;
  assert(pair_cmd_apply(&o, PAIR_CMD_BUTTON_A) && o.phone.confirm_signout && !o.phone_ha.confirm_signout);
  /* Forgetting the bridge (USB) signs out every provider. */
  o.phone.confirm_signout = false; o.pair.step = PV_IDLE; o.pair.state = PAIR_PAIRED;
  assert(pair_cmd_apply(&o, PAIR_CMD_FORGET) && o.phone.want_forget && o.phone_ha.want_forget);
}
/* 4. Flag 8 = shared sign-in: one approval / one sign-out shows on both tabs (the other view re-polls). */
static void shared(void) {
  home_ui s = board(); s.page = SETTINGS; s.settings_tab = SETTINGS_HERMES;
  set(&s, SETTINGS_HERMES, status_of(PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, ""));
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, ""));
  phone_status in = status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam");
  s.phone_ha.refresh = false; s.bots.refresh = false;
  home_provider_apply(&s, SETTINGS_HERMES, &in, true);
  assert(phone_signed_in(&s.phone) && phone_shared(&s.phone) && s.phone_ha.refresh && s.bots.refresh);
  /* The Home Assistant answer arrives (signed in too): Hermes asked to re-poll, no bots refresh. */
  s.phone.refresh = false; s.bots.refresh = false;
  home_provider_apply(&s, SETTINGS_HOME_ASSISTANT, &in, true);
  assert(phone_signed_in(&s.phone_ha) && s.phone.refresh && !s.bots.refresh);
  /* Signing out of a shared sign-in: the other one re-polls too. */
  s.phone.refresh = false;
  phone_status out = status_of(PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "");
  home_provider_apply(&s, SETTINGS_HOME_ASSISTANT, &out, false);
  assert(!phone_signed_in(&s.phone_ha) && s.phone.refresh);
  /* Not shared: each provider on its own, no cross re-poll. */
  home_ui u = board();
  set(&u, SETTINGS_HERMES, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  phone_status alone = status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam");
  home_provider_apply(&u, SETTINGS_HERMES, &alone, true);
  assert(phone_signed_in(&u.phone) && !u.phone_ha.refresh && !phone_signed_in(&u.phone_ha));
  /* ONE account tab names both providers; its sign-out confirmation says it signs out both. */
  char text[400];
  home_ui t = board(); t.page = SETTINGS; t.settings_tab = SETTINGS_HERMES;
  set(&t, SETTINGS_HERMES, in); set(&t, SETTINGS_HOME_ASSISTANT, in);
  assert(!home_settings_tab_reachable(&t, SETTINGS_HOME_ASSISTANT) && home_settings_tab_reachable(&t, SETTINGS_HERMES));
  screen_text(&t, text, sizeof text); assert(strstr(text, WAVESHARE_AI_SHARED_COVERS) && !strstr(text, "Shared with"));
  t.phone.confirm_signout = true; assert(home_settings_screen(&t) == SS_SIGNOUT);
  screen_text(&t, text, sizeof text); assert(strstr(text, WAVESHARE_AI_SHARED_SIGNOUT_NOTE) && strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE, "Signs out both"));
  /* Without flag 8 the note names only this provider. */
  home_ui n = board(); n.page = SETTINGS; n.settings_tab = SETTINGS_HERMES; set(&n, SETTINGS_HERMES, alone); n.phone.confirm_signout = true;
  screen_text(&n, text, sizeof text); assert(strstr(text, WAVESHARE_AI_HERMES_SIGNOUT_NOTE) && !strstr(text, "Signs out both") && !strstr(text, "Shared with"));
}
/* 5. HTTP 404 from a provider's status: not set up on the bridge. */
static void not_set_up(void) {
  int ask = tile_of(HELPER), sensor = tile_of(SENSORS), sp = tile_of(SPARKLES);
  home_ui s = board();
  set(&s, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  s.phone_ha.showing = true; s.phone_ha.confirm_signout = true;
  phone_absent(&s.phone_ha);
  assert(s.phone_ha.absent && !s.phone_ha.st.valid && !s.phone_ha.showing && !s.phone_ha.confirm_signout && !s.phone_ha.qr_ok);
  tile_status st = home_tile_status(&s, sensor);
  assert(st.state == TILE_OFF && st.blocks && !strcmp(st.label, "Not set up") && home_tile_state(&s, ask) == TILE_ON);
  assert(home_ha_off(&s) && home_ha_absent(&s));
  /* A tap opens its tab, which says where it is missing; no QR, no buttons. */
  s.tile = sensor; home_tap(&s, 184, 224);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && !s.phone_ha.want_start && !s.phone_ha.showing);
  assert(home_settings_screen(&s) == SS_STATUS && home_settings_buttons(&s, NOW).count == 0 && !home_settings_loading(&s));
  char text[400]; screen_text(&s, text, sizeof text);
  assert(strstr(text, "Not set up on Studio host") && strstr(text, WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME));
  s.pair.bridge[0] = 0; screen_text(&s, text, sizeof text); assert(strstr(text, "Not set up on this host"));
  assert(!pair_cmd_apply(&s, PAIR_CMD_PHONE) && !s.phone_ha.want_start);   /* WPC1 4 has nothing to open */
  /* A still screen (no spinner): no repaint while nothing changes. */
  home_ui same = s; same.input.scene.time += 0.5f; assert(home_visual_equal(&s, &same));
  home_ui back = s; set(&back, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, "")); assert(!home_visual_equal(&s, &back));
  /* Any valid frame clears it (the provider was added on the bridge). */
  phone_status none = status_of(PH_NONE, PHONE_FLAG_REQUIRED, "");
  phone_apply(&s.phone_ha, &none, true); assert(!s.phone_ha.absent && home_tile_status(&s, sensor).label[0] && strcmp(home_tile_status(&s, sensor).label, "Not set up"));
  /* Hermes not set up: Ask closed "Not set up", Sparkles keeps opening (optional) but says so. */
  home_ui h = board(); phone_absent(&h.phone); pair_live_result(&h.pair, 200, NOW);
  st = home_tile_status(&h, ask); assert(st.state == TILE_OFF && st.blocks && !strcmp(st.label, "Not set up"));
  st = home_tile_status(&h, sp); assert(st.state == TILE_OFF && !st.blocks && !strcmp(st.label, "Not set up"));
  h.tile = sp; home_tap(&h, 184, 224); assert(h.page == SPARKLES);
  h.page = HOME; h.tile = ask; home_tap(&h, 184, 224); assert(h.page == SETTINGS && h.settings_tab == SETTINGS_HERMES && !h.phone.want_start);
  /* The Sensor page shows no numbers either (the bridge has no Home Assistant). */
  home_ui p = board(); p.page = SENSORS; phone_absent(&p.phone_ha);
  p.sensors.data.valid = true; p.sensors.data.state = SS_OK; p.sensors.data.received_us = NOW; p.sensors.http = 200;
  for (int i = 0; i < SENSORS_COUNT; i++) p.sensors.data.r[i] = (sensors_row){.known = true, .x10 = 100, .quality = SQ_GOOD};
  sensors_view gated = p.sensors; gated.data.state = SS_SETUP;
  assert(!strcmp(sensors_note(&gated, NOW), "Set up on the host"));
  assert(home_render(&p, px, SPARKLES_PIXELS));
}
/* 6. Which pages poll which status (60 s while visible, 3 s while its QR shows). */
static void polling(void) {
  home_ui s = board();
  static const struct {home_page page; int tab; bool hermes, ha;} rows[] = {
    {HOME, SETTINGS_WIFI, true, true}, {HELPER, SETTINGS_WIFI, true, false}, {SENSORS, SETTINGS_WIFI, false, true},
    {SPARKLES, SETTINGS_WIFI, false, false}, {SETTINGS, SETTINGS_WIFI, true, false}, {SETTINGS, SETTINGS_HERMES, true, false},
    {SETTINGS, SETTINGS_HOME_ASSISTANT, true, true},
  };
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    s.page = rows[i].page; s.settings_tab = (settings_page)rows[i].tab;
    assert(home_phone_visible(&s, SETTINGS_HERMES) == rows[i].hermes && home_phone_visible(&s, SETTINGS_HOME_ASSISTANT) == rows[i].ha);
  }
  /* Its QR showing: every 3 s even off its pages. */
  s.page = SPARKLES; s.phone_ha.showing = true; s.phone_ha.poll_us = NOW;
  assert(!phone_poll_due(&s.phone_ha, home_phone_visible(&s, SETTINGS_HOME_ASSISTANT), NOW + 2000000));
  assert(phone_poll_due(&s.phone_ha, home_phone_visible(&s, SETTINGS_HOME_ASSISTANT), NOW + PHONE_POLL_US));
  s.phone_ha.showing = false; s.page = HOME;
  assert(!phone_poll_due(&s.phone_ha, true, NOW + PHONE_POLL_US) && phone_poll_due(&s.phone_ha, true, NOW + PHONE_IDLE_POLL_US));
}
/* 7. Settings visit: the QR that opens by itself is the first provider that requires a sign-in. */
static void auto_qr(void) {
  home_ui s = board(); s.page = SETTINGS; s.settings_tab = SETTINGS_WIFI;
  set(&s, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  home_settings_tick(&s); assert(s.settings_tab == SETTINGS_HOME_ASSISTANT && s.phone_ha.showing && s.auto_qr);
  home_ui b = board(); b.page = SETTINGS; b.settings_tab = SETTINGS_WIFI;
  set(&b, SETTINGS_HERMES, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  set(&b, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  home_settings_tick(&b); assert(b.settings_tab == SETTINGS_HERMES && b.phone.showing && !b.phone_ha.showing);
  /* Refused (signed in, not in that provider's groups): no QR by itself, neither on a Settings visit nor
   * from its tile -- a new device flow on a shared gateway would put the other provider back to pending.
   * Shared (SPEC3 Contract S): the Sensor tile shows the ONE account tab (signed in there, Sign out; its
   * status line says Home Assistant refused). */
  home_ui r = board(); r.page = SETTINGS; r.settings_tab = SETTINGS_WIFI;
  set(&r, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam"));
  set(&r, SETTINGS_HOME_ASSISTANT, status_of(PH_REFUSED, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, "sam"));
  home_settings_tick(&r); assert(r.settings_tab == SETTINGS_WIFI && !r.phone_ha.showing && !r.phone_ha.want_start && !r.phone.showing);
  r.page = HOME; r.tile = tile_of(SENSORS); home_tap(&r, 184, 224);
  assert(r.page == SETTINGS && r.settings_tab == SETTINGS_HERMES && !r.phone_ha.want_start && !r.phone.want_start);
  settings_buttons sb = home_settings_buttons(&r, NOW);
  assert(sb.count == 1 && sb.t[0].action == SA_SIGNOUT_ASK);
  /* Not shared: its own tab offers "Scan to sign in" instead; an explicit tap still starts one. */
  home_ui r2 = board(); r2.page = SETTINGS; r2.settings_tab = SETTINGS_WIFI;
  set(&r2, SETTINGS_HERMES, status_of(PH_AUTHORIZED, PHONE_FLAG_REQUIRED, "sam"));
  set(&r2, SETTINGS_HOME_ASSISTANT, status_of(PH_REFUSED, PHONE_FLAG_REQUIRED, "sam"));
  home_settings_tick(&r2); assert(r2.settings_tab == SETTINGS_WIFI && !r2.phone_ha.showing && !r2.phone_ha.want_start && r2.auto_qr);
  r2.page = HOME; r2.tile = tile_of(SENSORS); home_tap(&r2, 184, 224);
  assert(r2.page == SETTINGS && r2.settings_tab == SETTINGS_HOME_ASSISTANT && !r2.phone_ha.want_start && !r2.phone.want_start);
  sb = home_settings_buttons(&r2, NOW);
  assert(sb.count == 1 && sb.t[0].action == SA_SIGNIN);
  assert(home_settings_press(&r2, 0) && r2.phone_ha.want_start && r2.phone_ha.showing);
  /* Unpaired: the pairing screens show on whichever provider tab is open. */
  home_ui u = board(); u.page = SETTINGS; u.settings_tab = SETTINGS_HOME_ASSISTANT; u.pair.state = PAIR_NO_BRIDGE;
  home_settings_tick(&u); assert(home_settings_screen(&u) == SS_FIND && u.pair.want_scan && u.settings_tab == SETTINGS_HOME_ASSISTANT);
}
#endif

#if H && !HA
/* Hermes only (the ESP32 default): no Sensor, no Home Assistant tab or view; the Hermes sign-in. */
static void hermes_only(void) {
  home_ui s = board(); s.page = SETTINGS; s.settings_tab = SETTINGS_HERMES;
  set(&s, SETTINGS_HERMES, status_of(PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, ""));
  settings_buttons b = home_settings_buttons(&s, NOW);
  assert(b.count == 1 && b.t[0].action == SA_SIGNIN && !strcmp(b.t[0].caption, WAVESHARE_AI_HERMES_SIGNED_OUT));
  assert(strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE, "Ask stops working") && !strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE, "Sensor"));
  char text[400]; screen_text(&s, text, sizeof text);
  assert(strstr(text, WAVESHARE_AI_HERMES_ACCOUNT_NAME) && strstr(text, "Hermes: sign in to use Ask") && strstr(text, WAVESHARE_AI_HERMES_SHARED));
  /* Flag 8 with one provider built: today's tab, no folding (SPEC3 Contract S). */
  assert(home_settings_tabs(&s) == 5 && home_settings_tab_reachable(&s, SETTINGS_HERMES) && !strstr(text, WAVESHARE_AI_SHARED_COVERS));
  assert(home_ha_off(&s) && !home_ha_absent(&s));
  s.page = HOME; s.tile = tile_of(HELPER); assert(home_tile_disabled(&s, s.tile));
  home_tap(&s, 184, 224); assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HERMES && s.phone.want_start);
  phone_absent(&s.phone); assert(!strcmp(home_tile_off_label(&s, tile_of(HELPER)), "Not set up"));
}
#endif
#if HA && !H
/* Home Assistant only: no Hermes tiles or tab; the Sensor sign-in and its tab work on their own. */
static void home_assistant_only(void) {
  home_ui s = board();
  int sensor = tile_of(SENSORS); assert(sensor == 0 && HOME_TILES == 2);
  assert(home_tile_state(&s, sensor) == TILE_LOADING);
  set(&s, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  s.tile = sensor; home_tap(&s, 184, 224);
  assert(s.page == SETTINGS && s.settings_tab == SETTINGS_HOME_ASSISTANT && s.phone_ha.want_start);
  assert(strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE, "Sensor stops working") && !strstr(WAVESHARE_AI_SHARED_SIGNOUT_NOTE, "Ask"));
  s.phone_ha.showing = false; char text[400]; screen_text(&s, text, sizeof text);
  assert(strstr(text, WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME) && strstr(text, "Home Assistant: sign in to use Sensor") && !strstr(text, "Hermes:"));
  /* Flag 8 with one provider built: its own tab stays, says "Shared with", no folding (SPEC3 Contract S). */
  home_ui f = board(); f.page = SETTINGS; f.settings_tab = SETTINGS_HOME_ASSISTANT;
  phone_status sh = status_of(PH_NONE, PHONE_FLAG_REQUIRED | PHONE_FLAG_SHARED, ""); home_provider_apply(&f, SETTINGS_HOME_ASSISTANT, &sh, true);
  home_settings_tick(&f); assert(f.settings_tab == SETTINGS_HOME_ASSISTANT && home_settings_tab_reachable(&f, SETTINGS_HOME_ASSISTANT) && home_settings_tabs(&f) == 4);
  f.phone_ha.showing = false; screen_text(&f, text, sizeof text); assert(strstr(text, WAVESHARE_AI_HOME_ASSISTANT_SHARED) && !strstr(text, WAVESHARE_AI_SHARED_COVERS));
  f.page = HOME; f.tile = sensor; home_tap(&f, 184, 224); assert(f.page == SETTINGS && f.settings_tab == SETTINGS_HOME_ASSISTANT && f.phone_ha.want_start);
  /* WPC1 4 opens the Home Assistant QR (the only sign-in). */
  home_ui w = board(); set(&w, SETTINGS_HOME_ASSISTANT, status_of(PH_NONE, PHONE_FLAG_REQUIRED, ""));
  assert(pair_cmd_apply(&w, PAIR_CMD_PHONE) && w.settings_tab == SETTINGS_HOME_ASSISTANT && w.phone_ha.want_start);
  /* No Sound tab without a Hermes tile. */
  assert(!home_settings_tab_enabled(SETTINGS_SOUND));
}
#endif

int main(void) {
  build();
#if H && HA
  gates(); taps(); shared(); not_set_up(); polling(); auto_qr();
#endif
#if H && !HA
  hermes_only();
#endif
#if HA && !H
  home_assistant_only();
#endif
  printf("provider_signin hermes=%d home_assistant=%d: two sign-in views, gates per provider, shared flag 8, 404 -> Not set up, "
         "provider tabs + X-Provider names, polling pages: PASS\n", H, HA);
  return 0;
}
