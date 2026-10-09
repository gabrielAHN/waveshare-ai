#pragma once
/* Glowing orbs ("lava lamp") background for the Helper page.
 *
 * Six soft metaballs: field f = sum_i (r_i^2 / (d_i^2 + 1))^2 evaluated ONLY on a coarse
 * ORBS_GW x ORBS_GH grid (4 px spacing), each node reduced to an intensity (0..127) and a
 * warmth (0..7, the field-weighted hue of the orbs covering it). Pixels are bilinearly
 * interpolated from the grid with integer steps and mapped through a small dithered RGB565
 * palette LUT (deep violet -> magenta glow -> coral/amber cores), so the cost per frame is
 * GW*GH*6 field evaluations + one interpolation/LUT lookup per pixel.
 *
 * Motion is wall-clock (seconds, never per-frame): idle = slow drift, listening = swell with
 * the mic level, running = faster orbit and brighter, done = calm. Deliberately a different
 * palette from Sparkles' pale blue/ivory. Native LCD RGB565 (red in the high bits). */
#include <math.h>
#include <stdint.h>
#include <string.h>

#define ORBS_W 368
#define ORBS_H 448
#define ORBS_PIXELS (ORBS_W * ORBS_H)
/* Field grid: 8 px (was 4). The glow is smooth metaballs, and the bilinear spans below hide the grid.
 * 4x fewer field samples; the Ask page render was 49 of its 52 ms in this field (measured on device). */
#define ORBS_STEP 8
#define ORBS_SHIFT2 6  /* log2(ORBS_STEP^2) */
#define ORBS_GW (ORBS_W / ORBS_STEP + 1)
#define ORBS_GH (ORBS_H / ORBS_STEP + 1)
#define ORBS_MAX 7
#define ORBS_COUNT 6
#define ORBS_LEVELS 128
#define ORBS_WARM 8

typedef enum { ORBS_IDLE, ORBS_LISTEN, ORBS_RUN, ORBS_DONE } orbs_mode;
/* tint: per-bot accent (0 sunny yellow, 1 warm orange, 2 pale cream), all in the white/yellow/orange family. */
typedef struct { float time, phase, speed, energy, level, orbit; int ready, tint; } orbs_motion;

static unsigned orbs_field_samples, orbs_field_evals;

static inline float orbs_clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static inline void orbs_targets(orbs_mode mode, float level, float *speed, float *energy, float *orbit) {
  switch (mode) {
    case ORBS_LISTEN: *speed = 1.25f; *energy = 0.95f + 0.35f * level; *orbit = 0; break;
    case ORBS_RUN: *speed = 3.4f; *energy = 1.6f; *orbit = 1; break;
    case ORBS_DONE: *speed = 0.45f; *energy = 0.6f; *orbit = 0; break;
    default: *speed = 1.0f; *energy = 0.8f; *orbit = 0; break;
  }
}

/* Exponential approach with wall-clock dt: identical for one big or many small steps within
 * a smooth segment. Level: fast attack, slower release. */
static inline void orbs_advance(orbs_motion *m, float dt, orbs_mode mode, float level) {
  if (!m || !(dt > 0)) return;
  if (dt > 5) dt = 5;
  level = orbs_clamp(level, 0, 1);
  float speed, energy, orbit;
  orbs_targets(mode, mode == ORBS_LISTEN ? level : 0, &speed, &energy, &orbit);
  if (!m->ready) { m->speed = speed; m->energy = energy; m->orbit = orbit; m->ready = 1; }
  float k = 1 - expf(-dt * 2.2f);
  float target_level = level; /* listening voice, or the reply "talking" */
  float kl = 1 - expf(-dt * (target_level > m->level ? 18.f : 5.f));
  float start_speed = m->speed;
  m->speed += (speed - m->speed) * k;
  m->energy += (energy - m->energy) * k;
  m->orbit += (orbit - m->orbit) * k;
  m->level += (target_level - m->level) * kl;
  m->phase += dt * 0.5f * (start_speed + m->speed) * 0.35f;
  m->time += dt;
}

/* Orb centres/radii (screen pixels). Returns the orb count. */
static inline int orbs_layout(const orbs_motion *m, float *x, float *y, float *r) {
  static const float base_x[ORBS_COUNT] = {92, 276, 150, 232, 70, 300};
  static const float base_r[ORBS_COUNT] = {70, 62, 54, 76, 46, 50};
  static const float wy[ORBS_COUNT] = {0.61f, 0.47f, 0.73f, 0.39f, 0.83f, 0.55f};
  static const float wx[ORBS_COUNT] = {0.29f, 0.37f, 0.23f, 0.31f, 0.41f, 0.19f};
  float phase = m->phase, swell = 1 + 0.55f * m->level, orbit = orbs_clamp(m->orbit, 0, 1);
  for (int i = 0; i < ORBS_COUNT; i++) {
    float p = phase + i * 1.7f;
    float lx = base_x[i] + 46 * sinf(p * wx[i] * 3.1f + i);
    float ly = 224 + 150 * sinf(p * wy[i] * 2.3f + i * 2.1f);
    float a = phase * 0.9f + i * 6.2831853f / ORBS_COUNT;
    float rr = 92 + 22 * sinf(phase * 1.3f + i);
    float ox = 184 + rr * cosf(a), oy = 214 + rr * 1.15f * sinf(a);
    x[i] = lx + (ox - lx) * orbit;
    y[i] = ly + (oy - ly) * orbit;
    float pulse = 1 + 0.06f * sinf(m->time * (1.4f + 0.3f * i) + i);
    r[i] = base_r[i] * swell * pulse * (1 - 0.06f * orbit);
  }
  return ORBS_COUNT;
}

static uint16_t orbs_lut[4][ORBS_WARM][ORBS_LEVELS];
static int orbs_lut_ready, orbs_lut_tint, orbs_lut_dark;
/* Colour theme (theme.h, set by the renderer before orbs_render): 0 = the light paper below, 1 = Dark:
 * the same warm orbs glowing as embers on black paper (AMOLED pixels off), dimmer so the Ask page's
 * light ink stays readable over them. */
static int orbs_theme_dark;

static inline float orbs_mix(float a, float b, float t) { return a + (b - a) * t; }

static inline void orbs_build_lut_dark(int tint);
static inline void orbs_build_lut(int tint) {
  if (orbs_theme_dark) { orbs_build_lut_dark(tint); return; }
  static const float bayer[4] = {-0.375f, 0.125f, 0.375f, -0.125f};
  /* Accent shift of the core/rim/halo colours (tint 0 = the original palette). */
  static const float core_g[3] = {0, -26, -48}, core_b[3] = {0, -6, 52}, rim_g[3] = {0, -30, -46}, rim_b[3] = {0, -20, 44};
  static const float halo_g[3] = {0, -12, -26}, halo_b[3] = {0, -28, 18};  /* coding: coral rose like its bot */
  if (tint < 0 || tint > 2) tint = 0;
  for (int ph = 0; ph < 4; ph++)
    for (int w = 0; w < ORBS_WARM; w++)
      for (int i = 0; i < ORBS_LEVELS; i++) {
        float t = i / (float)(ORBS_LEVELS - 1), warm = w / (float)(ORBS_WARM - 1), R, G, B;
        /* Light theme: warm-white paper, orbs glow from pale lemon halos to golden-yellow cores
         * with just a hint of amber at the very centre. per-orb hue: sunny yellow .. golden. */
        float cr = orbs_mix(255, 252, warm), cg = orbs_mix(206, 186, warm) + core_g[tint], cb = orbs_mix(40, 30, warm) + core_b[tint];
        float er = orbs_mix(255, 255, warm), eg = orbs_mix(226, 212, warm) + rim_g[tint], eb = orbs_mix(86, 70, warm) + rim_b[tint];
        float hr = 255, hg = orbs_mix(246, 240, warm) + halo_g[tint], hb = orbs_mix(196, 184, warm) + halo_b[tint];  /* lemon halo */
        if (t < 0.16f) {            /* warm white paper */
          float u = t / 0.16f;
          R = 255; G = orbs_mix(252, 247, u); B = orbs_mix(247, 238, u);
        } else if (t < 0.46f) {     /* soft peach halo */
          float u = (t - 0.16f) / 0.30f; u = u * u * (3 - 2 * u);
          R = orbs_mix(255, hr, u); G = orbs_mix(247, hg, u); B = orbs_mix(238, hb, u);
        } else if (t < 0.60f) {     /* glowing orange rim */
          float u = (t - 0.46f) / 0.14f;
          R = orbs_mix(hr, er, u); G = orbs_mix(hg, eg, u); B = orbs_mix(hb, eb, u);
        } else {                    /* molten orange core with a hot golden centre */
          float u = (t - 0.60f) / 0.40f;
          R = orbs_mix(er, cr, u); G = orbs_mix(eg, cg, u); B = orbs_mix(eb, cb, u);
          if (u > 0.8f) { float h = (u - 0.8f) / 0.2f; R = orbs_mix(R, 250, h * .35f); G = orbs_mix(G, 170, h * .35f); B = orbs_mix(B, 30, h * .35f); }
        }
        float d = bayer[ph];
        int r = (int)(R / 8 + d + 0.5f), g = (int)(G / 4 + d + 0.5f), b = (int)(B / 8 + d + 0.5f);
        r = r < 0 ? 0 : (r > 31 ? 31 : r); g = g < 0 ? 0 : (g > 63 ? 63 : g); b = b < 0 ? 0 : (b > 31 ? 31 : b);
        orbs_lut[ph][w][i] = (uint16_t)((r << 11) | (g << 5) | b);
      }
  orbs_lut_ready = 1;
  orbs_lut_tint = tint;
  orbs_lut_dark = 0;
}
/* Dark: black paper -> deep ember halo -> amber rim -> golden-orange core (the light LUT's stops and
 * per-bot accent shifts, scaled down). The brightest entry stays near (176,92,16): a soft glow that
 * keeps the bubbles' light ink readable and most of the panel dark. */
static inline void orbs_build_lut_dark(int tint) {
  static const float bayer[4] = {-0.375f, 0.125f, 0.375f, -0.125f};
  static const float core_g[3] = {0, -10, -18}, core_b[3] = {0, -2, 22}, rim_g[3] = {0, -10, -16}, rim_b[3] = {0, -5, 18};
  static const float halo_g[3] = {0, -3, -7}, halo_b[3] = {0, -5, 7};
  if (tint < 0 || tint > 2) tint = 0;
  for (int ph = 0; ph < 4; ph++)
    for (int w = 0; w < ORBS_WARM; w++)
      for (int i = 0; i < ORBS_LEVELS; i++) {
        float t = i / (float)(ORBS_LEVELS - 1), warm = w / (float)(ORBS_WARM - 1), R, G, B;
        float cr = orbs_mix(166, 162, warm), cg = orbs_mix(92, 80, warm) + core_g[tint], cb = orbs_mix(16, 10, warm) + core_b[tint];
        float er = orbs_mix(124, 120, warm), eg = orbs_mix(62, 54, warm) + rim_g[tint], eb = orbs_mix(12, 8, warm) + rim_b[tint];
        float hr = 52, hg = orbs_mix(25, 22, warm) + halo_g[tint], hb = orbs_mix(7, 5, warm) + halo_b[tint];
        if (t < 0.16f) {            /* black paper */
          float u = t / 0.16f;
          R = orbs_mix(0, 5, u); G = orbs_mix(0, 2, u); B = 0;
        } else if (t < 0.46f) {     /* deep ember halo */
          float u = (t - 0.16f) / 0.30f; u = u * u * (3 - 2 * u);
          R = orbs_mix(5, hr, u); G = orbs_mix(2, hg, u); B = orbs_mix(0, hb, u);
        } else if (t < 0.60f) {     /* amber rim */
          float u = (t - 0.46f) / 0.14f;
          R = orbs_mix(hr, er, u); G = orbs_mix(hg, eg, u); B = orbs_mix(hb, eb, u);
        } else {                    /* golden-orange core */
          float u = (t - 0.60f) / 0.40f;
          R = orbs_mix(er, cr, u); G = orbs_mix(eg, cg, u); B = orbs_mix(eb, cb, u);
          if (u > 0.8f) { float h = (u - 0.8f) / 0.2f; R = orbs_mix(R, 176, h * .35f); G = orbs_mix(G, 86, h * .35f); B = orbs_mix(B, 12, h * .35f); }
        }
        float d = bayer[ph];
        int r = (int)(R / 8 + d + 0.5f), g = (int)(G / 4 + d + 0.5f), b = (int)(B / 8 + d + 0.5f);
        r = r < 0 ? 0 : (r > 31 ? 31 : r); g = g < 0 ? 0 : (g > 63 ? 63 : g); b = b < 0 ? 0 : (b > 31 ? 31 : b);
        if (t < 0.02f) r = g = b = 0;  /* true black where there is no glow: those pixels stay off */
        orbs_lut[ph][w][i] = (uint16_t)((r << 11) | (g << 5) | b);
      }
  orbs_lut_ready = 1;
  orbs_lut_tint = tint;
  orbs_lut_dark = 1;
}

static uint8_t orbs_gi[ORBS_GH][ORBS_GW], orbs_gw[ORBS_GH][ORBS_GW];

static inline void orbs_render(const orbs_motion *m, uint16_t *p) {
  static const float warmth[ORBS_COUNT] = {1.0f, 0.45f, 0.8f, 0.6f, 0.95f, 0.35f};
  if (!m || !p) return;
  if (!orbs_lut_ready || orbs_lut_tint != (m->tint >= 0 && m->tint <= 2 ? m->tint : 0) || orbs_lut_dark != (orbs_theme_dark != 0)) orbs_build_lut(m->tint);
  float x[ORBS_MAX], y[ORBS_MAX], r[ORBS_MAX], r2[ORBS_MAX];
  int n = orbs_layout(m, x, y, r);
  for (int i = 0; i < n; i++) r2[i] = r[i] * r[i];
  float gain = (0.62f + 0.48f * orbs_clamp(m->energy, 0, 2)) * 0.5f * (ORBS_LEVELS - 1);
  orbs_field_samples = orbs_field_evals = 0;
  for (int gy = 0; gy < ORBS_GH; gy++) {
    float py = (float)(gy * ORBS_STEP);
    for (int gx = 0; gx < ORBS_GW; gx++) {
      float px = (float)(gx * ORBS_STEP), f = 0, fw = 0;
      for (int i = 0; i < n; i++) {
        float dx = px - x[i], dy = py - y[i], v = r2[i] / (dx * dx + dy * dy + 1);
        v *= v;  /* squared kernel: tight glowing balls that still merge like wax */
        f += v; fw += v * warmth[i];
      }
      orbs_field_evals += (unsigned)n;
      float level = f * gain, soft = level / (1 + level * (1.0f / (ORBS_LEVELS - 1)));  /* soft knee, no flat clip */
      orbs_gi[gy][gx] = (uint8_t)(soft > ORBS_LEVELS - 1 ? ORBS_LEVELS - 1 : soft);
      float w = f > 1e-6f ? fw / f : 0;
      orbs_gw[gy][gx] = (uint8_t)(w * (ORBS_WARM - 1) + 0.5f);
    }
    orbs_field_samples += ORBS_GW;
  }
  /* Integer bilinear: row lerp of the two grid rows (scaled x8), then 8-pixel spans (x64). */
  int ri[ORBS_GW], rw[ORBS_GW];
  for (int yy = 0; yy < ORBS_H; yy++) {
    int gy = yy / ORBS_STEP, fy = yy % ORBS_STEP;
    const uint8_t *i0 = orbs_gi[gy], *i1 = orbs_gi[gy + 1], *w0 = orbs_gw[gy], *w1 = orbs_gw[gy + 1];
    for (int gx = 0; gx < ORBS_GW; gx++) {
      ri[gx] = i0[gx] * (ORBS_STEP - fy) + i1[gx] * fy;
      rw[gx] = w0[gx] * (ORBS_STEP - fy) + w1[gx] * fy;
    }
    uint16_t *row = p + yy * ORBS_W;
    const uint16_t (*lut0)[ORBS_LEVELS] = orbs_lut[(yy & 1) * 2], (*lut1)[ORBS_LEVELS] = orbs_lut[(yy & 1) * 2 + 1];
    for (int gx = 0; gx < ORBS_GW - 1; gx++) {
      int a = ri[gx] * ORBS_STEP, da = ri[gx + 1] - ri[gx], b = rw[gx] * ORBS_STEP + ORBS_STEP * ORBS_STEP / 2, db = rw[gx + 1] - rw[gx];
      uint16_t *out = row + gx * ORBS_STEP;
      for (int k = 0; k < ORBS_STEP; k += 2) {
        out[k] = lut0[b >> ORBS_SHIFT2][a >> ORBS_SHIFT2];
        a += da; b += db;
        out[k + 1] = lut1[b >> ORBS_SHIFT2][a >> ORBS_SHIFT2];
        a += da; b += db;
      }
    }
  }
}
