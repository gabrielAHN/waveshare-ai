#pragma once
/* The Ask bots are Kotaro -- an apricot toy poodle -- as a pixel-art sticker, in the style of a cute
 * 3/4-view pixel puppy ( 1-cell black outline round every part and
 * between overlapping parts, a white sticker rim, cream fur with a tan band on the far side and rose
 * shade on the lower/right edges, big black eyes with a white glint, brow dots, a nose glint). There is
 * no mic button: the dog IS the hold-to-talk control (helper_ui.h helper_bot_box / helper_bot_hit is its
 * touch box; every pixel drawn here stays inside it, tests/host/bot_graphic_test.c, tests/host/kotaro_motion_test.c).
 *
 * Drawn procedurally (no bitmaps) into a fixed 45 x 48 cell sprite grid -- shapes, then band shading,
 * decals, outline and rim passes -- and blown up by whole screen pixels per cell (4 on the Ask page,
 * 2 in the chat row and 4 on the Home tile), so cells stay crisp. The top BOT_GY rows are headroom
 * (ears up, a bounce, the thought cloud, a heart), so nothing he does is cut off at the grid edge.
 *
 * Kotaro (after his photos): a big round apricot head turned a little to the left, a cream muzzle, a
 * soft round far ear peeking out behind his cheek and a floppy near ear; sitting in 3/4 view with a
 * round chest, two front legs coming straight out of it, his back and rump to the right with the hind
 * thigh in front, and a pompom tail up behind; no tongue. Only the outfit changes, one per bot
 * (bot_outfits below -- append a row there to dress a new bot, see plugins/hermes/KOTARO.md):
 *   helper = his yellow-and-white striped t-shirt,
 *   coding = a coral hoodie with a cream </>,
 *   any other id = a blue collar and tag,
 *   plain = no clothes at all (the Home tile's Kotaro),
 *   your own bots = rows in the git-ignored bot_outfits_local.h (see bot_outfits_local.example.h).
 * A shirt is worn like a dog shirt: it wraps his chest, shoulders and back to just before the tail and
 * down over his belly with a curved hem, the front legs come out of it, stripes run round him; the hood
 * lies on the back of his neck; the collar is a band round his neck.
 *
 * Motion (bot_animate: a pure function of the pose and its clocks, all from real elapsed time). Whole-
 * body jumps move the sprite in screen pixels (eased); his parts move by whole cells (pixel-art frames);
 * he turns round through squashed turn frames (never an instant mirror flip); a mood switch eases from
 * the old mood's pose into the new one. Moods: idle breathes, blinks, sways his tail (a wag burst now and
 * then), glances, flicks an ear, and between quiet stretches plays a routine (helper_ui.h schedules them:
 * stretch = play bow, yawn, scratch with a hind leg, sniff the ground and turn round, chase his tail);
 * after 2 minutes with nothing going on he lies down for a nap (eyes shut, slow breaths, Zz) and wakes
 * with a stretch; listening perks the ears, opens the mouth with the voice level and glows round the
 * head; thinking / working (Hermes on it) rocks under a thought cloud whose dots fill in one by one;
 * stopping greys the dots; stopped looks sleepy; done celebrates (a jump and a spin, then the happy wag);
 * error is sad (droopy ears, sad brows, a tear, tail down); disabled sleeps (Zz) in greys; loading
 * dozes in colour (eyes shut, a slow nod, Zz). A tap on him: ears perk, a little bounce and a heart; the hold arming: he leans in
 * (ears up, head down to listen); a command sent: a nod. */
#include <math.h>
#include <string.h>
#include "soft_shapes.h"
#include "helper_ui.h"

/* A look = a row of bot_outfits (the index). The first four rows are fixed (named below); your own
 * rows come after them and need only their id string (bot_look_of finds it). */
enum { BOT_LOOK_HELPER, BOT_LOOK_CODING, BOT_LOOK_GENERIC, BOT_LOOK_PLAIN };
typedef int bot_look;
typedef enum { BOT_IDLE, BOT_LISTEN, BOT_THINK, BOT_WORK, BOT_STOPPING, BOT_HAPPY, BOT_SAD, BOT_STOPPED, BOT_SLEEP, BOT_LOADING } bot_mood;
typedef struct {
  bot_look look;
  bot_mood mood;
  float cx, cy, u;   /* body centre (px) and scale (1 = a BOT_BODY_W px body) */
  float t, mood_t;   /* page clock / seconds in this mood */
  float level;       /* listening: voice level 0..1 */
  float gaze;        /* > 0: he faces right (mirrored); turning happens through turn frames */
  bool talking, stop_badge, grey, shadow, compact;
  /* His life between commands (helper_ui.h helper_kotaro); all zero = sitting at rest (Home tile). */
  bool blend;        /* easing out of mood `from` (its clock from_t) for BOT_BLEND_S; blend_t in */
  bot_mood from;
  float from_t, blend_t;
  int routine;       /* an idle routine (BOT_RT_*), routine_s seconds in */
  float routine_s;
  float idle_s, wake_s, wake_from;    /* nap after BOT_NAP_S quiet; waking from a nap wake_from deep */
  float cheer_s, tap_s, lean, nod_s;  /* celebrate, tap reaction, hold lean-in 0..1, the send nod */
  float breath_s;    /* seconds he has been still: breathing starts from rest (never meets another move) */
} bot_pose;

/* ---- outfits: Kotaro is the same dog for every bot, only his clothes change ---------------------
 * One row per bot. id = the bot's id (its Hermes profile name, as the bridge lists it); NULL = a row
 * that is only picked by index (the "any other bot" row and the plain one). Keep the first four rows
 * in place (their indices are the BOT_LOOK_* names). Your own bots go in bot_outfits_local.h (git-ignored,
 * copy bot_outfits_local.example.h): its BOT_OUTFITS_LOCAL rows are appended after them.
 * wear: what he has on -- a t-shirt (optionally striped), a hoodie (a hood on the back of his neck), a
 *   collar, or nothing. Shirts are worn round his body (chest, shoulders, back, belly; curved hem).
 * pal: body = main cloth, deep = its shade, emblem = trims / hem / dark prints, light = stripes and
 *   light prints. dot_deep: the thought-cloud dots use `emblem` instead of `body` (pale outfits).
 * print[]: up to two chest prints, tiny bitmaps in sprite cells ('#' = a cell, '.' = none) at (x, y),
 *   inked in `light` (BOT_INK_LIGHT) or `emblem` (BOT_INK_EMBLEM). The chest is about x 10..27,
 *   y 21..33 of the 45 x 44 body grid (the head covers it above y 21); prints land on cloth only. */
typedef struct { uint8_t body[3], deep[3], emblem[3], light[3]; } bot_palette;
enum { BOT_WEAR_NONE, BOT_WEAR_TEE, BOT_WEAR_STRIPED_TEE, BOT_WEAR_HOODIE, BOT_WEAR_COLLAR };
enum { BOT_INK_LIGHT, BOT_INK_EMBLEM };
typedef struct { int8_t x, y; uint8_t ink, rows; const char *const *bits; } bot_print;
typedef struct {
  const char *id;
  uint8_t wear;
  bool dot_deep;
  bot_palette pal;
  bot_print print[2];
} bot_outfit;
/* Prints any outfit may use (also from bot_outfits_local.h): hoodie drawstrings, a front panel, </>, a tag. */
static const char *const bot_print_strings[3] = {"#.....#", "#.....#", "#.....#"};
static const char *const bot_print_panel[4] = {".############.", "#............#", "#............#", "#............#"};
static const char *const bot_print_code[5] = {"..#...#.#..", ".#...#...#.", "#....#....#", ".#..#....#.", "..#.#...#.."};
static const char *const bot_print_tag[3] = {".##.", "####", ".##."};
/* Your own bots' outfits, kept out of the repo: plugins/hermes/firmware/bot_outfits_local.h (git-ignored). */
#if !defined(BOT_NO_LOCAL_OUTFITS) && defined(__has_include)
#if __has_include("bot_outfits_local.h")
#include "bot_outfits_local.h"
#endif
#endif
#ifndef BOT_OUTFITS_LOCAL
#define BOT_OUTFITS_LOCAL
#endif
static const bot_outfit bot_outfits[] = {
    /* helper: a mustard-yellow and white striped t-shirt */
    {"helper", BOT_WEAR_STRIPED_TEE, true, {{236, 186, 58}, {204, 150, 40}, {170, 118, 30}, {252, 249, 240}}, {{0}}},
    /* coding: a coral hoodie with a cream </> */
    {"coding", BOT_WEAR_HOODIE, false, {{242, 128, 112}, {214, 96, 86}, {170, 60, 64}, {255, 240, 222}},
     {{13, 26, BOT_INK_LIGHT, 5, bot_print_code}}},
    /* any other bot: a blue collar with a light-blue round tag */
    {NULL, BOT_WEAR_COLLAR, false, {{56, 98, 168}, {40, 72, 130}, {30, 52, 100}, {150, 196, 240}},
     {{17, 25, BOT_INK_LIGHT, 3, bot_print_tag}}},
    /* plain: just Kotaro (the Home tile); the cloth colour only tints the thought-cloud dots */
    {NULL, BOT_WEAR_NONE, false, {{226, 150, 96}, {196, 120, 74}, {150, 90, 60}, {252, 240, 224}}, {{0}}},
    BOT_OUTFITS_LOCAL  /* your bots (bot_outfits_local.h), after the fixed rows */
};
#define BOT_LOOKS ((int)(sizeof bot_outfits / sizeof bot_outfits[0]))
static inline const bot_outfit *bot_outfit_of(bot_look look) {
  return &bot_outfits[look >= 0 && look < BOT_LOOKS ? look : BOT_LOOK_GENERIC];
}
/* The look for a bot id: its own row, else the "any other bot" collar. */
static inline bot_look bot_look_of(const char *id) {
  if (!id) return BOT_LOOK_GENERIC;
  for (int i = 0; i < BOT_LOOKS; i++)
    if (bot_outfits[i].id && !strcmp(id, bot_outfits[i].id)) return i;
  return BOT_LOOK_GENERIC;
}
static inline bool bot_wears_shirt(uint8_t wear) {
  return wear == BOT_WEAR_TEE || wear == BOT_WEAR_STRIPED_TEE || wear == BOT_WEAR_HOODIE;
}
#define BOT_RIM sp_pack_lcd(255, 252, 247)
#define BOT_STOP sp_pack_lcd(238, 112, 28)
#define BOT_WHITE sp_pack_lcd(255, 255, 255)
typedef struct {
  uint16_t line, fur_hi, fur, tan, rose, muzzle, eye, white, nose_hi, mouth, blush, tear;
  uint16_t cloth, cloth_sh, cloth_dk, cloth2, cloth2_sh, dot, dim, shadow, halo, halo2, heart, star;
} bot_colors;
/* Disabled = the same drawing in warm greys (luma, squeezed into the light half). */
static inline uint16_t bot_rgb(bool grey, unsigned r, unsigned g, unsigned b) {
  if (!grey) return sp_pack_lcd(r, g, b);
  unsigned l = 92 + ((r * 77 + g * 150 + b * 29) >> 8) * 5 / 8;
  return sp_pack_lcd(l + 4, l, l - 6);
}
static inline uint16_t bot_rgb3(bool grey, const uint8_t c[3]) { return bot_rgb(grey, c[0], c[1], c[2]); }
static inline bot_colors bot_colors_of(const bot_pose *b) {
  const bot_outfit *dress = bot_outfit_of(b->look);
  const bot_palette *pal = &dress->pal;
  bool g = b->grey;
  bot_colors c;
  /* his coat: apricot, a cream muzzle, tan on the far side, rose on the lower / right edges */
  c.line = bot_rgb(g, 32, 22, 22); c.fur_hi = bot_rgb(g, 253, 238, 214); c.fur = bot_rgb(g, 246, 214, 172);
  c.tan = bot_rgb(g, 226, 172, 126); c.rose = bot_rgb(g, 186, 120, 98); c.muzzle = bot_rgb(g, 253, 243, 228);
  c.eye = bot_rgb(g, 32, 22, 22); c.white = bot_rgb(g, 255, 255, 255); c.nose_hi = bot_rgb(g, 176, 196, 226);
  c.mouth = bot_rgb(g, 122, 40, 46); c.blush = bot_rgb(g, 246, 150, 150); c.tear = bot_rgb(g, 120, 190, 245);
  c.cloth = bot_rgb3(g, pal->body); c.cloth_sh = bot_rgb3(g, pal->deep); c.cloth_dk = bot_rgb3(g, pal->emblem);
  c.cloth2 = bot_rgb3(g, pal->light); c.cloth2_sh = bot_rgb(g, pal->light[0] * 88 / 100, pal->light[1] * 86 / 100, pal->light[2] * 82 / 100);
  /* the busy blocks pop on the warm orbs: the outfit colour (helper: its deep yellow) */
  c.dot = dress->dot_deep ? c.cloth_dk : c.cloth;
  c.dim = bot_rgb(g, 214, 186, 150);
  c.shadow = g ? sp_pack_lcd(190, 186, 180) : sp_pack_lcd(236, 166, 106);
  c.halo = sp_pack_lcd(255, 216, 164); c.halo2 = sp_pack_lcd(255, 200, 140);
  c.heart = bot_rgb(g, 240, 92, 116); c.star = bot_rgb(g, 255, 206, 64);
  return c;
}
static inline float bot_clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float bot_frac(float v) { return v - floorf(v); }
static inline float bot_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float bot_smooth(float v) { v = bot_clamp01(v); return v * v * (3 - 2 * v); }
static inline int bot_ri(float v) { return (int)floorf(v + .5f); }
/* 0 -> 1 -> 0 over [a, d]: eases up over [a, b], holds, eases down over [c, d]. */
static inline float bot_env(float s, float a, float b, float c, float d) {
  if (s <= a || s >= d) return 0;
  if (s < b) return bot_smooth((s - a) / (b - a));
  if (s > c) return 1 - bot_smooth((s - c) / (d - c));
  return 1;
}
static inline float bot_blink(const bot_pose *b) {
  float ph = fmodf(b->t + (float)b->look * 1.37f, 3.7f);
  return ph < .18f ? fabsf(ph / .09f - 1.f) : 1.f;
}
static inline int bot_step(float v, int n) { int k = (int)floorf(v); k %= n; return k < 0 ? k + n : k; }
#define BOT_PI 3.14159265f

/* ---- the sprite grid ---------------------------------------------------------------------------- */
#define BOT_GW 45
#define BOT_GH 48
#define BOT_GY 4  /* headroom rows above his head: art row y is grid row y + BOT_GY */
#define BOT_GROUND 41  /* the lowest art row a part may use (outline and rim go below it) */
enum { BX_NONE, BX_LINE, BX_RIM, BX_FUR_HI, BX_FUR, BX_TAN, BX_ROSE, BX_MUZZLE, BX_EYE, BX_WHITE, BX_NOSE_HI, BX_MOUTH,
       BX_BLUSH, BX_TEAR, BX_CLOTH, BX_CLOTH_SH, BX_CLOTH_DK, BX_CLOTH2, BX_CLOTH2_SH, BX_FX, BX_DOT, BX_DIM, BX_SHADOW,
       BX_HALO, BX_HALO2, BX_HEART, BX_STAR, BX_N };
/* Parts in drawing (z) order: a later part is in front; the outline goes on the part behind. The shirt
 * is its own part over the torso (no seam between them: its hem is a darker cloth row). */
enum { BP_NONE, BP_FXB, BP_TAIL, BP_TORSO, BP_SHIRT, BP_HAUNCH, BP_HOOD, BP_KICK /* a hind leg raised (scratching) */,
       BP_EARF, BP_LEGL, BP_LEGR, BP_HEAD, BP_EARN, BP_FXF, BP_FXN /* effects without an outline */, BP_PARTS, BP_LINE = 254,
       BP_RIM = 255 };
static inline bool bot_outlined(unsigned p) { return p >= BP_FXB && p <= BP_FXF; }
static inline bool bot_cloth(uint8_t c) { return c >= BX_CLOTH && c <= BX_CLOTH2_SH; }
static inline bool bot_fur(uint8_t c) { return c >= BX_FUR_HI && c <= BX_ROSE; }
typedef struct {
  uint8_t c[BOT_GH][BOT_GW], p[BOT_GH][BOT_GW], m[BOT_GH][BOT_GW];
  uint8_t rr[BOT_GH][BOT_GW], rb[BOT_GH][BOT_GW], rt[BOT_GH][BOT_GW], rl[BOT_GH][BOT_GW];  /* same-part runs */
  int dx, dy;   /* where the part being drawn sits now (animation offsets, cells; dy includes BOT_GY) */
  int clipped;  /* this frame: cells of a part (or the outline a part needs) that fell off the grid */
} bot_grid;
/* One grid for the renderer (it runs on the owner task only; keeps the stack small). */
static inline bot_grid *bot_grid_get(void) { static bot_grid g; return &g; }
/* Paint a cell (part != 0: the cell joins that part) or recolour one (part 0: only on part `on`, 0 = any). */
static inline void bg_put(bot_grid *g, int x, int y, uint8_t c, uint8_t part, uint8_t on) {
  x += g->dx;
  y += g->dy;
  if ((unsigned)x >= BOT_GW || (unsigned)y >= BOT_GH) { g->clipped += part != 0; return; }
  if (part) { g->c[y][x] = c; g->p[y][x] = part; }
  else if (on ? g->p[y][x] == on : (g->p[y][x] != 0 && g->p[y][x] < BP_LINE)) g->c[y][x] = c;
}
static inline void bg_ell(bot_grid *g, float cx, float cy, float rx, float ry, uint8_t c, uint8_t part, uint8_t on) {
  if (rx < .4f || ry < .4f) return;
  int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx), y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float dx = (x + .5f - cx) / rx, dy = (y + .5f - cy) / ry;
      if (dx * dx + dy * dy <= 1.f) bg_put(g, x, y, c, part, on);
    }
}
static inline void bg_rect(bot_grid *g, int x0, int y0, int x1, int y1, uint8_t c, uint8_t part, uint8_t on) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) bg_put(g, x, y, c, part, on);
}
static inline uint8_t bg_at(const bot_grid *g, int x, int y) {
  return (unsigned)x < BOT_GW && (unsigned)y < BOT_GH ? g->p[y][x] : BP_NONE;
}
/* A tiny bitmap (strings, any char but '.' or ' ' = a cell) as decals or cells. */
static inline void bg_bits(bot_grid *g, int x, int y, const char *const *rows, int n, uint8_t c, uint8_t part, uint8_t on) {
  for (int r = 0; r < n; r++)
    for (int i = 0; rows[r][i]; i++)
      if (rows[r][i] != '.' && rows[r][i] != ' ') bg_put(g, x + i, y + r, c, part, on);
}

/* ---- passes ------------------------------------------------------------------------------------- */
/* The first grid row with anything on it (the headroom above him is usually empty: the passes skip it). */
static inline int bot_top_row(const bot_grid *g) {
  for (int y = 0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++)
      if (g->p[y][x]) return y;
  return BOT_GH;
}
/* The same part, or a front leg growing out of the body (or the shirt) above it (no seam across the
 * top of a leg). */
static inline bool bot_joined(uint8_t upper, uint8_t lower) {
  return upper == lower || ((upper == BP_TORSO || upper == BP_SHIRT) && (lower == BP_LEGL || lower == BP_LEGR));
}
/* No outline between part p and the part q in front of it at (dx, dy): a leg out of the body above
 * it; the shirt on his body; the hind leg anywhere on his body or shirt (its shading sets it off). */
static inline bool bot_seamless(uint8_t p, uint8_t q, int dx, int dy) {
  if ((p == BP_TORSO || p == BP_SHIRT) && (q == BP_SHIRT || q == BP_HAUNCH)) return true;
  return !dx && (dy > 0 ? bot_joined(p, q) : bot_joined(q, p));
}
/* Band shading, light from the upper left: on each row a part's right end is rose and the cells
 * before it tan (tan_w wide), its lower edge on the right half rose / tan, the top edge on the left
 * half light. Fur cells shade to fur tones, cloth cells to cloth tones (stripes keep their colour).
 * Then the hem: cloth next to his fur is the dark trim. */
static inline void bot_pass_shade(bot_grid *g) {
  static const uint8_t tan_w[BP_PARTS] = {0, 0, 2, 2, 2, 5, 2, 3, 2, 2, 2, 4, 2, 0, 0};  /* the hind leg sits in shade */
  int lo[BP_PARTS], hi[BP_PARTS], y0 = bot_top_row(g);
  for (int p = 0; p < BP_PARTS; p++) { lo[p] = BOT_GW; hi[p] = -1; }
  for (int y = y0; y < BOT_GH; y++) {
    for (int x = BOT_GW - 1; x >= 0; x--) g->rr[y][x] = x + 1 < BOT_GW && g->p[y][x + 1] == g->p[y][x] ? g->rr[y][x + 1] + 1 : 0;
    for (int x = 0; x < BOT_GW; x++) {
      uint8_t p = g->p[y][x];
      g->rl[y][x] = x > 0 && g->p[y][x - 1] == p ? g->rl[y][x - 1] + 1 : 0;
      if (p < BP_PARTS) { if (x < lo[p]) lo[p] = x; if (x > hi[p]) hi[p] = x; }
    }
  }
  for (int x = 0; x < BOT_GW; x++) {  /* up / down runs go on from the body into the legs */
    for (int y = BOT_GH - 1; y >= y0; y--) g->rb[y][x] = y + 1 < BOT_GH && bot_joined(g->p[y][x], g->p[y + 1][x]) ? g->rb[y + 1][x] + 1 : 0;
    for (int y = y0; y < BOT_GH; y++) g->rt[y][x] = y > y0 && bot_joined(g->p[y - 1][x], g->p[y][x]) ? g->rt[y - 1][x] + 1 : 0;
  }
  int cy0 = BOT_GH, cy1 = -1;  /* rows with cloth (for the hem) */
  for (int y = y0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++) {
      uint8_t p = g->p[y][x], c = g->c[y][x];
      if (p < BP_TAIL || p > BP_EARN) continue;
      bool fur = c == BX_FUR, cloth = c == BX_CLOTH, stripe = c == BX_CLOTH2;
      if (!fur && !cloth && !stripe) continue;
      if (!fur) { if (y < cy0) cy0 = y; cy1 = y; }
      bool right = 2 * x >= lo[p] + hi[p];
      int cat;  /* 0 light, 1 base, 2 tan, 3 rose */
      if (g->rr[y][x] == 0 || (g->rb[y][x] == 0 && right)) cat = 3;
      else if (g->rr[y][x] <= tan_w[p] || (g->rb[y][x] <= 1 && right)) cat = 2;
      else if (fur && p != BP_HAUNCH && g->rt[y][x] == 0 && g->rl[y][x] >= 1 && !right) cat = 0;
      else cat = 1;
      if (fur) g->c[y][x] = cat == 0 ? BX_FUR_HI : (cat == 1 ? BX_FUR : (cat == 2 ? BX_TAN : BX_ROSE));
      else if (cloth) g->c[y][x] = cat <= 1 ? BX_CLOTH : (cat == 2 ? BX_CLOTH_SH : BX_CLOTH_DK);
      else g->c[y][x] = cat <= 1 ? BX_CLOTH2 : BX_CLOTH2_SH;
    }
  /* the hem: a shirt / collar edge on his fur (body or legs) is the dark trim row (in place: the trim is
   * still cloth, and only fur neighbours count) */
  static const int8_t d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int y = cy0; y <= cy1; y++)
    for (int x = 0; x < BOT_GW; x++) {
      if (!bot_cloth(g->c[y][x]) || g->p[y][x] == BP_HOOD) continue;
      for (int k = 0; k < 4; k++) {
        int nx = x + d4[k][0], ny = y + d4[k][1];
        uint8_t q = bg_at(g, nx, ny);
        if ((q == BP_TORSO || q == BP_LEGL || q == BP_LEGR) && bot_fur(g->c[ny][nx])) { g->c[y][x] = BX_CLOTH_DK; break; }
      }
    }
}
/* Outline: empty cells next to a part; cells of a part next to a part in front of it -- except where
 * they grow into each other (bot_seamless). Then the sticker rim round the outer outline. A part on the
 * grid edge (its outline would fall off) counts as clipped. */
static inline void bot_pass_outline(bot_grid *g) {
  static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  int y0 = bot_top_row(g) - 2;
  y0 = y0 < 0 ? 0 : y0;
  memset(g->m[y0], 0, sizeof g->m[0] * (size_t)(BOT_GH - y0));
  for (int y = y0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++) {
      uint8_t p = g->p[y][x];
      if (p != BP_NONE && !bot_outlined(p)) continue;
      if (p != BP_NONE && (x == 0 || y == 0 || x == BOT_GW - 1 || y == BOT_GH - 1)) g->clipped++;
      for (int k = 0; k < 4; k++) {
        uint8_t q = bg_at(g, x + d4[k][0], y + d4[k][1]);
        if (!bot_outlined(q)) continue;
        if (p == BP_NONE) { g->m[y][x] = 1; break; }
        if (q > p && !bot_seamless(p, q, d4[k][0], d4[k][1])) { g->m[y][x] = 2; break; }
      }
    }
  for (int y = y0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++)
      if (g->m[y][x]) {
        g->c[y][x] = BX_LINE;
        if (g->m[y][x] == 1) {
          g->p[y][x] = BP_LINE;
          if (x == 0 || y == 0 || x == BOT_GW - 1 || y == BOT_GH - 1) g->clipped++;  /* its rim is off the grid */
        }
      }
  memset(g->m[y0], 0, sizeof g->m[0] * (size_t)(BOT_GH - y0));
  for (int y = y0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++) {
      if (g->p[y][x] != BP_NONE) continue;
      for (int dy = -1; dy <= 1 && !g->m[y][x]; dy++)
        for (int dx = -1; dx <= 1; dx++)
          if (bg_at(g, x + dx, y + dy) == BP_LINE) { g->m[y][x] = 1; break; }
    }
  for (int y = y0; y < BOT_GH; y++)
    for (int x = 0; x < BOT_GW; x++)
      if (g->m[y][x]) { g->c[y][x] = BX_RIM; g->p[y][x] = BP_RIM; }
}
/* Blow the grid up: cw x ch screen pixels per cell (cw < ch = a turn frame), mirrored if asked. */
static inline void bot_blit(uint16_t *p, const bot_grid *g, int ox, int oy, int cw, int ch, bool mirror, const uint16_t *pc, const uint8_t *pa) {
  for (int y = 0; y < BOT_GH; y++) {
    int sy = oy + y * ch;
    if (sy + ch <= 0 || sy >= SS_H) continue;
    for (int x = 0; x < BOT_GW;) {
      uint8_t c = g->c[y][x];
      int x2 = x + 1;
      while (x2 < BOT_GW && g->c[y][x2] == c) x2++;  /* one span per run of a colour */
      if (c) {
        int a = mirror ? BOT_GW - x2 : x, e = mirror ? BOT_GW - x : x2;
        for (int yy = sy; yy < sy + ch; yy++)
          if (yy >= 0 && yy < SS_H) ss_span(p + yy * SS_W, ox + a * cw, ox + e * cw, pc[c], pa[c]);
      }
      x = x2;
    }
  }
}

/* ---- motion: what he does this frame (a pure function of the pose) ------------------------------ */
enum { BOT_EYE_OPEN, BOT_EYE_BLINK, BOT_EYE_HAPPY, BOT_EYE_ASLEEP, BOT_EYE_HALF, BOT_EYE_SAD };
enum { BOT_MOUTH_W, BOT_MOUTH_OPEN, BOT_MOUTH_SMILE, BOT_MOUTH_FROWN, BOT_MOUTH_LINE };
enum { BOT_BROW_DOT, BOT_BROW_UP, BOT_BROW_SAD };
typedef struct {
  /* continuous (eased across a mood switch) */
  float jump;          /* all of him up, cells; drawn in screen px (jump x cell px), not snapped */
  float turn;          /* facing angle: 0 = to the left (his own way), pi = to the right (mirrored) */
  float bx, by;        /* his body (torso, tail, clothes, head) vs his legs, cells */
  float hx, hy;        /* his head vs his body, cells */
  float ear_f, ear_n;  /* ears: + = droop, - = perk */
  float wag, droop;    /* tail: wag -1 (in) .. 1 (out); droop 0 (up) .. 1 (down) */
  float paw_l, paw_r;  /* front paws lifted, cells */
  float bow, lie;      /* play bow (front down, rump up) 0..1; lying down 0..1 */
  float scratch;       /* a hind foot raised to his neck 0..1 */
  /* frames (switch half way through a mood blend) */
  int eyes, mouth, open, brows, dots, scratch_k;
  float heart, twinkle, zzz, puff, dust, tear;  /* effects: phase 0..1 (0 = none) */
  bool cloud, dim_dots, spinner, sleep_z, blush;
} bot_motion;
static inline void bot_motion_rest(bot_motion *o) {
  memset(o, 0, sizeof *o);
  o->blush = true;
}
/* Quiet idle: breathing, a slow tail sway with a wag burst now and then, a glance each way, an ear
 * flick, the blink (each bot on its own offset). No fixed loop: smooth functions of the clock. */
static inline void bot_idle(const bot_pose *b, bot_motion *o) {
  float t = b->t, ph = t + (float)b->look * 2.3f;
  o->by = helper_breath_up(b->breath_s) ? -1.f : 0.f;
  float burst = bot_env(fmodf(ph, 9.f), 0, .35f, 1.6f, 2.1f);
  o->wag = (1 - burst) * .5f * sinf(t * 2.6f) + burst * sinf(t * 18.f);
  float gl = fmodf(ph + 4.f, 11.f);
  o->hx = gl > 2.2f && gl < 3.3f ? -1.f : (gl > 5.8f && gl < 6.9f ? 1.f : 0.f);
  if (fmodf(t, 3.1f) < .35f) o->ear_n = -1;
  if (bot_blink(b) < .5f) o->eyes = BOT_EYE_BLINK;
}
/* The idle routines (helper_ui.h picks them; s = seconds into it). Each starts and ends at rest. */
static inline void bot_routine(const bot_pose *b, int rt, float s, bot_motion *o) {
  float t = b->t;
  switch (rt) {
    case BOT_RT_STRETCH: {  /* a play bow: front down, rump up, tail wagging, eyes shut happily */
      float e = bot_env(s, .1f, .85f, 1.6f, 2.3f);
      o->bow = e;
      o->wag = bot_lerp(o->wag, sinf(t * 16.f), e);
      if (e > .5f) { o->eyes = BOT_EYE_HAPPY; o->ear_f = o->ear_n = 1; o->hx = 0; }
      break;
    }
    case BOT_RT_YAWN: {  /* head up, eyes squeezed, a big open mouth (no tongue), ears back */
      float e = bot_env(s, .1f, .4f, 1.35f, 1.85f), m = bot_env(s, .3f, .7f, 1.05f, 1.5f);
      o->hy = -e; o->hx = 0;
      if (e > .5f) o->ear_f = o->ear_n = 1;
      if (m > .1f) { o->mouth = BOT_MOUTH_OPEN; o->open = 1 + bot_ri(m * 2.4f); o->eyes = BOT_EYE_ASLEEP; }
      else if (e > .5f) o->eyes = BOT_EYE_HALF;
      break;
    }
    case BOT_RT_SCRATCH: {  /* a hind foot up under his ear, scratching fast; blissful */
      float e = bot_env(s, .15f, .5f, 1.6f, 2.f);
      o->scratch = e;
      o->scratch_k = bot_step(s * 14.f, 2);
      if (e > .5f) { o->hx = 1; o->hy = 1; o->eyes = BOT_EYE_HAPPY; o->ear_n = o->scratch_k ? -1.f : 0.f; o->ear_f = 0; }
      break;
    }
    case BOT_RT_SNIFF: {  /* nose down to the ground, sniffing; turns round (a hop) and sniffs the other way */
      float e = bot_env(s, .05f, .6f, 2.6f, 3.15f), turn = bot_env(s, 1.05f, 1.4f, 2.25f, 2.6f);
      o->bow = .45f * e;
      o->hy = 2.f * e; o->hx = -e;
      if (e > .5f) { o->ear_f = o->ear_n = 1; o->puff = bot_frac(s * 2.6f) + .001f; }
      o->turn += BOT_PI * turn;
      o->jump += .7f * sinf(BOT_PI * turn);
      break;
    }
    case BOT_RT_CHASE: {  /* looks round at his tail, then spins after it twice, little hops, dust */
      float look = bot_env(s, 0, .15f, .3f, .45f), spin = bot_smooth((s - .35f) / 1.6f);
      o->hx = look > .5f ? 1.f : 0.f;
      o->turn += 4.f * BOT_PI * spin;
      o->jump += .7f * fabsf(sinf(4.f * BOT_PI * spin));
      if (s > .3f && s < 2.05f) {
        o->eyes = BOT_EYE_HAPPY; o->mouth = BOT_MOUTH_SMILE;
        o->wag = sinf(t * 22.f);
        o->ear_f = o->ear_n = -sinf(4.f * BOT_PI * spin);
        o->dust = bot_frac(s * 3.f) + .001f;
      }
      break;
    }
    default: break;
  }
}
/* Celebrate (a reply arrived): crouch, jump with a spin in the air, land, sparkles. */
static inline void bot_cheer(float s, bot_motion *o) {
  if (s < .12f) o->by = 1;
  else if (s < .86f) {
    float u = (s - .12f) / .74f;
    o->jump = 3.2f * 4.f * u * (1 - u);
    o->by = 0;
    o->paw_l = o->paw_r = 1;
    o->ear_f = o->ear_n = bot_lerp(1.f, -2.f, bot_smooth((u - .3f) / .4f));  /* flopping down, then up */
  } else if (s < 1.f) o->by = 1;
  o->turn += 2.f * BOT_PI * bot_smooth((s - .22f) / .5f);
  o->eyes = BOT_EYE_HAPPY; o->mouth = BOT_MOUTH_SMILE; o->brows = BOT_BROW_UP;
  o->twinkle = s > .55f ? (s - .55f) / (BOT_CHEER_S - .55f) + .001f : 0;
  o->hx = 0; o->hy = 0;
}
/* The nap (BOT_NAP_S quiet) and waking from it. */
static inline void bot_nap(const bot_pose *b, bot_motion *o) {
  float d = helper_nap_depth(b->idle_s), t = b->t;
  if (d > 0) {
    o->lie = d;
    o->ear_f = o->ear_n = 2.f * d;
    o->hx = o->hy = 0;
    o->wag *= 1 - d; o->droop = d;
    if (d > .6f) { o->eyes = BOT_EYE_ASLEEP; o->mouth = BOT_MOUTH_LINE; }
    else if (d > .2f) o->eyes = BOT_EYE_HALF;
    o->by = 0;
    float n = b->idle_s - BOT_NAP_S - BOT_LIE_S;
    if (n > 0) {  /* slow breaths (from rest): his flank rises and falls under a still head; Zz drift up */
      o->by = sinf(n * 1.2f - .9f) > .2f ? -1.f : 0.f;
      o->hy = -o->by;
      o->zzz = bot_frac(t * .45f) + .001f;
    }
  }
  if (b->wake_s > 0) {
    float s = b->wake_s, d0 = b->wake_from;
    o->lie = fmaxf(o->lie, d0 * (1 - bot_smooth(s / .75f)));
    float e = bot_env(s, .3f, .7f, 1.05f, 1.5f) * d0;
    o->bow = fmaxf(o->bow, e);
    o->ear_f = fmaxf(o->ear_f, 2.f * o->lie);
    o->ear_n = fmaxf(o->ear_n, 2.f * o->lie);
    o->droop = fmaxf(o->droop, o->lie);
    o->eyes = s < .45f ? BOT_EYE_ASLEEP : (s < .85f ? BOT_EYE_HALF : o->eyes);
    if (e > .5f) { o->mouth = BOT_MOUTH_OPEN; o->open = 2; o->wag = sinf(t * 16.f); }
    o->by = 0;
  }
}
/* One mood's motion at its clock mt (routines, nap and celebrate included where they belong). */
static inline void bot_mood_motion(const bot_pose *b, bot_mood m, float mt, bot_motion *o) {
  float t = b->t, level = m == BOT_LISTEN ? bot_clamp01(b->level) : 0.f;
  int lv = (int)(level * 3.f + .5f);
  bool nap_ok = false, routines = false;
  bot_motion_rest(o);
  switch (m) {
    case BOT_IDLE:
      bot_idle(b, o);
      nap_ok = routines = true;
      break;
    case BOT_LOADING: {  /* still finding out (user, 2026-10-07: loading should be sleepy): he dozes in
                            colour, sitting; eyes shut, his head slowly nodding off (~6 s), slow breaths,
                            relaxed ears and tail, Zz drifting up. No spinner. */
      float nod = .5f - .5f * cosf(t * 1.1f);
      o->hy = 1.2f * nod;
      o->by = .4f * (.5f - .5f * cosf(t * .8f));
      o->ear_f = o->ear_n = 1; o->droop = .7f; o->wag = .15f * sinf(t * .6f);
      o->eyes = nod > .25f ? BOT_EYE_ASLEEP : BOT_EYE_HALF; o->mouth = BOT_MOUTH_LINE; o->sleep_z = true;
      break;
    }
    case BOT_LISTEN:
      o->ear_f = o->ear_n = (float)(-1 - (lv > 1));
      o->wag = sinf(t * 16.f) * .9f;
      o->by = -1;  /* sitting up tall (the voice moves his ears and mouth, never his whole body) */
      o->mouth = BOT_MOUTH_OPEN; o->open = 1 + lv; o->brows = BOT_BROW_UP;
      break;
    case BOT_THINK:
    case BOT_WORK: {  /* thinking it over (Hermes on it): he rocks under a still head, his ears take turns,
                         a paw taps, the tail sweeps; the cloud's dots fill in and when all three are lit
                         he gives a little bounce */
      int beat = bot_step(t * 1.4f, 2);
      o->bx = (float)-beat; o->hx = (float)beat;
      o->ear_n = beat ? 0.f : -1.f; o->ear_f = beat ? -1.f : 0.f;
      o->paw_r = (float)bot_step(t * 2.7f, 2);
      o->wag = sinf(t * 4.6f) * .8f;
      o->dots = bot_step(t * 1.35f, 4); o->cloud = true;
      o->blush = m == BOT_THINK;
      if (o->dots == 3) { o->jump = .8f * sinf(BOT_PI * bot_frac(t * 1.35f)); if (o->jump > .4f) o->ear_f = o->ear_n = 1; }
      break;
    }
    case BOT_STOPPING:
      o->ear_f = o->ear_n = 1; o->droop = 1; o->by = 1;
      o->eyes = BOT_EYE_HALF; o->mouth = BOT_MOUTH_LINE; o->cloud = o->dim_dots = true; o->blush = false;
      break;
    case BOT_HAPPY: {  /* done: the happy wag (celebrating first), then calm idle again */
      bool grin = mt < 3.5f || b->talking;
      o->ear_f = o->ear_n = -1;
      o->wag = sinf(t * 20.f);
      o->eyes = grin ? BOT_EYE_HAPPY : (bot_blink(b) < .5f ? BOT_EYE_BLINK : BOT_EYE_OPEN);
      o->mouth = grin ? BOT_MOUTH_SMILE : BOT_MOUTH_W; o->brows = grin ? BOT_BROW_UP : BOT_BROW_DOT;
      if (b->talking) { int k = 2 * bot_step(t * 8.f, 2); if (k) { o->mouth = BOT_MOUTH_OPEN; o->open = k; } }
      if (b->cheer_s > 0) bot_cheer(b->cheer_s, o);
      float calm = b->talking ? 0.f : bot_smooth((mt - (BOT_CALM_S - .8f)) / .8f);
      if (calm > 0) {
        bot_motion idle;
        bot_motion_rest(&idle);
        bot_idle(b, &idle);
        o->ear_f = bot_lerp(o->ear_f, idle.ear_f, calm); o->ear_n = bot_lerp(o->ear_n, idle.ear_n, calm);
        o->wag = bot_lerp(o->wag, idle.wag, calm); o->by = bot_lerp(o->by, idle.by, calm); o->hx = bot_lerp(o->hx, idle.hx, calm);
        if (calm > .5f) { o->eyes = idle.eyes; o->mouth = idle.mouth; o->brows = idle.brows; }
      }
      nap_ok = routines = calm >= 1;
      break;
    }
    case BOT_SAD:
      o->ear_f = o->ear_n = 2; o->droop = 1; o->hy = 1;
      o->eyes = BOT_EYE_SAD; o->mouth = BOT_MOUTH_FROWN; o->brows = BOT_BROW_SAD; o->blush = false;
      if (mt < 3.f) o->tear = (float)bot_step(mt * 2.f, 2) + .5f;
      nap_ok = true;
      break;
    case BOT_STOPPED:
      o->ear_f = o->ear_n = 1;
      o->eyes = BOT_EYE_HALF; o->mouth = BOT_MOUTH_LINE; o->blush = false;
      nap_ok = true;
      break;
    case BOT_SLEEP:
      o->ear_f = o->ear_n = 1; o->droop = 1; o->by = (float)bot_step(t * .5f, 2);
      o->eyes = BOT_EYE_ASLEEP; o->mouth = BOT_MOUTH_LINE; o->sleep_z = true; o->blush = false;
      break;
  }
  if (routines && b->routine > BOT_RT_NONE && b->routine < BOT_RT_N) bot_routine(b, b->routine, b->routine_s, o);
  if (nap_ok) bot_nap(b, o);
}
static inline float bot_wrap_pi(float a) { return a - 2.f * BOT_PI * floorf(a / (2.f * BOT_PI) + .5f); }
/* Ease from a (the old mood) into b by w: continuous parts in between, frames switch half way. */
static inline void bot_mix(bot_motion *b, const bot_motion *a, float w) {
  float ta = bot_wrap_pi(a->turn), tb = bot_wrap_pi(b->turn);
  if (tb - ta > BOT_PI) ta += 2.f * BOT_PI;
  else if (ta - tb > BOT_PI) ta -= 2.f * BOT_PI;
  b->turn = bot_lerp(ta, tb, w);
  b->jump = bot_lerp(a->jump, b->jump, w); b->bx = bot_lerp(a->bx, b->bx, w); b->by = bot_lerp(a->by, b->by, w);
  b->hx = bot_lerp(a->hx, b->hx, w); b->hy = bot_lerp(a->hy, b->hy, w);
  b->ear_f = bot_lerp(a->ear_f, b->ear_f, w); b->ear_n = bot_lerp(a->ear_n, b->ear_n, w);
  b->wag = bot_lerp(a->wag, b->wag, w); b->droop = bot_lerp(a->droop, b->droop, w);
  b->paw_l = bot_lerp(a->paw_l, b->paw_l, w); b->paw_r = bot_lerp(a->paw_r, b->paw_r, w);
  b->bow = bot_lerp(a->bow, b->bow, w); b->lie = bot_lerp(a->lie, b->lie, w); b->scratch = bot_lerp(a->scratch, b->scratch, w);
  if (w < .5f) {
    int keep_dots = b->dots;
    float keep_jump = b->jump, keep_turn = b->turn;
    *b = (bot_motion){.jump = keep_jump, .turn = keep_turn, .bx = b->bx, .by = b->by, .hx = b->hx, .hy = b->hy, .ear_f = b->ear_f,
                      .ear_n = b->ear_n, .wag = b->wag, .droop = b->droop, .paw_l = b->paw_l, .paw_r = b->paw_r, .bow = b->bow,
                      .lie = b->lie, .scratch = b->scratch, .eyes = a->eyes, .mouth = a->mouth, .open = a->open, .brows = a->brows,
                      .dots = a->cloud ? a->dots : keep_dots, .scratch_k = a->scratch_k, .heart = a->heart, .twinkle = a->twinkle,
                      .zzz = a->zzz, .puff = a->puff, .dust = a->dust, .tear = a->tear, .cloud = a->cloud, .dim_dots = a->dim_dots,
                      .spinner = a->spinner, .sleep_z = a->sleep_z, .blush = a->blush};
  }
}
/* Reactions on top of any mood: a tap, the hold arming (lean-in), the nod after sending. */
static inline void bot_events(const bot_pose *b, bot_motion *o) {
  if (b->tap_s > 0) {
    float s = b->tap_s;
    if (s < .34f) o->jump += 1.2f * sinf(BOT_PI * s / .34f);
    if (s < .5f) { o->ear_f = fminf(o->ear_f, -1.f); o->ear_n = fminf(o->ear_n, -2.f); }
    if (s > .03f && s < .42f) o->eyes = BOT_EYE_HAPPY;
    o->heart = s / BOT_TAP_S + .001f;
  }
  if (b->lean > 0) {  /* perks up and leans in to listen: sits up tall, head tilted towards you, ears up */
    float l = bot_smooth(b->lean);
    o->by = bot_lerp(o->by, -1.f, l); o->bx -= l;
    o->hx -= 2.f * l; o->hy += l;
    o->ear_n = bot_lerp(o->ear_n, -2.f, l); o->ear_f = bot_lerp(o->ear_f, 1.f, l);
    if (l > .5f) { o->eyes = BOT_EYE_OPEN; o->brows = BOT_BROW_UP; o->mouth = BOT_MOUTH_W; }
  }
  if (b->nod_s > 0) {
    float n = sinf(BOT_PI * bot_clamp01(b->nod_s / BOT_NOD_S));
    o->hy += 1.2f * n;
    if (n > .75f) o->eyes = BOT_EYE_BLINK;
  }
}
/* What he does this frame. */
static inline bot_motion bot_animate(const bot_pose *b) {
  bot_motion m;
  bot_mood_motion(b, b->mood, b->mood_t, &m);
  if (b->blend && b->from != b->mood && b->blend_t < BOT_BLEND_S) {
    bot_motion a;
    bot_mood_motion(b, b->from, b->from_t + b->blend_t, &a);
    bot_mix(&m, &a, bot_smooth(b->blend_t / BOT_BLEND_S));
  }
  bot_events(b, &m);
  if (b->gaze > 0) m.turn += BOT_PI;
  return m;
}
/* Screen px per cell across (a turn shows him squashed: 3/4, then 1/2 the width) and whether the
 * frame is mirrored, for a cell size cp. */
static inline int bot_turn_px(const bot_motion *m, int cp) {
  float c = fabsf(cosf(m->turn));
  int cw = c < .38f ? cp / 2 : (c < .8f ? (3 * cp + 2) / 4 : cp);
  return cw < 1 ? 1 : cw;
}
static inline bool bot_mirrored(const bot_motion *m) { return cosf(m->turn) < 0; }

/* ---- Kotaro ------------------------------------------------------------------------------------- */
/* His body in three key poses (art cells: x 0..44, y 0..43 above the headroom): sitting, the play bow
 * and lying down; a pose in between mixes them. Ellipses x, y, rx, ry; legs = x0, y0, x1, y1. */
typedef struct { float x, y, rx, ry; } bot_el;
typedef struct {
  bot_el shoulders, chest, rump, thigh, foot, shin, paw_l, paw_r, shirt, hood;
  float leg_l[4], leg_r[4];
  float head_x, head_y;  /* head offset from where he sits */
  float tail_x, tail_y;  /* tail root offset */
  float cut;             /* rows from here down, right of the near paw, are ground (sitting) */
} bot_body;
#define BOT_BODY_FLOATS ((int)(sizeof(bot_body) / sizeof(float)))
static const bot_body bot_sit = {
    {20.5f, 26.5f, 8.5f, 5.f}, {22.f, 32.5f, 10.5f, 7.6f}, {29.f, 31.8f, 6.8f, 6.2f}, {29.2f, 36.2f, 5.f, 3.9f},
    {28.6f, 40.4f, 4.4f, 1.6f}, {30.5f, 38.f, 0, 0}, {14.f, 40.2f, 2.6f, 1.7f}, {19.5f, 40.2f, 3.1f, 1.7f},
    {17.6f, 23.2f, 14.6f, 13.1f}, {29.2f, 24.2f, 4.9f, 3.4f},
    {12, 34, 15, 39}, {17, 34, 21, 39}, 0, 0, 0, 0, 37};
static const bot_body bot_bow = {
    {19.f, 31.5f, 8.f, 4.6f}, {21.f, 35.6f, 9.8f, 5.6f}, {29.8f, 27.6f, 6.6f, 6.f}, {31.f, 31.2f, 4.3f, 4.3f},
    {31.6f, 40.4f, 3.4f, 1.6f}, {31.4f, 36.6f, 2.2f, 4.2f}, {8.5f, 39.8f, 2.5f, 1.6f}, {10.5f, 40.4f, 2.8f, 1.5f},
    {16.5f, 27.6f, 13.5f, 11.8f}, {31.6f, 25.4f, 4.4f, 3.f},
    {9, 37, 16, 38}, {11, 38, 20, 39}, -1, 11, 0, -4, 38};
static const bot_body bot_lie = {
    {21.f, 33.5f, 7.f, 4.5f}, {24.5f, 36.2f, 10.f, 5.4f}, {32.f, 35.2f, 7.4f, 6.2f}, {33.f, 36.6f, 5.2f, 4.f},
    {27.5f, 40.6f, 4.f, 1.3f}, {30.5f, 38.f, 0, 0}, {9.5f, 38.8f, 2.4f, 1.5f}, {7.5f, 40.3f, 2.6f, 1.5f},
    {25.5f, 30.4f, 11.6f, 9.8f}, {32.f, 29.2f, 4.2f, 3.f},
    {10, 37, 17, 38}, {8, 39, 17, 40}, -3.5f, 15, 0, 0, 44};
static inline void bot_body_at(bot_body *o, float bow, float lie) {
  const float *s = (const float *)&bot_sit, *b = (const float *)&bot_bow, *l = (const float *)&bot_lie;
  float *d = (float *)o;
  for (int i = 0; i < BOT_BODY_FLOATS; i++) d[i] = s[i] + bow * (b[i] - s[i]) + lie * (l[i] - s[i]);
}
/* Where his body and head sit this frame (cells, vs sitting at rest; the sprite's jump is separate):
 * whole cells -- the pixel-art frames. tuck = how far the far ear tucks in behind his cheek (lying). */
static inline void bot_offsets(const bot_motion *m, const bot_body *B, int *bx, int *by, int *hx, int *hy, int *tuck) {
  int x = bot_ri(m->bx), y = bot_ri(m->by);
  x = x < -1 ? -1 : (x > 0 ? 0 : x);  /* his tail and far ear sit near the grid edges */
  int t = bot_ri(3.f * bot_clamp01(m->lie)), h = x + bot_ri(m->hx + B->head_x);
  *bx = x; *by = y; *tuck = t;
  *hx = h < -1 - t ? -1 - t : (h > 2 ? 2 : h);
  *hy = y + bot_ri(m->hy + B->head_y);
}
/* Tail curves off the top of his rump, out and curving up, thick at the base and tapering to a round
 * tip (base x, y; bend x, y; tip x, y -- a quadratic curve): up, wagged out, wagged in, down, and
 * curled round in front of his hind foot (lying). */
static const float bot_tail_up[6] = {31.5f, 28.5f, 38.5f, 28.5f, 40.5f, 22.8f};
static const float bot_tail_out[6] = {31.5f, 28.5f, 38.5f, 29.5f, 41.f, 25.f};
static const float bot_tail_in[6] = {31.5f, 28.5f, 37.5f, 27.5f, 38.8f, 21.8f};
static const float bot_tail_down[6] = {33.f, 32.f, 37.5f, 34.5f, 40.f, 38.6f};
static const float bot_tail_curl[6] = {36.5f, 34.f, 41.5f, 41.f, 33.5f, 40.6f};
/* Eyes, 4 x 5 cells at (x0, y0), fixed on his face (kind: BOT_EYE_*). */
static inline void bot_eye(bot_grid *g, int x0, int y0, int kind) {
  static const char *const open[5] = {".##.", "#o##", "####", "####", ".##."};
  static const char *const happy[3] = {".##.", "#..#", "#..#"};
  static const char *const asleep[3] = {"#..#", "#..#", ".##."};
  static const char *const half[3] = {"####", "####", ".##."};
  static const char *const sad[4] = {"####", "#o##", "####", ".##."};
  switch (kind) {
    case BOT_EYE_BLINK: bg_rect(g, x0, y0 + 3, x0 + 3, y0 + 3, BX_EYE, 0, BP_HEAD); break;
    case BOT_EYE_HAPPY: bg_bits(g, x0, y0 + 1, happy, 3, BX_EYE, 0, BP_HEAD); break;
    case BOT_EYE_ASLEEP: bg_bits(g, x0, y0 + 2, asleep, 3, BX_EYE, 0, BP_HEAD); break;
    case BOT_EYE_HALF: bg_bits(g, x0, y0 + 2, half, 3, BX_EYE, 0, BP_HEAD); break;
    case BOT_EYE_SAD:
      bg_bits(g, x0, y0 + 1, sad, 4, BX_EYE, 0, BP_HEAD);
      bg_put(g, x0 + 1, y0 + 2, BX_WHITE, 0, BP_HEAD);
      break;
    default:
      bg_bits(g, x0, y0, open, 5, BX_EYE, 0, BP_HEAD);
      bg_put(g, x0 + 1, y0 + 1, BX_WHITE, 0, BP_HEAD);
      break;
  }
}
/* The shirt's cut (art cells, body coordinates): inside the shirt ellipse; its stripes run round him. */
static inline bool bot_shirt_in(const bot_el *e, int x, int y) {
  float dx = (x + .5f - e->x) / e->rx, dy = (y + .5f - e->y) / e->ry;
  return dx * dx + dy * dy <= 1.f;
}
static inline bool bot_stripe(const bot_el *e, int x, int y) {
  float rel = y + .5f - e->y + .02f * (x + .5f - e->x) * (x + .5f - e->x);
  return ((int)floorf(rel * .5f + 64.f)) & 1;
}
/* Draw him into the grid for motion m. */
static inline void bot_draw_grid(bot_grid *g, const bot_pose *b, const bot_motion *m, const bot_outfit *dress) {
  memset(g->c, 0, sizeof g->c);
  memset(g->p, 0, sizeof g->p);
  g->clipped = 0;
  bot_body B;
  bot_body_at(&B, m->bow, m->lie);
  uint8_t wear = dress->wear;
  bool shirt = bot_wears_shirt(wear);
  int bx, by, hx, hy, tuck;
  bot_offsets(m, &B, &bx, &by, &hx, &hy, &tuck);
  int ef = bot_ri(m->ear_f), en = bot_ri(m->ear_n), pl = bot_ri(m->paw_l), pr = bot_ri(m->paw_r);
  /* his body: tail, torso (shoulders, chest and belly, back and rump) */
  g->dx = bx; g->dy = BOT_GY + by;
  {
    float tl[6], w = m->wag;
    for (int i = 0; i < 6; i++) {
      float v = bot_tail_up[i] + (w > 0 ? w * (bot_tail_out[i] - bot_tail_up[i]) : -w * (bot_tail_in[i] - bot_tail_up[i]));
      v = bot_lerp(v, bot_tail_down[i], bot_clamp01(m->droop)) + (i & 1 ? B.tail_y : B.tail_x);
      tl[i] = bot_lerp(v, bot_tail_curl[i], bot_clamp01(m->lie));
    }
    for (int k = 0; k <= 8; k++) {
      float s = k / 8.f, a = (1 - s) * (1 - s), bq = 2 * s * (1 - s), cq = s * s, r = 2.4f - .8f * s;
      bg_ell(g, a * tl[0] + bq * tl[2] + cq * tl[4], a * tl[1] + bq * tl[3] + cq * tl[5], r, r, BX_FUR, BP_TAIL, 0);
    }
  }
  bg_ell(g, B.shoulders.x, B.shoulders.y, B.shoulders.rx, B.shoulders.ry, BX_FUR, BP_TORSO, 0);
  bg_ell(g, B.chest.x, B.chest.y, B.chest.rx, B.chest.ry, BX_FUR, BP_TORSO, 0);
  bg_ell(g, B.rump.x, B.rump.y, B.rump.rx, B.rump.ry, BX_FUR, BP_TORSO, 0);
  /* the ground: between the front paw and the hind foot (sitting), and nothing below his paws */
  {
    const int ground = BOT_GY + BOT_GROUND;
    int cut = BOT_GY + (int)floorf(B.cut + .5f), x0 = (int)floorf(B.leg_r[2] + .5f) + 1, y0 = cut < ground + 1 ? cut : ground + 1;
    for (int y = y0 < 0 ? 0 : y0; y < BOT_GH; y++)
      for (int x = y > ground ? 0 : x0; x < BOT_GW; x++)
        if (g->p[y][x] == BP_TORSO) g->p[y][x] = g->c[y][x] = 0;
  }
  /* the shirt: his torso inside the shirt's cut becomes cloth (its own part, stripes round him) */
  int sx0 = 0, sx1 = -1, sy0 = 0, sy1 = -1;  /* the shirt's box on the grid (its body position) */
  if (shirt) {
    sx0 = (int)floorf(B.shirt.x - B.shirt.rx) + bx; sx1 = (int)ceilf(B.shirt.x + B.shirt.rx) + bx;
    sy0 = (int)floorf(B.shirt.y - B.shirt.ry) + BOT_GY + by; sy1 = (int)ceilf(B.shirt.y + B.shirt.ry) + BOT_GY + by;
    sx0 = sx0 < 0 ? 0 : sx0; sy0 = sy0 < 0 ? 0 : sy0; sx1 = sx1 >= BOT_GW ? BOT_GW - 1 : sx1; sy1 = sy1 >= BOT_GH ? BOT_GH - 1 : sy1;
  }
  if (shirt)
    for (int y = sy0; y <= sy1; y++)
      for (int x = sx0; x <= sx1; x++) {
        int ax = x - bx, ay = y - BOT_GY - by;
        if (g->p[y][x] != BP_TORSO || !bot_shirt_in(&B.shirt, ax, ay)) continue;
        g->p[y][x] = BP_SHIRT;
        g->c[y][x] = wear == BOT_WEAR_STRIPED_TEE && bot_stripe(&B.shirt, ax, ay) ? BX_CLOTH2 : BX_CLOTH;
      }
  /* his hind leg: the round thigh, its foot pointing forward (or raised under his ear, scratching) */
  g->dx = 0; g->dy = BOT_GY;
  bot_el th = B.thigh, ft = B.foot;
  float sc = bot_clamp01(m->scratch);
  if (sc > 0) {
    ft.x = bot_lerp(ft.x, 31.6f, sc); ft.y = bot_lerp(ft.y, 24.4f + (float)m->scratch_k, sc);
    ft.rx = bot_lerp(ft.rx, 2.4f, sc); ft.ry = bot_lerp(ft.ry, 1.9f, sc);
  }
  bg_ell(g, th.x, th.y, th.rx, th.ry, BX_FUR, BP_HAUNCH, 0);
  bg_ell(g, B.shin.x, B.shin.y, B.shin.rx, B.shin.ry, BX_FUR, BP_HAUNCH, 0);
  if (sc <= 0) bg_ell(g, ft.x, ft.y, ft.rx, ft.ry, BX_FUR, BP_HAUNCH, 0);
  /* the hood, lying on the back of his neck: a soft bump with its opening (a darker fold) on top */
  g->dx = bx; g->dy = BOT_GY + by;
  if (wear == BOT_WEAR_HOODIE) {
    const bot_el *hd = &B.hood;
    bg_ell(g, hd->x, hd->y, hd->rx, hd->ry, BX_CLOTH, BP_HOOD, 0);
    bg_ell(g, hd->x - .4f, hd->y - 1.4f, hd->rx * .5f, hd->ry * .38f, BX_CLOTH_DK, 0, BP_HOOD);
  }
  /* scratching: the near hind leg raised in front of his body (its own outline), the foot under his ear */
  g->dx = 0; g->dy = BOT_GY;
  if (sc > 0) {
    for (int k = 0; k <= 4; k++) {
      float s = k / 5.f;
      bg_ell(g, bot_lerp(th.x + .6f, ft.x, s), bot_lerp(th.y - 1.f, ft.y, s), 1.9f, 1.9f, BX_FUR, BP_KICK, 0);
    }
    bg_ell(g, ft.x, ft.y, ft.rx, ft.ry, BX_FUR, BP_KICK, 0);
  }
  /* far ear (behind his head) */
  g->dx = hx; g->dy = BOT_GY + hy;
  bg_ell(g, 8.4f + tuck, 11.8f + ef, 3.f, 3.4f, BX_FUR, BP_EARF, 0);
  bg_ell(g, 6.8f + tuck, 16.8f + ef, 4.2f, 5.f, BX_FUR, BP_EARF, 0);
  /* front legs out of his chest, round paws */
  g->dx = 0; g->dy = BOT_GY;
  for (int s = 0; s < 2; s++) {
    const float *lg = s ? B.leg_r : B.leg_l;
    const bot_el *pw = s ? &B.paw_r : &B.paw_l;
    int lift = s ? pr : pl, y1 = bot_ri(lg[3]) - lift;
    uint8_t part = s ? BP_LEGR : BP_LEGL;
    bg_rect(g, bot_ri(lg[0]), bot_ri(lg[1]), bot_ri(lg[2]), y1 < bot_ri(lg[1]) ? bot_ri(lg[1]) : y1, BX_FUR, part, 0);
    bg_ell(g, pw->x, pw->y - lift, pw->rx, pw->ry, BX_FUR, part, 0);
  }
  /* the shirt over the tops of his front legs: they come out of it (two rows of sleeve at most; the
   * legs stand on the ground, so the sleeves are cut where he sits, not where he breathes) */
  if (shirt)
    for (int y = sy0 > 2 ? sy0 - 2 : 0; y <= sy1 + 2 && y < BOT_GH; y++)
      for (int x = sx0 > 1 ? sx0 - 1 : 0; x <= sx1 + 1 && x < BOT_GW; x++) {
        int ax = x, ay = y - BOT_GY;
        uint8_t p = g->p[y][x];
        if ((p != BP_LEGL && p != BP_LEGR) || !bot_shirt_in(&B.shirt, ax, ay)) continue;
        if (y >= 2 && g->p[y - 1][x] == p && g->p[y - 2][x] == p) continue;
        if (y + 3 >= BOT_GH || g->p[y + 3][x] != p) continue;  /* keep the paw (the last three rows) fur */
        g->c[y][x] = wear == BOT_WEAR_STRIPED_TEE && bot_stripe(&B.shirt, ax, ay) ? BX_CLOTH2 : BX_CLOTH;
      }
  /* head, near ear */
  g->dx = hx; g->dy = BOT_GY + hy;
  bg_ell(g, 19.2f, 12.5f, 12.2f, 10.8f, BX_FUR, BP_HEAD, 0);
  bg_ell(g, 31.f, 11.4f + en, 3.f, 3.4f, BX_FUR, BP_EARN, 0);
  bg_ell(g, 32.f, 16.4f + en, 4.2f, 5.f, BX_FUR, BP_EARN, 0);
  /* the collar: a band round his neck, just under his head where it sits */
  if (wear == BOT_WEAR_COLLAR) {
    int rx0 = bx + bot_ri(B.head_x), ry0 = by + bot_ri(B.head_y), y0 = BOT_GY + ry0 + 15, y1 = BOT_GY + ry0 + 26;
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < BOT_GH; y++)
      for (int x = 0; x < BOT_GW; x++) {
        if (g->p[y][x] != BP_TORSO) continue;
        float dx = (x + .5f - rx0 - 19.2f) / 12.2f, dy = (y + .5f - BOT_GY - ry0 - 12.5f) / 10.8f, d = dx * dx + dy * dy;
        if (dy > .3f && d > 1.f && d <= 1.5f) g->c[y][x] = BX_CLOTH;
      }
  }
  /* effects with an outline: the thought cloud in front of his face (the stop badge keeps the other
   * corner), its puffs swelling in turn; the tap heart rising; celebration sparkles */
  g->dx = 0; g->dy = BOT_GY;
  int puff = m->cloud ? bot_step(b->t * 1.6f, 2) : 0;
  if (m->cloud) {
    bg_ell(g, 6.2f, 1.4f, 3.4f + .4f * puff, 2.6f, BX_FX, BP_FXF, 0);
    bg_ell(g, 3.6f, 2.6f, 1.7f, 1.6f + .4f * (1 - puff), BX_FX, BP_FXF, 0);
    bg_ell(g, 8.8f, 2.6f, 1.7f + .3f * (1 - puff), 1.6f, BX_FX, BP_FXF, 0);
    bg_put(g, 4, 6 + puff, BX_FX, BP_FXF, 0);  /* a little bubble rising from his head */
  }
  if (m->heart > 0) {
    static const char *const big[4] = {"##.##", "#####", ".###.", "..#.."}, *const small[3] = {"#.#", "###", ".#."};
    int rise = (int)(m->heart * 4.f);
    if (m->heart < .15f) bg_bits(g, 38, 3, small, 3, BX_HEART, BP_FXF, 0);
    else bg_bits(g, 37, 3 - rise, big, 4, BX_HEART, BP_FXF, 0);
  }
  if (m->twinkle > 0) {
    static const char *const star[3] = {".#.", "###", ".#."};
    static const int8_t at[3][2] = {{2, 7}, {40, 10}, {35, 2}};
    for (int i = 0; i < 3; i++)
      if (bot_frac(m->twinkle * 2.2f + i * .37f) < .6f) bg_bits(g, at[i][0], at[i][1], star, 3, BX_STAR, BP_FXF, 0);
  }
  bot_pass_shade(g);
  /* face: cream muzzle, eyes, brows, nose, mouth, cheeks (on the head, moving with it) */
  g->dx = hx; g->dy = BOT_GY + hy;
  bg_ell(g, 16.4f, 17.4f, 5.6f, 3.8f, BX_MUZZLE, 0, BP_HEAD);
  bot_eye(g, 11, 10, m->eyes);
  bot_eye(g, 20, 10, m->eyes);
  if (m->brows == BOT_BROW_SAD) {
    bg_rect(g, 11, 8, 12, 8, BX_EYE, 0, BP_HEAD); bg_put(g, 13, 7, BX_EYE, 0, BP_HEAD);
    bg_rect(g, 22, 8, 23, 8, BX_EYE, 0, BP_HEAD); bg_put(g, 21, 7, BX_EYE, 0, BP_HEAD);
  } else {
    int up = m->brows == BOT_BROW_UP;
    bg_put(g, 12, 7 - up, BX_EYE, 0, BP_HEAD);
    bg_put(g, 22, 7 - up, BX_EYE, 0, BP_HEAD);
  }
  if (m->tear > 0) {
    int k = (int)m->tear;
    bg_rect(g, 11, 15 + k, 11, 16 + k, BX_TEAR, 0, BP_HEAD);
  }
  {
    static const char *const nose[3] = {"####", "####", ".##."};
    bg_bits(g, 15, 15, nose, 3, BX_EYE, 0, BP_HEAD);
    bg_put(g, 16, 16, BX_NOSE_HI, 0, BP_HEAD);
  }
  if (m->mouth == BOT_MOUTH_OPEN && m->open > 0) {  /* talking / listening / a yawn: an open mouth */
    int open = m->open;
    bg_rect(g, 15, 18, 18, 17 + open, BX_MOUTH, 0, BP_HEAD);
    bg_rect(g, 14, 18, 14, 17 + open, BX_EYE, 0, BP_HEAD);
    bg_rect(g, 19, 18, 19, 17 + open, BX_EYE, 0, BP_HEAD);
    bg_rect(g, 15, 18 + open, 18, 18 + open, BX_EYE, 0, BP_HEAD);
  } else if (m->mouth == BOT_MOUTH_SMILE) {  /* a wide open smile */
    static const char *const smile[3] = {"#....#", ".####.", ".#oo#."};
    bg_bits(g, 14, 18, smile, 3, BX_EYE, 0, BP_HEAD);
    bg_rect(g, 16, 20, 17, 20, BX_MOUTH, 0, BP_HEAD);
  } else if (m->mouth == BOT_MOUTH_FROWN) {
    static const char *const frown[2] = {".##.", "#..#"};
    bg_bits(g, 15, 18, frown, 2, BX_EYE, 0, BP_HEAD);
  } else if (m->mouth == BOT_MOUTH_LINE) {
    bg_rect(g, 15, 19, 18, 19, BX_EYE, 0, BP_HEAD);
  } else {  /* the little w smile */
    static const char *const w[2] = {"#..#..#", ".##.##."};
    bg_bits(g, 13, 18, w, 2, BX_EYE, 0, BP_HEAD);
  }
  if (m->blush && !b->grey) {
    bg_rect(g, 9, 17, 10, 17, BX_BLUSH, 0, BP_HEAD);
    bg_rect(g, 24, 17, 25, 17, BX_BLUSH, 0, BP_HEAD);
  }
  /* outfit prints, on the chest (on cloth only: a print never spills onto the fur) */
  g->dx = bx + bot_ri(B.chest.x - bot_sit.chest.x); g->dy = BOT_GY + by + bot_ri(B.chest.y - bot_sit.chest.y);
  if (wear == BOT_WEAR_COLLAR) { g->dx = bx + bot_ri(B.head_x); g->dy = BOT_GY + by + bot_ri(B.head_y); }
  for (int i = 0; i < 2; i++) {
    const bot_print *pr2 = &dress->print[i];
    if (pr2->bits && pr2->rows) bg_bits(g, pr2->x, pr2->y, pr2->bits, pr2->rows, pr2->ink == BOT_INK_EMBLEM ? BX_CLOTH_DK : BX_CLOTH2, 0, wear == BOT_WEAR_COLLAR ? BP_TORSO : BP_SHIRT);
  }
  /* effects without an outline */
  g->dx = 0; g->dy = BOT_GY;
  if (m->cloud) {  /* the dots fill in one by one, then start again (dim while stopping) */
    for (int i = 0; i < 3; i++) bg_put(g, 3 + 2 * i, 2, !m->dim_dots && i < m->dots ? BX_DOT : BX_DIM, 0, BP_FXF);
  }
  if (m->spinner) {  /* a spinner he watches */
    static const int8_t ring[8][2] = {{0, -3}, {2, -2}, {3, 0}, {2, 2}, {0, 3}, {-2, 2}, {-3, 0}, {-2, -2}};
    int lead = bot_step(b->t * 9.6f, 8);
    for (int i = 0; i < 8; i++) bg_put(g, 39 + ring[i][0], 4 + ring[i][1], (lead - i + 8) % 8 < 2 ? BX_DOT : BX_DIM, BP_FXN, 0);
  }
  if (m->sleep_z || m->zzz > 0) {  /* Zz drifting up */
    static const char *const zbig[4] = {"####", "..#.", ".#..", "####"}, *const zsmall[3] = {"###", ".#.", "###"};
    int d = m->sleep_z ? bot_step(b->t * 1.05f, 3) : (int)(m->zzz * 3.f);
    int zx = m->sleep_z ? 36 : 31, zy = m->sleep_z ? 4 : 10;
    bg_bits(g, zx, zy - d, zbig, 4, BX_LINE, BP_FXN, 0);
    bg_bits(g, zx + 5, zy - 3 - (d > 1), zsmall, 3, BX_LINE, BP_FXN, 0);
  }
  if (m->puff > 0) {  /* sniffing: little puffs in front of his nose, below his ear */
    int k = m->puff < .5f;
    bg_rect(g, 3 - k, 28 + k, 4 - k, 28 + k, BX_DIM, BP_FXN, 0);
    if (m->puff > .25f && m->puff < .75f) bg_put(g, 1, 27, BX_DIM, BP_FXN, 0);
  }
  if (m->dust > 0) {  /* spinning: dust kicked up at his feet */
    int k = m->dust < .5f;
    bg_rect(g, 6 + k, 41 - k, 8 + k, 41 - k, BX_DIM, BP_FXN, 0);
    bg_rect(g, 37 - k, 41 - k, 39 - k, 41 - k, BX_DIM, BP_FXN, 0);
  }
  bot_pass_outline(g);
}
/* The soft ground shadow (it stays on the ground while he jumps, smaller the higher he is). */
static inline void bot_shadow(uint16_t *p, int ox, int oy, int cp, float jump, uint16_t c) {
  float rx = 14.f * (1.f - .3f * bot_clamp01(jump / 3.2f));
  for (int y = BOT_GROUND; y < BOT_GH - BOT_GY; y++) {
    float sy = (y + .5f - 42.7f) / 1.4f;
    if (sy * sy > 1.f) continue;
    float hw = rx * sqrtf(1.f - sy * sy);
    int x0 = (int)ceilf(22.5f - hw - .5f), x1 = (int)floorf(22.5f + hw - .5f);
    if (x1 < x0) continue;
    for (int yy = oy + (y + BOT_GY) * cp; yy < oy + (y + BOT_GY + 1) * cp; yy++)
      if (yy >= 0 && yy < SS_H) ss_span(p + yy * SS_W, ox + x0 * cp, ox + (x1 + 1) * cp, c, 70);
  }
}
static inline void bot_art_draw(uint16_t *p, const bot_pose *b) {
  if (!p || !b || b->u <= 0) return;
  bot_colors c = bot_colors_of(b);
  bot_look look = b->look >= 0 && b->look < BOT_LOOKS ? b->look : BOT_LOOK_GENERIC;
  const bot_outfit *dress = &bot_outfits[look];
  float u = b->u, t = b->t;
  bot_motion m = bot_animate(b);
  /* the grid in the box: whole pixels per cell, bottom-aligned, centred */
  float bottom = b->compact ? BOT_BOTTOM_COMPACT : BOT_BOTTOM, top = b->compact ? BOT_TOP_COMPACT : BOT_TOP;
  int cp = (int)floorf(fminf(2.f * BOT_HALF_W * u / BOT_GW, (top + bottom) * u / BOT_GH));
  if (cp < 1) cp = 1;
  int ox = (int)floorf(b->cx - BOT_GW * cp * .5f + .5f), oy = (int)floorf(b->cy + bottom * u) - BOT_GH * cp;
  int cw = bot_turn_px(&m, cp);
  bool mirror = bot_mirrored(&m);
  bot_grid *g = bot_grid_get();
  bot_draw_grid(g, b, &m, dress);
  /* the listening glow round his head (empty cells only); the pulse ring stays on the grid (whole) */
  if (b->mood == BOT_LISTEN && !m.lie) {
    bot_body B;
    bot_body_at(&B, m.bow, m.lie);
    int bx, by, hx, hy, tuck;
    bot_offsets(&m, &B, &bx, &by, &hx, &hy, &tuck);
    float top = BOT_GY + hy + 12.5f - 10.8f, ring = (top - 1.4f) * 11.5f / 10.8f;  /* room above his head */
    float level = bot_clamp01(b->level), rp = 1.5f + bot_frac(t * 1.1f) * fmaxf(ring - 1.5f, 0.f), hr = 1.5f + 2.5f * level;
    for (int y = 0; y < BOT_GH; y++)
      for (int x = 0; x < BOT_GW; x++) {
        if (g->p[y][x] != BP_NONE) continue;
        float ex = (x + .5f - 19.2f - hx) / 12.2f, ey = (y + .5f - BOT_GY - 12.5f - hy) / 10.8f, d = (sqrtf(ex * ex + ey * ey) - 1.f) * 11.5f;
        if (d <= hr) g->c[y][x] = BX_HALO;
        else if (fabsf(d - rp) < .7f && !b->compact) g->c[y][x] = BX_HALO2;
      }
  }
  /* colours */
  uint16_t pc[BX_N];
  uint8_t pa[BX_N];
  memset(pa, 255, sizeof pa);
  pc[BX_NONE] = 0; pc[BX_LINE] = c.line; pc[BX_RIM] = BOT_RIM; pc[BX_FUR_HI] = c.fur_hi; pc[BX_FUR] = c.fur; pc[BX_TAN] = c.tan;
  pc[BX_ROSE] = c.rose; pc[BX_MUZZLE] = c.muzzle; pc[BX_EYE] = c.eye; pc[BX_WHITE] = c.white; pc[BX_NOSE_HI] = c.nose_hi;
  pc[BX_MOUTH] = c.mouth; pc[BX_BLUSH] = c.blush; pc[BX_TEAR] = c.tear; pc[BX_CLOTH] = c.cloth; pc[BX_CLOTH_SH] = c.cloth_sh;
  pc[BX_CLOTH_DK] = c.cloth_dk; pc[BX_CLOTH2] = c.cloth2; pc[BX_CLOTH2_SH] = c.cloth2_sh; pc[BX_FX] = BOT_RIM; pc[BX_DOT] = c.dot;
  pc[BX_DIM] = c.dim; pc[BX_SHADOW] = c.shadow; pc[BX_HALO] = c.halo; pc[BX_HALO2] = c.halo2; pc[BX_HEART] = c.heart; pc[BX_STAR] = c.star;
  pa[BX_SHADOW] = 70; pa[BX_HALO] = 200; pa[BX_HALO2] = (uint8_t)(140.f * (1.f - bot_frac(t * 1.1f)));
  if (b->shadow) bot_shadow(p, ox, oy, cp, m.jump, c.shadow);
  bot_blit(p, g, (int)floorf(b->cx - BOT_GW * cw * .5f + .5f), oy - bot_ri(m.jump * cp), cw, cp, mirror, pc, pa);
  /* Stop badge while a tap on the bot stops the command */
  if (b->stop_badge) {
    float sx = b->cx + 50.f * u, sy = b->cy - 44.f * u, q = 4.2f * u;
    ss_disc(p, sx, sy, 13.f * u, BOT_RIM, 255, 1);
    ss_disc(p, sx, sy, 10.5f * u, BOT_STOP, 255, 1);
    ss_rrect(p, sx - q, sy - q, sx + q, sy + q, 1.5f * u, BOT_WHITE, 255, 1);
  }
}
