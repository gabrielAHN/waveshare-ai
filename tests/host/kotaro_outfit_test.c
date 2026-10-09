/* Kotaro's clothes and his drawing box (SPEC3 Contract K):
 *  - a shirt (tee, striped tee, hoodie) is worn round his body like a dog shirt: it covers his chest,
 *    shoulders and back (to just before the tail) and down over his belly, with a curved hem; the front
 *    legs come out of it (the shirt overlaps their tops); stripes run round him; the hem / back edge is
 *    the darker cloth; prints stay on the chest; the hood lies on the back of his neck; the collar is a
 *    band that follows his neck; no cloth ever lands on his head, ears, eyes, tail, hind leg or paws;
 *  - every pixel he draws stays inside helper_bot_box, and no part of him reaches the sprite grid's edge
 *    (where it would be cut off), in every look, mood, layout, mirrored, at many timestamps -- and with
 *    the new animations (routines, nap, celebrate, tap, lean-in, nod, mood blends) where they exist;
 *  - the boxes clear the chat clip line, the status row, the bottom gesture band and the panel edge.
 * Written against the sprite grid (bot_grid_get) so it also runs on the old art (070118f: RED). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "home_render.h"

#ifndef BOT_GY /* 070118f: no headroom rows, the shirt was painted on the torso part, no raised hind leg */
#define BOT_GY 0
#define BP_SHIRT BP_TORSO
#define BP_KICK BP_HAUNCH
#define KOTARO_OLD 1
#endif

static uint16_t fr[SPARKLES_PIXELS];
static int fails;
/* Count every failure; print the first three of each check (so one broken rule does not hide another). */
#define CHECK(cond, ...)                                     \
  do {                                                       \
    if (!(cond)) {                                           \
      static int shown_;                                     \
      if (shown_++ < 3) fprintf(stderr, "FAIL: " __VA_ARGS__); \
      fails++;                                               \
    }                                                        \
  } while (0)

static bool shirt_wear(int look) {
  int w = bot_outfits[look].wear;
  return w == BOT_WEAR_TEE || w == BOT_WEAR_STRIPED_TEE || w == BOT_WEAR_HOODIE;
}
static bool cloth(uint8_t c) { return c >= BX_CLOTH && c <= BX_CLOTH2_SH; }
static bool body(uint8_t p) { return p == BP_TORSO || p == BP_SHIRT; }
static bool leg(uint8_t p) { return p == BP_LEGL || p == BP_LEGR; }
static void pose_at(bot_pose *b, int look, bot_mood mood, bool compact, float t, float mt, bool mirror) {
  memset(b, 0, sizeof *b);
  helper_view h;
  memset(&h, 0, sizeof h);
  h.chat = compact;
  helper_bot_place(&h, &b->cx, &b->cy, &b->u);
  b->look = look; b->mood = mood; b->t = t; b->mood_t = mt; b->level = bot_frac(t * .7f);
  b->compact = compact; b->shadow = !compact; b->grey = mood == BOT_SLEEP; b->stop_badge = mood == BOT_WORK;
  b->gaze = mirror ? 1.f : 0.f;
}
static const bot_grid *draw(const bot_pose *b) {
  for (int i = 0; i < SPARKLES_PIXELS; i++) fr[i] = 0x0841;
  bot_art_draw(fr, b);
  return bot_grid_get();
}

/* ---- the clothes ---------------------------------------------------------------------------------- */
typedef struct { double cover, back, belly; int hem_dx, hem_dy, sleeves_ok, stripes_round; } shirt_stats;
static int checked_frames;
/* One frame of a shirt: the measures above, and the rules that hold in every frame. `sitting` = a plain
 * sitting pose (the coverage measures apply). */
static shirt_stats check_shirt(const bot_grid *g, int look, bool sitting, const char *what) {
  shirt_stats st = {0};
  int wear = bot_outfits[look].wear;
  long n = 0, sx = 0, sy = 0;
  for (int y = 0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++)
      if (body(g->p[y][x])) { n++; sx += x; sy += y; }
  if (!n) return st;
  double cx = (double)sx / n, cy = (double)sy / n;
  int all = 0, cl = 0, back = 0, back_cl = 0, belly = 0, belly_cl = 0;
  int hx0 = 99, hx1 = -1, hy0 = 99, hy1 = -1;
  for (int y = 0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++) {
      uint8_t p = g->p[y][x], c = g->c[y][x];
      /* no cloth on his head, ears (eyes are on the head), tail, hind leg or paws */
      if (cloth(c))
        CHECK(body(p) || leg(p) || p == BP_HOOD, "%s: cloth on part %d at %d,%d\n", what, p, x, y);
      if (leg(p) && cloth(c)) {
        int bottom = y;
        while (bottom + 1 < BOT_GH && g->p[bottom + 1][x] == p) bottom++;
        CHECK(bottom - y >= 3, "%s: cloth on a paw at %d,%d\n", what, x, y);
      }
      if (!body(p) || c == BX_LINE) continue;
      all++; cl += cloth(c);
      if (x > cx && y < cy) { back++; back_cl += cloth(c); }
      if (x < cx && y > cy) { belly++; belly_cl += cloth(c); }
      /* the hem: cloth next to bare body fur */
      static const int8_t d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      if (cloth(c))
        for (int k = 0; k < 4; k++) {
          int nx = x + d4[k][0], ny = y + d4[k][1];
          if (nx < 0 || ny < 0 || nx >= BOT_GW || ny >= BOT_GH) continue;
          if (body(g->p[ny][nx]) && !cloth(g->c[ny][nx]) && g->c[ny][nx] != BX_LINE) {
            if (x < hx0) hx0 = x;
            if (x > hx1) hx1 = x;
            if (y < hy0) hy0 = y;
            if (y > hy1) hy1 = y;
          }
        }
    }
  st.cover = all ? (double)cl / all : 0;
  st.back = back ? (double)back_cl / back : 0;
  st.belly = belly ? (double)belly_cl / belly : 0;
  st.hem_dx = hx1 - hx0; st.hem_dy = hy1 - hy0;
  /* sleeves: each front leg's top row is cloth (the legs come out of the shirt) */
  st.sleeves_ok = 1;
  for (int part = BP_LEGL; part <= BP_LEGR; part++) {
    int top = -1, top_cl = 0, top_n = 0;
    for (int y = 0; y < BOT_GH && top < 0; y++)
      for (int x = 0; x < BOT_GW; x++)
        if (g->p[y][x] == part) top = y;
    if (top < 0) { st.sleeves_ok = 0; continue; }
    for (int x = 0; x < BOT_GW; x++)
      if (g->p[top][x] == part && g->c[top][x] != BX_LINE) { top_n++; top_cl += cloth(g->c[top][x]); }
    if (!top_n || top_cl * 2 < top_n) st.sleeves_ok = 0;
  }
  /* stripes run round him: stripe rows reach from his chest to his back */
  if (wear == BOT_WEAR_STRIPED_TEE)
    for (int y = 0; y < BOT_GH; y++) {
      int s0 = 99, s1 = -1, b1 = -1;
      for (int x = 0; x < BOT_GW; x++) {
        if (body(g->p[y][x])) b1 = x;
        if (body(g->p[y][x]) && (g->c[y][x] == BX_CLOTH2 || g->c[y][x] == BX_CLOTH2_SH)) {
          if (x < s0) s0 = x;
          s1 = x;
        }
      }
      if (s1 >= 0 && s1 - s0 >= 8 && s1 >= b1 - 5) st.stripes_round++;
    }
  /* the back edge of each shirt row is shaded (darker cloth or the outline) */
  if (sitting)
    for (int y = 0; y < BOT_GH; y++) {
      int last = -1;
      for (int x = 0; x < BOT_GW; x++)
        if (body(g->p[y][x]) && cloth(g->c[y][x])) last = x;
      if (last < 0) continue;
      uint8_t c = g->c[y][last];
      bool edge = last + 1 < BOT_GW && g->c[y][last + 1] == BX_LINE;  /* or the outline right after it */
      CHECK(edge || c == BX_CLOTH_SH || c == BX_CLOTH_DK || c == BX_CLOTH2_SH, "%s: shirt row %d ends in a light colour %d\n", what, y, c);
    }
  /* prints: on the shirt, on his chest */
  if (wear != BOT_WEAR_STRIPED_TEE)
    for (int y = 0; y < BOT_GH; y++)
      for (int x = 0; x < BOT_GW; x++)
        if (g->c[y][x] == BX_CLOTH2 || g->c[y][x] == BX_CLOTH2_SH)
          CHECK(body(g->p[y][x]) && x >= 8 && x <= 27, "%s: a print off the chest at %d,%d\n", what, x, y);
  return st;
}
static void hood_and_collar(const bot_grid *g, int look, bool sitting, const char *what) {
  int wear = bot_outfits[look].wear;
  long hn = 0, hsx = 0, hsy = 0, hen = 0, hex = 0, hbot = -1;
  for (int y = 0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++) {
      uint8_t p = g->p[y][x];
      if (p == BP_HEAD) { hen++; hex += x; if (y > hbot) hbot = y; }
      if (p == BP_HOOD) { hn++; hsx += x; hsy += y; }
    }
  if (wear == BOT_WEAR_HOODIE) {
    /* the hood lies on the back of his neck: behind (right of) his head's middle, at its lower edge */
    CHECK(!sitting || hn >= 12, "%s: hood has %ld cells\n", what, hn);
    if (hn && hen) {  /* (bending or lying down, his head and ear may hide it: behind his head is enough) */
      double hx = (double)hsx / hn, hy = (double)hsy / hn, headx = (double)hex / hen;
      CHECK(hx >= headx + 5 && (!sitting || (hy >= hbot - 6 && hy <= hbot + 3)), "%s: hood at %.1f,%.1f (head x %.1f, bottom %ld)\n", what, hx, hy, headx, hbot);
    }
  } else
    CHECK(hn == 0, "%s: a hood without a hoodie\n", what);
  if (wear == BOT_WEAR_COLLAR) {
    /* a band round his neck: right under his head, across most of the neck, not a flat rectangle */
    int n = 0, x0 = 99, x1 = -1, y0 = 99, y1 = -1;
    for (int y = 0; y < BOT_GH; y++)
      for (int x = 0; x < BOT_GW; x++) {
        if (!(body(g->p[y][x]) && cloth(g->c[y][x]))) continue;
        bool near = false;
        for (int dy = -3; dy <= 0 && !near; dy++)
          for (int dx = -2; dx <= 2; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < BOT_GW && ny < BOT_GH && (g->p[ny][nx] == BP_HEAD || g->p[ny][nx] == BP_HOOD)) { near = true; break; }
          }
        if (g->c[y][x] == BX_CLOTH) CHECK(near, "%s: collar cell %d,%d not on his neck\n", what, x, y);
        n++;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    CHECK(!sitting || (n >= 10 && x1 - x0 >= 9), "%s: collar %d cells over %d columns\n", what, n, x1 - x0);
  }
}
static void clothes(void) {
  static const bot_mood moods[] = {BOT_IDLE, BOT_LISTEN, BOT_THINK, BOT_WORK, BOT_STOPPING, BOT_HAPPY, BOT_SAD, BOT_STOPPED, BOT_SLEEP, BOT_LOADING};
  double min_cover = 1, min_back = 1, min_belly = 1;
  int min_hdx = 99, min_hdy = 99, sleeves_bad = 0, stripe_min = 99, frames = 0;
  for (int look = 0; look < BOT_LOOKS; look++)
    for (int compact = 0; compact < 2; compact++)
      for (int mirror = 0; mirror < 2; mirror++)
        for (size_t k = 0; k < sizeof moods / sizeof *moods; k++)
          for (int f = 0; f < 9; f++) {
            bot_pose b;
            pose_at(&b, look, moods[k], compact, .3f + f * .911f, f * .45f, mirror);
            const bot_grid *g = draw(&b);
            char what[96];
            snprintf(what, sizeof what, "look=%d compact=%d mirror=%d mood=%d f=%d", look, compact, mirror, (int)moods[k], f);
            hood_and_collar(g, look, true, what);
            if (!shirt_wear(look)) {
              for (int y = 0; y < BOT_GH; y++)
                for (int x = 0; x < BOT_GW; x++)
                  if (cloth(g->c[y][x])) CHECK(bot_outfits[look].wear == BOT_WEAR_COLLAR && body(g->p[y][x]), "%s: cloth without a shirt at %d,%d\n", what, x, y);
              continue;
            }
            shirt_stats s = check_shirt(g, look, true, what);
            if (s.cover < min_cover) min_cover = s.cover;
            if (s.back < min_back) min_back = s.back;
            if (s.belly < min_belly) min_belly = s.belly;
            if (s.hem_dx < min_hdx) min_hdx = s.hem_dx;
            if (s.hem_dy < min_hdy) min_hdy = s.hem_dy;
            sleeves_bad += !s.sleeves_ok;
            if (bot_outfits[look].wear == BOT_WEAR_STRIPED_TEE && s.stripes_round < stripe_min) stripe_min = s.stripes_round;
            frames++;
          }
  printf("shirts over %d frames: cover >= %.2f, back >= %.2f, belly >= %.2f, hem spans >= %dx%d cells, legs out of the shirt in %d/%d, stripes round him >= %d rows\n",
         frames, min_cover, min_back, min_belly, min_hdx, min_hdy, frames - sleeves_bad, frames, stripe_min);
  CHECK(min_cover >= .60, "a shirt covers only %.2f of his body\n", min_cover);
  CHECK(min_back >= .60, "a shirt covers only %.2f of his back\n", min_back);
  CHECK(min_belly >= .55, "a shirt covers only %.2f of his belly\n", min_belly);
  CHECK(min_hdx >= 4 && min_hdy >= 4, "the hem is a straight edge (%dx%d)\n", min_hdx, min_hdy);
  CHECK(sleeves_bad == 0, "%d frames: the front legs do not come out of the shirt\n", sleeves_bad);
  CHECK(stripe_min >= 2, "stripes do not run round him (%d rows)\n", stripe_min);
  checked_frames += frames;
}
#ifndef KOTARO_OLD
/* The new animations keep the clothes on: every rule but the sitting coverage, in every frame. */
static void clothes_in_motion(void) {
  int frames = 0;
  for (int look = 0; look < BOT_LOOKS; look++)
    for (int mirror = 0; mirror < 2; mirror++)
      for (int f = 0; f < 24; f++) {
        bot_pose v[8];
        for (int i = 0; i < 8; i++) pose_at(&v[i], look, BOT_IDLE, f & 1, 1.f + f * .37f, 9.f, mirror);
        for (int r = 1; r < BOT_RT_N && r <= 5; r++) { v[r - 1].routine = r; v[r - 1].routine_s = bot_routine_len[r] * (f + .5f) / 24.f; }
        v[5].idle_s = BOT_NAP_S + f * .15f;
        v[6].wake_s = f * BOT_WAKE_S / 24.f; v[6].wake_from = 1;
        v[7].mood = BOT_HAPPY; v[7].mood_t = f * .07f; v[7].cheer_s = .01f + f * BOT_CHEER_S / 24.f;
        for (int i = 0; i < 8; i++) {
          const bot_grid *g = draw(&v[i]);
          char what[96];
          snprintf(what, sizeof what, "motion look=%d mirror=%d anim=%d f=%d", look, mirror, i, f);
          hood_and_collar(g, look, false, what);
          if (shirt_wear(look)) {
            shirt_stats s = check_shirt(g, look, false, what);
            CHECK(s.cover >= .3, "%s: the shirt covers %.2f\n", what, s.cover);
          }
          frames++;
        }
      }
  printf("clothes stay on through routines, nap, wake and celebrate: %d frames\n", frames);
  checked_frames += frames;
}
#endif

/* ---- the box: never cut off ------------------------------------------------------------------------ */
static int box_frames;
static void inside(const bot_pose *b, const char *what) {
  const bot_grid *g = draw(b);
  helper_view h;
  memset(&h, 0, sizeof h);
  h.chat = b->compact;
  helper_box bx = helper_bot_box(&h);
  int out = 0, ox = 0, oy = 0;
  for (int y = 0; y < 448; y++)
    for (int x = 0; x < 368; x++)
      if (fr[y * 368 + x] != 0x0841 && (x < bx.x0 || x >= bx.x1 || y < bx.y0 || y >= bx.y1)) { out++; ox = x; oy = y; }
  CHECK(!out, "%s: %d px outside the box, e.g. %d,%d\n", what, out, ox, oy);
  /* every outlined part (and its outline + rim) fits on the grid */
  int edge = 0;
  for (int y = 0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++) {
      uint8_t p = g->p[y][x];
      if (p == BP_NONE || p >= BP_FXN) continue;
      if (x < 2 || y < 2 || x > BOT_GW - 3 || y > BOT_GH - 3) edge++;
    }
  CHECK(!edge, "%s: %d cells of him at the sprite edge (cut off)\n", what, edge);
  /* the listening glow and its pulse ring are whole rings (never cut flat at the sprite edge) */
  int ring = 0;
  for (int y = 0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++)
      if ((g->c[y][x] == BX_HALO || g->c[y][x] == BX_HALO2) && (x == 0 || y == 0 || x == BOT_GW - 1 || y == BOT_GH - 1)) ring++;
  CHECK(!ring, "%s: the listening ring is cut at the sprite edge (%d cells)\n", what, ring);
#ifndef KOTARO_OLD
  CHECK(!g->clipped, "%s: %d cells fell off the sprite grid\n", what, g->clipped);
#endif
  box_frames++;
}
static void never_cut_off(void) {
  static const bot_mood moods[] = {BOT_IDLE, BOT_LISTEN, BOT_THINK, BOT_WORK, BOT_STOPPING, BOT_HAPPY, BOT_SAD, BOT_STOPPED, BOT_SLEEP, BOT_LOADING};
  for (int compact = 0; compact < 2; compact++)
    for (int look = 0; look < BOT_LOOKS; look++)
      for (int mirror = 0; mirror < 2; mirror++)
        for (size_t k = 0; k < sizeof moods / sizeof *moods; k++)
          for (int f = 0; f < 16; f++) {
            bot_pose b;
            pose_at(&b, look, moods[k], compact, f * .53f + .11f * look, f * .29f, mirror);
            char what[96];
            snprintf(what, sizeof what, "compact=%d look=%d mirror=%d mood=%d t=%.2f", compact, look, mirror, (int)moods[k], b.t);
            inside(&b, what);
          }
#ifndef KOTARO_OLD
  /* the new animations, every phase; mood blends between every pair */
  for (int compact = 0; compact < 2; compact++)
    for (int mirror = 0; mirror < 2; mirror++)
      for (int f = 0; f <= 30; f++) {
        float u = f / 30.f;
        char what[96];
        for (int r = 1; r < BOT_RT_N; r++) {
          bot_pose b;
          pose_at(&b, f % BOT_LOOKS, BOT_IDLE, compact, 2.f + f * .21f, 20.f, mirror);
          b.routine = r; b.routine_s = u * bot_routine_len[r];
          snprintf(what, sizeof what, "routine=%d s=%.2f compact=%d mirror=%d", r, b.routine_s, compact, mirror);
          inside(&b, what);
        }
        bot_pose b;
        pose_at(&b, f % BOT_LOOKS, BOT_HAPPY, compact, 3.f + f * .1f, u * BOT_CHEER_S, mirror);
        b.cheer_s = .001f + u * BOT_CHEER_S;
        snprintf(what, sizeof what, "celebrate s=%.2f compact=%d", b.cheer_s, compact);
        inside(&b, what);
        pose_at(&b, f % BOT_LOOKS, f & 1 ? BOT_IDLE : BOT_SAD, compact, 3.f + f * .43f, 30.f, mirror);
        b.idle_s = BOT_NAP_S + u * (BOT_LIE_S + 6.f);
        snprintf(what, sizeof what, "nap idle=%.2f compact=%d", b.idle_s, compact);
        inside(&b, what);
        b.idle_s = 0; b.wake_s = .001f + u * BOT_WAKE_S; b.wake_from = 1;
        snprintf(what, sizeof what, "wake s=%.2f compact=%d", b.wake_s, compact);
        inside(&b, what);
        pose_at(&b, f % BOT_LOOKS, BOT_IDLE, compact, 3.f + f * .43f, 30.f, mirror);
        b.tap_s = .001f + u * BOT_TAP_S; b.lean = u; b.nod_s = u * BOT_NOD_S;
        snprintf(what, sizeof what, "tap/lean/nod u=%.2f compact=%d", u, compact);
        inside(&b, what);
        for (int a = 0; a <= BOT_LOADING; a++)
          for (int z = 0; z <= BOT_LOADING; z += 3) {
            pose_at(&b, (a + z) % BOT_LOOKS, (bot_mood)z, compact, 3.f + f * .43f, u * BOT_BLEND_S, mirror);
            b.blend = true; b.from = (bot_mood)a; b.from_t = 2.f + a; b.blend_t = u * BOT_BLEND_S;
            snprintf(what, sizeof what, "blend %d->%d u=%.2f compact=%d", a, z, u, compact);
            inside(&b, what);
          }
      }
#endif
  /* the boxes clear the page around them */
  helper_view wv, cv;
  memset(&wv, 0, sizeof wv); memset(&cv, 0, sizeof cv); cv.chat = true;
  helper_box wb = helper_bot_box(&wv), cb = helper_bot_box(&cv);
  CHECK(wb.x0 >= 0 && wb.x1 <= 368 && cb.x0 >= 0 && cb.x1 <= 368, "box off the panel\n");
  CHECK(wb.y0 > HELPER_DOTS_Y + 8 && wb.y1 < HELPER_BOT_TEXT_Y, "welcome box %d..%d hits the dots or the text\n", wb.y0, wb.y1);
  CHECK(cb.y0 >= HELPER_STATUS_Y + 14 && cb.y0 >= HELPER_CHAT_CLIP_BOTTOM && cb.y1 <= 420, "chat box %d..%d hits the chat, the status row or the bottom band\n", cb.y0, cb.y1);
  /* the Home Ask tile: one plain Kotaro inside its 192 px icon box, on and closed */
  for (int off = 0; off < 2; off++) {
    for (int i = 0; i < SPARKLES_PIXELS; i++) fr[i] = 0x0841;
    home_bots_tile(fr, 0, off ? HOME_BOT_TILE_OFF : HOME_BOT_TILE_ON, .6f);
    int out = 0;
    for (int y = 0; y < 448; y++)
      for (int x = 0; x < 368; x++)
        if (fr[y * 368 + x] != 0x0841 && (x < 88 || x >= 280 || y < 108 || y >= 300)) out++;
    CHECK(!out, "Home tile (off=%d): %d px outside its icon box\n", off, out);
  }
  printf("never cut off: %d frames inside the bot box with room on the sprite grid; boxes clear the chat, status row, dots, text and bottom band\n", box_frames);
}

int main(void) {
  clothes();
#ifndef KOTARO_OLD
  clothes_in_motion();
#endif
  never_cut_off();
  if (fails) {
    fprintf(stderr, "kotaro_outfit: %d failures\n", fails);
    return 1;
  }
  printf("kotaro_outfit: shirts worn round him (%d frames), hood on his neck, collar on his neck, nothing cut off: PASS\n", checked_frames);
  return 0;
}
