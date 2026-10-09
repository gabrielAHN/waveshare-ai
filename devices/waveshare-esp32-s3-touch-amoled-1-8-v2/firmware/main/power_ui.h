#pragma once
/* Power UI (SPEC Contract D): the power-off countdown overlay, drawn LAST on any page by home_render
 * (a centred card: "Turning off", the seconds left 4..1 (0 at power off), "Release to cancel"), and
 * the Battery tab hint line. Everything stays inside the rounded safe area (set_safe_px); the host
 * suite tests/host/power_ui_test.c checks the pixels, the text and that nothing is drawn when idle.
 * home_render.h includes this file after its text helpers; the two macros below come first so the
 * Battery page can use them whichever header a file includes first. */
#define POWER_HINT_TEXT "Power: press = sleep, hold 5 s = off"
#define POWER_HINT_Y 392 /* one 9 px line (324 px wide), clear of the Battery readings and the Home band */
#include "home_render.h"

#define POWER_CARD_X 54
#define POWER_CARD_Y 124
#define POWER_CARD_W 260
#define POWER_CARD_H 200
#define POWER_CARD_R 28
#define POWER_DIGIT_SCALE 5
/* Colours are theme roles (theme.h): Light = the dark brown card (HH_TEXT), white title, accent digit,
 * peach note; Dark = a dark grey card, light title, the accent lifted for text, a muted note. */
static inline void power_ui_draw(const power_view *v, uint16_t *p) {
  if (!v || !v->countdown) return;
  home_rect(p, POWER_CARD_X, POWER_CARD_Y, POWER_CARD_W, POWER_CARD_H, POWER_CARD_R, theme_c(TC_POWER_CARD));
  set_block(p, POWER_CARD_X, POWER_CARD_W, POWER_CARD_Y + 22, "Turning off", 2, 1, theme_c(TC_POWER_INK));
  char digit[2] = {(char)('0' + (v->secs < 0 ? 0 : v->secs > 9 ? 9 : v->secs)), 0};
  int dw = 9 * POWER_DIGIT_SCALE;
  set_draw(p, 184 - dw / 2, POWER_CARD_Y + 66, digit, 1, POWER_DIGIT_SCALE, theme_c(TA_TEXT), false);
  set_block(p, POWER_CARD_X, POWER_CARD_W, POWER_CARD_Y + POWER_CARD_H - 39, "Release to cancel", 1, 1, theme_c(TA_NOTE));
}
