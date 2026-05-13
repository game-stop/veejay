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
#include "edgefoldtide.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define EH_PARAMS 10

#define P_SOURCE  0
#define P_FLOW    1
#define P_GEOM    2
#define P_MIRROR  3
#define P_GLOW    4
#define P_PULL    5
#define P_TRAIL   6
#define P_COLOR   7
#define P_SPEED   8
#define P_SOFT    9

#define EH_TRIG_SIZE 1024
#define EH_TRIG_MASK 1023
#define EH_TWO_PI 6.28318530718f
#define EH_INV_TWO_PI 0.15915494309f

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
    uint8_t *charge;
    uint8_t *next_charge;

    float *force_x;
    float *force_y;
    float *force_m;
    int grid_capacity;
    int grid_w;
    int grid_h;
    int grid_cell;

    uint8_t gamma_lut[256];
    uint8_t tone_lut[256];
    uint8_t charge_lut[256];
    uint8_t decay_lut[256];
    uint8_t source_lut[256];
    uint8_t glow_lut[256];

    float sin_lut[EH_TRIG_SIZE];
    float cos_lut[EH_TRIG_SIZE];

    int luts_valid;
    int last_source;
    int last_glow;
    int last_trail;
    int last_color;
    int last_tone;

    float maturity;
    float time;
    float phase;
} edgefoldtide_t;

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
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (uint8_t) (v + 0.5f);
}

static inline float eh_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline int eh_floor_to_int(float v)
{
    int i = (int) v;
    if (v < (float) i) i--;
    return i;
}

static inline float eh_wrap_2pi(float v)
{
    if (v >= EH_TWO_PI || v < 0.0f) {
        int k = (int) (v * EH_INV_TWO_PI);
        v -= (float) k * EH_TWO_PI;
        if (v < 0.0f) v += EH_TWO_PI;
        else if (v >= EH_TWO_PI) v -= EH_TWO_PI;
    }
    return v;
}

static inline float eh_fast_rsqrt(float x)
{
    union { float f; unsigned int i; } u;
    float y;
    if (x <= 0.0f) return 0.0f;
    u.f = x;
    u.i = 0x5f3759dfU - (u.i >> 1);
    y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

static inline void eh_lut_sincos_fast(const edgefoldtide_t *e, float phase, float *s, float *c)
{
    int idx = (int) (phase * ((float) EH_TRIG_SIZE * EH_INV_TWO_PI));
    idx &= EH_TRIG_MASK;
    *s = e->sin_lut[idx];
    *c = e->cos_lut[idx];
}

static inline size_t eh_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
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

static inline int eh_index_clamped(int w, int h, int x, int y)
{
    if (x < 0) x = 0;
    else if (x >= w) x = w - 1;
    if (y < 0) y = 0;
    else if (y >= h) y = h - 1;
    return y * w + x;
}

static inline float eh_mirror_float(float v, int n)
{
    float p;
    int k;
    if (n <= 1) return 0.0f;
    if (v >= 0.0f && v <= (float) (n - 1)) return v;
    p = (float) ((n - 1) << 1);
    k = eh_floor_to_int(v / p);
    v -= (float) k * p;
    if (v < 0.0f) v += p;
    if (v > (float) (n - 1)) v = p - v;
    return v;
}

static inline int eh_sample_y_bilinear_mirror(const uint8_t *restrict y, float fx, float fy, int w, int h)
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

    fx = eh_mirror_float(fx, w);
    fy = eh_mirror_float(fy, h);

    ix = (int) fx;
    iy = (int) fy;
    wx = (int) ((fx - (float) ix) * 256.0f);
    wy = (int) ((fy - (float) iy) * 256.0f);

    x1 = ix + 1;
    if (x1 >= w) x1 = ix;
    y1 = iy + 1;
    if (y1 >= h) y1 = iy;

    p00 = iy * w + ix;
    p10 = iy * w + x1;
    p01 = y1 * w + ix;
    p11 = y1 * w + x1;

    a = (int) y[p00] * (256 - wx) + (int) y[p10] * wx;
    b = (int) y[p01] * (256 - wx) + (int) y[p11] * wx;
    return ((a * (256 - wy) + b * wy) + 32768) >> 16;
}

static inline void eh_sample_uv_nearest_mirror(const uint8_t *restrict u,
                                               const uint8_t *restrict v,
                                               float fx,
                                               float fy,
                                               int w,
                                               int h,
                                               int *ou,
                                               int *ov)
{
    int ix;
    int iy;
    fx = eh_mirror_float(fx, w);
    fy = eh_mirror_float(fy, h);
    ix = (int) (fx + 0.5f);
    iy = (int) (fy + 0.5f);
    if (ix < 0) ix = 0;
    else if (ix >= w) ix = w - 1;
    if (iy < 0) iy = 0;
    else if (iy >= h) iy = h - 1;
    ix += iy * w;
    *ou = u[ix];
    *ov = v[ix];
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

static void eh_palette_source(float source_u,
                              float source_v,
                              float phase,
                              float color_t,
                              float charge_t,
                              float *out_u,
                              float *out_v)
{
    float su = source_u * (1.0f / 128.0f);
    float sv = source_v * (1.0f / 128.0f);
    float warm = eh_clampf(0.5f + sv * 0.38f - su * 0.10f, 0.0f, 1.0f);
    float cool = eh_clampf(0.5f - sv * 0.24f + su * 0.26f, 0.0f, 1.0f);
    float rose = eh_clampf(0.5f + sv * 0.28f + su * 0.16f, 0.0f, 1.0f);
    float wave = sinf(phase) * 0.5f + 0.5f;
    float a = eh_lerpf(warm, cool, wave * 0.48f + color_t * 0.22f);
    float b = eh_lerpf(rose, cool, (1.0f - wave) * 0.42f + color_t * 0.18f);
    float pu = eh_lerpf(12.0f, -18.0f, a) + eh_lerpf(source_u * 0.10f, source_u * 0.28f, color_t);
    float pv = eh_lerpf(20.0f, -10.0f, b) + eh_lerpf(source_v * 0.10f, source_v * 0.28f, color_t);
    float gain = (0.24f + color_t * 0.66f) * charge_t;
    *out_u = pu * gain;
    *out_v = pv * gain;
}

static void eh_build_luts(edgefoldtide_t *e,
                          int source,
                          int glow,
                          int trail,
                          int color,
                          int tone_mix)
{
    int i;
    int decay_power;
    int charge_gain;
    int source_base;

    if (e->luts_valid &&
        e->last_source == source &&
        e->last_glow == glow &&
        e->last_trail == trail &&
        e->last_color == color &&
        e->last_tone == tone_mix)
        return;

    decay_power = 128 + (trail * 119 + 50) / 100;
    if (decay_power > 250) decay_power = 250;
    charge_gain = 44 + glow + (color >> 1);
    source_base = 2 + (source * 22 + 50) / 100;

    for (i = 0; i < 256; i++) {
        int g = e->gamma_lut[i];
        int ch = i - 8;
        int src;
        int glowv;
        if (ch < 0) ch = 0;
        ch = (ch * charge_gain + 127) / 255;
        if (ch > 255) ch = 255;
        src = source_base + ((255 - i) >> 7);
        if (src > 32) src = 32;
        glowv = (i * (24 + glow) + 127) / 255;
        if (glowv > 84) glowv = 84;
        e->tone_lut[i] = eh_u8i(i + (((g - i) * tone_mix + 127) / 255));
        e->charge_lut[i] = (uint8_t) ch;
        e->decay_lut[i] = (uint8_t) ((i * decay_power + 127) / 255);
        e->source_lut[i] = (uint8_t) src;
        e->glow_lut[i] = (uint8_t) glowv;
    }

    e->last_source = source;
    e->last_glow = glow;
    e->last_trail = trail;
    e->last_color = color;
    e->last_tone = tone_mix;
    e->luts_valid = 1;
}

static void eh_build_cathedral_force(edgefoldtide_t *e,
                                     const uint8_t *restrict src_y,
                                     const uint8_t *restrict prev_y,
                                     int flow_i,
                                     int geom_i,
                                     int mirror_i,
                                     int pull_i,
                                     int trail_i,
                                     int speed_i,
                                     int soft_i)
{
    int w = e->w;
    int h = e->h;
    int cell = e->grid_cell;
    int gw = (w + cell - 1) / cell + 2;
    int gh = (h + cell - 1) / cell + 2;
    int gx;
    int gy;
    float flow_t = (float) flow_i * 0.01f;
    float geom_t = (float) geom_i * 0.01f;
    float mirror_t = (float) mirror_i * 0.01f;
    float pull_t = (float) pull_i * 0.01f;
    float trail_t = (float) trail_i * 0.01f;
    float soft_t = (float) soft_i * 0.01f;
    float speed_t = eh_absf((float) speed_i) * 0.01f;
    float persist = 0.66f + trail_t * 0.24f + soft_t * 0.08f - flow_t * 0.07f;
    float one_minus;
    float dir = speed_i < 0 ? -1.0f : 1.0f;
    uint32_t seed = (uint32_t) (e->frame >> 3);

    if (gw * gh > e->grid_capacity)
        return;

    e->grid_w = gw;
    e->grid_h = gh;
    persist = eh_clampf(persist, 0.56f, 0.96f);
    one_minus = 1.0f - persist;

    for (gy = 0; gy < gh; gy++) {
        int py = gy * cell;
        if (py >= h) py = h - 1;
        for (gx = 0; gx < gw; gx++) {
            int px = gx * cell;
            int r1 = cell + (soft_i >> 3) + (geom_i >> 4);
            int r2 = r1 + cell;
            int gi = gy * gw + gx;
            int cpos;
            int l1;
            int r1p;
            int u1;
            int d1;
            int l2;
            int r2p;
            int u2;
            int d2;
            int grad_x;
            int grad_y;
            int motion;
            int edge;
            int lum;
            float inv;
            float nx;
            float ny;
            float tx;
            float ty;
            float eg;
            float motion_push;
            float noise_x;
            float noise_y;
            float target_x;
            float target_y;
            float target_m;

            if (px >= w) px = w - 1;
            cpos = eh_index_clamped(w, h, px, py);

            l1 = eh_index_clamped(w, h, px - r1, py);
            r1p = eh_index_clamped(w, h, px + r1, py);
            u1 = eh_index_clamped(w, h, px, py - r1);
            d1 = eh_index_clamped(w, h, px, py + r1);
            l2 = eh_index_clamped(w, h, px - r2, py);
            r2p = eh_index_clamped(w, h, px + r2, py);
            u2 = eh_index_clamped(w, h, px, py - r2);
            d2 = eh_index_clamped(w, h, px, py + r2);

            grad_x = ((int) src_y[r1p] - (int) src_y[l1]) * 2 + ((int) src_y[r2p] - (int) src_y[l2]);
            grad_y = ((int) src_y[d1] - (int) src_y[u1]) * 2 + ((int) src_y[d2] - (int) src_y[u2]);
            grad_x /= 3;
            grad_y /= 3;
            edge = eh_absi(grad_x) + eh_absi(grad_y);
            if (edge > 510) edge = 510;
            lum = src_y[cpos];
            motion = (int) src_y[cpos] - (int) prev_y[cpos];

            inv = eh_fast_rsqrt((float) (grad_x * grad_x + grad_y * grad_y) + 12.0f);
            nx = (float) grad_x * inv;
            ny = (float) grad_y * inv;
            tx = -ny;
            ty = nx;
            eg = (float) edge * (1.0f / 510.0f);
            motion_push = (float) motion * (1.0f / 255.0f);

            noise_x = eh_hash_signed(((uint32_t) gx * 73856093U) ^ ((uint32_t) gy * 19349663U) ^ (seed * 83492791U));
            noise_y = eh_hash_signed(((uint32_t) gx * 19349663U) ^ ((uint32_t) gy * 73856093U) ^ (seed * 2654435761U));

            target_x = tx * flow_t * (0.32f + eg * 1.46f) * dir;
            target_y = ty * flow_t * (0.32f + eg * 1.46f) * dir;

            target_x += nx * pull_t * (0.18f + eg * 0.80f) * (0.38f + (float) lum * (1.0f / 255.0f));
            target_y += ny * pull_t * (0.18f + eg * 0.80f) * (0.38f + (float) lum * (1.0f / 255.0f));

            target_x += -nx * motion_push * (0.28f + speed_t * 0.88f + mirror_t * 0.35f);
            target_y += -ny * motion_push * (0.28f + speed_t * 0.88f + mirror_t * 0.35f);

            target_x += noise_x * geom_t * (0.030f + (1.0f - eg) * 0.090f);
            target_y += noise_y * geom_t * (0.030f + (1.0f - eg) * 0.090f);

            target_m = eg * (0.54f + geom_t * 0.50f) + eh_absf(motion_push) * (0.22f + mirror_t * 0.35f);
            if (target_m > 1.0f) target_m = 1.0f;

            e->force_x[gi] = e->force_x[gi] * persist + target_x * one_minus;
            e->force_y[gi] = e->force_y[gi] * persist + target_y * one_minus;
            e->force_m[gi] = e->force_m[gi] * persist + target_m * one_minus;
        }
    }
}

vj_effect *edgefoldtide_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve) return NULL;

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

    ve->limits[0][P_SOURCE] = 0; ve->limits[1][P_SOURCE] = 100; ve->defaults[P_SOURCE] = 56;
    ve->limits[0][P_FLOW] = 0; ve->limits[1][P_FLOW] = 100; ve->defaults[P_FLOW] = 62;
    ve->limits[0][P_GEOM] = 0; ve->limits[1][P_GEOM] = 100; ve->defaults[P_GEOM] = 74;
    ve->limits[0][P_MIRROR] = 0; ve->limits[1][P_MIRROR] = 100; ve->defaults[P_MIRROR] = 82;
    ve->limits[0][P_GLOW] = 0; ve->limits[1][P_GLOW] = 100; ve->defaults[P_GLOW] = 52;
    ve->limits[0][P_PULL] = 0; ve->limits[1][P_PULL] = 100; ve->defaults[P_PULL] = 42;
    ve->limits[0][P_TRAIL] = 0; ve->limits[1][P_TRAIL] = 100; ve->defaults[P_TRAIL] = 76;
    ve->limits[0][P_COLOR] = 0; ve->limits[1][P_COLOR] = 100; ve->defaults[P_COLOR] = 64;
    ve->limits[0][P_SPEED] = -100; ve->limits[1][P_SPEED] = 100; ve->defaults[P_SPEED] = 42;
    ve->limits[0][P_SOFT] = 0; ve->limits[1][P_SOFT] = 100; ve->defaults[P_SOFT] = 66;

    ve->description = "Stained Current";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Source Presence",
        "Contour Flow",
        "Cathedral Geometry",
        "Mirror Depth",
        "Biolume Glow",
        "Contour Pull",
        "Trail Memory",
        "Pastel Color",
        "Motion Speed",
        "Surface Softness"
    );

    (void) w;
    (void) h;
    return ve;
}

void *edgefoldtide_malloc(int w, int h)
{
    edgefoldtide_t *e;
    unsigned char *base;
    size_t len;
    size_t total;
    size_t off;
    size_t gridcap;
    int i;

    if (w <= 0 || h <= 0) return NULL;
    len = (size_t) w * (size_t) h;
    if (len == 0) return NULL;

    e = (edgefoldtide_t *) vj_calloc(sizeof(edgefoldtide_t));
    if (!e) return NULL;

    e->w = w;
    e->h = h;
    e->len = (int) len;
    e->seeded = 0;
    e->frame = 0;
    e->n_threads = vje_advise_num_threads((int) len);
    if (e->n_threads <= 0) e->n_threads = 1;

    gridcap = (size_t) (((w + 7) / 8) + 3) * (size_t) (((h + 7) / 8) + 3);
    total = len * 10 + sizeof(float) * gridcap * 3 + 128;
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
    e->charge = (uint8_t *) (base + off); off += len;
    e->next_charge = (uint8_t *) (base + off); off += len;
    off = eh_align_size(off, sizeof(float));
    e->grid_capacity = (int) gridcap;
    e->force_x = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_y = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_m = (float *) (base + off); off += sizeof(float) * gridcap;

    veejay_memset(e->src_y, 0, len);
    veejay_memset(e->paint_y, 0, len);
    veejay_memset(e->paint_u, 128, len);
    veejay_memset(e->paint_v, 128, len);
    veejay_memset(e->next_y, 0, len);
    veejay_memset(e->next_u, 128, len);
    veejay_memset(e->next_v, 128, len);
    veejay_memset(e->prev_y, 0, len);
    veejay_memset(e->charge, 0, len);
    veejay_memset(e->next_charge, 0, len);
    veejay_memset(e->force_x, 0, sizeof(float) * gridcap);
    veejay_memset(e->force_y, 0, sizeof(float) * gridcap);
    veejay_memset(e->force_m, 0, sizeof(float) * gridcap);

    for (i = 0; i < 256; i++) {
        float v = (float) i / 255.0f;
        int g = (int) (powf(v, 0.94f) * 255.0f + 0.5f);
        e->gamma_lut[i] = eh_u8i(g);
        e->tone_lut[i] = e->gamma_lut[i];
        e->charge_lut[i] = (uint8_t) i;
        e->decay_lut[i] = (uint8_t) i;
        e->source_lut[i] = 8;
        e->glow_lut[i] = 0;
    }

    for (i = 0; i < EH_TRIG_SIZE; i++) {
        float a = EH_TWO_PI * ((float) i / (float) EH_TRIG_SIZE);
        e->sin_lut[i] = sinf(a);
        e->cos_lut[i] = cosf(a);
    }

    e->grid_w = 0;
    e->grid_h = 0;
    e->grid_cell = 18;
    e->luts_valid = 0;
    e->maturity = 0.0f;
    e->time = 0.0f;
    e->phase = 0.0f;

    return (void *) e;
}

void edgefoldtide_free(void *ptr)
{
    edgefoldtide_t *e = (edgefoldtide_t *) ptr;
    if (!e) return;
    if (e->region) free(e->region);
    free(e);
}

static void eh_seed(edgefoldtide_t *e, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int len = e->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (i = 0; i < len; i++) {
        e->src_y[i] = Y[i];
        e->paint_y[i] = Y[i];
        e->paint_u[i] = U[i];
        e->paint_v[i] = V[i];
        e->next_y[i] = Y[i];
        e->next_u[i] = U[i];
        e->next_v[i] = V[i];
        e->prev_y[i] = Y[i];
        e->charge[i] = 0;
        e->next_charge[i] = 0;
    }
    e->seeded = 1;
    e->maturity = 0.0f;
}

static inline void eh_force_at_tile(const edgefoldtide_t *e,
                                    int x,
                                    int y,
                                    float *fx,
                                    float *fy,
                                    float *fm)
{
    int cell = e->grid_cell;
    int gx = x / cell;
    int gy = y / cell;
    float tx = ((float) (x - gx * cell)) / (float) cell;
    float ty = ((float) (y - gy * cell)) / (float) cell;
    int gw = e->grid_w;
    int gh = e->grid_h;
    int gi00;
    int gi10;
    int gi01;
    int gi11;
    float wx0;
    float wy0;
    float ax;
    float bx;
    float ay;
    float by;
    float am;
    float bm;

    if (gx < 0) gx = 0;
    else if (gx + 1 >= gw) gx = gw - 2;
    if (gy < 0) gy = 0;
    else if (gy + 1 >= gh) gy = gh - 2;

    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    wx0 = 1.0f - tx;
    wy0 = 1.0f - ty;

    gi00 = gy * gw + gx;
    gi10 = gi00 + 1;
    gi01 = gi00 + gw;
    gi11 = gi01 + 1;

    ax = e->force_x[gi00] * wy0 + e->force_x[gi01] * ty;
    bx = e->force_x[gi10] * wy0 + e->force_x[gi11] * ty;
    ay = e->force_y[gi00] * wy0 + e->force_y[gi01] * ty;
    by = e->force_y[gi10] * wy0 + e->force_y[gi11] * ty;
    am = e->force_m[gi00] * wy0 + e->force_m[gi01] * ty;
    bm = e->force_m[gi10] * wy0 + e->force_m[gi11] * ty;

    *fx = ax * wx0 + bx * tx;
    *fy = ay * wx0 + by * tx;
    *fm = am * wx0 + bm * tx;
}

static inline float eh_tri_wave(float x)
{
    int i = eh_floor_to_int(x);
    float f = x - (float) i;
    if (i & 1) f = 1.0f - f;
    return f;
}

static inline void eh_cathedral_sample(float px,
                                       float py,
                                       float vx,
                                       float vy,
                                       float fm,
                                       float cell_px,
                                       float mirror_depth,
                                       float phase_s,
                                       float phase_c,
                                       float *sx,
                                       float *sy)
{
    float advx = px - vx;
    float advy = py - vy;
    float cx = (eh_floor_to_int(px / cell_px) + 0.5f) * cell_px;
    float cy = (eh_floor_to_int(py / cell_px) + 0.5f) * cell_px;
    float lx = px - cx;
    float ly = py - cy;
    float rx = lx * phase_c - ly * phase_s;
    float ry = lx * phase_s + ly * phase_c;
    float twx = eh_tri_wave((rx / cell_px) + 0.5f) - 0.5f;
    float twy = eh_tri_wave((ry / cell_px) + 0.5f) - 0.5f;
    float foldx = cx + (twx * phase_c + twy * phase_s) * cell_px;
    float foldy = cy + (-twx * phase_s + twy * phase_c) * cell_px;
    float mix = mirror_depth * (0.18f + fm * 0.82f);
    mix = eh_clampf(mix, 0.0f, 0.92f);
    *sx = advx * (1.0f - mix) + foldx * mix;
    *sy = advy * (1.0f - mix) + foldy * mix;
}

void edgefoldtide_apply(void *ptr, VJFrame *frame, int *args)
{
    edgefoldtide_t *e = (edgefoldtide_t *) ptr;
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
    uint8_t *restrict charge;
    uint8_t *restrict next_charge;
    uint8_t *restrict tone_lut;
    uint8_t *restrict source_lut;
    uint8_t *restrict charge_lut;
    uint8_t *restrict decay_lut;
    uint8_t *restrict glow_lut;
    int w;
    int h;
    int len;
    int source_i;
    int flow_i;
    int geom_i;
    int mirror_i;
    int glow_i;
    int pull_i;
    int trail_i;
    int color_i;
    int speed_i;
    int soft_i;
    int do_soft;
    float source_t;
    float flow_t;
    float geom_t;
    float mirror_t;
    float glow_t;
    float pull_t;
    float trail_t;
    float color_t;
    float speed_t;
    float soft_t;
    float speed_curve;
    float mature_rate;
    float source_base;
    float flow_pixels;
    float cell_px;
    float mirror_depth;
    float phase_s;
    float phase_c;
    float tone_mix_f;
    int tone_mix;
    int qcols;
    int qrows;
    int qy;
    float source_delta_base;
    float glow_display_scale;

    if (!e || !frame || !args) return;
    if (!frame->data[0] || !frame->data[1] || !frame->data[2]) return;

    w = frame->width;
    h = frame->height;
    len = w * h;
    if (w != e->w || h != e->h || len != e->len || w < 8 || h < 8) return;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    if (!e->seeded)
        eh_seed(e, frame);

    source_i = eh_clampi(args[P_SOURCE], 0, 100);
    flow_i = eh_clampi(args[P_FLOW], 0, 100);
    geom_i = eh_clampi(args[P_GEOM], 0, 100);
    mirror_i = eh_clampi(args[P_MIRROR], 0, 100);
    glow_i = eh_clampi(args[P_GLOW], 0, 100);
    pull_i = eh_clampi(args[P_PULL], 0, 100);
    trail_i = eh_clampi(args[P_TRAIL], 0, 100);
    color_i = eh_clampi(args[P_COLOR], 0, 100);
    speed_i = eh_clampi(args[P_SPEED], -100, 100);
    soft_i = eh_clampi(args[P_SOFT], 0, 100);
    do_soft = soft_i > 0;

    source_t = (float) source_i * 0.01f;
    flow_t = (float) flow_i * 0.01f;
    geom_t = (float) geom_i * 0.01f;
    mirror_t = (float) mirror_i * 0.01f;
    glow_t = (float) glow_i * 0.01f;
    pull_t = (float) pull_i * 0.01f;
    trail_t = (float) trail_i * 0.01f;
    color_t = (float) color_i * 0.01f;
    speed_t = eh_absf((float) speed_i) * 0.01f;
    soft_t = (float) soft_i * 0.01f;
    speed_curve = speed_t * speed_t * (3.0f - 2.0f * speed_t);

    mature_rate = 0.004f + source_t * 0.018f;
    e->maturity += (1.0f - e->maturity) * mature_rate;
    if (e->maturity > 1.0f) e->maturity = 1.0f;

    tone_mix_f = 0.18f + glow_t * 0.18f + color_t * 0.16f;
    tone_mix = eh_clampi((int) (tone_mix_f * 255.0f + 0.5f), 0, 255);
    eh_build_luts(e, source_i, glow_i, trail_i, color_i, tone_mix);

    e->time = eh_wrap_2pi(e->time + (0.0015f + flow_t * 0.006f + geom_t * 0.003f) * (0.16f + speed_curve * 1.85f));
    e->phase = eh_wrap_2pi(e->phase + (0.0020f + mirror_t * 0.011f) * (0.10f + speed_curve * 1.60f) * (speed_i < 0 ? -1.0f : 1.0f));
    eh_lut_sincos_fast(e, e->phase, &phase_s, &phase_c);

    e->grid_cell = 18 + (soft_i >> 3) - (flow_i >> 4);
    if (e->grid_cell < 10) e->grid_cell = 10;
    else if (e->grid_cell > 34) e->grid_cell = 34;

    src_y = e->src_y;
    old_y = e->paint_y;
    old_u = e->paint_u;
    old_v = e->paint_v;
    next_y = e->next_y;
    next_u = e->next_u;
    next_v = e->next_v;
    prev_y = e->prev_y;
    charge = e->charge;
    next_charge = e->next_charge;
    tone_lut = e->tone_lut;
    source_lut = e->source_lut;
    charge_lut = e->charge_lut;
    decay_lut = e->decay_lut;
    glow_lut = e->glow_lut;

    veejay_memcpy(src_y, Y, len);

    eh_build_cathedral_force(e, src_y, prev_y, flow_i, geom_i, mirror_i, pull_i, trail_i, speed_i, soft_i);

    source_base = (0.004f + source_t * source_t * 0.078f) * (1.0f - trail_t * 0.48f);
    source_base = eh_clampf(source_base, 0.003f, 0.095f);
    flow_pixels = (0.45f + flow_t * flow_t * 9.0f + pull_t * 3.0f) * (0.45f + speed_curve * 0.95f);
    cell_px = 26.0f + (1.0f - geom_t) * 38.0f + soft_t * 22.0f;
    mirror_depth = mirror_t * (0.18f + geom_t * 0.78f);
    source_delta_base = 0.010f + source_t * 0.020f;
    glow_display_scale = 0.08f + glow_t * 0.18f;

    qcols = (w + 3) >> 2;
    qrows = (h + 3) >> 2;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (qy = 0; qy < qrows; qy++) {
        int yy = qy << 2;
        int qx;
        for (qx = 0; qx < qcols; qx++) {
            int xx = qx << 2;
            int txlim = (xx + 4 <= w) ? 4 : w - xx;
            int tylim = (yy + 4 <= h) ? 4 : h - yy;
            int center_x = xx + (txlim >> 1);
            int center_y = yy + (tylim >> 1);
            float ff_x;
            float ff_y;
            float ff_m;
            float vx;
            float vy;
            float v2;
            float max_v;
            float tile_charge;
            float uv_sx;
            float uv_sy;
            int adv_u;
            int adv_v;
            int y0;
            int x0;
            float pal_u;
            float pal_v;
            float src_us;
            float src_vs;
            float chroma_limit;
            float ff_m_boost;
            float glow_y_scale;
            float soft_tile_base;
            float vel_boost;
            int center_pos = center_y * w + center_x;

            eh_force_at_tile(e, center_x, center_y, &ff_x, &ff_y, &ff_m);

            vel_boost = flow_pixels * (1.0f + ff_m * 1.35f);
            vx = ff_x * vel_boost;
            vy = ff_y * vel_boost;
            v2 = vx * vx + vy * vy;
            max_v = 3.0f + flow_t * 14.0f + geom_t * 16.0f + ff_m * 20.0f;
            if (v2 > max_v * max_v) {
                float sc = max_v * eh_fast_rsqrt(v2);
                vx *= sc;
                vy *= sc;
            }

            tile_charge = eh_clampf(ff_m * (0.50f + glow_t * 0.85f + geom_t * 0.40f), 0.0f, 1.0f);
            eh_cathedral_sample((float) center_x + 0.5f, (float) center_y + 0.5f, vx, vy, ff_m,
                                cell_px, mirror_depth, phase_s, phase_c, &uv_sx, &uv_sy);
            eh_sample_uv_nearest_mirror(old_u, old_v, uv_sx, uv_sy, w, h, &adv_u, &adv_v);

            src_us = (float) U[center_pos] - 128.0f;
            src_vs = (float) V[center_pos] - 128.0f;
            eh_palette_source(src_us, src_vs, e->time + ff_m * EH_TWO_PI, color_t, tile_charge, &pal_u, &pal_v);

            {
                float hist_u = (float) adv_u - 128.0f;
                float hist_v = (float) adv_v - 128.0f;
                float src_chroma = 0.016f + source_t * 0.045f + (1.0f - trail_t) * 0.012f;
                float neutral = soft_t * 0.018f + (1.0f - color_t) * 0.010f;
                float hist_keep = 1.0f - src_chroma;
                float neutral_keep = 1.0f - neutral;
                float out_u = hist_u * hist_keep + src_us * src_chroma + pal_u;
                float out_v = hist_v * hist_keep + src_vs * src_chroma + pal_v;
                out_u *= neutral_keep;
                out_v *= neutral_keep;
                chroma_limit = 28.0f + color_t * 42.0f;
                eh_limit_chroma(&out_u, &out_v, chroma_limit);
                adv_u = eh_u8f(128.0f + out_u);
                adv_v = eh_u8f(128.0f + out_v);
            }

            ff_m_boost = ff_m * 24.0f;
            glow_y_scale = (0.20f + glow_t * 0.42f) * (0.65f + ff_m * 0.70f);
            soft_tile_base = soft_t * (0.07f + mirror_depth * 0.08f);

            for (y0 = 0; y0 < tylim; y0++) {
                int y = yy + y0;
                int row = y * w;
                int has_y_neighbors = (y > 0 && y + 1 < h);
                for (x0 = 0; x0 < txlim; x0++) {
                    int x = xx + x0;
                    int pos = row + x;
                    int src_l = src_y[pos];
                    int motion = eh_absi((int) src_l - (int) prev_y[pos]);
                    int edge = 0;
                    int event;
                    int old_c;
                    int ch;
                    float px = (float) x + 0.5f;
                    float py = (float) y + 0.5f;
                    float sx;
                    float sy;
                    int adv_y;
                    int src_tone;
                    int glowv;
                    float ch_t;
                    float local_src;
                    float out_yf;
                    float display_yf;

                    if (x > 0 && x + 1 < w)
                        edge += eh_absi((int) src_y[pos + 1] - (int) src_y[pos - 1]);
                    if (has_y_neighbors)
                        edge += eh_absi((int) src_y[pos + w] - (int) src_y[pos - w]);
                    if (edge > 255) edge = 255;
                    event = edge + motion;
                    if (event > 255) event = 255;
                    old_c = charge[pos];
                    ch = decay_lut[old_c] + charge_lut[event] / 3 + (int) ff_m_boost;
                    if (ch > 255) ch = 255;
                    next_charge[pos] = (uint8_t) ch;
                    prev_y[pos] = (uint8_t) src_l;

                    eh_cathedral_sample(px, py, vx, vy, ff_m, cell_px, mirror_depth, phase_s, phase_c, &sx, &sy);
                    adv_y = eh_sample_y_bilinear_mirror(old_y, sx, sy, w, h);
                    src_tone = tone_lut[src_l];
                    ch_t = (float) ch * (1.0f / 255.0f);
                    local_src = source_base + ((float) source_lut[ch] * (1.0f / 255.0f)) * source_t * 0.18f;
                    local_src *= 1.0f - mirror_depth * ch_t * 0.28f;
                    local_src = eh_clampf(local_src, 0.002f, 0.135f);

                    out_yf = (float) adv_y * (1.0f - local_src) + (float) src_tone * local_src;
                    out_yf += ((float) src_tone - (float) adv_y) * source_delta_base * (1.0f - ch_t * soft_t * 0.55f);

                    if (do_soft && ch > 24 && x > 0 && x + 1 < w && has_y_neighbors) {
                        int nsum = (int) old_y[pos - 1] + (int) old_y[pos + 1] + (int) old_y[pos - w] + (int) old_y[pos + w];
                        float smooth = ch_t * soft_tile_base;
                        if (smooth > 0.18f) smooth = 0.18f;
                        out_yf = out_yf * (1.0f - smooth) + (float) (nsum >> 2) * smooth;
                    }

                    glowv = glow_lut[ch];
                    out_yf += (float) glowv * glow_y_scale;
                    if (out_yf > 238.0f)
                        out_yf = 238.0f + (out_yf - 238.0f) * 0.25f;

                    display_yf = out_yf + (float) glowv * glow_display_scale;
                    if (display_yf > 255.0f) display_yf = 255.0f;

                    next_y[pos] = eh_u8f(out_yf);
                    next_u[pos] = (uint8_t) adv_u;
                    next_v[pos] = (uint8_t) adv_v;
                    Y[pos] = eh_u8f(display_yf);
                    U[pos] = (uint8_t) adv_u;
                    V[pos] = (uint8_t) adv_v;
                }
            }
        }
    }

    {
        uint8_t *tmp;
        tmp = e->paint_y; e->paint_y = e->next_y; e->next_y = tmp;
        tmp = e->paint_u; e->paint_u = e->next_u; e->next_u = tmp;
        tmp = e->paint_v; e->paint_v = e->next_v; e->next_v = tmp;
        tmp = e->charge; e->charge = e->next_charge; e->next_charge = tmp;
    }

    e->frame++;
}
