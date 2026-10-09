/* Sparkles always opens on the default style (SPEC3 Contract S): entering the Sparkles page by any path
 * (the Home tile, USB WLV1 1 / any page switch outside the touch poller) starts on style 0 (Sea), from
 * the very first frame (the page that slides up over Home is already Sea). Swipes change the style only
 * until you leave the page. The style is never saved to NVS and no stored value is read (home_wifi.c).
 * HOST SIMULATION ONLY. RED first. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "home_render.h"

static int64_t now = 1000000;
static void sample(home_ui *s, bool down, int x, int y) { home_sample(s, now += 10000, down, x, y); }
static void flick(home_ui *s, int dir) { /* dir -1 = finger moves left = next style */
  for (int k = 0; k <= 8; k++) sample(s, true, 184 - dir * 75 + dir * 150 * k / 8, 220);
  sample(s, false, 184 + dir * 75, 220);
}
static void tap(home_ui *s, int x, int y) { sample(s, true, x, y); sample(s, true, x, y); sample(s, false, x, y); }
static void idle(home_ui *s, int n) { for (int k = 0; k < n; k++) sample(s, false, 0, 0); }
static uint16_t a[SPARKLES_PIXELS], b[SPARKLES_PIXELS];
/* The same scene drawn as a fresh boot would (style 0, never swiped). */
static void render_sea(const home_ui *s, uint16_t *out) {
  home_ui c = *s; c.input.scene.style = c.input.scene.style_from = 0; c.input.scene.style_at = 0;
  sp_direct_water_time = -1; assert(home_render_page(&c, out, SPARKLES_PIXELS));
}
static void render(const home_ui *s, uint16_t *out) { sp_direct_water_time = -1; assert(home_render_page(s, out, SPARKLES_PIXELS)); }
static bool sea(const sparkles_state *c) { return c->style == 0 && c->style_from == 0; }
/* Whole text of a source file (the tests run from the repository root). */
static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb"); assert(f);
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *t = malloc((size_t)n + 1); assert(t && fread(t, 1, (size_t)n, f) == (size_t)n); t[n] = 0; fclose(f);
  return t;
}

int main(void) {
  int sp = home_tile_index(SPARKLES); assert(sp >= 0);
  home_ui s; memset(&s, 0, sizeof s); s.page = HOME; s.tile = sp; s.session_off = true;  /* Sparkles works offline */
  /* 1. The Home tile opens Sea; a flick picks Sunset; it stays Sunset while you stay on the page. */
  tap(&s, 184, 224); assert(s.page == SPARKLES && sea(&s.input.scene));
  idle(&s, 30); flick(&s, -1); assert(s.page == SPARKLES && s.input.scene.style == 1);
  idle(&s, 50); sample(&s, true, 120, 300); sample(&s, false, 120, 300); idle(&s, 20);
  assert(s.page == SPARKLES && s.input.scene.style == 1);
  /* 2. Leave (USB WLV1 0: the page is switched outside the touch path), come back by the tile: Sea, in the
   *    same sample as the tap, so the page that slides up over Home is drawn in Sea from its first frame. */
  s.page = HOME; s.drag_offset = 0; idle(&s, 40);
  s.tile = sp; tap(&s, 184, 224);
  assert(s.page == SPARKLES && sea(&s.input.scene) && !home_sparkle_label_on(&s));
  {
    static uint16_t snapshot[SPARKLES_PIXELS];
    home_snapshot snap = {.px = snapshot};
    assert(home_page_offset(&s) > 0);                         /* the open slide is under way */
    assert(home_compose(&s, a, SPARKLES_PIXELS, &snap, NULL, -1) && snap.valid && snap.page == SPARKLES);
    render_sea(&s, b); assert(!memcmp(snapshot, b, sizeof b));
  }
  /* 3. Sunset again, then WLV1 1 while somewhere else: the board switches the page and calls
   *    home_sparkle_entry() under the same lock (home_wifi.c), so even the first frame is Sea. */
  idle(&s, 40); flick(&s, -1); assert(s.input.scene.style == 1);
  s.page = SETTINGS; idle(&s, 5);
  s.page = SPARKLES; home_sparkle_entry(&s);
  assert(sea(&s.input.scene) && !home_sparkle_label_on(&s));
  render(&s, a); render_sea(&s, b); assert(!memcmp(a, b, sizeof a));
  /* ... and without that call the next poller sample catches any other page switch. */
  flick(&s, -1); assert(s.input.scene.style == 1);
  s.page = HELPER; idle(&s, 3); s.page = SPARKLES; idle(&s, 1); assert(sea(&s.input.scene));
  /* 4. A page switch that stays on Sparkles is not an entry: the swipe holds. */
  flick(&s, -1); assert(s.input.scene.style == 1);
  home_sparkle_entry(&s); idle(&s, 5); assert(s.input.scene.style == 1);
  /* 5. Boot: zero-init is Sea, no label. */
  home_ui z; memset(&z, 0, sizeof z); z.page = SPARKLES; idle(&z, 1); assert(sea(&z.input.scene) && !home_sparkle_label_on(&z));
  /* 6. Nothing is saved or restored: no NVS key "style" in the Wi-Fi/NVS worker, no save request field. */
  char *w = slurp("devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main/home_wifi.c"),
       *u = slurp("devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main/home_ui.h");
  assert(!strstr(w, "\"style\"") && !strstr(w, "save_style") && !strstr(w, "load_style") && !strstr(w, "STYLE_SAVED") && !strstr(w, "style_save"));
  assert(!strstr(u, "style_save"));
  assert(strstr(w, "home_sparkle_entry(ui)"));   /* the WLV1 page select starts Sparkles on Sea at once */
  free(w); free(u);
  puts("sparkle_default: Sparkles opens on Sea by every path (tile, WLV1, any page switch), first frame included; "
       "swipes last until you leave; no NVS style: PASS");
  return 0;
}
