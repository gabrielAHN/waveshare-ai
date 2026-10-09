/* Power UI (power_ui.h): the power-off countdown overlay is drawn last on any page, only inside the
 * rounded safe area, shows "Turning off", the seconds left (4..1, 0 at power off) and "Release to
 * cancel", and draws nothing at all while idle. The Battery tab carries the power hint line. */
#include <assert.h>
#include <stdio.h>
#include "settings_states.h"

static uint16_t base[SPARKLES_PIXELS], over[SPARKLES_PIXELS];
static bool rect_safe(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      if (!set_safe_px(xx, yy)) return false;
  return true;
}
static const settings_text *find_text(const settings_record *r, const char *t) {
  for (int k = 0; k < r->count; k++)
    if (!strcmp(r->t[k].text, t)) return &r->t[k];
  return NULL;
}
/* One page under test: Home, Settings Wi-Fi / Battery, Sparkles, Ask, Sensor. */
static void page_state(int i, home_ui *s) {
  settings_state(12, s);  /* signed in, Wi-Fi up */
  switch (i) {
    case 0: s->page = HOME; s->tile = 0; break;
    case 1: s->settings_tab = SETTINGS_WIFI; break;
    case 2: settings_state(27, s); break;  /* Battery, charging */
    case 3: s->page = home_page_enabled(SPARKLES) ? SPARKLES : HOME; break;
    case 4: s->page = home_page_enabled(HELPER) ? HELPER : HOME; break;
    default: s->page = home_page_enabled(SENSORS) ? SENSORS : HOME; break;
  }
}

int main(void) {
  int changed_total = 0;
  for (int i = 0; i < 6; i++) {
    home_ui s;
    page_state(i, &s);
    /* Idle: home_render == the page alone, pixel for pixel. */
    assert(!s.power.countdown);
    assert(home_render_page(&s, base, SPARKLES_PIXELS) && home_render(&s, over, SPARKLES_PIXELS));
    assert(!memcmp(base, over, sizeof base));
    s.power.secs = 3;  /* a stale digit with the countdown off is still nothing */
    assert(home_render(&s, over, SPARKLES_PIXELS) && !memcmp(base, over, sizeof base));
    static uint16_t digit_px[5][45 * 85];
    for (int secs = 4; secs >= 0; secs--) {
      s.power.countdown = true;
      s.power.secs = (int8_t)secs;
      settings_record rec;
      memset(&rec, 0, sizeof rec);
      set_rec = &rec;
      assert(home_render(&s, over, SPARKLES_PIXELS));
      set_rec = NULL;
      /* Every pixel the overlay changed is inside the rounded safe area, and it changed plenty. */
      int changed = 0, outside = 0;
      for (int y = 0; y < 448; y++)
        for (int x = 0; x < 368; x++)
          if (over[y * 368 + x] != base[y * 368 + x]) {
            changed++;
            if (!set_safe_px(x, y)) outside++;
          }
      assert(outside == 0 && changed > 20000);
      changed_total += changed;
      /* The card is drawn on top: its corners-in pixel is the card colour on every page. */
      assert(over[(POWER_CARD_Y + POWER_CARD_H / 2) * 368 + POWER_CARD_X + 4] == HH_TEXT);
      char d[2] = {(char)('0' + secs), 0};
      const settings_text *t1 = find_text(&rec, "Turning off"), *t2 = find_text(&rec, d), *t3 = find_text(&rec, "Release to cancel");
      assert(t1 && t2 && t3 && t1->scale == 2 && t2->scale == POWER_DIGIT_SCALE && t3->scale == 1);
      const settings_text *ts[3] = {t1, t2, t3};
      for (int k = 0; k < 3; k++) {
        assert(rect_safe(ts[k]->x, ts[k]->y, ts[k]->w, ts[k]->h) && !ts[k]->truncated);
        assert(ts[k]->x >= POWER_CARD_X + 8 && ts[k]->x + ts[k]->w <= POWER_CARD_X + POWER_CARD_W - 8);
        assert(ts[k]->y >= POWER_CARD_Y + 8 && ts[k]->y + ts[k]->h <= POWER_CARD_Y + POWER_CARD_H - 8);
        for (int j = 0; j < k; j++) assert(ts[k]->y >= ts[j]->y + ts[j]->h + 4);  /* stacked, no overlap */
      }
      /* The digit area really shows a different glyph per second. */
      for (int y = 0; y < 85; y++)
        for (int x = 0; x < 45; x++) digit_px[secs][y * 45 + x] = over[(t2->y + y) * 368 + t2->x + x];
      for (int other = 4; other > secs; other--) assert(memcmp(digit_px[secs], digit_px[other], sizeof digit_px[0]));
    }
  }
  /* Repaint rule: a still page repaints when the countdown appears, changes second, or goes away. */
  {
    home_ui a, b;
    settings_state(27, &a);
    b = a;
    assert(home_visual_equal(&a, &b));
    b.power.countdown = true;
    b.power.secs = 4;
    assert(!home_visual_equal(&a, &b) && !home_visual_equal(&b, &a));
    a.power = b.power;
    assert(home_visual_equal(&a, &b));
    b.power.secs = 3;
    assert(!home_visual_equal(&a, &b));
    a.power.countdown = b.power.countdown = false;
    assert(home_visual_equal(&a, &b));
  }
  /* Battery tab: the hint line, exactly, inside the safe area; not on the other tabs. */
  assert(!strcmp(POWER_HINT_TEXT, "Power: press = sleep, hold 5 s = off"));
  for (int i = 0; i < SETTINGS_STATES; i++) {
    home_ui s;
    const char *name = settings_state(i, &s);
    settings_record rec;
    memset(&rec, 0, sizeof rec);
    set_rec = &rec;
    assert(home_render(&s, base, SPARKLES_PIXELS));
    set_rec = NULL;
    const settings_text *h = find_text(&rec, POWER_HINT_TEXT);
    if (s.settings_tab == SETTINGS_BATTERY) {
      assert(h && h->scale == 1 && !h->truncated && h->y == POWER_HINT_Y && rect_safe(h->x, h->y, h->w, h->h));
      for (int k = 0; k < rec.count; k++)
        if (&rec.t[k] != h) assert(rec.t[k].y + rec.t[k].h + 4 <= h->y);  /* below every reading */
      printf("BATTERY_HINT %s at %d,%d %dx%d\n", name, h->x, h->y, h->w, h->h);
    } else
      assert(!h);
  }
  printf("power_ui_test ok overlay_px_total=%d\n", changed_total);
  return 0;
}
