/* Bot art preview: every look (bot_outfits row) in every mood, as the Ask page shows it, mirrored and in
 * the chat (compact) size, a frame strip of every animation (idle routines, nap, wake, celebrate, tap,
 * lean-in, nod), plus each look's whole Ask page and the Home Ask tile. Writes binary PPMs into OUT_DIR.
 *   tools/bot_preview.sh [OUT_DIR]      (builds this, renders, makes PNG sheets if ImageMagick is there)
 * Host-only (no ESP-IDF): it draws with the same named-device firmware/main headers the board uses. */
#include <stdio.h>
#include "home_render.h"

static uint16_t px[SPARKLES_PIXELS];

static void put_rgb(FILE *f, unsigned v) {
  fputc((int)((v >> 11) * 255 / 31), f);
  fputc((int)(((v >> 5) & 63) * 255 / 63), f);
  fputc((int)((v & 31) * 255 / 31), f);
}
/* The bot's touch box with an 8 px margin (everything the art may draw is inside the box). */
static int save_box(const char *path, const uint16_t *p, helper_box b) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  fprintf(f, "P6\n%d %d\n255\n", b.x1 - b.x0 + 16, b.y1 - b.y0 + 16);
  for (int y = b.y0 - 8; y < b.y1 + 8; y++)
    for (int x = b.x0 - 8; x < b.x1 + 8; x++) put_rgb(f, (x < 0 || y < 0 || x >= 368 || y >= 448) ? 0 : p[y * 368 + x]);
  fclose(f);
  return 1;
}
static int save_page(const char *path, const uint16_t *p) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  fprintf(f, "P6\n368 448\n255\n");
  for (int i = 0; i < SPARKLES_PIXELS; i++) put_rgb(f, p[i]);
  fclose(f);
  return 1;
}
/* One frame of Kotaro on the Ask page's orbs, saved as his box (big or compact as b->compact says). */
static int frame(const char *path, const bot_pose *in) {
  helper_view h;
  memset(&h, 0, sizeof h);
  h.chat = in->compact;
  h.orbs.time = 1.7f;
  orbs_render(&h.orbs, px);
  bot_pose b = *in;
  helper_bot_place(&h, &b.cx, &b.cy, &b.u);
  b.shadow = !b.compact;
  bot_art_draw(px, &b);
  return save_box(path, px, helper_bot_box(&h));
}
static bot_pose pose(int look, bot_mood mood) {
  bot_pose b;
  memset(&b, 0, sizeof b);
  b.look = look; b.mood = mood; b.t = 1.7f; b.mood_t = .8f; b.level = .8f;
  b.grey = mood == BOT_SLEEP; b.stop_badge = mood == BOT_WORK;
  return b;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  static const bot_mood moods[] = {BOT_IDLE, BOT_LISTEN, BOT_WORK, BOT_STOPPING, BOT_HAPPY, BOT_SAD, BOT_STOPPED, BOT_SLEEP, BOT_LOADING};
  static const char *const names[] = {"idle", "listen", "work", "stopping", "happy", "sad", "stopped", "sleep", "loading"};
  char path[512];
  int ok = 1;
  /* every look in every mood; the same mirrored and in the chat (compact) size */
  for (int look = 0; look < BOT_LOOKS; look++)
    for (unsigned k = 0; k < sizeof moods / sizeof moods[0]; k++) {
      bot_pose b = pose(look, moods[k]);
      if (b.mood == BOT_HAPPY) b.mood_t = 4.f;  /* the happy face, after the celebration */
      snprintf(path, sizeof path, "%s/look%d-%02u-%s.ppm", dir, look, k, names[k]);
      ok &= frame(path, &b);
      b.gaze = 1;
      snprintf(path, sizeof path, "%s/mirror-look%d-%02u-%s.ppm", dir, look, k, names[k]);
      ok &= frame(path, &b);
      b.gaze = 0; b.compact = true;
      snprintf(path, sizeof path, "%s/compact-look%d-%02u-%s.ppm", dir, look, k, names[k]);
      ok &= frame(path, &b);
    }
  /* a 10-frame strip of every animation, in the helper striped tee and the coding hoodie */
  static const char *const anim[] = {"stretch", "yawn", "scratch", "sniff", "chase", "celebrate", "nap", "wake", "tap", "lean", "nod"};
  for (int look = 0; look < 2; look++)
    for (int a = 0; a < (int)(sizeof anim / sizeof anim[0]); a++)
      for (int f = 0; f < 10; f++) {
        float u = (f + .5f) / 10.f;
        bot_pose b = pose(look, BOT_IDLE);
        b.t = 3.f + f * .11f; b.mood_t = 20.f;
        if (a < 5) { b.routine = a + 1; b.routine_s = u * bot_routine_len[a + 1]; }
        else if (a == 5) { b.mood = BOT_HAPPY; b.mood_t = u * BOT_CHEER_S; b.cheer_s = .001f + u * BOT_CHEER_S; }
        else if (a == 6) b.idle_s = BOT_NAP_S + u * (BOT_LIE_S + 6.f);  /* lies down, then sleeps */
        else if (a == 7) { b.wake_s = .001f + u * BOT_WAKE_S; b.wake_from = 1; }
        else if (a == 8) b.tap_s = .001f + u * BOT_TAP_S;
        else if (a == 9) {  /* the press arming, then listening */
          b.lean = f < 7 ? (u * 2.f > 1.f ? 1.f : u * 2.f) : 1.f - (f - 6) / 3.f;
          if (f >= 7) b.mood = BOT_LISTEN;
        } else { b.mood = BOT_THINK; b.nod_s = u * BOT_NOD_S; }
        snprintf(path, sizeof path, "%s/anim%d-%02d-%s-%d.ppm", dir, look, a, anim[a], f);
        ok &= frame(path, &b);
      }
  for (int look = 0; look < BOT_LOOKS; look++) {
    helper_view h;
    memset(&h, 0, sizeof h);
    h.orbs.time = 2.f;
    const char *id = bot_outfits[look].id;
    helper_render_view(&h, px, id ? id : (look == BOT_LOOK_PLAIN ? "plain" : "other bot"), look, "");
    snprintf(path, sizeof path, "%s/page-look%d.ppm", dir, look);
    ok &= save_page(path, px);
  }
  {
    static home_ui s;
    memset(&s, 0, sizeof s);
    s.connected = s.saved = true;
    s.pair.state = PAIR_ENROLLED_UNPAIRED; s.pair.live_http = 200; s.pair.live_ok = true;
    s.phone.st.valid = true; s.phone.st.state = PH_AUTHORIZED; s.phone.st.flags = PHONE_FLAG_REQUIRED | PHONE_FLAG_HOME;
    s.tile = home_tile_index(HELPER);
    home_render(&s, px, SPARKLES_PIXELS);
    snprintf(path, sizeof path, "%s/home-ask-tile.ppm", dir);
    ok &= save_page(path, px);
  }
  printf("bot_preview: %d looks x %u moods (+ mirrored, compact), %u animation strips -> %s\n", BOT_LOOKS,
         (unsigned)(sizeof moods / sizeof moods[0]), (unsigned)(2 * sizeof anim / sizeof anim[0]), dir);
  return ok ? 0 : 1;
}
