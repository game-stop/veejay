/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "eventhorizon.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define EH_PARAMS 13

#define P_BUILD   0
#define P_SOURCE  1
#define P_FLOW    2
#define P_SWIRL   3
#define P_SMOKE   4
#define P_DECAY   5
#define P_DENSITY 6
#define P_LENS    7
#define P_FOLDS   8
#define P_SPIN    9
#define P_TRAIL   10
#define P_CHROMA  11
#define P_SPEED   12

#define EH_TRIG_SIZE 1024
#define EH_TRIG_MASK 1023
#define EH_TWO_PI 6.28318530718f
#define EH_INV_TWO_PI 0.15915494309f
#define EH_GATE 6

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int frame;
    int n_threads;
    void *region;
    uint8_t *src_y;
    uint8_t *paint_y;
    uint8_t *paint_u;
    uint8_t *paint_v;
    uint8_t *next_y;
    uint8_t *next_u;
    uint8_t *next_v;
    uint8_t *prev_y;
    uint8_t *ref_y;
    uint8_t *on_y;
    uint8_t *off_y;
    uint8_t *veil;
    uint8_t *nx_on_y;
    uint8_t *nx_off_y;
    uint8_t *nx_veil;
    float *xnorm;
    float *grid_x;
    float *grid_y;
    float *force_x;
    float *force_y;
    float *force_l;
    float *force_m;
    float *drop_a;
    float *drop_b;
    float *drop_tmp;
    float *flow_xf;
    float *flow_yf;
    float *force_xf;
    float *force_yf;
    uint16_t *flow_xi;
    uint16_t *flow_yi;
    uint16_t *force_xi;
    uint16_t *force_yi;
    int grid_capacity;
    int grid_w;
    int grid_h;
    int flow_map_cell;
    int flow_map_w;
    int flow_map_h;
    int force_w;
    int force_h;
    int force_cell;
    int force_map_cell;
    int force_map_w;
    int force_map_h;
    int drop_w;
    int drop_h;
    int drop_cell;
    uint8_t event_lut[256];
    uint8_t smoke_decay_lut[256];
    uint8_t veil_decay_lut[256];
    uint8_t rise_lut[256];
    uint8_t spread_lut[256];
    uint8_t density_lut[256];
    uint8_t adapt_lut[256];
    uint8_t slave_blend_lut[256];
    uint8_t gamma_lut[256];
    uint8_t tone_lut[256];
    float sin_lut[EH_TRIG_SIZE];
    float cos_lut[EH_TRIG_SIZE];
    int luts_valid;
    int tone_lut_valid;
    int last_tone_mix;
    int last_smoke;
    int last_decay;
    int last_density;
    int last_flow;
    int last_lens;
    float maturity;
    float time;
    float orbit;
    float spin_phase1;
    float spin_phase2;
    float pulse;
    float pal_amber_u;
    float pal_amber_v;
    float pal_cobalt_u;
    float pal_cobalt_v;
    float pal_rose_u;
    float pal_rose_v;
    float pal_teal_u;
    float pal_teal_v;
    float pal_haze_u;
    float pal_haze_v;
    float pal_gain;
    float pal_haze_gain;
} eventhorizon_t;

static inline int eh_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float eh_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int eh_absi(int v)
{
    int m = v >> 31;
    return (v + m) ^ m;
}

static inline float eh_absf(float x)
{
    union { float f; unsigned int i; } u;
    u.f = x;
    u.i &= 0x7fffffffU;
    return u.f;
}

static inline uint8_t eh_u8i(int v)
{
    return (uint8_t) eh_clampi(v, 0, 255);
}

static inline uint8_t eh_u8f(float v)
{
    if (v <= 0.0f)
        return 0;
    if (v >= 255.0f)
        return 255;
    return (uint8_t) (v + 0.5f);
}

static inline int eh_q10_from_float(float v)
{
    return (int) (v * 1024.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

static inline float eh_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float eh_smooth01(float t)
{
    t = eh_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline float eh_triangle_weight(float x, float center, float width)
{
    float d = eh_absf(x - center) / width;
    return eh_clampf(1.0f - d, 0.0f, 1.0f);
}

static void eh_update_palette(eventhorizon_t *e, float chroma_t)
{
    float q = eh_clampf(chroma_t, 0.0f, 1.0f);
    float warm = eh_triangle_weight(q, 0.00f, 0.38f) + eh_triangle_weight(q, 1.00f, 0.46f) * 0.58f;
    float blue = eh_triangle_weight(q, 0.36f, 0.42f) + eh_triangle_weight(q, 0.88f, 0.42f) * 0.35f;
    float violet = eh_triangle_weight(q, 0.56f, 0.34f);
    float teal = eh_triangle_weight(q, 0.82f, 0.40f);
    float sum = warm + blue + violet + teal + 1.0e-6f;
    float hue_push = eh_smooth01(q);

    warm /= sum;
    blue /= sum;
    violet /= sum;
    teal /= sum;

    e->pal_gain = q * (0.42f + q * 0.74f);
    e->pal_haze_gain = q * (0.30f + q * 0.46f);

    e->pal_amber_u = -24.0f * (0.70f + warm * 1.42f + teal * 0.18f);
    e->pal_amber_v =  36.0f * (0.72f + warm * 1.35f + violet * 0.20f);

    e->pal_cobalt_u =  30.0f * (0.54f + blue * 1.55f + teal * 0.50f);
    e->pal_cobalt_v =   7.0f * (0.56f + blue * 0.90f) - 7.0f * teal * hue_push;

    e->pal_rose_u =  12.0f * (0.34f + violet * 1.12f + warm * 0.25f);
    e->pal_rose_v =  30.0f * (0.36f + violet * 1.25f + warm * 0.24f);

    e->pal_teal_u =  21.0f * (0.22f + teal * 1.72f + blue * 0.38f);
    e->pal_teal_v =  -5.0f * (0.16f + teal * 1.50f);

    e->pal_haze_u =  blue * 12.0f + teal * 15.0f + violet * 5.0f - warm * 7.0f;
    e->pal_haze_v =  warm * 10.0f + violet * 9.0f + blue * 4.0f - teal * 3.0f;
}

static inline uint8_t eh_blend_u8(uint8_t a, uint8_t b, int amount)
{
    return (uint8_t) (((int) a * (256 - amount) + (int) b * amount) >> 8);
}

static inline int eh_floor_to_int(float v)
{
    int i = (int) v;
    if (v < (float) i)
        i--;
    return i;
}

static inline float eh_wrap_2pi(float v)
{
    if (v >= EH_TWO_PI || v < 0.0f) {
        int k = (int) (v * EH_INV_TWO_PI);
        v -= (float) k * EH_TWO_PI;
        if (v < 0.0f)
            v += EH_TWO_PI;
        else if (v >= EH_TWO_PI)
            v -= EH_TWO_PI;
    }
    return v;
}

static inline float eh_fast_rsqrt(float x)
{
    union { float f; unsigned int i; } u;
    float y;
    if (x <= 0.0f)
        return 0.0f;
    u.f = x;
    u.i = 0x5f3759dfU - (u.i >> 1);
    y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

static inline float eh_lut_sin(const eventhorizon_t *e, float phase)
{
    float fidx = phase * ((float) EH_TRIG_SIZE * EH_INV_TWO_PI);
    int idx0 = eh_floor_to_int(fidx);
    float frac = fidx - (float) idx0;
    int i0 = idx0 & EH_TRIG_MASK;
    int i1 = (i0 + 1) & EH_TRIG_MASK;
    float a = e->sin_lut[i0];
    float b = e->sin_lut[i1];
    return a + (b - a) * frac;
}


static inline __attribute__((always_inline)) float eh_lut_sin_fast(const eventhorizon_t *e, float phase)
{
    int idx = (int) (phase * ((float) EH_TRIG_SIZE * EH_INV_TWO_PI));
    return e->sin_lut[idx & EH_TRIG_MASK];
}

static inline void eh_lut_sincos(const eventhorizon_t *e, float phase, float *s, float *c)
{
    float fidx = phase * ((float) EH_TRIG_SIZE * EH_INV_TWO_PI);
    int idx0 = eh_floor_to_int(fidx);
    float frac = fidx - (float) idx0;
    int i0 = idx0 & EH_TRIG_MASK;
    int i1 = (i0 + 1) & EH_TRIG_MASK;
    float sa = e->sin_lut[i0];
    float sb = e->sin_lut[i1];
    float ca = e->cos_lut[i0];
    float cb = e->cos_lut[i1];
    *s = sa + (sb - sa) * frac;
    *c = ca + (cb - ca) * frac;
}

static inline size_t eh_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

static inline void eh_store4_u8(uint8_t *restrict p, uint8_t v)
{
    uint32_t q = 0x01010101U * (uint32_t) v;
    memcpy(p, &q, sizeof(q));
}

static inline __attribute__((always_inline)) void eh_store2_u8(uint8_t *restrict p, uint8_t v)
{
    p[0] = v;
    p[1] = v;
}

static inline __attribute__((always_inline)) int eh_cross5_y(const uint8_t *restrict p, int pos, int w)
{
    return ((int) p[pos] * 4 + (int) p[pos - 1] + (int) p[pos + 1] +
            (int) p[pos - w] + (int) p[pos + w] + 4) >> 3;
}

static inline __attribute__((always_inline)) int eh_escape_smooth_y(int y, int ref, int threshold, int q)
{
    if (q > 0) {
        int d = eh_absi(y - ref);
        if (d > threshold)
            y = (y * (1024 - q) + ref * q + 512) >> 10;
    }
    return y;
}

static inline __attribute__((always_inline)) uint8_t eh_blend_u8_q8(uint8_t a, uint8_t b, int q)
{
    return (uint8_t) (((int) a * (256 - q) + (int) b * q + 128) >> 8);
}

static inline uint32_t eh_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float eh_hash_signed(uint32_t x)
{
    return ((float) (eh_hash_u32(x) & 0xffffU) * (1.0f / 65535.0f)) * 2.0f - 1.0f;
}

static inline float eh_grid_noise(int x, int y, uint32_t seed, uint32_t salt)
{
    return eh_hash_signed(((uint32_t) x * 374761393U) ^ ((uint32_t) y * 668265263U) ^ (seed * 2246822519U) ^ salt);
}

static inline int eh_index_clamped(int w, int h, int x, int y)
{
    if (x < 0)
        x = 0;
    else if (x >= w)
        x = w - 1;
    if (y < 0)
        y = 0;
    else if (y >= h)
        y = h - 1;
    return y * w + x;
}

static inline void eh_limit_chroma(float *u, float *v, float limit)
{
    float au = eh_absf(*u);
    float av = eh_absf(*v);
    float m = au > av ? au : av;
    if (m > limit && m > 0.000001f) {
        float s = limit / m;
        *u *= s;
        *v *= s;
    }
}

static void eh_build_axis_map(int n,
                              int cell,
                              int max_g,
                              uint16_t *restrict idx,
                              float *restrict frac)
{
    int i;
    float inv_cell = 1.0f / (float) cell;

    for (i = 0; i < n; i++) {
        int g = i / cell;
        int l = i - g * cell;
        float f = (float) l * inv_cell;

        if (g < 0)
            g = 0;
        else if (g + 1 >= max_g)
            g = max_g - 2;

        f = f * f * (3.0f - 2.0f * f);
        idx[i] = (uint16_t) g;
        frac[i] = f;
    }
}

static void eh_build_flow_grid(eventhorizon_t *e, int cell, float persistence)
{
    int gw = (e->w + cell - 1) / cell + 2;
    int gh = (e->h + cell - 1) / cell + 2;
    uint32_t seed0 = (uint32_t) e->frame >> 4;
    uint32_t seed1 = seed0 + 1U;
    float phase = (float) ((uint32_t) e->frame & 15U) * (1.0f / 16.0f);
    int gx;
    int gy;

    if (gw * gh > e->grid_capacity)
        return;

    e->grid_w = gw;
    e->grid_h = gh;
    if (e->flow_map_cell != cell || e->flow_map_w != gw || e->flow_map_h != gh) {
        eh_build_axis_map(e->w, cell, gw, e->flow_xi, e->flow_xf);
        eh_build_axis_map(e->h, cell, gh, e->flow_yi, e->flow_yf);
        e->flow_map_cell = cell;
        e->flow_map_w = gw;
        e->flow_map_h = gh;
    }
    persistence = eh_clampf(persistence, 0.0f, 0.94f);
    {
        float keep = persistence;
        float fresh = 1.0f - persistence;

    for (gy = 0; gy < gh; gy++) {
        for (gx = 0; gx < gw; gx++) {
            int gi = gy * gw + gx;
            float ax0 = eh_grid_noise(gx, gy, seed0, 0x1234abcdU);
            float ay0 = eh_grid_noise(gx, gy, seed0, 0x9182a6f1U);
            float ax1 = eh_grid_noise(gx, gy, seed1, 0x1234abcdU);
            float ay1 = eh_grid_noise(gx, gy, seed1, 0x9182a6f1U);
            float tx = eh_lerpf(ax0, ax1, phase);
            float ty = eh_lerpf(ay0, ay1, phase);
            e->grid_x[gi] = e->grid_x[gi] * keep + tx * fresh;
            e->grid_y[gi] = e->grid_y[gi] * keep + ty * fresh;
        }
    }
    }
}

static inline __attribute__((always_inline)) void eh_sample_flow_grid(const eventhorizon_t *e, int x, int y, int cell, float *vx, float *vy)
{
    (void) cell;
    int gx = e->flow_xi[x];
    int gy = e->flow_yi[y];
    float fx = e->flow_xf[x];
    float fy = e->flow_yf[y];
    float wx0;
    float wy0;
    int gw = e->grid_w;
    int gh = e->grid_h;
    int gi00;
    int gi10;
    int gi01;
    int gi11;
    float ax;
    float bx;
    float ay;
    float by;

    (void) gw;
    (void) gh;
    wx0 = 1.0f - fx;
    wy0 = 1.0f - fy;

    gi00 = gy * gw + gx;
    gi10 = gi00 + 1;
    gi01 = gi00 + gw;
    gi11 = gi01 + 1;

    ax = e->grid_x[gi00] * wy0 + e->grid_x[gi01] * fy;
    bx = e->grid_x[gi10] * wy0 + e->grid_x[gi11] * fy;
    ay = e->grid_y[gi00] * wy0 + e->grid_y[gi01] * fy;
    by = e->grid_y[gi10] * wy0 + e->grid_y[gi11] * fy;

    *vx = ax * wx0 + bx * fx;
    *vy = ay * wx0 + by * fx;
}


static void eh_build_light_force_grid(eventhorizon_t *e,
                                      const uint8_t *restrict src_y,
                                      const uint8_t *restrict prev_y,
                                      int cell,
                                      float gravity_gain,
                                      float vortex_gain,
                                      float lens_curve,
                                      float flow_t,
                                      float motion_scale,
                                      float persistence)
{
    int w = e->w;
    int h = e->h;
    int gw = (w + cell - 1) / cell + 2;
    int gh = (h + cell - 1) / cell + 2;
    int gx;
    int gy;

    if (gw * gh > e->grid_capacity)
        return;

    e->force_w = gw;
    e->force_h = gh;
    e->force_cell = cell;
    if (e->force_map_cell != cell || e->force_map_w != gw || e->force_map_h != gh) {
        eh_build_axis_map(e->w, cell, gw, e->force_xi, e->force_xf);
        eh_build_axis_map(e->h, cell, gh, e->force_yi, e->force_yf);
        e->force_map_cell = cell;
        e->force_map_w = gw;
        e->force_map_h = gh;
    }
    persistence = eh_clampf(persistence, 0.0f, 0.90f);
    {
        float keep = persistence;
        float fresh = 1.0f - persistence;
        int step2_const = cell * 3;
        if (step2_const > 48)
            step2_const = 48;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (gy = 0; gy < gh; gy++) {
        for (gx = 0; gx < gw; gx++) {
            int x = (gx - 1) * cell;
            int y = (gy - 1) * cell;
            int pos;
            int step1 = cell;
            int step2 = step2_const;
            int xl1;
            int xr1;
            int yu1;
            int yd1;
            int xl2;
            int xr2;
            int yu2;
            int yd2;
            int cy0;
            int dl1;
            int dr1;
            int du1;
            int dd1;
            int dl2;
            int dr2;
            int du2;
            int dd2;
            int gx_luma;
            int gy_luma;
            int dt_luma;
            int gi = gy * gw + gx;
            float light_t;
            float lgx;
            float lgy;
            float grad_light;
            float grad2;
            float nf_x;
            float nf_y;
            float motion_abs;
            float local_est;
            float gravity_scale;
            float vortex_scale;
            float motion_factor;
            float fx;
            float fy;

            if (x < 0)
                x = 0;
            else if (x >= w)
                x = w - 1;
            if (y < 0)
                y = 0;
            else if (y >= h)
                y = h - 1;

            xl1 = x - step1;
            xr1 = x + step1;
            yu1 = y - step1;
            yd1 = y + step1;
            xl2 = x - step2;
            xr2 = x + step2;
            yu2 = y - step2;
            yd2 = y + step2;

            if (xl1 < 0) xl1 = 0;
            if (xr1 >= w) xr1 = w - 1;
            if (yu1 < 0) yu1 = 0;
            if (yd1 >= h) yd1 = h - 1;
            if (xl2 < 0) xl2 = 0;
            if (xr2 >= w) xr2 = w - 1;
            if (yu2 < 0) yu2 = 0;
            if (yd2 >= h) yd2 = h - 1;

            pos = y * w + x;
            cy0 = src_y[pos];

            dl1 = (int) src_y[y * w + xl1];
            dr1 = (int) src_y[y * w + xr1];
            du1 = (int) src_y[yu1 * w + x];
            dd1 = (int) src_y[yd1 * w + x];
            dl2 = (int) src_y[y * w + xl2];
            dr2 = (int) src_y[y * w + xr2];
            du2 = (int) src_y[yu2 * w + x];
            dd2 = (int) src_y[yd2 * w + x];

            gx_luma = dr1 - dl1;
            gy_luma = dd1 - du1;
            dt_luma = cy0 - (int) prev_y[pos];

            light_t = (float) cy0 * (1.0f / 255.0f);
            lgx = ((float) (dr1 - dl1) * 0.68f + (float) (dr2 - dl2) * 0.32f) * (1.0f / 255.0f);
            lgy = ((float) (dd1 - du1) * 0.68f + (float) (dd2 - du2) * 0.32f) * (1.0f / 255.0f);
            grad_light = eh_absf(lgx) + eh_absf(lgy);
            if (grad_light > 1.45f)
                grad_light = 1.45f;

            grad2 = (float) (gx_luma * gx_luma + gy_luma * gy_luma) + 260.0f;
            nf_x = -((float) dt_luma * (float) gx_luma) / grad2;
            nf_y = -((float) dt_luma * (float) gy_luma) / grad2;
            nf_x = eh_clampf(nf_x, -2.4f, 2.4f);
            nf_y = eh_clampf(nf_y, -2.4f, 2.4f);
            motion_abs = eh_absf(nf_x) + eh_absf(nf_y);
            if (motion_abs > 2.0f)
                motion_abs = 2.0f;

            local_est = eh_smooth01((light_t * 1.16f + grad_light * 0.44f + motion_abs * 0.12f - 0.16f) * 1.85f);
            gravity_scale = gravity_gain * (0.20f + local_est * 2.00f + light_t * 0.92f);
            vortex_scale = vortex_gain * (0.18f + local_est * 1.42f + grad_light * 0.75f);
            motion_factor = (0.85f + lens_curve * 3.40f + flow_t * 3.50f) * (0.38f + local_est * 1.48f) * motion_scale;

            fx = lgx * gravity_scale + (-lgy) * vortex_scale + nf_x * motion_factor;
            fy = lgy * gravity_scale + ( lgx) * vortex_scale + nf_y * motion_factor;

            e->force_x[gi] = e->force_x[gi] * keep + fx * fresh;
            e->force_y[gi] = e->force_y[gi] * keep + fy * fresh;
            e->force_l[gi] = e->force_l[gi] * keep + light_t * fresh;
            e->force_m[gi] = e->force_m[gi] * keep + (grad_light * 0.86f + motion_abs * 0.045f) * fresh;
        }
    }
    }
}

static inline __attribute__((always_inline)) void eh_sample_force_grid(const eventhorizon_t *e,
                                        int x,
                                        int y,
                                        float *fx_out,
                                        float *fy_out,
                                        float *light_out,
                                        float *motion_out)
{
    int cell = e->force_cell;
    int gx;
    int gy;
    float fx;
    float fy;
    float wx0;
    float wy0;
    int gw = e->force_w;
    int gh = e->force_h;
    int gi00;
    int gi10;
    int gi01;
    int gi11;
    float ax;
    float bx;
    float ay;
    float by;
    float al;
    float bl;
    float am;
    float bm;

    if (cell <= 0 || gw < 2 || gh < 2) {
        *fx_out = 0.0f;
        *fy_out = 0.0f;
        *light_out = 0.0f;
        *motion_out = 0.0f;
        return;
    }

    gx = e->force_xi[x];
    gy = e->force_yi[y];
    fx = e->force_xf[x];
    fy = e->force_yf[y];

    wx0 = 1.0f - fx;
    wy0 = 1.0f - fy;

    gi00 = gy * gw + gx;
    gi10 = gi00 + 1;
    gi01 = gi00 + gw;
    gi11 = gi01 + 1;

    ax = e->force_x[gi00] * wy0 + e->force_x[gi01] * fy;
    bx = e->force_x[gi10] * wy0 + e->force_x[gi11] * fy;
    ay = e->force_y[gi00] * wy0 + e->force_y[gi01] * fy;
    by = e->force_y[gi10] * wy0 + e->force_y[gi11] * fy;
    al = e->force_l[gi00] * wy0 + e->force_l[gi01] * fy;
    bl = e->force_l[gi10] * wy0 + e->force_l[gi11] * fy;
    am = e->force_m[gi00] * wy0 + e->force_m[gi01] * fy;
    bm = e->force_m[gi10] * wy0 + e->force_m[gi11] * fy;

    *fx_out = ax * wx0 + bx * fx;
    *fy_out = ay * wx0 + by * fx;
    *light_out = al * wx0 + bl * fx;
    *motion_out = am * wx0 + bm * fx;
}


static void eh_update_drop_field(eventhorizon_t *e,
                                 int cell,
                                 float lens_curve,
                                 float flow_t,
                                 float smoke_t,
                                 float density_t,
                                 float trail_t,
                                 float motion_scale)
{
    int gw = e->force_w;
    int gh = e->force_h;
    int gy;
    float damp;
    float impulse_gain;

    if (gw < 3 || gh < 3 || gw * gh > e->grid_capacity)
        return;

    if (e->drop_w != gw || e->drop_h != gh || e->drop_cell != cell) {
        size_t n = (size_t) gw * (size_t) gh;
        veejay_memset(e->drop_a, 0, sizeof(float) * n);
        veejay_memset(e->drop_b, 0, sizeof(float) * n);
        veejay_memset(e->drop_tmp, 0, sizeof(float) * n);
        e->drop_w = gw;
        e->drop_h = gh;
        e->drop_cell = cell;
    }

    damp = 0.948f + trail_t * 0.030f - flow_t * 0.030f - smoke_t * 0.012f;
    damp = eh_clampf(damp, 0.880f, 0.982f);
    impulse_gain = (0.018f + lens_curve * 0.052f + flow_t * 0.028f + density_t * 0.030f + smoke_t * 0.024f) * (0.45f + motion_scale * 0.23f);

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (gy = 1; gy < gh - 1; gy++) {
        int gx;
        int row = gy * gw;
        for (gx = 1; gx < gw - 1; gx++) {
            int i = row + gx;
            float c = e->drop_a[i];
            float n4 = e->drop_a[i - 1] + e->drop_a[i + 1] + e->drop_a[i - gw] + e->drop_a[i + gw];
            float nd = e->drop_a[i - gw - 1] + e->drop_a[i - gw + 1] + e->drop_a[i + gw - 1] + e->drop_a[i + gw + 1];
            float light = e->force_l[i];
            float motion = e->force_m[i];
            float pull = eh_absf(e->force_x[i]) + eh_absf(e->force_y[i]);
            float impulse = (light * (0.35f + motion * 0.60f) + pull * 0.012f - 0.10f) * impulse_gain;
            float smooth = n4 * 0.500f + nd * 0.125f;
            float next = (smooth - e->drop_b[i]) * damp + impulse;

            next = next * 0.972f + c * 0.028f;
            if (next > 3.25f)
                next = 3.25f;
            else if (next < -3.25f)
                next = -3.25f;
            e->drop_tmp[i] = next;
        }
    }

    {
        float *tmp = e->drop_b;
        e->drop_b = e->drop_a;
        e->drop_a = e->drop_tmp;
        e->drop_tmp = tmp;
    }
}

static inline __attribute__((always_inline)) void eh_sample_drop_grid(const eventhorizon_t *e,
                                                                      int x,
                                                                      int y,
                                                                      float *dx_out,
                                                                      float *dy_out,
                                                                      float *h_out)
{
    int gw = e->drop_w;
    int gh = e->drop_h;
    int gx;
    int gy;
    float fx;
    float fy;
    float wx0;
    float wy0;
    int gi00;
    int gi10;
    int gi01;
    int gi11;
    float h00;
    float h10;
    float h01;
    float h11;
    float ax;
    float bx;

    if (gw < 2 || gh < 2 || e->drop_cell <= 0) {
        *dx_out = 0.0f;
        *dy_out = 0.0f;
        *h_out = 0.0f;
        return;
    }

    gx = e->force_xi[x];
    gy = e->force_yi[y];
    fx = e->force_xf[x];
    fy = e->force_yf[y];

    if (gx < 0)
        gx = 0;
    else if (gx + 1 >= gw)
        gx = gw - 2;
    if (gy < 0)
        gy = 0;
    else if (gy + 1 >= gh)
        gy = gh - 2;

    wx0 = 1.0f - fx;
    wy0 = 1.0f - fy;
    gi00 = gy * gw + gx;
    gi10 = gi00 + 1;
    gi01 = gi00 + gw;
    gi11 = gi01 + 1;

    h00 = e->drop_a[gi00];
    h10 = e->drop_a[gi10];
    h01 = e->drop_a[gi01];
    h11 = e->drop_a[gi11];

    ax = h00 * wy0 + h01 * fy;
    bx = h10 * wy0 + h11 * fy;
    *h_out = ax * wx0 + bx * fx;
    *dx_out = bx - ax;
    *dy_out = (h01 * wx0 + h11 * fx) - (h00 * wx0 + h10 * fx);
}

static inline void eh_sample_uv_nearest(const uint8_t *restrict U,
                                        const uint8_t *restrict V,
                                        float fx,
                                        float fy,
                                        int w,
                                        int h,
                                        int *ou,
                                        int *ov)
{
    int ix;
    int iy;
    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);
    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);
    ix = (int) (fx + 0.5f);
    iy = (int) (fy + 0.5f);
    if (ix < 0)
        ix = 0;
    else if (ix >= w)
        ix = w - 1;
    if (iy < 0)
        iy = 0;
    else if (iy >= h)
        iy = h - 1;
    ix += iy * w;
    *ou = U[ix];
    *ov = V[ix];
}


static inline int eh_sample_y_bilinear(const uint8_t *restrict Y,
                                       float fx,
                                       float fy,
                                       int w,
                                       int h)
{
    int ix;
    int iy;
    int wx;
    int wy;
    int x1;
    int y1;
    int p00;
    int p10;
    int p01;
    int p11;
    int a;
    int b;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);
    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);

    ix = (int) fx;
    iy = (int) fy;
    wx = (int) ((fx - (float) ix) * 256.0f);
    wy = (int) ((fy - (float) iy) * 256.0f);
    x1 = ix + 1;
    if (x1 >= w)
        x1 = ix;
    y1 = iy + 1;
    if (y1 >= h)
        y1 = iy;

    p00 = iy * w + ix;
    p10 = iy * w + x1;
    p01 = y1 * w + ix;
    p11 = y1 * w + x1;

    a = (int) Y[p00] * (256 - wx) + (int) Y[p10] * wx;
    b = (int) Y[p01] * (256 - wx) + (int) Y[p11] * wx;
    return ((a * (256 - wy) + b * wy) + 32768) >> 16;
}

static void eh_build_luts(eventhorizon_t *e, int smoke, int decay, int density, int flow, int lens)
{
    int i;
    int threshold;
    int denom;
    int decay_power;
    int veil_power;
    int rise_power;
    int spread_power;
    int density_power;

    if (e->luts_valid &&
        e->last_smoke == smoke &&
        e->last_decay == decay &&
        e->last_density == density &&
        e->last_flow == flow &&
        e->last_lens == lens)
        return;

    threshold = 12 + ((100 - smoke) * 126 + 50) / 100;
    denom = 255 - threshold;
    if (denom < 1)
        denom = 1;

    decay_power = 82 + (decay * 173 + 50) / 100;
    if (decay_power > 255)
        decay_power = 255;

    veil_power = decay_power + ((255 - decay_power) >> 2);
    if (veil_power > 255)
        veil_power = 255;

    rise_power = 68 + (flow * 96 + density * 38 + 100) / 200;
    if (rise_power > 255)
        rise_power = 255;

    spread_power = 48 + (flow * 56 + density * 76 + lens * 22 + 100) / 200;
    if (spread_power > 255)
        spread_power = 255;

    density_power = 74 + (density * 181 + 50) / 100;
    if (density_power > 255)
        density_power = 255;

    for (i = 0; i < 256; i++) {
        int event_strength = 0;
        int gain;
        int excess;
        int mem;

        if (i > threshold) {
            excess = i - threshold;
            gain = 86 + density + smoke + (flow >> 1);
            event_strength = (excess * gain + denom / 2) / denom;
            if (event_strength > 255)
                event_strength = 255;
        }

        e->event_lut[i] = (uint8_t) event_strength;
        e->smoke_decay_lut[i] = (uint8_t) ((i * decay_power + 127) / 255);
        e->veil_decay_lut[i] = (uint8_t) ((i * veil_power + 127) / 255);
        e->rise_lut[i] = (uint8_t) ((i * rise_power + 127) / 255);
        e->spread_lut[i] = (uint8_t) ((i * spread_power + 127) / 255);
        e->density_lut[i] = (uint8_t) ((i * density_power + 127) / 255);
        mem = 3 + ((100 - decay) >> 2) + (event_strength >> 4);
        if (mem > 255)
            mem = 255;
        e->adapt_lut[i] = (uint8_t) mem;
    }

    e->last_smoke = smoke;
    e->last_decay = decay;
    e->last_density = density;
    e->last_flow = flow;
    e->last_lens = lens;
    e->luts_valid = 1;
}

vj_effect *eventhorizon_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve)
        return NULL;

    ve->num_params = EH_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if (!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if (ve->defaults) free(ve->defaults);
        if (ve->limits[0]) free(ve->limits[0]);
        if (ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_BUILD] = 0; ve->limits[1][P_BUILD] = 100; ve->defaults[P_BUILD] = 72;
    ve->limits[0][P_SOURCE] = 0; ve->limits[1][P_SOURCE] = 100; ve->defaults[P_SOURCE] = 100;
    ve->limits[0][P_FLOW] = 0; ve->limits[1][P_FLOW] = 100; ve->defaults[P_FLOW] = 19;
    ve->limits[0][P_SWIRL] = 0; ve->limits[1][P_SWIRL] = 100; ve->defaults[P_SWIRL] = 18;
    ve->limits[0][P_SMOKE] = 0; ve->limits[1][P_SMOKE] = 100; ve->defaults[P_SMOKE] = 99;
    ve->limits[0][P_DECAY] = 0; ve->limits[1][P_DECAY] = 100; ve->defaults[P_DECAY] = 85;
    ve->limits[0][P_DENSITY] = 0; ve->limits[1][P_DENSITY] = 100; ve->defaults[P_DENSITY] = 100;
    ve->limits[0][P_LENS] = 0; ve->limits[1][P_LENS] = 100; ve->defaults[P_LENS] = 46;
    ve->limits[0][P_FOLDS] = 0; ve->limits[1][P_FOLDS] = 100; ve->defaults[P_FOLDS] = 72;
    ve->limits[0][P_SPIN] = -100; ve->limits[1][P_SPIN] = 100; ve->defaults[P_SPIN] = 100;
    ve->limits[0][P_TRAIL] = 0; ve->limits[1][P_TRAIL] = 100; ve->defaults[P_TRAIL] = 50;
    ve->limits[0][P_CHROMA] = 0; ve->limits[1][P_CHROMA] = 100; ve->defaults[P_CHROMA] = 78;
    ve->limits[0][P_SPEED] = -100; ve->limits[1][P_SPEED] = 100; ve->defaults[P_SPEED] = 56;

    ve->description = "Event Horizon Ink";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Build Speed",
        "Source Feed",
        "Liquid Flow",
        "Swirl Memory",
        "Ignition",
        "Decay",
        "Density",
        "Light Gravity",
        "River Detail",
        "Well Spin",
        "Trail Memory",
        "Nebula Palette",
        "Motion Speed"
    );

    (void) w;
    (void) h;
    return ve;
}

void *eventhorizon_malloc(int w, int h)
{
    eventhorizon_t *e;
    unsigned char *base;
    size_t len;
    size_t total;
    size_t off;
    size_t gridcap;
    int i;

    if (w <= 0 || h <= 0)
        return NULL;

    len = (size_t) w * (size_t) h;
    if (len == 0)
        return NULL;

    e = (eventhorizon_t *) vj_calloc(sizeof(eventhorizon_t));
    if (!e)
        return NULL;

    e->w = w;
    e->h = h;
    e->len = (int) len;
    e->seeded = 0;
    e->frame = 0;
    e->n_threads = vje_advise_num_threads((int) len);
    if (e->n_threads <= 0)
        e->n_threads = 1;

    gridcap = (size_t) (((w + 7) / 8) + 2) * (size_t) (((h + 7) / 8) + 2);
    total = len * 15 + sizeof(float) * ((size_t) w + gridcap * 9 + ((size_t) w + (size_t) h) * 4) + sizeof(uint16_t) * ((size_t) w + (size_t) h) * 4 + 128;
    e->region = vj_malloc(total);
    if (!e->region) {
        free(e);
        return NULL;
    }

    base = (unsigned char *) e->region;
    off = 0;

    e->src_y = (uint8_t *) (base + off); off += len;
    e->paint_y = (uint8_t *) (base + off); off += len;
    e->paint_u = (uint8_t *) (base + off); off += len;
    e->paint_v = (uint8_t *) (base + off); off += len;
    e->next_y = (uint8_t *) (base + off); off += len;
    e->next_u = (uint8_t *) (base + off); off += len;
    e->next_v = (uint8_t *) (base + off); off += len;
    e->prev_y = (uint8_t *) (base + off); off += len;
    e->ref_y = (uint8_t *) (base + off); off += len;
    e->on_y = (uint8_t *) (base + off); off += len;
    e->off_y = (uint8_t *) (base + off); off += len;
    e->veil = (uint8_t *) (base + off); off += len;
    e->nx_on_y = (uint8_t *) (base + off); off += len;
    e->nx_off_y = (uint8_t *) (base + off); off += len;
    e->nx_veil = (uint8_t *) (base + off); off += len;

    off = eh_align_size(off, sizeof(float));
    e->xnorm = (float *) (base + off); off += sizeof(float) * (size_t) w;
    e->grid_capacity = (int) gridcap;
    e->grid_x = (float *) (base + off); off += sizeof(float) * gridcap;
    e->grid_y = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_x = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_y = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_l = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_m = (float *) (base + off); off += sizeof(float) * gridcap;
    e->drop_a = (float *) (base + off); off += sizeof(float) * gridcap;
    e->drop_b = (float *) (base + off); off += sizeof(float) * gridcap;
    e->drop_tmp = (float *) (base + off); off += sizeof(float) * gridcap;
    e->flow_xf = (float *) (base + off); off += sizeof(float) * (size_t) w;
    e->flow_yf = (float *) (base + off); off += sizeof(float) * (size_t) h;
    e->force_xf = (float *) (base + off); off += sizeof(float) * (size_t) w;
    e->force_yf = (float *) (base + off); off += sizeof(float) * (size_t) h;
    off = eh_align_size(off, sizeof(uint16_t));
    e->flow_xi = (uint16_t *) (base + off); off += sizeof(uint16_t) * (size_t) w;
    e->flow_yi = (uint16_t *) (base + off); off += sizeof(uint16_t) * (size_t) h;
    e->force_xi = (uint16_t *) (base + off); off += sizeof(uint16_t) * (size_t) w;
    e->force_yi = (uint16_t *) (base + off); off += sizeof(uint16_t) * (size_t) h;

    veejay_memset(e->src_y, 0, len);
    veejay_memset(e->paint_y, 0, len);
    veejay_memset(e->paint_u, 128, len);
    veejay_memset(e->paint_v, 128, len);
    veejay_memset(e->next_y, 0, len);
    veejay_memset(e->next_u, 128, len);
    veejay_memset(e->next_v, 128, len);
    veejay_memset(e->prev_y, 0, len);
    veejay_memset(e->ref_y, 0, len);
    veejay_memset(e->on_y, 0, len);
    veejay_memset(e->off_y, 0, len);
    veejay_memset(e->veil, 0, len);
    veejay_memset(e->nx_on_y, 0, len);
    veejay_memset(e->nx_off_y, 0, len);
    veejay_memset(e->nx_veil, 0, len);
    veejay_memset(e->grid_x, 0, sizeof(float) * gridcap);
    veejay_memset(e->grid_y, 0, sizeof(float) * gridcap);
    veejay_memset(e->force_x, 0, sizeof(float) * gridcap);
    veejay_memset(e->force_y, 0, sizeof(float) * gridcap);
    veejay_memset(e->force_l, 0, sizeof(float) * gridcap);
    veejay_memset(e->force_m, 0, sizeof(float) * gridcap);
    veejay_memset(e->drop_a, 0, sizeof(float) * gridcap);
    veejay_memset(e->drop_b, 0, sizeof(float) * gridcap);
    veejay_memset(e->drop_tmp, 0, sizeof(float) * gridcap);
    veejay_memset(e->flow_xf, 0, sizeof(float) * (size_t) w);
    veejay_memset(e->flow_yf, 0, sizeof(float) * (size_t) h);
    veejay_memset(e->force_xf, 0, sizeof(float) * (size_t) w);
    veejay_memset(e->force_yf, 0, sizeof(float) * (size_t) h);
    veejay_memset(e->flow_xi, 0, sizeof(uint16_t) * (size_t) w);
    veejay_memset(e->flow_yi, 0, sizeof(uint16_t) * (size_t) h);
    veejay_memset(e->force_xi, 0, sizeof(uint16_t) * (size_t) w);
    veejay_memset(e->force_yi, 0, sizeof(uint16_t) * (size_t) h);
    e->grid_w = 0;
    e->grid_h = 0;
    e->flow_map_cell = 0;
    e->flow_map_w = 0;
    e->flow_map_h = 0;
    e->force_w = 0;
    e->force_h = 0;
    e->force_cell = 0;
    e->force_map_cell = 0;
    e->force_map_w = 0;
    e->force_map_h = 0;
    e->drop_w = 0;
    e->drop_h = 0;
    e->drop_cell = 0;

    for (i = 0; i < 256; i++) {
        float v = (float) i / 255.0f;
        int g = (int) (powf(v, 0.94f) * 255.0f + 0.5f);
        e->gamma_lut[i] = eh_u8i(g);
        e->tone_lut[i] = e->gamma_lut[i];
        e->slave_blend_lut[i] = (uint8_t) (3 + (i >> 5));
        if (e->slave_blend_lut[i] > 12)
            e->slave_blend_lut[i] = 12;
    }

    for (i = 0; i < EH_TRIG_SIZE; i++) {
        float a = EH_TWO_PI * ((float) i / (float) EH_TRIG_SIZE);
        e->sin_lut[i] = sinf(a);
        e->cos_lut[i] = cosf(a);
    }

    {
        float cx = (float) w * 0.5f;
        for (i = 0; i < w; i++)
            e->xnorm[i] = ((float) i - cx) / cx;
    }

    e->luts_valid = 0;
    e->maturity = 0.0f;
    e->time = 0.0f;
    e->orbit = 0.0f;
    e->spin_phase1 = 0.0f;
    e->spin_phase2 = 0.0f;
    e->pulse = 0.0f;
    return (void *) e;
}

void eventhorizon_free(void *ptr)
{
    eventhorizon_t *e = (eventhorizon_t *) ptr;
    if (!e)
        return;
    if (e->region)
        free(e->region);
    free(e);
}

static void eh_seed(eventhorizon_t *e, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int len = e->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (i = 0; i < len; i++) {
        uint8_t y = Y[i];
        uint8_t u = U[i];
        uint8_t v = V[i];
        e->src_y[i] = y;
        e->paint_y[i] = y;
        e->paint_u[i] = u;
        e->paint_v[i] = v;
        e->next_y[i] = y;
        e->next_u[i] = u;
        e->next_v[i] = v;
        e->prev_y[i] = y;
        e->ref_y[i] = y;
        e->on_y[i] = 0;
        e->off_y[i] = 0;
        e->veil[i] = 0;
        e->nx_on_y[i] = 0;
        e->nx_off_y[i] = 0;
        e->nx_veil[i] = 0;
    }

    e->seeded = 1;
    e->maturity = 0.0f;
}

static inline __attribute__((always_inline)) void eh_update_smoke_pixel(eventhorizon_t *e,
                                         uint8_t *restrict src_y,
                                         uint8_t *restrict prev_y,
                                         uint8_t *restrict ref_y,
                                         uint8_t *restrict on_y,
                                         uint8_t *restrict off_y,
                                         uint8_t *restrict veil,
                                         uint8_t *restrict nx_on,
                                         uint8_t *restrict nx_off,
                                         uint8_t *restrict nx_veil,
                                         int x,
                                         int y,
                                         int pos,
                                         int rise_step,
                                         int w,
                                         int h,
                                         int density_i,
                                         int lens_i,
                                         int *out_active,
                                         int *out_plume,
                                         int *out_gx,
                                         int *out_gy,
                                         int *out_diff_prev)
{
    (void) e;
    uint8_t cy = src_y[pos];
    int ref = ref_y[pos];
    int diff_raw = (int) cy - ref;
    int diff_ref = eh_absi(diff_raw);
    int diff_prev = eh_absi((int) cy - (int) prev_y[pos]);
    int diff = diff_prev > diff_ref ? diff_prev : diff_ref;
    int event_strength = e->event_lut[diff];
    int event_for_adapt = event_strength;
    int old_on = on_y[pos];
    int old_off = off_y[pos];
    int old_veil = veil[pos];
    int on = e->smoke_decay_lut[old_on];
    int off = e->smoke_decay_lut[old_off];
    int vl = e->veil_decay_lut[old_veil];
    int src_pos = eh_index_clamped(w, h, x, y + rise_step);
    int v_on = e->rise_lut[on_y[src_pos]];
    int v_off = e->rise_lut[off_y[src_pos]];
    int avg;
    int gx = 0;
    int gy = 0;
    int plume;
    int active;

    if (v_on > on)
        on = v_on;
    if (v_off > off)
        off = v_off;

    avg = old_veil + old_on + old_off;
    if (x > 0) avg += veil[pos - 1];
    if (x + 1 < w) avg += veil[pos + 1];
    if (y > 0) avg += veil[pos - w];
    if (y + 1 < h) avg += veil[pos + w];
    avg = (avg * 37 + 128) >> 8;
    v_on = e->spread_lut[avg];
    if (v_on > vl)
        vl = v_on;

    if (event_strength > 0) {
        int edge;
        int cavity;
        if (x > 0 && x + 1 < w)
            gx = (int) src_y[pos + 1] - (int) src_y[pos - 1];
        if (y > 0 && y + 1 < h)
            gy = (int) src_y[pos + w] - (int) src_y[pos - w];
        edge = eh_absi(gx);
        if (eh_absi(gy) > edge)
            edge = eh_absi(gy);
        cavity = 255 - (int) cy;
        event_strength += (event_strength * edge) >> 8;
        event_strength += (event_strength * cavity) >> 10;
        if (event_strength > 255)
            event_strength = 255;
        if (diff_raw >= 0) {
            on += event_strength;
            if (on > 255)
                on = 255;
        }
        else {
            off += event_strength;
            if (off > 255)
                off = 255;
        }
        vl += (event_strength * (70 + density_i + (lens_i >> 1)) + 32767) >> 16;
        if (vl > 255)
            vl = 255;
    }

    {
        int m = on < off ? on : off;
        int cut = m >> 4;
        on -= cut;
        off -= cut;
    }

    nx_on[pos] = (uint8_t) on;
    nx_off[pos] = (uint8_t) off;
    nx_veil[pos] = (uint8_t) vl;
    ref_y[pos] = eh_blend_u8((uint8_t) ref, cy, e->adapt_lut[event_for_adapt]);
    prev_y[pos] = cy;

    plume = on + off;
    if (plume > 255)
        plume = 255;
    active = plume + (vl >> 1);
    if (active > 255)
        active = 255;

    *out_active = active;
    *out_plume = plume;
    *out_gx = gx;
    *out_gy = gy;
    *out_diff_prev = diff_prev;
}


static inline void eh_update_smoke_slave(eventhorizon_t *e,
                                         uint8_t *restrict src_y,
                                         uint8_t *restrict prev_y,
                                         uint8_t *restrict ref_y,
                                         uint8_t *restrict nx_on,
                                         uint8_t *restrict nx_off,
                                         uint8_t *restrict nx_veil,
                                         int pos,
                                         int master_on,
                                         int master_off,
                                         int master_veil)
{
    (void) e;
    uint8_t cy = src_y[pos];
    uint8_t rf = ref_y[pos];
    int d = eh_absi((int) cy - (int) prev_y[pos]);
    int a = e->slave_blend_lut[d];
    nx_on[pos] = (uint8_t) master_on;
    nx_off[pos] = (uint8_t) master_off;
    nx_veil[pos] = (uint8_t) master_veil;
    ref_y[pos] = eh_blend_u8(rf, cy, a);
    prev_y[pos] = cy;
}

static inline void eh_render_from_sample(eventhorizon_t *e,
                                         uint8_t *restrict Y,
                                         uint8_t *restrict U,
                                         uint8_t *restrict V,
                                         uint8_t *restrict src_y,
                                         uint8_t *restrict next_y,
                                         uint8_t *restrict next_u,
                                         uint8_t *restrict next_v,
                                         int pos,
                                         int adv_y,
                                         int adv_u,
                                         int adv_v,
                                         float local,
                                         float local_core,
                                         float local_ring,
                                         float bridge_gate,
                                         float disk_wave,
                                         float src_mix,
                                         float chroma_src_mix,
                                         float chroma_gain,
                                         float smoke_t,
                                         float density_t,
                                         float trail_t,
                                         float chroma_t,
                                         float lens_curve,
                                         float ring_scale,
                                         float shadow_scale,
                                         float redshift,
                                         int active,
                                         int plume,
                                         int vl)
{
    int src_yy = e->tone_lut[src_y[pos]];
    int smoke_y = (plume >> 1) + (vl >> 3) - (e->density_lut[vl] >> 4);
    int ring;
    int shadow;
    float out_yf;
    float paint_yf;
    float display_yf;
    float out_us;
    float out_vs;
    float smoke_alpha;

    smoke_y = eh_clampi(smoke_y, 0, 255);

    ring = (int) (local_ring * ring_scale * bridge_gate);
    if (local_ring > 0.001f)
        ring += (int) ((disk_wave * 0.5f + 0.5f) * local_ring * lens_curve * (2.0f + chroma_t * 3.0f) * bridge_gate);
    ring = eh_clampi(ring, 0, 90);

    shadow = (int) (local_core * shadow_scale);
    shadow = eh_clampi(shadow, 0, 96);

    smoke_alpha = ((float) active * (1.0f / 255.0f)) * (0.001f + smoke_t * smoke_t * 0.050f) * (1.0f - local_core * lens_curve * 0.88f);
    smoke_alpha = eh_clampf(smoke_alpha, 0.0f, 0.120f);

    {
        float local_src = src_mix * (1.0f - lens_curve * local * (0.72f + trail_t * 0.62f));
        float detail = (0.002f + density_t * density_t * 0.032f) * (1.0f - trail_t * 0.70f) * (1.0f - local * lens_curve * 0.84f);
        local_src = eh_clampf(local_src, 0.0008f, 0.20f);
        out_yf = (float) adv_y * (1.0f - local_src) + (float) src_yy * local_src;
        out_yf += ((float) src_yy - (float) adv_y) * detail;
    }

    paint_yf = out_yf;
    paint_yf += (float) smoke_y * smoke_alpha * 0.76f;
    paint_yf += (float) ring * 0.46f;
    paint_yf -= redshift * 0.13f;
    paint_yf -= (float) shadow * 0.12f;

    {
        float floor_y = 3.0f;
        floor_y += (float) src_yy * (0.010f + src_mix * 0.65f);
        floor_y += (float) adv_y * (0.006f + trail_t * 0.006f);
        floor_y += local * lens_curve * (1.4f + smoke_t * 2.3f);
        if (paint_yf < floor_y)
            paint_yf += (floor_y - paint_yf) * (0.52f + trail_t * 0.18f);
    }

    display_yf = paint_yf;
    display_yf += (float) ring * 0.15f;
    display_yf -= redshift * 0.56f;
    display_yf -= (float) shadow * 0.68f;

    {
        float floor_y = 1.0f + local_ring * lens_curve * 1.7f;
        if (display_yf < floor_y)
            display_yf += (floor_y - display_yf) * 0.38f;
    }

    {
        float hist_u = (float) adv_u - 128.0f;
        float hist_v = (float) adv_v - 128.0f;
        float src_u = (float) U[pos] - 128.0f;
        float src_v = (float) V[pos] - 128.0f;
        float local_chroma_src = chroma_src_mix * (1.0f - lens_curve * local * (0.58f + trail_t * 0.42f));
        float neutral_pull = 0.006f + trail_t * 0.008f + lens_curve * local * (0.018f + trail_t * 0.022f);
        float source_anchor = (0.006f + chroma_t * 0.010f + chroma_src_mix * 0.22f) * (1.0f - local_core * lens_curve * 0.56f);
        float smoke_neutral;
        local_chroma_src = eh_clampf(local_chroma_src, 0.0010f, 0.15f);
        out_us = (hist_u * (1.0f - local_chroma_src) + src_u * local_chroma_src) * chroma_gain;
        out_vs = (hist_v * (1.0f - local_chroma_src) + src_v * local_chroma_src) * chroma_gain;
        neutral_pull = eh_clampf(neutral_pull, 0.0f, 0.14f);
        out_us *= 1.0f - neutral_pull;
        out_vs *= 1.0f - neutral_pull;
        source_anchor = eh_clampf(source_anchor, 0.0f, 0.038f);
        out_us = out_us * (1.0f - source_anchor) + src_u * source_anchor;
        out_vs = out_vs * (1.0f - source_anchor) + src_v * source_anchor;
        smoke_neutral = smoke_alpha * 0.45f;
        out_us *= 1.0f - smoke_neutral;
        out_vs *= 1.0f - smoke_neutral;
        if (out_us < 0.0f && out_vs < 0.0f) {
            float g = (eh_absf(out_us) + eh_absf(out_vs)) * (0.0018f + chroma_t * 0.0014f + lens_curve * local * 0.0032f + trail_t * 0.0012f);
            g = eh_clampf(g, 0.0f, 0.58f);
            out_us *= 1.0f - g;
            out_vs *= 1.0f - g;
        }
        {
            float nebula = local_ring * lens_curve * chroma_t * (0.32f + bridge_gate * 0.88f);
            float haze = ((float) active * (1.0f / 255.0f)) * smoke_t * chroma_t * (1.0f - local_core * 0.70f) * 0.26f;
            float wave = disk_wave * 0.5f + 0.5f;
            float amber = eh_smooth01((wave - 0.05f) * 1.55f);
            float cobalt = eh_smooth01((0.95f - wave) * 1.55f);
            float rose = 1.0f - eh_absf(wave * 2.0f - 1.0f);
            float teal = eh_clampf((float) src_yy * (1.0f / 255.0f) * (1.0f - local_core) * 0.85f, 0.0f, 1.0f);

            out_us += (-22.0f * amber + 30.0f * cobalt + 13.0f * rose + 18.0f * teal) * nebula;
            out_vs += ( 34.0f * amber +  9.0f * cobalt + 30.0f * rose -  2.0f * teal) * nebula;
            out_us += ( 12.0f * cobalt - 6.0f * amber + 5.0f * rose) * haze;
            out_vs += (  6.0f * cobalt + 9.0f * amber + 8.0f * rose) * haze;
        }
        {
            float limit = 42.0f + chroma_t * 54.0f;
            if (local > 0.20f)
                limit -= local * lens_curve * 3.0f;
            if (out_us < 0.0f && out_vs < 0.0f)
                limit *= 0.82f;
            eh_limit_chroma(&out_us, &out_vs, limit);
        }
    }

    {
        uint8_t py = eh_u8f(paint_yf);
        uint8_t oy = eh_u8f(display_yf);
        uint8_t ou = eh_u8f(128.0f + out_us);
        uint8_t ov = eh_u8f(128.0f + out_vs);
        next_y[pos] = py;
        next_u[pos] = ou;
        next_v[pos] = ov;
        Y[pos] = oy;
        U[pos] = ou;
        V[pos] = ov;
    }
}



static inline __attribute__((always_inline)) int eh_tile_range_u8(const uint8_t *restrict p,
                                   int pos00,
                                   int w,
                                   int bw,
                                   int bh)
{
    int yy;
    int xx;
    int mn = 255;
    int mx = 0;

    if (bw == 8 && bh == 8) {
        int r0 = pos00;
        int r1 = r0 + w;
        int r2 = r1 + w;
        int r3 = r2 + w;
        int r4 = r3 + w;
        int r5 = r4 + w;
        int r6 = r5 + w;
        int r7 = r6 + w;
        int v;
#define EH_RANGE_ADD(IDX) \
        v = p[(IDX)];     \
        if (v < mn) mn = v; \
        if (v > mx) mx = v
        EH_RANGE_ADD(r0);     EH_RANGE_ADD(r0 + 7);
        EH_RANGE_ADD(r1 + 2); EH_RANGE_ADD(r1 + 5);
        EH_RANGE_ADD(r2 + 1); EH_RANGE_ADD(r2 + 6);
        EH_RANGE_ADD(r3 + 3); EH_RANGE_ADD(r3 + 4);
        EH_RANGE_ADD(r4 + 3); EH_RANGE_ADD(r4 + 4);
        EH_RANGE_ADD(r5 + 1); EH_RANGE_ADD(r5 + 6);
        EH_RANGE_ADD(r6 + 2); EH_RANGE_ADD(r6 + 5);
        EH_RANGE_ADD(r7);     EH_RANGE_ADD(r7 + 7);
#undef EH_RANGE_ADD
        return mx - mn;
    }

    if (bw == 4 && bh == 4) {
        int r0 = pos00;
        int r1 = r0 + w;
        int r2 = r1 + w;
        int r3 = r2 + w;
        int v;
#define EH_RANGE_ADD(IDX) \
        v = p[(IDX)];     \
        if (v < mn) mn = v; \
        if (v > mx) mx = v
        EH_RANGE_ADD(r0);     EH_RANGE_ADD(r0 + 3);
        EH_RANGE_ADD(r1 + 1); EH_RANGE_ADD(r1 + 2);
        EH_RANGE_ADD(r2 + 1); EH_RANGE_ADD(r2 + 2);
        EH_RANGE_ADD(r3);     EH_RANGE_ADD(r3 + 3);
#undef EH_RANGE_ADD
        return mx - mn;
    }

    for (yy = 0; yy < bh; yy++) {
        int row = pos00 + yy * w;
        for (xx = 0; xx < bw; xx++) {
            int v = p[row + xx];
            if (v < mn)
                mn = v;
            if (v > mx)
                mx = v;
        }
    }

    return mx - mn;
}


static inline __attribute__((always_inline)) void eh_render_chroma_from_sample(eventhorizon_t *e,
                                                uint8_t *restrict U,
                                                uint8_t *restrict V,
                                                uint8_t *restrict next_u,
                                                uint8_t *restrict next_v,
                                                uint8_t *restrict src_y,
                                                int pos,
                                                int adv_u,
                                                int adv_v,
                                                float local,
                                                float local_core,
                                                float local_ring,
                                                float bridge_gate,
                                                float disk_wave,
                                                float chroma_src_mix,
                                                float chroma_gain,
                                                float smoke_t,
                                                float trail_t,
                                                float chroma_t,
                                                float lens_curve,
                                                float drop_cohesion,
                                                int active)
{
    int src_yy = e->tone_lut[src_y[pos]];
    float hist_u = (float) adv_u - 128.0f;
    float hist_v = (float) adv_v - 128.0f;
    float src_u = (float) U[pos] - 128.0f;
    float src_v = (float) V[pos] - 128.0f;
    float local_chroma_src = chroma_src_mix * (1.0f - lens_curve * local * (0.58f + trail_t * 0.42f));
    float neutral_pull = 0.006f + trail_t * 0.008f + lens_curve * local * (0.018f + trail_t * 0.022f);
    float source_anchor = (0.006f + chroma_t * 0.010f + chroma_src_mix * 0.22f) * (1.0f - local_core * lens_curve * 0.56f);
    float out_us;
    float out_vs;
    float smoke_alpha = ((float) active * (1.0f / 255.0f)) * (0.001f + smoke_t * smoke_t * 0.050f) * (1.0f - local_core * lens_curve * 0.88f);
    float smoke_neutral;

    drop_cohesion = eh_clampf(drop_cohesion, 0.0f, 1.0f);
    smoke_alpha = eh_clampf(smoke_alpha, 0.0f, 0.120f);
    smoke_alpha *= 1.0f - drop_cohesion * 0.45f;
    local_chroma_src *= 1.0f - drop_cohesion * 0.42f;
    source_anchor *= 1.0f - drop_cohesion * 0.34f;
    neutral_pull *= 1.0f - drop_cohesion * 0.18f;
    local_chroma_src = eh_clampf(local_chroma_src, 0.0010f, 0.15f);
    out_us = (hist_u * (1.0f - local_chroma_src) + src_u * local_chroma_src) * chroma_gain;
    out_vs = (hist_v * (1.0f - local_chroma_src) + src_v * local_chroma_src) * chroma_gain;
    neutral_pull = eh_clampf(neutral_pull, 0.0f, 0.14f);
    out_us *= 1.0f - neutral_pull;
    out_vs *= 1.0f - neutral_pull;
    source_anchor = eh_clampf(source_anchor, 0.0f, 0.038f);
    out_us = out_us * (1.0f - source_anchor) + src_u * source_anchor;
    out_vs = out_vs * (1.0f - source_anchor) + src_v * source_anchor;
    smoke_neutral = smoke_alpha * 0.45f;
    out_us *= 1.0f - smoke_neutral;
    out_vs *= 1.0f - smoke_neutral;
    if (out_us < 0.0f && out_vs < 0.0f) {
        float g = (eh_absf(out_us) + eh_absf(out_vs)) * (0.0018f + chroma_t * 0.0014f + lens_curve * local * 0.0032f + trail_t * 0.0012f);
        g = eh_clampf(g, 0.0f, 0.58f);
        out_us *= 1.0f - g;
        out_vs *= 1.0f - g;
    }
    {
        float nebula = local_ring * lens_curve * (0.32f + bridge_gate * 0.88f) * e->pal_gain;
        float haze = ((float) active * (1.0f / 255.0f)) * smoke_t * (1.0f - local_core * 0.70f) * e->pal_haze_gain;
        float wave = disk_wave * 0.5f + 0.5f;
        float amber = eh_smooth01((wave - 0.05f) * 1.55f);
        float cobalt = eh_smooth01((0.95f - wave) * 1.55f);
        float rose = (1.0f - eh_absf(wave * 2.0f - 1.0f)) * (0.72f + e->pal_gain * 0.28f);
        float teal = eh_clampf((float) src_yy * (1.0f / 255.0f) * (1.0f - local_core) * 0.85f, 0.0f, 1.0f);

        out_us += (e->pal_amber_u * amber + e->pal_cobalt_u * cobalt + e->pal_rose_u * rose + e->pal_teal_u * teal) * nebula;
        out_vs += (e->pal_amber_v * amber + e->pal_cobalt_v * cobalt + e->pal_rose_v * rose + e->pal_teal_v * teal) * nebula;
        out_us += e->pal_haze_u * haze;
        out_vs += e->pal_haze_v * haze;
    }
    {
        float limit = 42.0f + chroma_t * 54.0f;
        if (local > 0.20f)
            limit -= local * lens_curve * 3.0f;
        if (out_us < 0.0f && out_vs < 0.0f)
            limit *= 0.72f;
        eh_limit_chroma(&out_us, &out_vs, limit);
    }

    next_u[pos] = eh_u8f(128.0f + out_us);
    next_v[pos] = eh_u8f(128.0f + out_vs);
    U[pos] = next_u[pos];
    V[pos] = next_v[pos];
}

static inline __attribute__((always_inline)) void eh_bilinear_y_row_kernel(const uint8_t *restrict src,
                                                int srow,
                                                int w,
                                                int bw,
                                                int wx,
                                                int wy,
                                                uint8_t *restrict dst)
{
    int i;
    int iwx = 256 - wx;
    int iwy = 256 - wy;

#if defined(__AVX2__)
    if (bw == 8) {
        __m128i b00 = _mm_loadl_epi64((const __m128i *) (src + srow));
        __m128i b10 = _mm_loadl_epi64((const __m128i *) (src + srow + 1));
        __m128i b01 = _mm_loadl_epi64((const __m128i *) (src + srow + w));
        __m128i b11 = _mm_loadl_epi64((const __m128i *) (src + srow + w + 1));
        __m256i p00 = _mm256_cvtepu8_epi32(b00);
        __m256i p10 = _mm256_cvtepu8_epi32(b10);
        __m256i p01 = _mm256_cvtepu8_epi32(b01);
        __m256i p11 = _mm256_cvtepu8_epi32(b11);
        __m256i vwx = _mm256_set1_epi32(wx);
        __m256i viwx = _mm256_set1_epi32(iwx);
        __m256i vwy = _mm256_set1_epi32(wy);
        __m256i viwy = _mm256_set1_epi32(iwy);
        __m256i bias = _mm256_set1_epi32(32768);
        __m256i a = _mm256_add_epi32(_mm256_mullo_epi32(p00, viwx), _mm256_mullo_epi32(p10, vwx));
        __m256i b = _mm256_add_epi32(_mm256_mullo_epi32(p01, viwx), _mm256_mullo_epi32(p11, vwx));
        __m256i v = _mm256_add_epi32(_mm256_add_epi32(_mm256_mullo_epi32(a, viwy), _mm256_mullo_epi32(b, vwy)), bias);
        int tmp[8];
        v = _mm256_srli_epi32(v, 16);
        _mm256_storeu_si256((__m256i *) tmp, v);
        dst[0] = (uint8_t) tmp[0];
        dst[1] = (uint8_t) tmp[1];
        dst[2] = (uint8_t) tmp[2];
        dst[3] = (uint8_t) tmp[3];
        dst[4] = (uint8_t) tmp[4];
        dst[5] = (uint8_t) tmp[5];
        dst[6] = (uint8_t) tmp[6];
        dst[7] = (uint8_t) tmp[7];
        return;
    }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if (bw == 8) {
        uint8x8_t q00 = vld1_u8(src + srow);
        uint8x8_t q10 = vld1_u8(src + srow + 1);
        uint8x8_t q01 = vld1_u8(src + srow + w);
        uint8x8_t q11 = vld1_u8(src + srow + w + 1);
        uint16x8_t p00 = vmovl_u8(q00);
        uint16x8_t p10 = vmovl_u8(q10);
        uint16x8_t p01 = vmovl_u8(q01);
        uint16x8_t p11 = vmovl_u8(q11);
        uint32x4_t a0 = vmull_n_u16(vget_low_u16(p00), (uint16_t) iwx);
        uint32x4_t a1 = vmull_n_u16(vget_high_u16(p00), (uint16_t) iwx);
        uint32x4_t b0 = vmull_n_u16(vget_low_u16(p01), (uint16_t) iwx);
        uint32x4_t b1 = vmull_n_u16(vget_high_u16(p01), (uint16_t) iwx);
        uint32x4_t v0;
        uint32x4_t v1;
        uint16x8_t r16;
        uint8x8_t r8;
        a0 = vmlal_n_u16(a0, vget_low_u16(p10), (uint16_t) wx);
        a1 = vmlal_n_u16(a1, vget_high_u16(p10), (uint16_t) wx);
        b0 = vmlal_n_u16(b0, vget_low_u16(p11), (uint16_t) wx);
        b1 = vmlal_n_u16(b1, vget_high_u16(p11), (uint16_t) wx);
        v0 = vaddq_u32(vmlaq_n_u32(vmulq_n_u32(a0, (uint32_t) iwy), b0, (uint32_t) wy), vdupq_n_u32(32768));
        v1 = vaddq_u32(vmlaq_n_u32(vmulq_n_u32(a1, (uint32_t) iwy), b1, (uint32_t) wy), vdupq_n_u32(32768));
        r16 = vcombine_u16(vshrn_n_u32(v0, 16), vshrn_n_u32(v1, 16));
        r8 = vqmovn_u16(r16);
        vst1_u8(dst, r8);
        return;
    }
#endif

    for (i = 0; i < bw; i++) {
        int p = srow + i;
        int a = (int) src[p] * iwx + (int) src[p + 1] * wx;
        int b = (int) src[p + w] * iwx + (int) src[p + w + 1] * wx;
        dst[i] = (uint8_t) (((a * iwy + b * wy) + 32768) >> 16);
    }
}

static inline __attribute__((always_inline)) void eh_render_area_sampled(eventhorizon_t *e,
                                          uint8_t *restrict Y,
                                          uint8_t *restrict U,
                                          uint8_t *restrict V,
                                          uint8_t *restrict src_y,
                                          uint8_t *restrict old_y,
                                          uint8_t *restrict old_u,
                                          uint8_t *restrict old_v,
                                          uint8_t *restrict next_y,
                                          uint8_t *restrict next_u,
                                          uint8_t *restrict next_v,
                                          uint8_t *restrict prev_y,
                                          uint8_t *restrict ref_y,
                                          uint8_t *restrict nx_on,
                                          uint8_t *restrict nx_off,
                                          uint8_t *restrict nx_veil,
                                          int pos00,
                                          int x,
                                          int y,
                                          int w,
                                          int h,
                                          int bw,
                                          int bh,
                                          float vel_x,
                                          float vel_y,
                                          float local,
                                          float local_core,
                                          float local_ring,
                                          float bridge_gate,
                                          float disk_wave,
                                          float src_mix,
                                          float chroma_src_mix,
                                          float chroma_gain,
                                          float smoke_t,
                                          float density_t,
                                          float trail_t,
                                          float chroma_t,
                                          float lens_curve,
                                          float ring_scale,
                                          float shadow_scale,
                                          float redshift,
                                          int active,
                                          int plume,
                                          int vl,
                                          int master_on,
                                          int master_off,
                                          int master_veil,
                                          float drop_cohesion,
                                          int use_bilinear)
{
    int yy;
    int xx;
    int adv_u;
    int adv_v;
    int smoke_y;
    int ring;
    int shadow;
    float smoke_alpha;
    float local_src;
    float detail;
    float adv_coeff;
    float src_coeff;
    float paint_add;
    float floor_src_coeff;
    float floor_adv_coeff;
    float floor_const;
    float floor_blend;
    float display_add;
    float display_floor;
    int tension_q;
    int inv_tension_q;
    int tension_safe;
    int uv_tension_q;
    int escape_q;
    int escape_thresh;
    int escape_ref_y;
    float advxc = (float) x + ((float) bw * 0.5f) - vel_x;
    float advyc = (float) y + ((float) bh * 0.5f) - vel_y;

    (void) density_t;

    eh_sample_uv_nearest(old_u, old_v, advxc, advyc, w, h, &adv_u, &adv_v);

    eh_render_chroma_from_sample(e, U, V, next_u, next_v, src_y,
                                 pos00, adv_u, adv_v, local, local_core, local_ring,
                                 bridge_gate, disk_wave, chroma_src_mix, chroma_gain,
                                 smoke_t, trail_t, chroma_t, lens_curve, drop_cohesion, active);

    smoke_y = (plume >> 1) + (vl >> 3) - (e->density_lut[vl] >> 4);
    smoke_y = eh_clampi(smoke_y, 0, 255);

    ring = (int) (local_ring * ring_scale * bridge_gate);
    if (local_ring > 0.001f)
        ring += (int) ((disk_wave * 0.5f + 0.5f) * local_ring * lens_curve * (2.0f + chroma_t * 3.0f) * bridge_gate);
    ring = eh_clampi(ring, 0, 90);

    shadow = (int) (local_core * shadow_scale);
    shadow = eh_clampi(shadow, 0, 96);

    smoke_alpha = ((float) active * (1.0f / 255.0f)) * (0.001f + smoke_t * smoke_t * 0.050f) * (1.0f - local_core * lens_curve * 0.88f);
    drop_cohesion = eh_clampf(drop_cohesion, 0.0f, 1.0f);
    smoke_alpha = eh_clampf(smoke_alpha, 0.0f, 0.120f);
    smoke_alpha *= 1.0f - drop_cohesion * 0.88f;

    local_src = src_mix * (1.0f - lens_curve * local * (0.72f + trail_t * 0.62f));
    local_src *= 1.0f - drop_cohesion * 0.36f;
    local_src = eh_clampf(local_src, 0.0006f, 0.18f);
    detail = (0.0012f + density_t * density_t * 0.026f) * (1.0f - trail_t * 0.74f) * (1.0f - local * lens_curve * 0.86f);
    detail *= 1.0f - drop_cohesion * 0.98f;

    adv_coeff = 1.0f - local_src - detail;
    src_coeff = local_src + detail;
    paint_add = (float) smoke_y * smoke_alpha * 0.76f + (float) ring * 0.46f - redshift * 0.13f - (float) shadow * 0.12f;
    floor_src_coeff = 0.010f + src_mix * 0.65f;
    floor_adv_coeff = 0.006f + trail_t * 0.006f;
    floor_const = 3.0f + local * lens_curve * (1.4f + smoke_t * 2.3f);
    floor_blend = 0.52f + trail_t * 0.18f;
    display_add = (float) ring * 0.15f - redshift * 0.56f - (float) shadow * 0.68f;
    display_floor = 1.0f + local_ring * lens_curve * 1.7f;
    if (drop_cohesion > 0.145f)
        tension_q = eh_clampi((int) ((drop_cohesion - 0.120f) * (118.0f + trail_t * 138.0f + local * 32.0f) + 0.5f), 0, 260);
    else
        tension_q = 0;
    inv_tension_q = 1024 - tension_q;
    tension_safe = (x > 0 && y > 0 && x + bw < w && y + bh < h);
    if (drop_cohesion > 0.220f)
        uv_tension_q = eh_clampi((int) ((drop_cohesion - 0.180f) * (52.0f + trail_t * 92.0f) + 0.5f), 0, 116);
    else
        uv_tension_q = 0;

    {
        int p0 = pos00;
        int p1 = pos00 + bw - 1;
        int p2 = pos00 + (bh - 1) * w;
        int p3 = p2 + bw - 1;
        int pc = pos00 + (bh >> 1) * w + (bw >> 1);
        escape_ref_y = ((int) old_y[p0] + (int) old_y[p1] +
                        (int) old_y[p2] + (int) old_y[p3] +
                        ((int) old_y[pc] << 1) + 3) / 6;
    }

    if (drop_cohesion > 0.050f)
        escape_q = eh_clampi((int) ((drop_cohesion - 0.035f) * (168.0f + trail_t * 150.0f + local * 58.0f) + 0.5f), 0, 342);
    else
        escape_q = 0;

    escape_thresh = 23 - (int) (drop_cohesion * 12.0f) - (int) (trail_t * 3.0f);
    if (escape_thresh < 8)
        escape_thresh = 8;

    {
        uint8_t cu = next_u[pos00];
        uint8_t cv = next_v[pos00];
        int adv_q = eh_q10_from_float(adv_coeff);
        int src_q = eh_q10_from_float(src_coeff);
        int paint_add_q = eh_q10_from_float(paint_add);
        int floor_src_q = eh_q10_from_float(floor_src_coeff);
        int floor_adv_q = eh_q10_from_float(floor_adv_coeff);
        int floor_const_q = eh_q10_from_float(floor_const);
        int floor_blend_q = eh_q10_from_float(floor_blend);
        int display_add_q = eh_q10_from_float(display_add);
        int display_floor_q = eh_q10_from_float(display_floor);

        if (uv_tension_q > 0) {
            cu = eh_blend_u8_q8(cu, old_u[pos00], uv_tension_q);
            cv = eh_blend_u8_q8(cv, old_v[pos00], uv_tension_q);
            next_u[pos00] = cu;
            next_v[pos00] = cv;
            U[pos00] = cu;
            V[pos00] = cv;
        }

        if (use_bilinear) {
            float fx0__ = (float) x + 0.5f - vel_x;
            float fy0__ = (float) y + 0.5f - vel_y;
            int sx0__ = (int) fx0__;
            int sy0__ = (int) fy0__;
            if (fx0__ < (float) sx0__)
                sx0__--;
            if (fy0__ < (float) sy0__)
                sy0__--;
            if (sx0__ >= 0 && sy0__ >= 0 && sx0__ + bw < w && sy0__ + bh < h) {
                int wx__ = (int) ((fx0__ - (float) sx0__) * 256.0f);
                int wy__ = (int) ((fy0__ - (float) sy0__) * 256.0f);
                {
                    uint8_t advrow__[16];
                    if (tension_q == 0 && escape_q == 0) {
                        for (yy = 0; yy < bh; yy++) {
                            int row = pos00 + yy * w;
                            int srow = (sy0__ + yy) * w + sx0__;
                            eh_bilinear_y_row_kernel(old_y, srow, w, bw, wx__, wy__, advrow__);
                            for (xx = 0; xx < bw; xx++) {
                                int pos = row + xx;
                                int adv_y = advrow__[xx];
                                int src_yy = e->tone_lut[src_y[pos]];
                                int paint_q = adv_y * adv_q + src_yy * src_q + paint_add_q;
                                int floor_q = floor_const_q + src_yy * floor_src_q + adv_y * floor_adv_q;
                                int display_q;
                                if (paint_q < floor_q)
                                    paint_q += ((floor_q - paint_q) * floor_blend_q) >> 10;
                                display_q = paint_q + display_add_q;
                                if (display_q < display_floor_q)
                                    display_q += ((display_floor_q - display_q) * 389) >> 10;
                                next_y[pos] = eh_u8i((paint_q + 512) >> 10);
                                Y[pos] = eh_u8i((display_q + 512) >> 10);
                                if (pos != pos00) {
                                    uint8_t cyv = src_y[pos];
                                    uint8_t rf = ref_y[pos];
                                    int d = eh_absi((int) cyv - (int) prev_y[pos]);
                                    int a = e->slave_blend_lut[d];
                                    nx_on[pos] = (uint8_t) master_on;
                                    nx_off[pos] = (uint8_t) master_off;
                                    nx_veil[pos] = (uint8_t) master_veil;
                                    ref_y[pos] = eh_blend_u8(rf, cyv, a);
                                    prev_y[pos] = cyv;
                                    next_u[pos] = cu;
                                    next_v[pos] = cv;
                                    U[pos] = cu;
                                    V[pos] = cv;
                                }
                            }
                        }
                        return;
                    }
                    for (yy = 0; yy < bh; yy++) {
                        int row = pos00 + yy * w;
                        int srow = (sy0__ + yy) * w + sx0__;
                        eh_bilinear_y_row_kernel(old_y, srow, w, bw, wx__, wy__, advrow__);
                        for (xx = 0; xx < bw; xx++) {
                            int pos = row + xx;
                            int adv_y = advrow__[xx];
                            int src_yy;
                            int paint_q;
                            int floor_q;
                            int display_q;

                            if (tension_q > 0) {
                                int coh_y = tension_safe ? eh_cross5_y(old_y, pos, w) : (int) old_y[pos];
                                adv_y = (adv_y * inv_tension_q + coh_y * tension_q + 512) >> 10;
                            }
                            adv_y = eh_escape_smooth_y(adv_y, escape_ref_y, escape_thresh, escape_q);
                            src_yy = e->tone_lut[src_y[pos]];
                            paint_q = adv_y * adv_q + src_yy * src_q + paint_add_q;
                            floor_q = floor_const_q + src_yy * floor_src_q + adv_y * floor_adv_q;
                            if (paint_q < floor_q)
                                paint_q += ((floor_q - paint_q) * floor_blend_q) >> 10;
                            display_q = paint_q + display_add_q;
                            if (display_q < display_floor_q)
                                display_q += ((display_floor_q - display_q) * 389) >> 10;

                            next_y[pos] = eh_u8i((paint_q + 512) >> 10);
                            Y[pos] = eh_u8i((display_q + 512) >> 10);

                            if (pos != pos00) {
                                uint8_t cyv = src_y[pos];
                                uint8_t rf = ref_y[pos];
                                int d = eh_absi((int) cyv - (int) prev_y[pos]);
                                int a = e->slave_blend_lut[d];
                                nx_on[pos] = (uint8_t) master_on;
                                nx_off[pos] = (uint8_t) master_off;
                                nx_veil[pos] = (uint8_t) master_veil;
                                ref_y[pos] = eh_blend_u8(rf, cyv, a);
                                prev_y[pos] = cyv;
                                next_u[pos] = cu;
                                next_v[pos] = cv;
                                U[pos] = cu;
                                V[pos] = cv;
                            }
                        }
                    }
                }
                return;
            }
            for (yy = 0; yy < bh; yy++) {
                int row = pos00 + yy * w;
                float py = (float) (y + yy);
                for (xx = 0; xx < bw; xx++) {
                    int pos = row + xx;
                    int adv_y;
                    int src_yy;
                    float advx = (float) (x + xx) + 0.5f - vel_x;
                    float advy = py + 0.5f - vel_y;
                    int paint_q;
                    int floor_q;
                    int display_q;

                    adv_y = eh_sample_y_bilinear(old_y, advx, advy, w, h);
                    if (tension_q > 0) {
                        int coh_y = tension_safe ? eh_cross5_y(old_y, pos, w) : (int) old_y[pos];
                        adv_y = (adv_y * inv_tension_q + coh_y * tension_q + 512) >> 10;
                    }
                    adv_y = eh_escape_smooth_y(adv_y, escape_ref_y, escape_thresh, escape_q);
                    src_yy = e->tone_lut[src_y[pos]];
                    paint_q = adv_y * adv_q + src_yy * src_q + paint_add_q;
                    floor_q = floor_const_q + src_yy * floor_src_q + adv_y * floor_adv_q;
                    if (paint_q < floor_q)
                        paint_q += ((floor_q - paint_q) * floor_blend_q) >> 10;
                    display_q = paint_q + display_add_q;
                    if (display_q < display_floor_q)
                        display_q += ((display_floor_q - display_q) * 389) >> 10;

                    next_y[pos] = eh_u8i((paint_q + 512) >> 10);
                    Y[pos] = eh_u8i((display_q + 512) >> 10);

                    if (pos != pos00) {
                        uint8_t cyv = src_y[pos];
                        uint8_t rf = ref_y[pos];
                        int d = eh_absi((int) cyv - (int) prev_y[pos]);
                        int a = e->slave_blend_lut[d];
                        nx_on[pos] = (uint8_t) master_on;
                        nx_off[pos] = (uint8_t) master_off;
                        nx_veil[pos] = (uint8_t) master_veil;
                        ref_y[pos] = eh_blend_u8(rf, cyv, a);
                        prev_y[pos] = cyv;
                        next_u[pos] = cu;
                        next_v[pos] = cv;
                        U[pos] = cu;
                        V[pos] = cv;
                    }
                }
            }
        }
        else {
            int sx0 = (int) ((float) x + 1.0f - vel_x);
            int sy0 = (int) ((float) y + 1.0f - vel_y);
            int x_inside = (sx0 >= 0 && sx0 + bw <= w);

            if (tension_q == 0 && uv_tension_q == 0 && x_inside && sy0 >= 0 && sy0 + bh <= h) {
                for (yy = 0; yy < bh; yy++) {
                    int row = pos00 + yy * w;
                    int sample = (sy0 + yy) * w + sx0;
                    for (xx = 0; xx < bw; xx++) {
                        int pos = row + xx;
                        int adv_y__ = old_y[sample + xx];
                        int src_yy__ = e->tone_lut[src_y[pos]];
                        int paint_q__;
                        int floor_q__;
                        int display_q__;
                        adv_y__ = eh_escape_smooth_y(adv_y__, escape_ref_y, escape_thresh, escape_q);
                        paint_q__ = adv_y__ * adv_q + src_yy__ * src_q + paint_add_q;
                        floor_q__ = floor_const_q + src_yy__ * floor_src_q + adv_y__ * floor_adv_q;
                        if (paint_q__ < floor_q__)
                            paint_q__ += ((floor_q__ - paint_q__) * floor_blend_q) >> 10;
                        display_q__ = paint_q__ + display_add_q;
                        if (display_q__ < display_floor_q)
                            display_q__ += ((display_floor_q - display_q__) * 389) >> 10;
                        next_y[pos] = eh_u8i((paint_q__ + 512) >> 10);
                        Y[pos] = eh_u8i((display_q__ + 512) >> 10);
                        if (pos != pos00) {
                            uint8_t cyv__ = src_y[pos];
                            uint8_t rf__ = ref_y[pos];
                            int d__ = eh_absi((int) cyv__ - (int) prev_y[pos]);
                            int a__ = e->slave_blend_lut[d__];
                            ref_y[pos] = eh_blend_u8(rf__, cyv__, a__);
                            prev_y[pos] = cyv__;
                        }
                    }
                    veejay_memset(next_u + row, cu, (size_t) bw);
                    veejay_memset(next_v + row, cv, (size_t) bw);
                    veejay_memset(U + row, cu, (size_t) bw);
                    veejay_memset(V + row, cv, (size_t) bw);
                    veejay_memset(nx_on + row, master_on, (size_t) bw);
                    veejay_memset(nx_off + row, master_off, (size_t) bw);
                    veejay_memset(nx_veil + row, master_veil, (size_t) bw);
                }
                return;
            }

            if (bw == 4 && bh == 4 && x_inside && sy0 >= 0 && sy0 + 4 <= h) {
                int sample0 = sy0 * w + sx0;
#define EH_FAST_PIXEL(POS, SAMPLE, SLAVE) do { \
                    int adv_y__ = old_y[(SAMPLE)]; \
                    if (tension_q > 0) { int coh_y__ = tension_safe ? eh_cross5_y(old_y, (POS), w) : (int) old_y[(POS)]; adv_y__ = (adv_y__ * inv_tension_q + coh_y__ * tension_q + 512) >> 10; } \
                    adv_y__ = eh_escape_smooth_y(adv_y__, escape_ref_y, escape_thresh, escape_q); \
                    int src_yy__ = e->tone_lut[src_y[(POS)]]; \
                    int paint_q__ = adv_y__ * adv_q + src_yy__ * src_q + paint_add_q; \
                    int floor_q__ = floor_const_q + src_yy__ * floor_src_q + adv_y__ * floor_adv_q; \
                    int display_q__; \
                    if (paint_q__ < floor_q__) paint_q__ += ((floor_q__ - paint_q__) * floor_blend_q) >> 10; \
                    display_q__ = paint_q__ + display_add_q; \
                    if (display_q__ < display_floor_q) display_q__ += ((display_floor_q - display_q__) * 389) >> 10; \
                    next_y[(POS)] = eh_u8i((paint_q__ + 512) >> 10); \
                    Y[(POS)] = eh_u8i((display_q__ + 512) >> 10); \
                    if (SLAVE) { \
                        uint8_t cyv__ = src_y[(POS)]; \
                        uint8_t rf__ = ref_y[(POS)]; \
                        int d__ = eh_absi((int) cyv__ - (int) prev_y[(POS)]); \
                        int a__ = e->slave_blend_lut[d__]; \
                        ref_y[(POS)] = eh_blend_u8(rf__, cyv__, a__); \
                        prev_y[(POS)] = cyv__; \
                    } \
                } while (0)
                EH_FAST_PIXEL(pos00, sample0, 0);
                EH_FAST_PIXEL(pos00 + 1, sample0 + 1, 1);
                EH_FAST_PIXEL(pos00 + 2, sample0 + 2, 1);
                EH_FAST_PIXEL(pos00 + 3, sample0 + 3, 1);
                EH_FAST_PIXEL(pos00 + w, sample0 + w, 1);
                EH_FAST_PIXEL(pos00 + w + 1, sample0 + w + 1, 1);
                EH_FAST_PIXEL(pos00 + w + 2, sample0 + w + 2, 1);
                EH_FAST_PIXEL(pos00 + w + 3, sample0 + w + 3, 1);
                EH_FAST_PIXEL(pos00 + w + w, sample0 + w + w, 1);
                EH_FAST_PIXEL(pos00 + w + w + 1, sample0 + w + w + 1, 1);
                EH_FAST_PIXEL(pos00 + w + w + 2, sample0 + w + w + 2, 1);
                EH_FAST_PIXEL(pos00 + w + w + 3, sample0 + w + w + 3, 1);
                EH_FAST_PIXEL(pos00 + w + w + w, sample0 + w + w + w, 1);
                EH_FAST_PIXEL(pos00 + w + w + w + 1, sample0 + w + w + w + 1, 1);
                EH_FAST_PIXEL(pos00 + w + w + w + 2, sample0 + w + w + w + 2, 1);
                EH_FAST_PIXEL(pos00 + w + w + w + 3, sample0 + w + w + w + 3, 1);
#undef EH_FAST_PIXEL
#define EH_STORE4_ROWS(PLANE, VAL) do { \
                    uint8_t v__ = (uint8_t) (VAL); \
                    eh_store4_u8((PLANE) + pos00, v__); \
                    eh_store4_u8((PLANE) + pos00 + w, v__); \
                    eh_store4_u8((PLANE) + pos00 + w + w, v__); \
                    eh_store4_u8((PLANE) + pos00 + w + w + w, v__); \
                } while (0)
                if (uv_tension_q == 0) {
                    EH_STORE4_ROWS(next_u, cu);
                    EH_STORE4_ROWS(next_v, cv);
                    EH_STORE4_ROWS(U, cu);
                    EH_STORE4_ROWS(V, cv);
                }
                else {
#define EH_STORE_UV_BLEND4(POS) do { \
                    uint8_t tu__ = eh_blend_u8_q8(cu, old_u[(POS)], uv_tension_q); \
                    uint8_t tv__ = eh_blend_u8_q8(cv, old_v[(POS)], uv_tension_q); \
                    next_u[(POS)] = tu__; next_v[(POS)] = tv__; U[(POS)] = tu__; V[(POS)] = tv__; \
                } while (0)
                    EH_STORE_UV_BLEND4(pos00);
                    EH_STORE_UV_BLEND4(pos00 + 1);
                    EH_STORE_UV_BLEND4(pos00 + 2);
                    EH_STORE_UV_BLEND4(pos00 + 3);
                    EH_STORE_UV_BLEND4(pos00 + w);
                    EH_STORE_UV_BLEND4(pos00 + w + 1);
                    EH_STORE_UV_BLEND4(pos00 + w + 2);
                    EH_STORE_UV_BLEND4(pos00 + w + 3);
                    EH_STORE_UV_BLEND4(pos00 + w + w);
                    EH_STORE_UV_BLEND4(pos00 + w + w + 1);
                    EH_STORE_UV_BLEND4(pos00 + w + w + 2);
                    EH_STORE_UV_BLEND4(pos00 + w + w + 3);
                    EH_STORE_UV_BLEND4(pos00 + w + w + w);
                    EH_STORE_UV_BLEND4(pos00 + w + w + w + 1);
                    EH_STORE_UV_BLEND4(pos00 + w + w + w + 2);
                    EH_STORE_UV_BLEND4(pos00 + w + w + w + 3);
#undef EH_STORE_UV_BLEND4
                }
                EH_STORE4_ROWS(nx_on, master_on);
                EH_STORE4_ROWS(nx_off, master_off);
                EH_STORE4_ROWS(nx_veil, master_veil);
#undef EH_STORE4_ROWS
                return;
            }

            if (bw == 2 && bh == 2 && x_inside && sy0 >= 0 && sy0 + 2 <= h) {
                int sample0 = sy0 * w + sx0;
#define EH_FAST_PIXEL2(POS, SAMPLE, SLAVE) do { \
                    int adv_y__ = old_y[(SAMPLE)]; \
                    if (tension_q > 0) { int coh_y__ = tension_safe ? eh_cross5_y(old_y, (POS), w) : (int) old_y[(POS)]; adv_y__ = (adv_y__ * inv_tension_q + coh_y__ * tension_q + 512) >> 10; } \
                    adv_y__ = eh_escape_smooth_y(adv_y__, escape_ref_y, escape_thresh, escape_q); \
                    int src_yy__ = e->tone_lut[src_y[(POS)]]; \
                    int paint_q__ = adv_y__ * adv_q + src_yy__ * src_q + paint_add_q; \
                    int floor_q__ = floor_const_q + src_yy__ * floor_src_q + adv_y__ * floor_adv_q; \
                    int display_q__; \
                    if (paint_q__ < floor_q__) paint_q__ += ((floor_q__ - paint_q__) * floor_blend_q) >> 10; \
                    display_q__ = paint_q__ + display_add_q; \
                    if (display_q__ < display_floor_q) display_q__ += ((display_floor_q - display_q__) * 389) >> 10; \
                    next_y[(POS)] = eh_u8i((paint_q__ + 512) >> 10); \
                    Y[(POS)] = eh_u8i((display_q__ + 512) >> 10); \
                    if (SLAVE) { \
                        uint8_t cyv__ = src_y[(POS)]; \
                        uint8_t rf__ = ref_y[(POS)]; \
                        int d__ = eh_absi((int) cyv__ - (int) prev_y[(POS)]); \
                        int a__ = e->slave_blend_lut[d__]; \
                        ref_y[(POS)] = eh_blend_u8(rf__, cyv__, a__); \
                        prev_y[(POS)] = cyv__; \
                    } \
                } while (0)
                EH_FAST_PIXEL2(pos00, sample0, 0);
                EH_FAST_PIXEL2(pos00 + 1, sample0 + 1, 1);
                EH_FAST_PIXEL2(pos00 + w, sample0 + w, 1);
                EH_FAST_PIXEL2(pos00 + w + 1, sample0 + w + 1, 1);
#undef EH_FAST_PIXEL2
#define EH_STORE2_ROWS(PLANE, VAL) do { \
                    uint8_t v__ = (uint8_t) (VAL); \
                    eh_store2_u8((PLANE) + pos00, v__); \
                    eh_store2_u8((PLANE) + pos00 + w, v__); \
                } while (0)
                if (uv_tension_q == 0) {
                    EH_STORE2_ROWS(next_u, cu);
                    EH_STORE2_ROWS(next_v, cv);
                    EH_STORE2_ROWS(U, cu);
                    EH_STORE2_ROWS(V, cv);
                }
                else {
#define EH_STORE_UV_BLEND2(POS) do { \
                    uint8_t tu__ = eh_blend_u8_q8(cu, old_u[(POS)], uv_tension_q); \
                    uint8_t tv__ = eh_blend_u8_q8(cv, old_v[(POS)], uv_tension_q); \
                    next_u[(POS)] = tu__; next_v[(POS)] = tv__; U[(POS)] = tu__; V[(POS)] = tv__; \
                } while (0)
                    EH_STORE_UV_BLEND2(pos00);
                    EH_STORE_UV_BLEND2(pos00 + 1);
                    EH_STORE_UV_BLEND2(pos00 + w);
                    EH_STORE_UV_BLEND2(pos00 + w + 1);
#undef EH_STORE_UV_BLEND2
                }
                EH_STORE2_ROWS(nx_on, master_on);
                EH_STORE2_ROWS(nx_off, master_off);
                EH_STORE2_ROWS(nx_veil, master_veil);
#undef EH_STORE2_ROWS
                return;
            }

            for (yy = 0; yy < bh; yy++) {
                int row = pos00 + yy * w;
                int iy = sy0 + yy;
                int src_row;

                if (iy < 0)
                    iy = 0;
                else if (iy >= h)
                    iy = h - 1;

                src_row = iy * w;

                if (x_inside) {
                    int sample = src_row + sx0;
                    for (xx = 0; xx < bw; xx++) {
                        int pos = row + xx;
                        int adv_y = old_y[sample + xx];
                        if (tension_q > 0) {
                            int coh_y = tension_safe ? eh_cross5_y(old_y, pos, w) : (int) old_y[pos];
                            adv_y = (adv_y * inv_tension_q + coh_y * tension_q + 512) >> 10;
                        }
                        adv_y = eh_escape_smooth_y(adv_y, escape_ref_y, escape_thresh, escape_q);
                        int src_yy = e->tone_lut[src_y[pos]];
                        int paint_q = adv_y * adv_q + src_yy * src_q + paint_add_q;
                        int floor_q = floor_const_q + src_yy * floor_src_q + adv_y * floor_adv_q;
                        int display_q;

                        if (paint_q < floor_q)
                            paint_q += ((floor_q - paint_q) * floor_blend_q) >> 10;

                        display_q = paint_q + display_add_q;
                        if (display_q < display_floor_q)
                            display_q += ((display_floor_q - display_q) * 389) >> 10;

                        next_y[pos] = eh_u8i((paint_q + 512) >> 10);
                        Y[pos] = eh_u8i((display_q + 512) >> 10);

                        if (pos != pos00) {
                            uint8_t cyv = src_y[pos];
                            uint8_t rf = ref_y[pos];
                            int d = eh_absi((int) cyv - (int) prev_y[pos]);
                            int a = e->slave_blend_lut[d];
                            nx_on[pos] = (uint8_t) master_on;
                            nx_off[pos] = (uint8_t) master_off;
                            nx_veil[pos] = (uint8_t) master_veil;
                            ref_y[pos] = eh_blend_u8(rf, cyv, a);
                            prev_y[pos] = cyv;
                            if (uv_tension_q > 0) {
                                uint8_t tu = eh_blend_u8_q8(cu, old_u[pos], uv_tension_q);
                                uint8_t tv = eh_blend_u8_q8(cv, old_v[pos], uv_tension_q);
                                next_u[pos] = tu;
                                next_v[pos] = tv;
                                U[pos] = tu;
                                V[pos] = tv;
                            }
                            else {
                                next_u[pos] = cu;
                                next_v[pos] = cv;
                                U[pos] = cu;
                                V[pos] = cv;
                            }
                        }
                    }
                }
                else {
                    for (xx = 0; xx < bw; xx++) {
                        int pos = row + xx;
                        int ix = sx0 + xx;
                        int adv_y;
                        int src_yy;
                        int paint_q;
                        int floor_q;
                        int display_q;

                        if (ix < 0)
                            ix = 0;
                        else if (ix >= w)
                            ix = w - 1;

                        adv_y = old_y[src_row + ix];
                        if (tension_q > 0) {
                            int coh_y = tension_safe ? eh_cross5_y(old_y, pos, w) : (int) old_y[pos];
                            adv_y = (adv_y * inv_tension_q + coh_y * tension_q + 512) >> 10;
                        }
                        src_yy = e->tone_lut[src_y[pos]];
                        paint_q = adv_y * adv_q + src_yy * src_q + paint_add_q;
                        floor_q = floor_const_q + src_yy * floor_src_q + adv_y * floor_adv_q;

                        if (paint_q < floor_q)
                            paint_q += ((floor_q - paint_q) * floor_blend_q) >> 10;

                        display_q = paint_q + display_add_q;
                        if (display_q < display_floor_q)
                            display_q += ((display_floor_q - display_q) * 389) >> 10;

                        next_y[pos] = eh_u8i((paint_q + 512) >> 10);
                        Y[pos] = eh_u8i((display_q + 512) >> 10);

                        if (pos != pos00) {
                            uint8_t cyv = src_y[pos];
                            uint8_t rf = ref_y[pos];
                            int d = eh_absi((int) cyv - (int) prev_y[pos]);
                            int a = e->slave_blend_lut[d];
                            nx_on[pos] = (uint8_t) master_on;
                            nx_off[pos] = (uint8_t) master_off;
                            nx_veil[pos] = (uint8_t) master_veil;
                            ref_y[pos] = eh_blend_u8(rf, cyv, a);
                            prev_y[pos] = cyv;
                            if (uv_tension_q > 0) {
                                uint8_t tu = eh_blend_u8_q8(cu, old_u[pos], uv_tension_q);
                                uint8_t tv = eh_blend_u8_q8(cv, old_v[pos], uv_tension_q);
                                next_u[pos] = tu;
                                next_v[pos] = tv;
                                U[pos] = tu;
                                V[pos] = tv;
                            }
                            else {
                                next_u[pos] = cu;
                                next_v[pos] = cv;
                                U[pos] = cu;
                                V[pos] = cv;
                            }
                        }
                    }
                }
            }
        }
    }
}

static inline __attribute__((always_inline)) void eh_process_light_tile(eventhorizon_t *e,
                                         uint8_t *restrict Y,
                                         uint8_t *restrict U,
                                         uint8_t *restrict V,
                                         uint8_t *restrict src_y,
                                         uint8_t *restrict old_y,
                                         uint8_t *restrict old_u,
                                         uint8_t *restrict old_v,
                                         uint8_t *restrict next_y,
                                         uint8_t *restrict next_u,
                                         uint8_t *restrict next_v,
                                         uint8_t *restrict prev_y,
                                         uint8_t *restrict ref_y,
                                         uint8_t *restrict on_y,
                                         uint8_t *restrict off_y,
                                         uint8_t *restrict veil,
                                         uint8_t *restrict nx_on,
                                         uint8_t *restrict nx_off,
                                         uint8_t *restrict nx_veil,
                                         int xx,
                                         int yy,
                                         int bw,
                                         int bh,
                                         int w,
                                         int h,
                                         int rise_step,
                                         int flow_cell,
                                         int density_i,
                                         int lens_i,
                                         int flow_i,
                                         float fluid_pixels,
                                         float flow_pixels,
                                         float src_mix,
                                         float chroma_src_mix,
                                         float chroma_gain,
                                         float smoke_t,
                                         float density_t,
                                         float trail_t,
                                         float chroma_t,
                                         float lens_curve,
                                         float fold_t,
                                         float ring_scale,
                                         float shadow_scale,
                                         float redshift_scale,
                                         float wave_s,
                                         float wave_c,
                                         float cy,
                                         float inv_cy,
                                         float half_x_step,
                                         float half_y_step,
                                         float motion_scale,
                                         float spin_drive)
{
    int cxp = xx + (bw >> 1);
    int cyp = yy + (bh >> 1);
    int pos00 = yy * w + xx;
    float force_x;
    float force_y;
    float light_t0;
    float force_m;
    float drop_x;
    float drop_y;
    float drop_h;
    float drop_mag;
    float drop_cohesion;
    float grad_light;
    float motion_abs;
    float well_gate;
    float local;
    float local_core;
    float local_ring;
    float disk_wave;
    float gvx;
    float gvy;
    float vel_x;
    float vel_y;
    float max_v;
    float v2;
    float redshift;
    float xn;
    float yn;
    int active00;
    int plume00;
    int gx00;
    int gy00;
    int diff00;
    int use_bilinear;

    if (cxp >= w)
        cxp = w - 1;
    if (cyp >= h)
        cyp = h - 1;

    eh_sample_force_grid(e, cxp, cyp, &force_x, &force_y, &light_t0, &force_m);
    eh_sample_flow_grid(e, cxp, cyp, flow_cell, &gvx, &gvy);
    eh_sample_drop_grid(e, cxp, cyp, &drop_x, &drop_y, &drop_h);

    drop_mag = eh_absf(drop_x) + eh_absf(drop_y);
    if (drop_mag > 3.0f)
        drop_mag = 3.0f;
    drop_cohesion = eh_smooth01((drop_mag * 0.38f + eh_absf(drop_h) * 0.24f - 0.028f) * 2.25f);

    grad_light = force_m;
    if (grad_light > 1.45f)
        grad_light = 1.45f;
    motion_abs = force_m;
    if (motion_abs > 2.0f)
        motion_abs = 2.0f;

    well_gate = 0.50f + light_t0 * 0.50f + grad_light * 0.28f;
    well_gate = eh_clampf(well_gate, 0.34f, 1.0f);
    local = eh_smooth01((light_t0 * 1.08f + grad_light * 0.32f + motion_abs * 0.05f - 0.18f) * 1.64f);
    local_core = eh_smooth01((light_t0 - 0.58f) * 2.30f + grad_light * 0.24f);
    local_ring = local * (0.22f + grad_light * 0.36f + fold_t * 0.70f) * (1.0f - local_core * 0.60f) * well_gate;
    local_ring += drop_cohesion * (0.032f + fold_t * 0.036f + chroma_t * 0.020f) * (1.0f - local_core * 0.62f);
    local_ring = eh_clampf(local_ring, 0.0f, 1.0f);

    xn = e->xnorm[cxp] + half_x_step;
    yn = ((float) cyp + 0.5f - cy) * inv_cy + half_y_step;
    disk_wave = eh_lut_sin_fast(e,
                           (xn * wave_c + yn * wave_s) * (4.0f + fold_t * 8.2f) +
                           (force_x - force_y) * (0.020f + fold_t * 0.018f) +
                           light_t0 * (1.5f + chroma_t * 1.1f) +
                           e->spin_phase2);
    local_ring += (disk_wave * 0.5f + 0.5f) * local * fold_t * lens_curve * 0.135f * well_gate;
    if (local_ring > 1.0f)
        local_ring = 1.0f;

    vel_x = gvx * fluid_pixels * (1.08f + local * 0.68f) + force_x;
    vel_y = gvy * fluid_pixels * (1.08f + local * 0.68f) + force_y;
    vel_x += drop_x * (0.82f + ((float) flow_i * 0.0115f) + lens_curve * 0.82f) * (0.74f + local * 0.58f);
    vel_y += drop_y * (0.82f + ((float) flow_i * 0.0115f) + lens_curve * 0.82f) * (0.74f + local * 0.58f);

    if (spin_drive > 0.000001f || spin_drive < -0.000001f) {
        float spin_gate = 0.28f + local * 1.52f + light_t0 * 0.36f + grad_light * 0.22f;
        vel_x += -force_y * spin_drive * spin_gate;
        vel_y +=  force_x * spin_drive * spin_gate;
    }

    vel_x *= 0.78f + motion_scale * 0.28f;
    vel_y *= 0.78f + motion_scale * 0.28f;

    v2 = vel_x * vel_x + vel_y * vel_y;
    max_v = 4.0f + fluid_pixels * 0.28f + lens_curve * 30.0f + local * 54.0f + motion_abs * 7.0f;
    if (v2 > max_v * max_v) {
        float sc = max_v * eh_fast_rsqrt(v2);
        vel_x *= sc;
        vel_y *= sc;
    }

    eh_update_smoke_pixel(e, src_y, prev_y, ref_y, on_y, off_y, veil, nx_on, nx_off, nx_veil,
                          xx, yy, pos00, rise_step, w, h, density_i, lens_i,
                          &active00, &plume00, &gx00, &gy00, &diff00);

    if (active00 > EH_GATE) {
        float edge_scale = (float) (eh_absi(gx00) + eh_absi(gy00)) * (1.0f / 510.0f);
        float motion_push = (float) diff00 * (1.0f / 255.0f);
        float push_gain = 0.085f * (1.0f - drop_cohesion * 0.74f);
        vel_x += (float) gx00 * (1.0f / 255.0f) * flow_pixels * (edge_scale + motion_push) * push_gain;
        vel_y += (float) gy00 * (1.0f / 255.0f) * flow_pixels * (edge_scale + motion_push) * push_gain;
    }

    redshift = (local * 0.68f + local_core * 0.54f + light_t0 * 0.11f) * redshift_scale;
    if (redshift > 72.0f)
        redshift = 72.0f;

    use_bilinear = (local > 0.50f || active00 > 220 || flow_i > 98 || drop_cohesion > 0.40f ||
                    (drop_cohesion > 0.24f && light_t0 < 0.34f) ||
                    (drop_cohesion > 0.30f && motion_abs > 0.18f));

    eh_render_area_sampled(e, Y, U, V, src_y, old_y, old_u, old_v, next_y, next_u, next_v,
                           prev_y, ref_y, nx_on, nx_off, nx_veil,
                           pos00, xx, yy, w, h, bw, bh, vel_x, vel_y,
                           local, local_core, local_ring, well_gate, disk_wave,
                           src_mix, chroma_src_mix, chroma_gain, smoke_t, density_t, trail_t, chroma_t,
                           lens_curve, ring_scale, shadow_scale, redshift, active00, plume00,
                           nx_veil[pos00], nx_on[pos00], nx_off[pos00], nx_veil[pos00], drop_cohesion, use_bilinear);
}

void eventhorizon_apply(void *ptr, VJFrame *frame, int *args)
{
    eventhorizon_t *e = (eventhorizon_t *) ptr;
    uint8_t *restrict Y;
    uint8_t *restrict U;
    uint8_t *restrict V;
    uint8_t *restrict src_y;
    uint8_t *restrict old_y;
    uint8_t *restrict old_u;
    uint8_t *restrict old_v;
    uint8_t *restrict next_y;
    uint8_t *restrict next_u;
    uint8_t *restrict next_v;
    uint8_t *restrict prev_y;
    uint8_t *restrict ref_y;
    uint8_t *restrict on_y;
    uint8_t *restrict off_y;
    uint8_t *restrict veil;
    uint8_t *restrict nx_on;
    uint8_t *restrict nx_off;
    uint8_t *restrict nx_veil;
    int w;
    int h;
    int len;
    int build_i;
    int source_i;
    int flow_i;
    int swirl_i;
    int smoke_i;
    int decay_i;
    int density_i;
    int lens_i;
    int folds_i;
    int spin_i;
    int trail_i;
    int chroma_i;
    int speed_i;
    float build_t;
    float source_t;
    float flow_t;
    float swirl_t;
    float smoke_t;
    float density_t;
    float lens_t;
    float fold_t;
    float trail_t;
    float chroma_t;
    float speed_signed;
    float speed_mag;
    float motion_scale;
    float mature_rate;
    float src_mix;
    float chroma_src_mix;
    float chroma_gain;
    float flow_pixels;
    float fluid_pixels;
    float fluid_persistence;
    float lens_curve;
    float gravity_gain;
    float vortex_gain;
    float ring_scale;
    float shadow_scale;
    float redshift_scale;
    float wave_s;
    float wave_c;
    float spin_f;
    float spin_abs;
    float spin_drive;
    float cy;
    float inv_cy;
    float half_x_step;
    float half_y_step;
    int tone_mix;
    int rise_step;
    int flow_cell;
    int force_cell;
    int qrows;
    int qcols;
    int qy;
    int i;

    if (!e || !frame || !args)
        return;
    if (!frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    w = frame->width;
    h = frame->height;
    len = w * h;
    if (w != e->w || h != e->h || len != e->len || w < 4 || h < 4)
        return;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    if (!e->seeded)
        eh_seed(e, frame);

    build_i = eh_clampi(args[P_BUILD], 0, 100);
    source_i = eh_clampi(args[P_SOURCE], 0, 100);
    flow_i = eh_clampi(args[P_FLOW], 0, 100);
    swirl_i = eh_clampi(args[P_SWIRL], 0, 100);
    smoke_i = eh_clampi(args[P_SMOKE], 0, 100);
    decay_i = eh_clampi(args[P_DECAY], 0, 100);
    density_i = eh_clampi(args[P_DENSITY], 0, 100);
    lens_i = eh_clampi(args[P_LENS], 0, 100);
    folds_i = eh_clampi(args[P_FOLDS], 0, 100);
    spin_i = eh_clampi(args[P_SPIN], -100, 100);
    trail_i = eh_clampi(args[P_TRAIL], 0, 100);
    chroma_i = eh_clampi(args[P_CHROMA], 0, 100);
    speed_i = eh_clampi(args[P_SPEED], -100, 100);

    build_t = (float) build_i * 0.01f;
    source_t = (float) source_i * 0.01f;
    flow_t = (float) flow_i * 0.01f;
    swirl_t = (float) swirl_i * 0.01f;
    smoke_t = (float) smoke_i * 0.01f;
    density_t = (float) density_i * 0.01f;
    lens_t = (float) lens_i * 0.01f;
    fold_t = (float) folds_i * 0.01f;
    trail_t = (float) trail_i * 0.01f;
    chroma_t = (float) chroma_i * 0.01f;
    speed_signed = (float) speed_i * 0.01f;
    speed_mag = eh_absf(speed_signed);
    motion_scale = 0.006f + speed_mag * 0.18f + speed_mag * speed_mag * 1.85f;
    if (speed_i == 0)
        motion_scale = 0.0f;

    eh_build_luts(e, smoke_i, decay_i, density_i, flow_i, lens_i);

    mature_rate = 0.0005f + build_t * build_t * 0.0500f;
    e->maturity += (1.0f - e->maturity) * mature_rate;
    if (e->maturity > 1.0f)
        e->maturity = 1.0f;

    src_mix = eh_lerpf(0.0008f + source_t * 0.035f + source_t * source_t * 0.185f,
                       0.0006f + source_t * 0.018f + source_t * source_t * 0.092f,
                       e->maturity);
    src_mix *= 1.0f - trail_t * 0.68f;
    src_mix = eh_clampf(src_mix, 0.0007f, 0.28f);

    chroma_src_mix = src_mix + 0.0020f + source_t * 0.045f + (1.0f - trail_t) * 0.006f;
    chroma_src_mix = eh_clampf(chroma_src_mix, 0.0020f, 0.30f);
    chroma_gain = 0.982f + chroma_t * 0.022f + chroma_t * chroma_t * 0.118f;
    eh_update_palette(e, chroma_t);

    lens_curve = eh_smooth01(lens_t);
    flow_pixels = (0.35f + e->maturity * 1.18f) * (0.55f + flow_t * 3.50f + flow_t * flow_t * 14.5f) * (0.60f + speed_mag * 0.82f);
    fluid_pixels = (1.10f + e->maturity * 1.28f) * (0.70f + flow_t * 3.20f + flow_t * flow_t * 18.0f) * (0.66f + speed_mag * 1.02f);
    fluid_persistence = 0.78f + trail_t * 0.16f - flow_t * 0.050f;
    fluid_persistence = eh_clampf(fluid_persistence, 0.64f, 0.970f);

    spin_f = (float) spin_i * 0.01f;
    spin_abs = eh_absf(spin_f);
    gravity_gain = lens_curve * (12.0f + lens_curve * 88.0f + flow_t * 20.0f);
    vortex_gain = lens_curve * (2.0f + swirl_t * 32.0f + fold_t * 16.0f) * (0.22f + spin_abs * 1.80f);
    if (spin_i < 0)
        vortex_gain = -vortex_gain;

    spin_drive = spin_f * lens_curve * (0.34f + spin_abs * 1.05f) * (0.70f + swirl_t * 1.85f + flow_t * 0.72f) * (0.70f + motion_scale * 0.30f);

    ring_scale = (3.0f + density_t * 7.0f + fold_t * 18.0f + e->pal_gain * 5.5f) * lens_curve;
    shadow_scale = (12.0f + lens_curve * 26.0f + density_t * 9.0f) * lens_curve;
    redshift_scale = (12.0f + lens_curve * 24.0f + density_t * 10.0f) * lens_curve;

    e->time = eh_wrap_2pi(e->time + (0.0011f + flow_t * 0.0110f + spin_abs * 0.0040f) * motion_scale);
    e->orbit = eh_wrap_2pi(e->orbit + (0.0022f + lens_curve * 0.0080f + fold_t * 0.0120f) * motion_scale * (speed_i < 0 ? -1.0f : 1.0f));
    e->spin_phase1 = eh_wrap_2pi(e->spin_phase1 + (0.009f + spin_abs * 0.090f + lens_curve * 0.018f) * motion_scale * (spin_i < 0 ? -1.0f : 1.0f));
    e->spin_phase2 = eh_wrap_2pi(e->spin_phase2 - (0.008f + spin_abs * 0.078f + lens_curve * 0.016f) * motion_scale * (spin_i < 0 ? -1.0f : 1.0f));
    e->pulse = e->pulse * 0.95f + (eh_lut_sin(e, e->time * 0.73f) * 0.5f + 0.5f) * lens_curve * flow_t * 0.010f;

    eh_lut_sincos(e, e->spin_phase1 + e->orbit, &wave_s, &wave_c);

    cy = (float) h * 0.5f;
    inv_cy = 1.0f / cy;
    half_x_step = 1.0f / (float) w;
    half_y_step = 1.0f / (float) h;

    tone_mix = (int) (lens_curve * 168.0f + chroma_t * 6.0f + 0.5f);
    tone_mix = eh_clampi(tone_mix, 0, 255);
    if (!e->tone_lut_valid || e->last_tone_mix != tone_mix) {
        for (i = 0; i < 256; i++) {
            int g = e->gamma_lut[i];
            e->tone_lut[i] = eh_u8i(i + (((g - i) * tone_mix + 127) / 255));
        }
        e->last_tone_mix = tone_mix;
        e->tone_lut_valid = 1;
    }

    rise_step = 1 + (flow_i * 3 + 50) / 100;
    flow_cell = 52 - (flow_i * 24 + 50) / 100 - (swirl_i * 10 + 50) / 100;
    if (flow_cell < 12)
        flow_cell = 12;
    else if (flow_cell > 72)
        flow_cell = 72;
    eh_build_flow_grid(e, flow_cell, fluid_persistence);

    force_cell = 22 + ((100 - lens_i) * 18 + 50) / 100 - (flow_i * 8 + 50) / 100 - (swirl_i * 4 + 50) / 100;
    if (force_cell < 10)
        force_cell = 10;
    else if (force_cell > 42)
        force_cell = 42;

    src_y = e->src_y;
    old_y = e->paint_y;
    old_u = e->paint_u;
    old_v = e->paint_v;
    next_y = e->next_y;
    next_u = e->next_u;
    next_v = e->next_v;
    prev_y = e->prev_y;
    ref_y = e->ref_y;
    on_y = e->on_y;
    off_y = e->off_y;
    veil = e->veil;
    nx_on = e->nx_on_y;
    nx_off = e->nx_off_y;
    nx_veil = e->nx_veil;

    veejay_memcpy(src_y, Y, len);

    eh_build_light_force_grid(e, src_y, prev_y, force_cell, gravity_gain, vortex_gain,
                              lens_curve, flow_t, motion_scale,
                              0.24f + trail_t * 0.34f);
    if ((e->frame & 1) == 0 || e->drop_w != e->force_w || e->drop_h != e->force_h || e->drop_cell != force_cell)
        eh_update_drop_field(e, force_cell, lens_curve, flow_t, smoke_t, density_t, trail_t, motion_scale);

    qcols = (w + 7) >> 3;
    qrows = (h + 7) >> 3;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (qy = 0; qy < qrows; qy++) {
        int yy = qy << 3;
        int qx;

        for (qx = 0; qx < qcols; qx++) {
            int xx = qx << 3;
            int bw = (xx + 8 <= w) ? 8 : (w - xx);
            int bh = (yy + 8 <= h) ? 8 : (h - yy);
            int tile_detail = eh_tile_range_u8(src_y, yy * w + xx, w, bw, bh);
            int dark_protect = 0;

            if (tile_detail <= 56 && bw >= 4 && bh >= 4) {
                int p0 = yy * w + xx;
                int p1 = p0 + bw - 1;
                int p2 = p0 + (bh - 1) * w;
                int p3 = p2 + bw - 1;
                int pc = p0 + (bh >> 1) * w + (bw >> 1);
                int tile_avg = ((int) src_y[p0] + (int) src_y[p1] +
                                (int) src_y[p2] + (int) src_y[p3] +
                                ((int) src_y[pc] << 1) + 3) / 6;
                dark_protect = (tile_avg < 66) || (tile_avg < 92 && tile_detail > 30);
            }

            if (tile_detail > 56 || bw < 4 || bh < 4) {
                int sy;
                for (sy = 0; sy < bh; sy += 2) {
                    int sx;
                    int sh = (sy + 2 <= bh) ? 2 : (bh - sy);
                    for (sx = 0; sx < bw; sx += 2) {
                        int sw = (sx + 2 <= bw) ? 2 : (bw - sx);
                        eh_process_light_tile(e, Y, U, V, src_y, old_y, old_u, old_v,
                                              next_y, next_u, next_v, prev_y, ref_y,
                                              on_y, off_y, veil, nx_on, nx_off, nx_veil,
                                              xx + sx, yy + sy, sw, sh, w, h,
                                              rise_step, flow_cell, density_i, lens_i, flow_i,
                                              fluid_pixels, flow_pixels,
                                              src_mix, chroma_src_mix, chroma_gain,
                                              smoke_t, density_t, trail_t, chroma_t,
                                              lens_curve, fold_t, ring_scale, shadow_scale, redshift_scale,
                                              wave_s, wave_c, cy, inv_cy, half_x_step, half_y_step,
                                              motion_scale, spin_drive);
                    }
                }
            }
            else if (dark_protect && bw >= 4 && bh >= 4) {
                int sy;
                for (sy = 0; sy < bh; sy += 4) {
                    int sx;
                    int sh = (sy + 4 <= bh) ? 4 : (bh - sy);
                    for (sx = 0; sx < bw; sx += 4) {
                        int sw = (sx + 4 <= bw) ? 4 : (bw - sx);
                        eh_process_light_tile(e, Y, U, V, src_y, old_y, old_u, old_v,
                                              next_y, next_u, next_v, prev_y, ref_y,
                                              on_y, off_y, veil, nx_on, nx_off, nx_veil,
                                              xx + sx, yy + sy, sw, sh, w, h,
                                              rise_step, flow_cell, density_i, lens_i, flow_i,
                                              fluid_pixels, flow_pixels,
                                              src_mix, chroma_src_mix, chroma_gain,
                                              smoke_t, density_t, trail_t, chroma_t,
                                              lens_curve, fold_t, ring_scale, shadow_scale, redshift_scale,
                                              wave_s, wave_c, cy, inv_cy, half_x_step, half_y_step,
                                              motion_scale, spin_drive);
                    }
                }
            }
            else {
                eh_process_light_tile(e, Y, U, V, src_y, old_y, old_u, old_v,
                                      next_y, next_u, next_v, prev_y, ref_y,
                                      on_y, off_y, veil, nx_on, nx_off, nx_veil,
                                      xx, yy, bw, bh, w, h,
                                      rise_step, flow_cell, density_i, lens_i, flow_i,
                                      fluid_pixels, flow_pixels,
                                      src_mix, chroma_src_mix, chroma_gain,
                                      smoke_t, density_t, trail_t, chroma_t,
                                      lens_curve, fold_t, ring_scale, shadow_scale, redshift_scale,
                                      wave_s, wave_c, cy, inv_cy, half_x_step, half_y_step,
                                      motion_scale, spin_drive);
            }
        }
    }

    {
        uint8_t *tmp;
        tmp = e->paint_y; e->paint_y = e->next_y; e->next_y = tmp;
        tmp = e->paint_u; e->paint_u = e->next_u; e->next_u = tmp;
        tmp = e->paint_v; e->paint_v = e->next_v; e->next_v = tmp;
        tmp = e->on_y; e->on_y = e->nx_on_y; e->nx_on_y = tmp;
        tmp = e->off_y; e->off_y = e->nx_off_y; e->nx_off_y = tmp;
        tmp = e->veil; e->veil = e->nx_veil; e->nx_veil = tmp;
    }

    e->frame++;
}
