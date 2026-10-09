#pragma once
/* Soft anti-aliased shapes for the warm UI (Ask bots, Home Ask tile, Settings battery): rounded boxes,
 * discs, capsules, arcs and triangles on the 368x448 native RGB565 frame (sp_pack_lcd colours).
 * Every loop is bounded to the shape's own box (clipped to the screen). Rounded boxes and discs -- the
 * big areas -- fill each row's fully covered span with plain stores (or one blend per pixel when
 * translucent) and work out coverage only for the few edge pixels at its ends; the small shapes
 * (eyes, mouths, emblems) evaluate a distance per pixel inside their small box. Unions of many
 * discs/boxes (Kotaro's curly head, ears, body: ss_union / ss_union2) work row by row the same way:
 * solid spans inside, coverage only on the shared outline.
 * `fe` = edge feather in px (1 = crisp anti-aliased edge, larger = soft glow / shadow). `a` = 0..255. */
#include <math.h>
#include <stdint.h>
#include "sparkles.h"  /* sp_blend565, sp_pack_lcd */

#define SS_W 368
#define SS_H 448
static inline void ss_px(uint16_t *row, int x, uint16_t c, unsigned a) {
  if (a >= 252) row[x] = c;
  else if (a >= 4) row[x] = sp_blend565(row[x], c, a);
}
static inline int ss_lo(float v, int lim) { int i = (int)floorf(v); return i < 0 ? 0 : (i > lim ? lim : i); }
static inline int ss_hi(float v, int lim) { int i = (int)ceilf(v); return i < 0 ? 0 : (i > lim ? lim : i); }
static inline void ss_span(uint16_t *row, int x0, int x1, uint16_t c, unsigned a) {
  if (x0 < 0) x0 = 0;
  if (x1 > SS_W) x1 = SS_W;
  if (a >= 252) { for (int x = x0; x < x1; x++) row[x] = c; }
  else if (a >= 4) { for (int x = x0; x < x1; x++) row[x] = sp_blend565(row[x], c, a); }
}
/* A rectangle the rounded box must not paint (the solid middle of a later, opaque shape): halos
 * around the bot skip the pixels its body covers anyway. NULL = none. */
typedef struct { float x0, y0, x1, y1, r; } ss_hole;
/* Half-width of the solid (fully covered) span of a rounded box's row at |dy| from its centre, or -1. */
static inline float ss_row_in(float hw, float hh, float r, float dy, float hf) {
  float ey = dy - (hh - r);
  if (ey <= 0) return hw - hf;
  float ri = r - hf;
  return ri > ey ? (hw - r) + sqrtf(ri * ri - ey * ey) : -1.f;
}
/* Horizontal stripes for a filled union (a striped t-shirt): rows alternate between the fill colour and
 * c2 every `band` px from y0 (even bands = fill colour); a row that straddles a band edge is blended. */
typedef struct { float y0, band; uint16_t c2; } ss_stripes;
static inline uint16_t ss_stripe_row(const ss_stripes *st, uint16_t c, int y) {
  float v = (y - st->y0) / st->band;
  int k = (int)floorf(v);
  float edge = (k + 1 - v) * st->band;  /* px of this row still in band k */
  uint16_t ck = (k & 1) ? st->c2 : c, cn = (k & 1) ? c : st->c2;
  if (edge >= 1.f) return ck;
  return sp_blend565(ck, cn, (unsigned)((1.f - edge) * 255.f + .5f));
}
static inline void ss_rrect_hole(uint16_t *p, float x0, float y0, float x1, float y1, float r, uint16_t c, unsigned a, float fe, const ss_hole *hole) {
  if (x1 <= x0 || y1 <= y0 || !a) return;
  if (fe < 0.75f) fe = 0.75f;
  float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f, hw = (x1 - x0) * .5f, hh = (y1 - y0) * .5f, hf = fe * .5f, inv = 1.f / fe;
  if (r > hw) r = hw;
  if (r > hh) r = hh;
  if (r < hf) r = hf;
  float hcx = 0, hcy = 0, hhw = 0, hhh = 0, hr = 0;
  if (hole) {
    hcx = (hole->x0 + hole->x1) * .5f; hcy = (hole->y0 + hole->y1) * .5f;
    hhw = (hole->x1 - hole->x0) * .5f; hhh = (hole->y1 - hole->y0) * .5f;
    hr = hole->r > hhw ? hhw : (hole->r > hhh ? hhh : hole->r);
  }
  int ya = ss_lo(cy - hh - hf, SS_H), yb = ss_hi(cy + hh + hf, SS_H);
  for (int y = ya; y < yb; y++) {
    float dy = fabsf(y + .5f - cy), ey = dy - (hh - r), out;
    if (ey <= 0) out = hw + hf;
    else {
      float ro = r + hf;
      if (ey >= ro) continue;
      out = (hw - r) + sqrtf(ro * ro - ey * ey);
    }
    float in = ss_row_in(hw, hh, r, dy, hf);
    int xa = ss_lo(cx - out, SS_W), xb = ss_hi(cx + out, SS_W), ia = xb, ib = xb;
    if (in >= 0) {  /* pixels whose centre is >= hf inside: full coverage */
      ia = (int)ceilf(cx - in - .5f); ib = (int)floorf(cx + in - .5f) + 1;
      if (ia < xa) ia = xa;
      if (ib > xb) ib = xb;
      if (ib < ia) ib = ia;
    }
    uint16_t *row = p + y * SS_W;
    for (int pass = 0; pass < 2; pass++) {  /* left then right edge band: per-pixel coverage */
      int e0 = pass ? ib : xa, e1 = pass ? xb : ia;
      for (int x = e0; x < e1; x++) {
        float dx = fabsf(x + .5f - cx), qx = dx - (hw - r), d;
        if (qx > 0 && ey > 0) d = sqrtf(qx * qx + ey * ey) - r;
        else d = (qx > ey ? qx : ey) - r;
        float cov = .5f - d * inv;
        if (cov > 0) ss_px(row, x, c, (unsigned)((cov > 1 ? 1 : cov) * (float)a + .5f));
      }
    }
    if (hole) {  /* skip the hole's solid middle on this row */
      float hdy = fabsf(y + .5f - hcy), hin = hdy < hhh ? ss_row_in(hhw, hhh, hr, hdy, 1.f) : -1.f;
      if (hin > 0) {
        int ha = (int)ceilf(hcx - hin), hb = (int)floorf(hcx + hin);
        if (ha < ia) ha = ia;
        if (hb > ib) hb = ib;
        if (hb > ha) { ss_span(row, ia, ha, c, a); ss_span(row, hb, ib, c, a); continue; }
      }
    }
    ss_span(row, ia, ib, c, a);
  }
}
/* Rounded box [x0,x1) x [y0,y1), corner radius r. */
static inline void ss_rrect(uint16_t *p, float x0, float y0, float x1, float y1, float r, uint16_t c, unsigned a, float fe) {
  ss_rrect_hole(p, x0, y0, x1, y1, r, c, a, fe, 0);
}
static inline void ss_disc(uint16_t *p, float cx, float cy, float r, uint16_t c, unsigned a, float fe) {
  ss_rrect(p, cx - r, cy - r, cx + r, cy + r, r, c, a, fe);
}
/* Union of rounded boxes / discs (a disc: hw = hh = r), each grown by g px, in one colour (optionally
 * striped): a scalloped outline of overlapping curls costs one pass. Per row every shape gives a
 * touched and a fully covered interval (two square roots); the covered intervals are filled once as
 * spans and only the pixels on the union's outline get a coverage (the largest over the shapes that
 * touch them, first-order distance to the corner circle: no per-pixel root). Each pixel is written once,
 * so translucent unions don't double up where the shapes overlap. ss_union_ring also skips what the
 * same shapes grown by only g_in (< g - 1, may be negative) cover fully: a rim band under later parts.
 * ss_union2 draws a part in one pass: its contour (the shapes grown by lw, colour `line`) round its
 * fill -- the band between gets the fill blended in by the fill's coverage. */
typedef struct { float cx, cy, hw, hh, r; } ss_shape;
#define SS_UNION_MAX 32
static inline float ss_shape_r(const ss_shape *s, float g) {
  float hw = s->hw + g, hh = s->hh + g, r = s->r + g;
  if (r > hw) r = hw;
  if (r > hh) r = hh;
  return r < .5f ? .5f : r;
}
/* Fully covered pixel run [*i0, *i1] of shape s grown by g on the row at py; false if none. */
static inline bool ss_shape_solid(const ss_shape *s, float g, float py, int *i0, int *i1) {
  float hw = s->hw + g, hh = s->hh + g;
  if (hw <= .5f || hh <= .5f) return false;
  float r = ss_shape_r(s, g), ey = fabsf(py - s->cy) - (hh - r), in;
  if (ey <= 0) in = hw - .5f;
  else {
    float ri = r - .5f;
    if (ri <= ey) return false;
    in = (hw - r) + sqrtf(ri * ri - ey * ey);
  }
  *i0 = (int)ceilf(s->cx - in - .5f); *i1 = (int)floorf(s->cx + in - .5f);
  return *i1 >= *i0;
}
/* Insert run [a0,a1] into the start-sorted list (sa,sb)[0..*n), then (caller) merge. */
static inline void ss_run_insert(int16_t *sa, int16_t *sb, int *n, int a0, int a1) {
  int j = (*n)++;
  while (j > 0 && sa[j - 1] > a0) { sa[j] = sa[j - 1]; sb[j] = sb[j - 1]; j--; }
  sa[j] = (int16_t)a0; sb[j] = (int16_t)a1;
}
static inline int ss_run_merge(int16_t *sa, int16_t *sb, int n) {
  int k = 0;
  for (int j = 0; j < n; j++) {
    if (sb[j] < sa[j]) continue;
    if (k && sa[j] <= sb[k - 1] + 1) { if (sb[j] > sb[k - 1]) sb[k - 1] = sb[j]; }
    else { sa[k] = sa[j]; sb[k] = sb[j]; k++; }
  }
  return k;
}
/* Shape indices sorted by top edge (cy - half height), so each row only visits the shapes it crosses. */
static inline void ss_by_top(const ss_shape *s, const float *hh, int n, int16_t *oy) {
  for (int i = 0; i < n; i++) {
    int j = i;
    while (j > 0 && s[oy[j - 1]].cy - hh[oy[j - 1]] > s[i].cy - hh[i]) { oy[j] = oy[j - 1]; j--; }
    oy[j] = (int16_t)i;
  }
}
static inline void ss_union_core(uint16_t *p, const ss_shape *s, int n, float g, float g_in, uint16_t c0, unsigned a, const ss_stripes *st) {
  if (n <= 0 || !a) return;
  if (n > SS_UNION_MAX) n = SS_UNION_MAX;
  float top = 1e9f, bot = -1e9f, hw[SS_UNION_MAX], hh[SS_UNION_MAX], rr[SS_UNION_MAX], i2r[SS_UNION_MAX];
  for (int i = 0; i < n; i++) {  /* grown sizes, once */
    hw[i] = s[i].hw + g; hh[i] = s[i].hh + g; rr[i] = ss_shape_r(&s[i], g); i2r[i] = .5f / rr[i];
    if (s[i].cy - hh[i] < top) top = s[i].cy - hh[i];
    if (s[i].cy + hh[i] > bot) bot = s[i].cy + hh[i];
  }
  int ya = ss_lo(top - .5f, SS_H), yb = ss_hi(bot + .5f, SS_H);
  int16_t act[SS_UNION_MAX], oa[SS_UNION_MAX], ob[SS_UNION_MAX], ord[SS_UNION_MAX], cand[SS_UNION_MAX];
  int16_t sa[SS_UNION_MAX], sb[SS_UNION_MAX], ka[SS_UNION_MAX], kb[SS_UNION_MAX], oy[SS_UNION_MAX], live[SS_UNION_MAX];
  int nlive = 0, nx = 0;
  float eys[SS_UNION_MAX];
  ss_by_top(s, hh, n, oy);
  for (int y = ya; y < yb; y++) {
    float py = y + .5f;
    int m = 0, ns = 0, nk = 0, keep = 0;
    while (nx < n && s[oy[nx]].cy - hh[oy[nx]] - .5f < py) live[nlive++] = oy[nx++];
    for (int q = 0; q < nlive; q++) {
      int i = live[q];
      if (s[i].cy + hh[i] + .5f <= py) continue;  /* finished above this row */
      live[keep++] = i;
      float r = rr[i], ey = fabsf(py - s[i].cy) - (hh[i] - r), out, in;
      if (ey >= r + .5f) continue;
      if (ey <= 0) { out = hw[i] + .5f; in = hw[i] - .5f; }
      else {
        float ro = r + .5f, ri = r - .5f;
        out = (hw[i] - r) + sqrtf(ro * ro - ey * ey);
        in = ri > ey ? (hw[i] - r) + sqrtf(ri * ri - ey * ey) : -1.f;
      }
      int o0 = (int)ceilf(s[i].cx - out - .5f), o1 = (int)floorf(s[i].cx + out - .5f);
      if (o0 < 0) o0 = 0;
      if (o1 > SS_W - 1) o1 = SS_W - 1;
      if (o1 < o0) continue;
      act[m] = i; eys[m] = ey; oa[m] = o0; ob[m] = o1;
      if (in >= 0) {  /* fully covered pixels */
        int i0 = (int)ceilf(s[i].cx - in - .5f), i1 = (int)floorf(s[i].cx + in - .5f);
        ss_run_insert(sa, sb, &ns, i0 < o0 ? o0 : i0, i1 > o1 ? o1 : i1);
        int k0, k1;
        if (g_in > -1e8f && ss_shape_solid(&s[i], g_in, py, &k0, &k1)) ss_run_insert(ka, kb, &nk, k0, k1);
      }
      m++;
    }
    nlive = keep;
    if (!m) continue;
    ns = ss_run_merge(sa, sb, ns);
    nk = ss_run_merge(ka, kb, nk);
    for (int k = 0; k < m; k++) {  /* touched intervals by start */
      int j = k;
      while (j > 0 && oa[ord[j - 1]] > oa[k]) { ord[j] = ord[j - 1]; j--; }
      ord[j] = k;
    }
    uint16_t *row = p + y * SS_W, c = st ? ss_stripe_row(st, c0, y) : c0;
    int x = 0, si = 0, ki = 0;
    for (int k = 0; k < m; k++) {
      int q = ord[k];
      if (ob[q] < x) continue;
      if (oa[q] > x) x = oa[q];
      int end = ob[q];  /* extend over every touched interval that overlaps / abuts this one */
      while (k + 1 < m && oa[ord[k + 1]] <= end + 1) { k++; if (ob[ord[k]] > end) end = ob[ord[k]]; }
      while (x <= end) {
        while (si < ns && sb[si] < x) si++;
        if (si < ns && sa[si] <= x) {  /* covered run, minus what a later fill covers */
          int e = sb[si] > end ? end : sb[si];
          while (x <= e) {
            while (ki < nk && kb[ki] < x) ki++;
            if (ki < nk && ka[ki] <= x) { x = kb[ki] + 1; continue; }
            int e2 = ki < nk && ka[ki] - 1 < e ? ka[ki] - 1 : e;
            ss_span(row, x, e2 + 1, c, a);
            x = e2 + 1;
          }
          x = e + 1;
          continue;
        }
        int stop = si < ns && sa[si] - 1 < end ? sa[si] - 1 : end, nc = 0;
        for (int j = 0; j < m; j++)  /* shapes that touch this stretch of outline */
          if (oa[j] <= stop && ob[j] >= x) cand[nc++] = j;
        for (; x <= stop; x++) {
          float px = x + .5f, cov = 0;
          for (int q2 = 0; q2 < nc && cov < 1.f; q2++) {
            int j = cand[q2], i = act[j];
            if (oa[j] > x || ob[j] < x) continue;
            float r = rr[i], qx = fabsf(px - s[i].cx) - (hw[i] - r), ey = eys[j], d;
            if (ey <= 0) d = qx - r;
            else if (qx <= 0) d = ey - r;
            else d = (qx * qx + ey * ey - r * r) * i2r[i];
            if (.5f - d > cov) cov = .5f - d;
          }
          if (cov > 0) ss_px(row, x, c, (unsigned)((cov > 1 ? 1 : cov) * (float)a + .5f));
        }
      }
    }
  }
}
static inline void ss_union2(uint16_t *p, const ss_shape *s, int n, float lw, uint16_t line, uint16_t fill0, const ss_stripes *st) {
  if (n <= 0) return;
  if (n > SS_UNION_MAX) n = SS_UNION_MAX;
  float top = 1e9f, bot = -1e9f, rl[SS_UNION_MAX], r0[SS_UNION_MAX], hhl[SS_UNION_MAX], il[SS_UNION_MAX], i0r[SS_UNION_MAX];
  for (int i = 0; i < n; i++) {
    rl[i] = ss_shape_r(&s[i], lw); r0[i] = ss_shape_r(&s[i], 0); hhl[i] = s[i].hh + lw; il[i] = .5f / rl[i]; i0r[i] = .5f / r0[i];
    if (s[i].cy - s[i].hh - lw < top) top = s[i].cy - s[i].hh - lw;
    if (s[i].cy + s[i].hh + lw > bot) bot = s[i].cy + s[i].hh + lw;
  }
  int ya = ss_lo(top - .5f, SS_H), yb = ss_hi(bot + .5f, SS_H);
  int16_t act[SS_UNION_MAX], oa[SS_UNION_MAX], ob[SS_UNION_MAX], ta[SS_UNION_MAX], tb[SS_UNION_MAX], ord[SS_UNION_MAX], cand[SS_UNION_MAX];
  int16_t la[SS_UNION_MAX], lb[SS_UNION_MAX], fa[SS_UNION_MAX], fb[SS_UNION_MAX], oy[SS_UNION_MAX], live[SS_UNION_MAX];
  float eyl[SS_UNION_MAX], ey0[SS_UNION_MAX];
  int nlive = 0, nx = 0;
  ss_by_top(s, hhl, n, oy);
  for (int y = ya; y < yb; y++) {
    float py = y + .5f;
    int m = 0, nl = 0, nf = 0, keep = 0;
    while (nx < n && s[oy[nx]].cy - hhl[oy[nx]] - .5f < py) live[nlive++] = oy[nx++];
    for (int q2 = 0; q2 < nlive; q2++) {
      int i = live[q2];
      if (s[i].cy + hhl[i] + .5f <= py) continue;  /* finished above this row */
      live[keep++] = i;
      float dy = fabsf(py - s[i].cy), r = rl[i], hw = s[i].hw + lw, ey = dy - (s[i].hh + lw - r), out, in;
      if (ey >= r + .5f) continue;
      if (ey <= 0) { out = hw + .5f; in = hw - .5f; }
      else {
        float ro = r + .5f, ri = r - .5f;
        out = (hw - r) + sqrtf(ro * ro - ey * ey);
        in = ri > ey ? (hw - r) + sqrtf(ri * ri - ey * ey) : -1.f;
      }
      int o0 = (int)ceilf(s[i].cx - out - .5f), o1 = (int)floorf(s[i].cx + out - .5f);
      if (o0 < 0) o0 = 0;
      if (o1 > SS_W - 1) o1 = SS_W - 1;
      if (o1 < o0) continue;
      act[m] = i; eyl[m] = ey; oa[m] = o0; ob[m] = o1; ta[m] = 1; tb[m] = 0;
      if (in >= 0) {
        int i0 = (int)ceilf(s[i].cx - in - .5f), i1 = (int)floorf(s[i].cx + in - .5f);
        ss_run_insert(la, lb, &nl, i0 < o0 ? o0 : i0, i1 > o1 ? o1 : i1);
      }
      /* the fill shape (grown by 0) on this row: touched + fully covered */
      float q = r0[i], e0 = dy - (s[i].hh - q), out0, in0 = -1.f;
      ey0[m] = e0;
      if (e0 < q + .5f) {
        if (e0 <= 0) { out0 = s[i].hw + .5f; in0 = s[i].hw - .5f; }
        else {
          float ro = q + .5f, ri = q - .5f;
          out0 = (s[i].hw - q) + sqrtf(ro * ro - e0 * e0);
          if (ri > e0) in0 = (s[i].hw - q) + sqrtf(ri * ri - e0 * e0);
        }
        ta[m] = (int)ceilf(s[i].cx - out0 - .5f); tb[m] = (int)floorf(s[i].cx + out0 - .5f);
        if (in0 >= 0) {
          int i0 = (int)ceilf(s[i].cx - in0 - .5f), i1 = (int)floorf(s[i].cx + in0 - .5f);
          ss_run_insert(fa, fb, &nf, i0 < o0 ? o0 : i0, i1 > o1 ? o1 : i1);
        }
      }
      m++;
    }
    nlive = keep;
    if (!m) continue;
    nl = ss_run_merge(la, lb, nl);
    nf = ss_run_merge(fa, fb, nf);
    for (int k = 0; k < m; k++) {
      int j = k;
      while (j > 0 && oa[ord[j - 1]] > oa[k]) { ord[j] = ord[j - 1]; j--; }
      ord[j] = k;
    }
    uint16_t *row = p + y * SS_W, fill = st ? ss_stripe_row(st, fill0, y) : fill0;
    int x = 0, li = 0, fi = 0;
    for (int k = 0; k < m; k++) {
      int q = ord[k];
      if (ob[q] < x) continue;
      if (oa[q] > x) x = oa[q];
      int end = ob[q];
      while (k + 1 < m && oa[ord[k + 1]] <= end + 1) { k++; if (ob[ord[k]] > end) end = ob[ord[k]]; }
      while (x <= end) {
        while (fi < nf && fb[fi] < x) fi++;
        while (li < nl && lb[li] < x) li++;
        if (fi < nf && fa[fi] <= x) {  /* inside the fill */
          int e = fb[fi] > end ? end : fb[fi];
          ss_span(row, x, e + 1, fill, 255);
          x = e + 1;
          continue;
        }
        bool band = li < nl && la[li] <= x;  /* fully inside the contour: line, with the fill's edge blended in */
        int stop = band ? lb[li] : (li < nl ? la[li] - 1 : end);
        if (fi < nf && fa[fi] - 1 < stop) stop = fa[fi] - 1;
        if (stop > end) stop = end;
        int nc = 0, t0 = SS_W, t1 = -1;
        for (int j = 0; j < m; j++) {
          if (band ? (ta[j] <= stop && tb[j] >= x) : (oa[j] <= stop && ob[j] >= x)) {
            cand[nc++] = j;
            if (band && ta[j] < t0) t0 = ta[j];
            if (band && tb[j] > t1) t1 = tb[j];
          }
        }
        if (band) {  /* line outside every fill shape this stretch touches: plain span */
          if (t0 > x) {
            int e = t0 - 1 < stop ? t0 - 1 : stop;
            ss_span(row, x, e + 1, line, 255);
            x = e + 1;
          }
          if (t1 < stop) {
            if (t1 >= x) stop = t1;
            else { ss_span(row, x, stop + 1, line, 255); x = stop + 1; continue; }
          }
        }
        for (; x <= stop; x++) {
          float px = x + .5f, cov = 0;
          for (int c2 = 0; c2 < nc && cov < 1.f; c2++) {
            int j = cand[c2], i = act[j];
            float r, qx, ey, d, ir;
            if (band) {
              if (ta[j] > x || tb[j] < x) continue;
              r = r0[i]; qx = fabsf(px - s[i].cx) - (s[i].hw - r); ey = ey0[j]; ir = i0r[i];
            } else {
              if (oa[j] > x || ob[j] < x) continue;
              r = rl[i]; qx = fabsf(px - s[i].cx) - (s[i].hw + lw - r); ey = eyl[j]; ir = il[i];
            }
            if (ey <= 0) d = qx - r;
            else if (qx <= 0) d = ey - r;
            else d = (qx * qx + ey * ey - r * r) * ir;
            if (.5f - d > cov) cov = .5f - d;
          }
          if (band) row[x] = cov <= 0 ? line : (cov >= 1 ? fill : sp_blend565(line, fill, (unsigned)(cov * 255.f + .5f)));
          else if (cov > 0) ss_px(row, x, line, (unsigned)((cov > 1 ? 1 : cov) * 255.f + .5f));
        }
      }
    }
  }
}
static inline void ss_union(uint16_t *p, const ss_shape *s, int n, float g, uint16_t c, unsigned a, const ss_stripes *st) {
  ss_union_core(p, s, n, g, -1e9f, c, a, st);
}
static inline void ss_union_ring(uint16_t *p, const ss_shape *s, int n, float g, float g_in, uint16_t c, unsigned a) {
  ss_union_core(p, s, n, g, g_in, c, a, 0);
}
/* Thick segment with round caps (radius r). */
static inline void ss_capsule(uint16_t *p, float ax, float ay, float bx, float by, float r, uint16_t c, unsigned a, float fe) {
  if (!a || r <= 0) return;
  if (fe < 0.75f) fe = 0.75f;
  float hf = fe * .5f, inv = 1.f / fe, R = r + hf, vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
  float il2 = l2 > 1e-6f ? 1.f / l2 : 0, rin = r - hf, rin2 = rin > 0 ? rin * rin : -1.f, R2 = R * R;
  int x0 = ss_lo((ax < bx ? ax : bx) - R, SS_W), x1 = ss_hi((ax > bx ? ax : bx) + R, SS_W);
  int y0 = ss_lo((ay < by ? ay : by) - R, SS_H), y1 = ss_hi((ay > by ? ay : by) + R, SS_H);
  for (int y = y0; y < y1; y++) {
    uint16_t *row = p + y * SS_W;
    float py = y + .5f - ay;
    for (int x = x0; x < x1; x++) {
      float px = x + .5f - ax, t = (px * vx + py * vy) * il2;
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      float dx = px - t * vx, dy = py - t * vy, d2 = dx * dx + dy * dy;
      if (d2 >= R2) continue;
      if (d2 <= rin2) { ss_px(row, x, c, a); continue; }
      float cov = .5f - (sqrtf(d2) - r) * inv;
      if (cov > 0) ss_px(row, x, c, (unsigned)((cov > 1 ? 1 : cov) * (float)a + .5f));
    }
  }
}
/* Ring stroke (radius r, half-width w) where the direction from the centre is within `half` rad of
 * the unit vector (ux,uy); round caps. Smile: (0,1). Frown / happy eye: (0,-1). */
static inline void ss_arc(uint16_t *p, float cx, float cy, float r, float w, float ux, float uy, float half, uint16_t c, unsigned a) {
  if (!a || r <= 0 || w <= 0) return;
  float ch = cosf(half), sh = sinf(half), R = r + w + 1, ri = r - w - 1;
  float R2 = R * R, ri2 = ri > 0 ? ri * ri : 0;
  int x0 = ss_lo(cx - R, SS_W), x1 = ss_hi(cx + R, SS_W), y0 = ss_lo(cy - R, SS_H), y1 = ss_hi(cy + R, SS_H);
  for (int y = y0; y < y1; y++) {
    uint16_t *row = p + y * SS_W;
    float py = y + .5f - cy;
    for (int x = x0; x < x1; x++) {
      float px = x + .5f - cx, d2 = px * px + py * py;
      if (d2 > R2 || d2 < ri2 || d2 < 1e-4f) continue;
      float dist = sqrtf(d2);
      if (px * ux + py * uy < ch * dist) continue;
      float cov = .5f - (fabsf(dist - r) - w);
      if (cov > 0) ss_px(row, x, c, (unsigned)((cov > 1 ? 1 : cov) * (float)a + .5f));
    }
  }
  float e1x = ux * ch - uy * sh, e1y = ux * sh + uy * ch, e2x = ux * ch + uy * sh, e2y = -ux * sh + uy * ch;
  ss_disc(p, cx + e1x * r, cy + e1y * r, w, c, a, 1);
  ss_disc(p, cx + e2x * r, cy + e2y * r, w, c, a, 1);
}
/* Filled triangle, anti-aliased by the distance to its nearest edge (edge normals worked out once). */
static inline void ss_tri(uint16_t *p, float ax, float ay, float bx, float by, float cx, float cy, uint16_t c, unsigned a) {
  float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  if (!a || fabsf(area) < 1e-3f) return;
  float s = area > 0 ? -1.f : 1.f, vx[3] = {ax, bx, cx}, vy[3] = {ay, by, cy}, nx[3], ny[3], k0[3];
  for (int i = 0; i < 3; i++) {  /* inside distance of edge i at (px,py) = nx*px + ny*py + k0 */
    float ex = vx[(i + 1) % 3] - vx[i], ey = vy[(i + 1) % 3] - vy[i], l = sqrtf(ex * ex + ey * ey);
    nx[i] = s * ey / l; ny[i] = -s * ex / l; k0[i] = -(nx[i] * vx[i] + ny[i] * vy[i]);
  }
  int x0 = ss_lo(fminf(ax, fminf(bx, cx)) - 1, SS_W), x1 = ss_hi(fmaxf(ax, fmaxf(bx, cx)) + 1, SS_W);
  int y0 = ss_lo(fminf(ay, fminf(by, cy)) - 1, SS_H), y1 = ss_hi(fmaxf(ay, fmaxf(by, cy)) + 1, SS_H);
  for (int y = y0; y < y1; y++) {
    uint16_t *row = p + y * SS_W;
    float py = y + .5f;
    for (int x = x0; x < x1; x++) {
      float px = x + .5f;
      float d = fminf(nx[0] * px + ny[0] * py + k0[0], fminf(nx[1] * px + ny[1] * py + k0[1], nx[2] * px + ny[2] * py + k0[2]));
      float cov = d + .5f;
      if (cov > 0) ss_px(row, x, c, (unsigned)((cov > 1 ? 1 : cov) * (float)a + .5f));
    }
  }
}
/* Four-point spark: four slim rays from the centre (rotated by `angle`) + a round heart. */
static inline void ss_spark(uint16_t *p, float cx, float cy, float R, float angle, uint16_t c, unsigned a) {
  float w = R * .3f;
  for (int k = 0; k < 4; k++) {
    float an = angle + k * 1.5707963f, ux = cosf(an), uy = sinf(an);
    ss_tri(p, cx + ux * R, cy + uy * R, cx - uy * w, cy + ux * w, cx + uy * w, cy - ux * w, c, a);
  }
  ss_disc(p, cx, cy, w * 1.15f, c, a, 1);
}
static inline uint16_t ss_rgb(const uint8_t c[3]) { return sp_pack_lcd(c[0], c[1], c[2]); }
