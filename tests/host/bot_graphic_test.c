/* The bot graphic IS the Ask page's hold-to-talk control (no mic button any more):
 *  - every pixel a bot draws, in every mood, look, layout and animation phase, lies inside
 *    helper_bot_box(), and the box is not much bigger than the drawing;
 *  - over the whole page, a still press records IFF it lands on the graphic (box + fingertip margin):
 *    there is no other (mic button) region; press/hold/release keep HELPER_MIN_US + cancel;
 *  - while a command can be stopped, a tap on the working bot = Stop, and it wears a stop badge;
 *  - moods animate (idle breathes/blinks, listening follows the level, working orbits), the status
 *    row shows Hermes' live phrase, unknown bot ids fall back to the generic look, Home Ask tile
 *    shows the bots. Through the real touch path (home_sample) and renderer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"

static int64_t t = 1000000;
static void sample(home_ui *s, bool down, int x, int y) { t += 10000; home_sample(s, t, down, x, y); }
static void idle(home_ui *s, int ms) { for (int i = 0; i < ms / 10; i++) sample(s, false, 0, 0); }
/* A real finger: the CST816S reports no point on release (0,0). */
static void finger_tap(home_ui *s, int x, int y) { sample(s, true, x, y); sample(s, true, x, y); sample(s, false, 0, 0); }
static home_ui runnable(const char *id1) {
  home_ui s;
  memset(&s, 0, sizeof s);
  s.page = HELPER;
  s.bots.data = (bots_data){.valid = true, .count = 3, .received_us = t};
  const char *ids[3] = {"helper", id1, "coding"};
  for (int i = 0; i < 3; i++) { strcpy(s.bots.data.b[i].id, ids[i]); strcpy(s.bots.data.b[i].name, ids[i]); s.bots.data.b[i].available = true; s.bots.data.b[i].reset_s = BOTS_NONE32; }
  return s;
}
static void chat_history(home_ui *s) {
  s->helper.chat = true; s->helper.state = HV_DONE;
  helper_log_push(&s->helper.log, 'U', "earlier question");
  helper_log_push(&s->helper.log, 'B', "earlier answer");
}
static uint16_t base[SPARKLES_PIXELS], fr[SPARKLES_PIXELS], fr2[SPARKLES_PIXELS];
/* Draw only the bot over a flat background (any colour the bot never uses exactly). */
static void bot_only(uint16_t *p, const helper_view *h, int look) {
  for (int i = 0; i < SPARKLES_PIXELS; i++) p[i] = 0x0841;
  helper_draw_bot(p, h, look);
}
static int changed(const uint16_t *a, const uint16_t *b, helper_box r) {
  int n = 0;
  for (int y = r.y0 < 0 ? 0 : r.y0; y < (r.y1 > 448 ? 448 : r.y1); y++)
    for (int x = r.x0 < 0 ? 0 : r.x0; x < (r.x1 > 368 ? 368 : r.x1); x++) n += a[y * 368 + x] != b[y * 368 + x];
  return n;
}
static int count_color(const uint16_t *p, helper_box r, uint16_t c) {
  int n = 0;
  for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) n += p[y * 368 + x] == c;
  return n;
}

/* 1. Drawn pixels stay inside the box; the box hugs the drawing. */
static void drawing_inside_box(void) {
  static const helper_state states[] = {HV_IDLE, HV_LISTENING, HV_TRANSCRIBING, HV_RUNNING, HV_STOPPING, HV_DONE, HV_ERROR, HV_INTERRUPTED};
  int frames = 0, minw[2] = {368, 368}, minh[2] = {448, 448};
  for (int chat = 0; chat < 2; chat++)
    for (int look = 0; look < BOT_LOOKS; look++)
      for (size_t k = 0; k < sizeof states / sizeof *states; k++)
        for (int blk = 0; blk < 3; blk++)
          for (int f = 0; f < 14; f++) {
            helper_view h;
            memset(&h, 0, sizeof h);
            h.chat = chat; h.state = states[k]; h.block = blk == 0 ? 0 : (blk == 1 ? BR_EXHAUSTED : BR_LOADING);
            if (h.state == HV_TRANSCRIBING && f % 2) strcpy(h.id, "0123456789abcdef0123456789abcdef");
            if (h.state == HV_RUNNING) strcpy(h.id, "0123456789abcdef0123456789abcdef");
            h.orbs.time = f * .377f; h.orbs.level = (f % 4) / 3.f; h.level_milli = 1000;
            h.mood_state = h.state; h.mood_t = (f % 7) * .6f;
            if (f % 3 == 0) { strcpy(h.reply, "a reply that is still typing out"); h.reveal = 3; }
            bot_only(fr, &h, look);
            helper_box b = helper_bot_box(&h);
            int x0 = 368, x1 = -1, y0 = 448, y1 = -1, ink = 0;
            for (int y = 0; y < 448; y++)
              for (int x = 0; x < 368; x++) {
                if (fr[y * 368 + x] == 0x0841) continue;
                ink++;
                if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) {
                  fprintf(stderr, "chat=%d look=%d state=%d blk=%d f=%d: pixel %d,%d outside box [%d,%d)-[%d,%d)\n", chat, look, h.state, blk, f, x, y, b.x0, b.y0, b.x1, b.y1);
                  exit(1);
                }
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
              }
            assert(ink > (chat ? 3000 : 9000));
            if (x1 - x0 + 1 < minw[chat]) minw[chat] = x1 - x0 + 1;
            if (y1 - y0 + 1 < minh[chat]) minh[chat] = y1 - y0 + 1;
            frames++;
          }
  /* The box is the graphic, not a big invisible button: the drawing always fills most of it. */
  for (int chat = 0; chat < 2; chat++) {
    helper_view h = {.chat = chat};
    helper_box b = helper_bot_box(&h);
    printf("layout=%s box=%dx%d at %d,%d  min drawn %dx%d\n", chat ? "chat" : "welcome", b.x1 - b.x0, b.y1 - b.y0, b.x0, b.y0, minw[chat], minh[chat]);
    assert(minw[chat] * 100 >= (b.x1 - b.x0) * 80 && minh[chat] * 100 >= (b.y1 - b.y0) * 80);
  }
  printf("drawn pixels inside the bot box: %d frames\n", frames);
}
/* 2. The only press-and-hold region is the graphic (both layouts). */
static void hold_grid(void) {
  int checked = 0, hits = 0;
  for (int chat = 0; chat < 2; chat++)
    for (int y = 2; y < 448; y += 8)
      for (int x = 2; x < 368; x += 8) {
        home_ui s = runnable("atlas");
        if (chat) chat_history(&s);
        int tx=x,ty=chat&&y==426?418:y; /* keep this bot-edge row page-owned, above the reserved strip */
        bool expect = helper_bot_hit(&s.helper, tx, ty);
        assert(!(helper_bot_hit(&s.helper, tx, ty) && ty < HELPER_TOP_BAND));  /* never inside the top swipe band */
        for (int i = 0; i < 46; i++) sample(&s, true, tx, ty);
        bool rec = s.helper.state == HV_LISTENING && s.helper.want_record;
        if (rec != expect) { fprintf(stderr, "chat=%d hold at %d,%d: recording=%d expected=%d\n", chat, tx, ty, rec, expect); exit(1); }
        hits += rec; checked++;
      }
  printf("hold grid: %d points, %d on the bot record, nothing else does\n", checked, hits);
  assert(hits > 300);
}
/* 3. Press / hold / release on the graphic (its edges too) keep the timing rules. */
static void press_hold_release(void) {
  for (int chat = 0; chat < 2; chat++) {
    home_ui s = runnable("atlas");
    if (chat) chat_history(&s);
    helper_box b = helper_bot_box(&s.helper);
    float cx, cy, u; helper_bot_place(&s.helper, &cx, &cy, &u);
    int pts[3][2] = {{(int)cx, b.y0 + 3}, {b.x0 + 4, (int)cy}, {b.x1 - 4, (int)cy + 10}};  /* antenna badge, ears */
    for (int k = 0; k < 3; k++) {
      s = runnable("atlas");
      if (chat) chat_history(&s);
      int x = pts[k][0], y = pts[k][1];
      for (int i = 0; i < 30; i++) sample(&s, true, x, y);
      assert(s.helper.state != HV_LISTENING && s.helper.armed);  /* 0.3 s: armed, not recording */
      for (int i = 0; i < 15; i++) sample(&s, true, x, y);
      assert(s.helper.state == HV_LISTENING && s.helper.want_record);
      for (int i = 0; i < 10; i++) sample(&s, true, x, y);
      sample(&s, false, 0, 0);  /* released 0.1 s into the recording: < HELPER_MIN_US = cancelled */
      assert(s.helper.state == HV_IDLE && s.helper.want_cancel && !s.helper.want_send);
      assert(s.helper.chat == (bool)chat);  /* an empty chat goes back to the big bot */
      s.helper.want_record = s.helper.want_cancel = false;
      for (int i = 0; i < 100; i++) sample(&s, true, x, y);
      sample(&s, false, 0, 0);
      assert(s.helper.state == HV_TRANSCRIBING && s.helper.want_send && s.helper.chat);
    }
    /* A quick tap on the bot records nothing. */
    s = runnable("atlas");
    if (chat) chat_history(&s);
    finger_tap(&s, (int)cx, (int)cy);
    assert(!s.helper.want_record && s.helper.state == (chat ? HV_DONE : HV_IDLE));
    /* A horizontal swipe that starts on the bot switches bots (never records). */
    sample(&s, true, (int)cx, (int)cy);
    for (int x = (int)cx; x > (int)cx - 120; x -= 8) sample(&s, true, x, (int)cy);
    sample(&s, false, 0, 0);
    assert(s.helper.bot == 1 && !s.helper.want_record);
  }
  /* Swipe from the top still starts a new chat on the same bot. */
  home_ui s = runnable("atlas");
  chat_history(&s);
  for (int i = 0; i <= 20; i++) sample(&s, true, 184, 30 + i * 10);
  sample(&s, false, 0, 0);
  assert(!s.helper.chat && !s.helper.log.used && s.helper.bot == 0);
  puts("press/hold/release on the graphic: arm 0.4 s, short = cancel, long = send, tap = nothing, swipe = next bot, top swipe = new chat");
}
/* 4. Stop: a tap on the working bot (it wears a stop badge); nothing else stops, nothing records. */
static void stop_by_tap(void) {
  home_ui s = runnable("atlas");
  chat_history(&s);
  s.helper.state = HV_RUNNING; strcpy(s.helper.id, "0123456789abcdef0123456789abcdef");
  helper_box b = helper_bot_box(&s.helper);
  float cx, cy, u; helper_bot_place(&s.helper, &cx, &cy, &u);
  assert(helper_stop_visible(&s.helper));
  finger_tap(&s, 40, 360);  /* beside the bot */
  assert(s.helper.state == HV_RUNNING && !s.helper.want_stop);
  finger_tap(&s, (int)cx, (int)cy);
  assert(s.helper.state == HV_STOPPING && s.helper.want_stop && !s.helper.want_record);
  s.helper.want_stop = false;
  finger_tap(&s, (int)cx, (int)cy);
  assert(!s.helper.want_stop && !s.helper.want_record);  /* no repeat stop, no recording while stopping */
  /* The badge itself (top-right of the body) stops too; a sent upload with its id is stoppable. */
  s = runnable("atlas"); chat_history(&s);
  s.helper.state = HV_TRANSCRIBING; strcpy(s.helper.id, "0123456789abcdef0123456789abcdef");
  int bx = (int)(cx + 50 * u), by = (int)(cy - 44 * u);
  assert(bx < b.x1 && by > b.y0);
  finger_tap(&s, bx, by);
  assert(s.helper.state == HV_STOPPING && s.helper.want_stop);
  /* Badge drawn exactly while stoppable. */
  helper_view h; memset(&h, 0, sizeof h); h.chat = true; h.state = HV_RUNNING; strcpy(h.id, "0123456789abcdef0123456789abcdef");
  bot_only(fr, &h, BOT_LOOK_CODING);
  helper_box badge = {bx - 5, by - 5, bx + 5, by + 5};
  int stop_px = count_color(fr, badge, BOT_STOP), white_px = count_color(fr, (helper_box){bx - 1, by - 1, bx + 1, by + 1}, BOT_WHITE);
  h.state = HV_DONE; h.id[0] = 0;
  bot_only(fr2, &h, BOT_LOOK_CODING);
  printf("stop badge: %d stop-orange px around it, centre white %d; done: %d\n", stop_px, white_px, count_color(fr2, badge, BOT_STOP));
  assert(stop_px >= 8 && white_px == 4 && count_color(fr2, badge, BOT_STOP) == 0);
  /* Running shows Hermes' live phrase on the status row (and "Tap to stop" without one). */
  helper_view r; memset(&r, 0, sizeof r); r.chat = true; r.state = HV_RUNNING; strcpy(r.id, h.id);
  for (int i = 0; i < SPARKLES_PIXELS; i++) fr[i] = fr2[i] = 0x0841;
  helper_chat_controls(&r, fr, BOT_LOOK_HELPER, "");
  helper_note(&r, "Searching the web");
  helper_chat_controls(&r, fr2, BOT_LOOK_HELPER, "");
  helper_box row = {0, HELPER_STATUS_Y - 1, 368, HELPER_STATUS_Y + 18};
  assert(changed(fr, fr2, row) > 100 && !strcmp(helper_status(&r), "Searching the web"));
  r.note[0] = 0; assert(!strcmp(helper_status(&r), "Tap to stop"));
  puts("stop: tap on the working bot / its badge stops, beside it nothing; badge only while stoppable; live note on the status row");
}
/* 5. Moods animate; looks differ; unknown ids use the generic look; disabled = greys. */
static void moods_and_looks(void) {
  helper_view h; memset(&h, 0, sizeof h);
  helper_box b = helper_bot_box(&h);
  /* idle breathes / blinks over time */
  h.orbs.time = .3f; bot_only(fr, &h, 0);
  h.orbs.time = 2.2f; bot_only(fr2, &h, 0);
  assert(changed(fr, fr2, b) > 200);
  h.orbs.time = 3.79f; bot_only(fr2, &h, 0);  /* mid-blink: the eyes close */
  h.orbs.time = 3.79f; bot_only(fr, &h, 0);
  assert(changed(fr, fr2, b) == 0);  /* the clock alone decides the frame (wall-clock motion) */
  h.orbs.time = 3.40f; bot_only(fr, &h, 0);
  assert(changed(fr, fr2, b) > 60);
  /* listening follows the voice level */
  h.state = HV_LISTENING; h.orbs.time = 1; h.orbs.level = 0; h.level_milli = 0; bot_only(fr, &h, 0);
  h.orbs.level = 1; bot_only(fr2, &h, 0);
  assert(changed(fr, fr2, b) > 1500);
  /* working: the orbit moves on */
  h.state = HV_RUNNING; strcpy(h.id, "0123456789abcdef0123456789abcdef"); h.orbs.time = 1; bot_only(fr, &h, 0);
  h.orbs.time = 1.4f; bot_only(fr2, &h, 0);
  assert(changed(fr, fr2, b) > 300);
  /* done = happy, error = sad, both differ from idle */
  helper_view i; memset(&i, 0, sizeof i); i.orbs.time = 1; bot_only(fr, &i, 0);
  i.state = HV_DONE; bot_only(fr2, &i, 0); assert(changed(fr, fr2, b) > 150);
  i.state = HV_ERROR; bot_only(fr2, &i, 0); assert(changed(fr, fr2, b) > 150);
  i.state = HV_INTERRUPTED; bot_only(fr2, &i, 0); assert(changed(fr, fr2, b) > 40);
  assert(helper_bot_mood(&i) == BOT_STOPPED);
  /* every bot's look has its own outfit / colours (the head covers the top of each outfit: 1500 px);
   * the plain look (no clothes, the Home tile) differs from the collar only by the collar and tag */
  i.state = HV_IDLE;
  for (int a = 0; a < BOT_LOOKS; a++)
    for (int c = a + 1; c < BOT_LOOKS; c++) {
      bot_only(fr, &i, a); bot_only(fr2, &i, c);
      assert(changed(fr, fr2, b) > (c == BOT_LOOK_PLAIN ? 300 : 1500));
    }
  assert(BOT_LOOKS >= 4 && bot_outfits[BOT_LOOK_GENERIC].wear == BOT_WEAR_COLLAR && !bot_outfits[BOT_LOOK_GENERIC].id && bot_outfits[BOT_LOOK_PLAIN].wear == BOT_WEAR_NONE && !bot_outfits[BOT_LOOK_PLAIN].id);
  assert(bot_look_of("helper") == BOT_LOOK_HELPER && bot_look_of("coding") == BOT_LOOK_CODING);
  assert(bot_look_of("research") == BOT_LOOK_GENERIC && bot_look_of("") == BOT_LOOK_GENERIC && bot_look_of(NULL) == BOT_LOOK_GENERIC);
  /* plain = Kotaro in no clothes: none of any outfit's cloth colours in the box */
  bot_only(fr, &i, BOT_LOOK_PLAIN);
  for (int k = 0; k < BOT_LOOK_PLAIN; k++) {
    assert(count_color(fr, b, ss_rgb(bot_outfits[k].pal.body)) == 0 && count_color(fr, b, ss_rgb(bot_outfits[k].pal.deep)) == 0);
    assert(count_color(fr, b, ss_rgb(bot_outfits[k].pal.emblem)) == 0);
  }
  /* the page picks the look from the bridge's bot id */
  home_ui s = runnable("research");
  helper_restore_bot(&s.helper, 1); idle(&s, 30);
  assert(home_render(&s, fr, SPARKLES_PIXELS));
  orbs_render(&s.helper.orbs, base); memcpy(fr2, base, sizeof base);
  helper_render_overlay(&s.helper, fr2, HELPER_CHAT_CLIP, BOT_LOOK_GENERIC, ""); helper_shift(fr2, (int)s.helper.slide); helper_bot_header(fr2, &s.helper, "research");
  assert(changed(fr, fr2, b) == 0);
  /* disabled: a sleeping bot in greys (no warm accent), loading: dozing in colour */
  i.block = BR_EXHAUSTED; bot_only(fr, &i, 0);
  assert(helper_bot_mood(&i) == BOT_SLEEP);
  for (int y = b.y0; y < b.y1; y++)
    for (int x = b.x0; x < b.x1; x++) {
      uint16_t v = fr[y * 368 + x]; if (v == 0x0841) continue;
      int R = (v >> 11) * 255 / 31, G = ((v >> 5) & 63) * 255 / 63, B = (v & 31) * 255 / 31;
      assert(abs(R - G) < 40 && abs(G - B) < 40);
    }
  i.block = BR_LOADING; assert(helper_bot_mood(&i) == BOT_LOADING);
  i.orbs.time = 1; bot_only(fr, &i, 0); i.orbs.time = 1.3f; bot_only(fr2, &i, 0);
  assert(changed(fr, fr2, b) > 40);
  puts("moods: idle breathes/blinks, listening follows the level, working orbits, happy/sad/stopped faces, sleeping greys, loading dozes; 4 looks");
}
/* 6. Home Ask tile: one Kotaro in no clothes (in greys while the tile is closed), no mic icon; each
 * bot's outfit shows on its own Ask page only. */
static void home_tile(void) {
  home_ui s = (home_ui){.connected = true, .saved = true, .pair = {.state = PAIR_ENROLLED_UNPAIRED, .live_http = 200, .live_ok = true},
                        .phone = {.st = {.valid = true, .state = PH_AUTHORIZED, .flags = PHONE_FLAG_REQUIRED}}};
  s.tile = home_tile_index(HELPER);
  assert(home_tiles[s.tile].icon == TILE_ICON_BOTS && !strcmp(home_tiles[s.tile].name, "Ask"));
  assert(home_render(&s, fr, SPARKLES_PIXELS));
  helper_box art = {88, 108, 280, 300};
  /* one Kotaro (his fur), no outfit colour of any bot */
  bot_pose any = {.look = BOT_LOOK_HELPER};
  uint16_t fur = bot_colors_of(&any).fur;
  int fur_px = count_color(fr, art, fur), cloth_px = 0;
  for (int k = 0; k < BOT_LOOK_PLAIN; k++)
    cloth_px += count_color(fr, art, ss_rgb(bot_outfits[k].pal.body)) + count_color(fr, art, ss_rgb(bot_outfits[k].pal.deep));
  printf("Home tile: fur %d px, outfit %d px\n", fur_px, cloth_px);
  assert(fur_px > 4000 && cloth_px == 0);
  /* the same picture whichever bot is selected */
  s.helper.bot = 1; assert(home_render(&s, fr2, SPARKLES_PIXELS));
  assert(changed(fr, fr2, art) == 0);
  home_ui off = {0}; off.tile = s.tile;
  assert(home_render(&off, fr, SPARKLES_PIXELS) && count_color(fr, art, fur) == 0 && count_color(fr, art, BOT_RIM) > 300);
  puts("Home Ask tile: one Kotaro in no clothes, the same for every bot, greys when closed");
}
int main(void) {
  drawing_inside_box();
  hold_grid();
  press_hold_release();
  stop_by_tap();
  moods_and_looks();
  home_tile();
  puts("bot_graphic: the bot is the hold-to-talk control (box = drawing), no mic button region, tap-to-stop with badge, moods, looks, Home tile: PASS");
  return 0;
}
