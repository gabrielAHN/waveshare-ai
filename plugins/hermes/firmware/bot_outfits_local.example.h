#pragma once
/* Your own bots' outfits for the Ask page (plugins/hermes/KOTARO.md). Copy this file to bot_outfits_local.h in the
 * same folder: that copy is git-ignored, so your bot names and colours stay out of the repo. bot_art.h
 * includes it before the outfit table and appends BOT_OUTFITS_LOCAL after the built-in rows.
 *
 * One row per bot: {id, wear, dot_deep, {body, deep, emblem, light}, {print, print}}
 *   id     your Hermes profile name, exactly as bridge.json "bots" lists it
 *   wear   BOT_WEAR_TEE, BOT_WEAR_STRIPED_TEE, BOT_WEAR_HOODIE, BOT_WEAR_COLLAR or BOT_WEAR_NONE
 *   prints {x, y, BOT_INK_LIGHT | BOT_INK_EMBLEM, rows, bits}: tiny bitmaps in sprite cells on his chest
 *          (about x 10..27, y 21..32); reuse bot_print_strings / bot_print_panel / bot_print_code /
 *          bot_print_tag from bot_art.h or define your own here.
 * Every row ends with a comma. Preview with tools/bot_preview.sh. */

static const char *const bot_print_star[3] = {".#.", "###", ".#."};

#define BOT_OUTFITS_LOCAL                                                                                 \
  /* research: a green t-shirt with a white star */                                                    \
  {"research", BOT_WEAR_TEE, false, {{80, 170, 110}, {56, 140, 86}, {36, 104, 64}, {250, 250, 240}},   \
   {{17, 26, BOT_INK_LIGHT, 3, bot_print_star}}},                                                         \
  /* maps: a red hoodie with drawstrings and a front panel */                                          \
  {"maps", BOT_WEAR_HOODIE, false, {{214, 64, 52}, {178, 48, 40}, {130, 32, 30}, {252, 236, 232}},       \
   {{16, 24, BOT_INK_LIGHT, 3, bot_print_strings}, {12, 28, BOT_INK_EMBLEM, 4, bot_print_panel}}},
