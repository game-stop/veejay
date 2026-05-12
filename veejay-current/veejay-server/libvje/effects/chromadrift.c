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
#include <config.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "chromadrift.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define EH_PARAMS 10

#define P_SOURCE  0
#define P_DRIFT   1
#define P_CONTOUR 2
#define P_GLOW    3
#define P_PULL    4
#define P_CURL    5
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
    uint8_t decay_lut[256];
    uint8_t charge_lut[256];
    uint8_t source_lut[256];

    float sin_lut[EH_TRIG_SIZE];
    float cos_lut[EH_TRIG_SIZE];

    int luts_valid;
    int tone_valid;
    int last_glow;
    int last_decay;
    int last_density;
    int last_color;
    int last_tone_mix;

    float maturity;
    float time;
    float phase;
} chromadrift_t;

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

static inline float eh_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float eh_smooth01(float t)
{
    t = eh_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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

static inline float eh_lut_sin_fast(const chromadrift_t *e, float phase)
{
    int idx = (int) (phase * ((float) EH_TRIG_SIZE * EH_INV_TWO_PI));
    return e->sin_lut[idx & EH_TRIG_MASK];
}

static inline void eh_lut_sincos(const chromadrift_t *e, float phase, float *s, float *c)
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

static inline int eh_sample_y_nearest(const uint8_t *restrict y, float fx, float fy, int w, int h)
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
    return y[iy * w + ix];
}

static inline int eh_sample_y_bilinear(const uint8_t *restrict y, float fx, float fy, int w, int h)
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

    a = (int) y[p00] * (256 - wx) + (int) y[p10] * wx;
    b = (int) y[p01] * (256 - wx) + (int) y[p11] * wx;
    return ((a * (256 - wy) + b * wy) + 32768) >> 16;
}

static inline void eh_sample_uv_nearest(const uint8_t *restrict u,
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
    *ou = u[ix];
    *ov = v[ix];
}

static void eh_build_luts(chromadrift_t *e, int glow, int decay, int density, int color, int tone_mix)
{
    int i;
    int decay_power;
    int event_gain;
    int src_floor;

    if (e->luts_valid && e->tone_valid &&
        e->last_glow == glow &&
        e->last_decay == decay &&
        e->last_density == density &&
        e->last_color == color &&
        e->last_tone_mix == tone_mix)
        return;

    decay_power = 146 + (decay * 104 + 50) / 100;
    if (decay_power > 252)
        decay_power = 252;
    event_gain = 52 + glow + (density >> 1);
    src_floor = 1 + (color >> 4);

    for (i = 0; i < 256; i++) {
        int g = e->gamma_lut[i];
        int ev = i - 12;
        int ch;
        int src;
        if (ev < 0)
            ev = 0;
        ev = (ev * event_gain + 127) / 255;
        if (ev > 255)
            ev = 255;
        ch = (i * decay_power + 127) / 255;
        src = src_floor + ((255 - i) >> 7);
        if (src > 18)
            src = 18;
        e->tone_lut[i] = eh_u8i(i + (((g - i) * tone_mix + 127) / 255));
        e->charge_lut[i] = (uint8_t) ev;
        e->decay_lut[i] = (uint8_t) ch;
        e->source_lut[i] = (uint8_t) src;
    }

    e->last_glow = glow;
    e->last_decay = decay;
    e->last_density = density;
    e->last_color = color;
    e->last_tone_mix = tone_mix;
    e->luts_valid = 1;
    e->tone_valid = 1;
}

static void eh_build_contour_force(chromadrift_t *e,
                                   const uint8_t *restrict src_y,
                                   const uint8_t *restrict prev_y,
                                   int drift_i,
                                   int contour_i,
                                   int pull_i,
                                   int curl_i,
                                   int trail_i,
                                   int speed_i)
{
    int w = e->w;
    int h = e->h;
    int cell = e->grid_cell;
    int gw = (w + cell - 1) / cell + 2;
    int gh = (h + cell - 1) / cell + 2;
    int gx;
    int gy;
    float drift_t = (float) drift_i * 0.01f;
    float contour_t = (float) contour_i * 0.01f;
    float pull_t = (float) pull_i * 0.01f;
    float curl_t = (float) curl_i * 0.01f;
    float trail_t = (float) trail_i * 0.01f;
    float speed_t = eh_absf((float) speed_i) * 0.01f;
    float persist = 0.70f + trail_t * 0.22f - drift_t * 0.08f;
    float one_minus;
    uint32_t seed = (uint32_t) (e->frame >> 3);

    if (gw * gh > e->grid_capacity)
        return;

    e->grid_w = gw;
    e->grid_h = gh;
    persist = eh_clampf(persist, 0.58f, 0.94f);
    one_minus = 1.0f - persist;

    for (gy = 0; gy < gh; gy++) {
        int py = gy * cell;
        if (py >= h)
            py = h - 1;
        for (gx = 0; gx < gw; gx++) {
            int px = gx * cell;
            int r = cell;
            int gi = gy * gw + gx;
            int xl;
            int xr;
            int yu;
            int yd;
            int cpos;
            int lpos;
            int rpos;
            int upos;
            int dpos;
            int lum;
            int grad_x;
            int grad_y;
            int motion;
            int edge;
            float eg;
            float inv;
            float nx;
            float ny;
            float tx;
            float ty;
            float motion_push;
            float noise_x;
            float noise_y;
            float target_x;
            float target_y;
            float target_m;
            float curl_sign;

            if (px >= w)
                px = w - 1;

            xl = px - r;
            xr = px + r;
            yu = py - r;
            yd = py + r;
            cpos = eh_index_clamped(w, h, px, py);
            lpos = eh_index_clamped(w, h, xl, py);
            rpos = eh_index_clamped(w, h, xr, py);
            upos = eh_index_clamped(w, h, px, yu);
            dpos = eh_index_clamped(w, h, px, yd);

            lum = src_y[cpos];
            grad_x = (int) src_y[rpos] - (int) src_y[lpos];
            grad_y = (int) src_y[dpos] - (int) src_y[upos];
            motion = (int) src_y[cpos] - (int) prev_y[cpos];
            edge = eh_absi(grad_x) + eh_absi(grad_y);

            eg = (float) edge * (1.0f / 510.0f);
            inv = eh_fast_rsqrt((float) (grad_x * grad_x + grad_y * grad_y) + 8.0f);
            nx = (float) grad_x * inv;
            ny = (float) grad_y * inv;
            tx = -ny;
            ty = nx;

            curl_sign = ((lum > 127) ? 1.0f : -1.0f) * (speed_i < 0 ? -1.0f : 1.0f);
            motion_push = (float) motion * (1.0f / 255.0f);

            noise_x = eh_hash_signed(((uint32_t) gx * 73856093U) ^ ((uint32_t) gy * 19349663U) ^ (seed * 83492791U));
            noise_y = eh_hash_signed(((uint32_t) gx * 19349663U) ^ ((uint32_t) gy * 73856093U) ^ (seed * 2654435761U));

            target_x = tx * contour_t * (0.45f + eg * 1.55f) * curl_sign;
            target_y = ty * contour_t * (0.45f + eg * 1.55f) * curl_sign;

            target_x += nx * pull_t * (0.18f + eg * 0.95f) * (0.35f + (float) lum * (1.0f / 255.0f));
            target_y += ny * pull_t * (0.18f + eg * 0.95f) * (0.35f + (float) lum * (1.0f / 255.0f));

            target_x += -nx * motion_push * (0.30f + speed_t * 1.10f);
            target_y += -ny * motion_push * (0.30f + speed_t * 1.10f);

            target_x += noise_x * drift_t * (0.08f + (1.0f - eg) * 0.20f);
            target_y += noise_y * drift_t * (0.08f + (1.0f - eg) * 0.20f);

            target_x += tx * curl_t * eg * 0.55f * (speed_i < 0 ? -1.0f : 1.0f);
            target_y += ty * curl_t * eg * 0.55f * (speed_i < 0 ? -1.0f : 1.0f);

            target_m = eg + eh_absf(motion_push) * 0.38f + drift_t * 0.06f;
            if (target_m > 1.0f)
                target_m = 1.0f;

            e->force_x[gi] = e->force_x[gi] * persist + target_x * one_minus;
            e->force_y[gi] = e->force_y[gi] * persist + target_y * one_minus;
            e->force_m[gi] = e->force_m[gi] * persist + target_m * one_minus;
        }
    }
}

vj_effect *chromadrift_init(int w, int h)
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

    ve->limits[0][P_SOURCE] = 0; ve->limits[1][P_SOURCE] = 100; ve->defaults[P_SOURCE] = 42;
    ve->limits[0][P_DRIFT] = 0; ve->limits[1][P_DRIFT] = 100; ve->defaults[P_DRIFT] = 22;
    ve->limits[0][P_CONTOUR] = 0; ve->limits[1][P_CONTOUR] = 100; ve->defaults[P_CONTOUR] = 58;
    ve->limits[0][P_GLOW] = 0; ve->limits[1][P_GLOW] = 100; ve->defaults[P_GLOW] = 30;
    ve->limits[0][P_PULL] = 0; ve->limits[1][P_PULL] = 100; ve->defaults[P_PULL] = 36;
    ve->limits[0][P_CURL] = -100; ve->limits[1][P_CURL] = 100; ve->defaults[P_CURL] = 46;
    ve->limits[0][P_TRAIL] = 0; ve->limits[1][P_TRAIL] = 100; ve->defaults[P_TRAIL] = 64;
    ve->limits[0][P_COLOR] = 0; ve->limits[1][P_COLOR] = 100; ve->defaults[P_COLOR] = 54;
    ve->limits[0][P_SPEED] = -100; ve->limits[1][P_SPEED] = 100; ve->defaults[P_SPEED] = 30;
    ve->limits[0][P_SOFT] = 0; ve->limits[1][P_SOFT] = 100; ve->defaults[P_SOFT] = 46;

    ve->description = "Chroma Drift";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Source Feed",
        "Ink Drift",
        "Contour Current",
        "Aurora Glow",
        "Luma Pull",
        "Curl Direction",
        "Trail Memory",
        "Pastel Palette",
        "Motion Speed",
        "Surface Softness"
    );

    (void) w;
    (void) h;
    return ve;
}

void *chromadrift_malloc(int w, int h)
{
    chromadrift_t *e;
    unsigned char *base;
    size_t len;
    size_t gridcap;
    size_t total;
    size_t off;
    int i;

    if (w <= 0 || h <= 0)
        return NULL;
    len = (size_t) w * (size_t) h;
    if (len == 0)
        return NULL;

    e = (chromadrift_t *) vj_calloc(sizeof(chromadrift_t));
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
    e->force_x = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_y = (float *) (base + off); off += sizeof(float) * gridcap;
    e->force_m = (float *) (base + off); off += sizeof(float) * gridcap;

    e->grid_capacity = (int) gridcap;
    e->grid_w = 0;
    e->grid_h = 0;
    e->grid_cell = 12;

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
        int g = (int) (powf(v, 0.92f) * 255.0f + 0.5f);
        e->gamma_lut[i] = eh_u8i(g);
        e->tone_lut[i] = e->gamma_lut[i];
        e->decay_lut[i] = (uint8_t) i;
        e->charge_lut[i] = 0;
        e->source_lut[i] = 1;
    }

    for (i = 0; i < EH_TRIG_SIZE; i++) {
        float a = EH_TWO_PI * ((float) i / (float) EH_TRIG_SIZE);
        e->sin_lut[i] = sinf(a);
        e->cos_lut[i] = cosf(a);
    }

    e->luts_valid = 0;
    e->tone_valid = 0;
    e->maturity = 0.0f;
    e->time = 0.0f;
    e->phase = 0.0f;

    return (void *) e;
}

void chromadrift_free(void *ptr)
{
    chromadrift_t *e = (chromadrift_t *) ptr;
    if (!e)
        return;
    if (e->region)
        free(e->region);
    free(e);
}

static void eh_seed(chromadrift_t *e, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int len = e->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (i = 0; i < len; i++) {
        uint8_t y = Y[i];
        e->src_y[i] = y;
        e->paint_y[i] = y;
        e->paint_u[i] = U[i];
        e->paint_v[i] = V[i];
        e->next_y[i] = y;
        e->next_u[i] = U[i];
        e->next_v[i] = V[i];
        e->prev_y[i] = y;
        e->charge[i] = 0;
        e->next_charge[i] = 0;
    }

    e->seeded = 1;
    e->maturity = 0.0f;
}

static inline void eh_palette_offsets(float t, float color_t, float *ou, float *ov)
{
    static const float pu[5] = { -10.0f,  15.0f,  5.0f, -14.0f, -10.0f };
    static const float pv[5] = {  18.0f, -12.0f, 19.0f,  -7.0f,  18.0f };
    int k;
    float f;
    float u;
    float v;

    k = (int) t;
    t -= (float) k;
    if (t < 0.0f)
        t += 1.0f;

    t *= 4.0f;
    k = (int) t;
    if (k < 0)
        k = 0;
    else if (k > 3)
        k = 3;

    f = t - (float) k;
    f = f * f * (3.0f - 2.0f * f);

    u = pu[k] + (pu[k + 1] - pu[k]) * f;
    v = pv[k] + (pv[k + 1] - pv[k]) * f;

    *ou = u * color_t;
    *ov = v * color_t;
}

static inline void eh_render_fast_pixel(chromadrift_t *e,
                                        uint8_t *restrict Y,
                                        uint8_t *restrict U,
                                        uint8_t *restrict V,
                                        const uint8_t *restrict old_y,
                                        uint8_t *restrict next_y,
                                        uint8_t *restrict next_u,
                                        uint8_t *restrict next_v,
                                        uint8_t *restrict next_charge,
                                        int pos,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        float vel_x,
                                        float vel_y,
                                        float tile_force_m,
                                        float src_mix,
                                        float chroma_src_mix,
                                        float glow_base,
                                        float density_t,
                                        float trail_t,
                                        float pal_u,
                                        float pal_v,
                                        float band,
                                        int adv_u,
                                        int adv_v,
                                        int tile_edge_event,
                                        int force_charge,
                                        int motion_gain,
                                        int use_bilinear)
{
    float advx = (float) x - vel_x;
    float advy = (float) y - vel_y;
    int adv_y;
    int src_raw = e->src_y[pos];
    int src_yy = e->tone_lut[src_raw];
    int motion = eh_absi(src_raw - (int) e->prev_y[pos]);
    int ch = e->decay_lut[e->charge[pos]] + tile_edge_event + ((motion * motion_gain) >> 8) + force_charge;
    float charge_f;
    float wet;
    float detail;
    float body_y;
    float glow;
    float out_y;
    float out_us;
    float out_vs;
    float src_us;
    float src_vs;
    float limit;

    if (ch > 255)
        ch = 255;

    if (use_bilinear)
        adv_y = eh_sample_y_bilinear(old_y, advx, advy, w, h);
    else
        adv_y = eh_sample_y_nearest(old_y, advx, advy, w, h);

    charge_f = (float) ch * (1.0f / 255.0f);
    wet = charge_f * (0.08f + trail_t * 0.12f);
    if (wet > 0.16f)
        wet = 0.16f;

    if (wet > 0.012f && x > 0 && x + 1 < w && y > 0 && y + 1 < h) {
        int avg = ((int) old_y[pos - 1] + (int) old_y[pos + 1] + (int) old_y[pos - w] + (int) old_y[pos + w] + 2) >> 2;
        adv_y = (int) ((float) adv_y * (1.0f - wet) + (float) avg * wet + 0.5f);
    }

    detail = (0.020f + density_t * 0.070f) * (1.0f - trail_t * 0.58f) * (1.0f - charge_f * 0.46f);
    body_y = (float) adv_y * (1.0f - src_mix) + (float) src_yy * src_mix;
    body_y += ((float) src_yy - (float) adv_y) * detail;

    glow = charge_f * glow_base;
    glow += tile_force_m * band * (1.4f + glow_base * 0.13f);
    out_y = body_y + glow;
    if (out_y > 238.0f)
        out_y = 238.0f + (out_y - 238.0f) * 0.22f;
    if (out_y > 255.0f)
        out_y = 255.0f;

    src_us = (float) U[pos] - 128.0f;
    src_vs = (float) V[pos] - 128.0f;
    out_us = ((float) adv_u - 128.0f) * (1.0f - chroma_src_mix) + src_us * chroma_src_mix;
    out_vs = ((float) adv_v - 128.0f) * (1.0f - chroma_src_mix) + src_vs * chroma_src_mix;
    out_us += pal_u * charge_f;
    out_vs += pal_v * charge_f;
    out_us *= 1.0f - charge_f * trail_t * 0.012f;
    out_vs *= 1.0f - charge_f * trail_t * 0.012f;

    limit = 34.0f + (eh_absf(pal_u) + eh_absf(pal_v)) * 0.26f;
    eh_limit_chroma(&out_us, &out_vs, limit);

    next_charge[pos] = (uint8_t) ch;
    next_y[pos] = eh_u8f(body_y + glow * 0.18f);
    next_u[pos] = eh_u8f(128.0f + out_us);
    next_v[pos] = eh_u8f(128.0f + out_vs);
    Y[pos] = eh_u8f(out_y);
    U[pos] = next_u[pos];
    V[pos] = next_v[pos];
    e->prev_y[pos] = (uint8_t) src_raw;
}

static inline void eh_render_fast_pixel_sampled(chromadrift_t *e,
                                                uint8_t *restrict Y,
                                                uint8_t *restrict U,
                                                uint8_t *restrict V,
                                                const uint8_t *restrict old_y,
                                                uint8_t *restrict next_y,
                                                uint8_t *restrict next_u,
                                                uint8_t *restrict next_v,
                                                uint8_t *restrict next_charge,
                                                int pos,
                                                int adv_y,
                                                float src_mix,
                                                float chroma_src_mix,
                                                float glow_add,
                                                float chroma_limit,
                                                float pal_u,
                                                float pal_v,
                                                const float *restrict charge_f_lut,
                                                const uint8_t *restrict wet_lut,
                                                const float *restrict detail_lut,
                                                const float *restrict glow_lut,
                                                const float *restrict fade_lut,
                                                int adv_u,
                                                int adv_v,
                                                int tile_edge_event,
                                                int force_charge,
                                                int motion_gain,
                                                int wet_ok)
{
    int src_raw = e->src_y[pos];
    int src_yy = e->tone_lut[src_raw];
    int motion = eh_absi(src_raw - (int) e->prev_y[pos]);
    int ch = e->decay_lut[e->charge[pos]] + tile_edge_event + ((motion * motion_gain) >> 8) + force_charge;
    float charge_f;
    int wet_amt;
    float detail;
    float body_y;
    float glow;
    float out_y;
    float out_us;
    float out_vs;
    float src_us;
    float src_vs;

    if (ch > 255)
        ch = 255;

    charge_f = charge_f_lut[ch];
    wet_amt = wet_lut[ch];

    if (wet_amt && wet_ok) {
        int ww = e->w;
        int avg = ((int) old_y[pos - 1] + (int) old_y[pos + 1] + (int) old_y[pos - ww] + (int) old_y[pos + ww] + 2) >> 2;
        int d = avg - adv_y;
        adv_y += (d * wet_amt + (d >= 0 ? 128 : -128)) >> 8;
    }

    detail = detail_lut[ch];
    body_y = (float) adv_y * (1.0f - src_mix) + (float) src_yy * src_mix;
    body_y += ((float) src_yy - (float) adv_y) * detail;

    glow = glow_lut[ch] + glow_add;
    out_y = body_y + glow;
    if (out_y > 238.0f)
        out_y = 238.0f + (out_y - 238.0f) * 0.22f;
    if (out_y > 255.0f)
        out_y = 255.0f;

    src_us = (float) U[pos] - 128.0f;
    src_vs = (float) V[pos] - 128.0f;
    out_us = ((float) adv_u - 128.0f) * (1.0f - chroma_src_mix) + src_us * chroma_src_mix;
    out_vs = ((float) adv_v - 128.0f) * (1.0f - chroma_src_mix) + src_vs * chroma_src_mix;
    out_us += pal_u * charge_f;
    out_vs += pal_v * charge_f;
    {
        float fade = fade_lut[ch];
        out_us *= fade;
        out_vs *= fade;
    }

    if (ch > 18)
        eh_limit_chroma(&out_us, &out_vs, chroma_limit);

    next_charge[pos] = (uint8_t) ch;
    next_y[pos] = eh_u8f(body_y + glow * 0.18f);
    next_u[pos] = eh_u8f(128.0f + out_us);
    next_v[pos] = eh_u8f(128.0f + out_vs);
    Y[pos] = eh_u8f(out_y);
    U[pos] = next_u[pos];
    V[pos] = next_v[pos];
    e->prev_y[pos] = (uint8_t) src_raw;
}

static inline void eh_render_tile_sampled(chromadrift_t *e,
                                          uint8_t *restrict Y,
                                          uint8_t *restrict U,
                                          uint8_t *restrict V,
                                          const uint8_t *restrict old_y,
                                          uint8_t *restrict next_y,
                                          uint8_t *restrict next_u,
                                          uint8_t *restrict next_v,
                                          uint8_t *restrict next_charge,
                                          int xx,
                                          int yy,
                                          int tw,
                                          int th,
                                          int w,
                                          int h,
                                          float vel_x,
                                          float vel_y,
                                          float tile_force_m,
                                          float src_mix,
                                          float chroma_src_mix,
                                          float glow_base,
                                          float glow_add,
                                          float chroma_limit,
                                          float density_t,
                                          float trail_t,
                                          float pal_u,
                                          float pal_v,
                                          const float *restrict charge_f_lut,
                                          const uint8_t *restrict wet_lut,
                                          const float *restrict detail_lut,
                                          const float *restrict glow_lut,
                                          const float *restrict fade_lut,
                                          float band,
                                          int adv_u,
                                          int adv_v,
                                          int tile_edge_event,
                                          int force_charge,
                                          int motion_gain,
                                          int use_bilinear)
{
    float base_x = (float) xx - vel_x;
    float base_y = (float) yy - vel_y;
    int wet_ok = (xx > 0 && yy > 0 && xx + tw < w && yy + th < h);
    int dy;

    if (use_bilinear) {
        int ix0 = eh_floor_to_int(base_x);
        int iy0 = eh_floor_to_int(base_y);
        int wx = (int) ((base_x - (float) ix0) * 256.0f);
        int wy = (int) ((base_y - (float) iy0) * 256.0f);

        if (ix0 >= 0 && iy0 >= 0 && ix0 + tw < w && iy0 + th < h) {
            int inv_wx = 256 - wx;
            int inv_wy = 256 - wy;
            for (dy = 0; dy < th; dy++) {
                int pos = (yy + dy) * w + xx;
                int sp = (iy0 + dy) * w + ix0;
                int dx;
                for (dx = 0; dx < tw; dx++, pos++, sp++) {
                    int a = (int) old_y[sp] * inv_wx + (int) old_y[sp + 1] * wx;
                    int b = (int) old_y[sp + w] * inv_wx + (int) old_y[sp + w + 1] * wx;
                    int adv_y = ((a * inv_wy + b * wy) + 32768) >> 16;
                    eh_render_fast_pixel_sampled(e, Y, U, V, old_y, next_y, next_u, next_v, next_charge,
                                                  pos, adv_y,
                                                  src_mix, chroma_src_mix, glow_add, chroma_limit,
                                                  pal_u, pal_v,
                                                  charge_f_lut, wet_lut, detail_lut, glow_lut, fade_lut,
                                                  adv_u, adv_v,
                                                  tile_edge_event, force_charge, motion_gain, wet_ok);
                }
            }
            return;
        }
    }
    else {
        int sx0 = (int) (base_x + 0.5f);
        int sy0 = (int) (base_y + 0.5f);

        if (sx0 >= 0 && sy0 >= 0 && sx0 + tw <= w && sy0 + th <= h) {
            for (dy = 0; dy < th; dy++) {
                int pos = (yy + dy) * w + xx;
                int sp = (sy0 + dy) * w + sx0;
                int dx;
                for (dx = 0; dx < tw; dx++, pos++, sp++) {
                    eh_render_fast_pixel_sampled(e, Y, U, V, old_y, next_y, next_u, next_v, next_charge,
                                                  pos, old_y[sp],
                                                  src_mix, chroma_src_mix, glow_add, chroma_limit,
                                                  pal_u, pal_v,
                                                  charge_f_lut, wet_lut, detail_lut, glow_lut, fade_lut,
                                                  adv_u, adv_v,
                                                  tile_edge_event, force_charge, motion_gain, wet_ok);
                }
            }
            return;
        }
    }

    for (dy = 0; dy < th; dy++) {
        int y0 = yy + dy;
        int row = y0 * w + xx;
        int dx;
        for (dx = 0; dx < tw; dx++) {
            int pos = row + dx;
            eh_render_fast_pixel(e, Y, U, V, old_y, next_y, next_u, next_v, next_charge,
                                 pos, xx + dx, y0, w, h, vel_x, vel_y, tile_force_m,
                                 src_mix, chroma_src_mix, glow_base, density_t, trail_t,
                                 pal_u, pal_v, band, adv_u, adv_v, tile_edge_event,
                                 force_charge, motion_gain, use_bilinear);
        }
    }
}

void chromadrift_apply(void *ptr, VJFrame *frame, int *args)
{
    chromadrift_t *e = (chromadrift_t *) ptr;
    uint8_t *restrict Y;
    uint8_t *restrict U;
    uint8_t *restrict V;
    uint8_t *restrict old_y;
    uint8_t *restrict old_u;
    uint8_t *restrict old_v;
    uint8_t *restrict next_y;
    uint8_t *restrict next_u;
    uint8_t *restrict next_v;
    uint8_t *restrict next_charge;
    int w;
    int h;
    int len;
    int build_i;
    int source_i;
    int drift_i;
    int contour_i;
    int glow_i;
    int decay_i;
    int density_i;
    int pull_i;
    int bands_i;
    int curl_i;
    int trail_i;
    int color_i;
    int speed_i;
    int soft_i;
    float build_t;
    float source_t;
    float drift_t;
    float contour_t;
    float glow_t;
    float density_t;
    float pull_t;
    float bands_t;
    float trail_t;
    float color_t;
    float soft_t;
    float speed_signed;
    float speed_mag;
    float speed_curve;
    float mature_rate;
    float src_mix;
    float chroma_src_mix;
    float flow_pixels;
    float curl_pixels;
    float phase_step;
    int tone_mix;
    int flow_cell;
    int qrows;
    int qcols;
    int qy;
    float global_glow_base;
    float global_detail_base;
    float global_wet_base;
    float global_chroma_fade;
    float charge_f_lut[256];
    uint8_t wet_lut[256];
    float detail_lut[256];
    float glow_lut[256];
    float fade_lut[256];
    float pal_u_lut[256];
    float pal_v_lut[256];
    float chroma_limit_lut[256];
    int motion_gain_global;
    float force_charge_scale;
    float max_v_global;
    float max_v2_global;
    float curl_dir;
    float band_scale;
    int force_update;
    int cell_changed;

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

    source_i = eh_clampi(args[P_SOURCE], 0, 100);
    drift_i = eh_clampi(args[P_DRIFT], 0, 100);
    contour_i = eh_clampi(args[P_CONTOUR], 0, 100);
    glow_i = eh_clampi(args[P_GLOW], 0, 100);
    pull_i = eh_clampi(args[P_PULL], 0, 100);
    curl_i = eh_clampi(args[P_CURL], -100, 100);
    trail_i = eh_clampi(args[P_TRAIL], 0, 100);
    color_i = eh_clampi(args[P_COLOR], 0, 100);
    speed_i = eh_clampi(args[P_SPEED], -100, 100);
    soft_i = eh_clampi(args[P_SOFT], 0, 100);

    build_i = 66;
    decay_i = eh_clampi(44 + (trail_i * 42 + 50) / 100 + (soft_i * 20 + 50) / 100, 0, 100);
    density_i = eh_clampi(30 + (glow_i * 22 + 50) / 100 + (contour_i * 18 + 50) / 100 - (soft_i * 12 + 50) / 100, 0, 100);
    bands_i = eh_clampi(18 + (color_i * 58 + 50) / 100 + (glow_i * 10 + 50) / 100, 0, 100);

    build_t = (float) build_i * 0.01f;
    source_t = (float) source_i * 0.01f;
    drift_t = (float) drift_i * 0.01f;
    contour_t = (float) contour_i * 0.01f;
    glow_t = (float) glow_i * 0.01f;
    density_t = (float) density_i * 0.01f;
    pull_t = (float) pull_i * 0.01f;
    bands_t = (float) bands_i * 0.01f;
    trail_t = (float) trail_i * 0.01f;
    color_t = (float) color_i * 0.01f;
    soft_t = (float) soft_i * 0.01f;
    speed_signed = (float) speed_i * 0.01f;
    speed_mag = eh_absf(speed_signed);
    speed_curve = speed_mag * speed_mag * (0.42f + speed_mag * 0.58f);

    mature_rate = 0.0008f + build_t * build_t * 0.035f;
    e->maturity += (1.0f - e->maturity) * mature_rate;
    if (e->maturity > 1.0f)
        e->maturity = 1.0f;

    tone_mix = (int) ((0.055f + glow_t * 0.22f + color_t * 0.055f) * 255.0f + 0.5f);
    tone_mix = eh_clampi(tone_mix, 0, 255);
    eh_build_luts(e, glow_i, decay_i, density_i, color_i, tone_mix);

    src_mix = (0.010f + source_t * source_t * 0.135f + source_t * 0.038f) * (1.0f - trail_t * 0.48f);
    src_mix = eh_clampf(src_mix, 0.0030f, 0.22f);
    chroma_src_mix = src_mix + 0.014f + source_t * 0.025f + (1.0f - color_t) * 0.012f;
    chroma_src_mix = eh_clampf(chroma_src_mix, 0.008f, 0.24f);

    flow_pixels = (0.55f + e->maturity * 1.85f) * (0.50f + contour_t * 7.0f + drift_t * 3.0f + pull_t * 3.2f) * (0.34f + speed_curve * 1.10f);
    curl_pixels = (0.40f + contour_t * 2.8f + density_t * 1.6f) * (0.55f + speed_curve * 1.20f);
    if (speed_i == 0) {
        flow_pixels *= 0.18f;
        curl_pixels *= 0.12f;
    }

    global_glow_base = glow_t * (7.0f + density_t * 15.0f) * (1.0f - soft_t * 0.10f);
    global_detail_base = (0.018f + density_t * 0.050f) * (1.0f - trail_t * 0.52f) * (1.0f - soft_t * 0.36f);
    global_wet_base = 0.065f + trail_t * 0.105f + soft_t * 0.075f;
    global_chroma_fade = trail_t * (0.004f + soft_t * 0.003f);
    {
        int li;
        float pal_scale = 0.10f + glow_t * 0.085f + soft_t * 0.025f;
        float pal_color = color_t * (0.55f + source_t * 0.18f);
        for (li = 0; li < 256; li++) {
            float cf = (float) li * (1.0f / 255.0f);
            float wet = cf * global_wet_base;
            float pu;
            float pv;
            if (wet > 0.16f)
                wet = 0.16f;
            charge_f_lut[li] = cf;
            {
                int wet_amt = (int) (wet * 256.0f + 0.5f);
                if (wet_amt <= 4)
                    wet_amt = 0;
                else if (wet_amt > 48)
                    wet_amt = 48;
                wet_lut[li] = (uint8_t) wet_amt;
            }
            detail_lut[li] = global_detail_base * (1.0f - cf * 0.46f);
            glow_lut[li] = cf * global_glow_base;
            fade_lut[li] = 1.0f - cf * global_chroma_fade;
            eh_palette_offsets((float) li * (1.0f / 256.0f), pal_color, &pu, &pv);
            pu *= pal_scale;
            pv *= pal_scale;
            pal_u_lut[li] = pu;
            pal_v_lut[li] = pv;
            chroma_limit_lut[li] = 28.0f + color_t * 16.0f + (eh_absf(pu) + eh_absf(pv)) * 0.18f;
        }
    }

    motion_gain_global = 14 + (int) (glow_t * 36.0f);
    force_charge_scale = 12.0f + density_t * 28.0f;
    max_v_global = 2.0f + contour_t * 7.5f + drift_t * 3.4f + pull_t * 5.5f;
    max_v2_global = max_v_global * max_v_global;
    curl_dir = (curl_i < 0 ? -1.0f : 1.0f);
    band_scale = 0.30f + bands_t * 1.00f;
    force_update = !((e->frame & 1) && speed_mag < 0.62f && drift_i < 70 && contour_i < 84 && pull_i < 84);

    phase_step = (0.0012f + speed_curve * 0.025f + contour_t * 0.004f) * (speed_i < 0 ? -1.0f : 1.0f);
    e->time = eh_wrap_2pi(e->time + phase_step);
    e->phase = eh_wrap_2pi(e->phase + phase_step * (0.43f + bands_t));

    flow_cell = 28 - (contour_i * 8 + 50) / 100 - (drift_i * 4 + 50) / 100;
    if (flow_cell < 12)
        flow_cell = 12;
    else if (flow_cell > 30)
        flow_cell = 30;
    cell_changed = (e->grid_cell != flow_cell);
    e->grid_cell = flow_cell;

    old_y = e->paint_y;
    old_u = e->paint_u;
    old_v = e->paint_v;
    next_y = e->next_y;
    next_u = e->next_u;
    next_v = e->next_v;
    next_charge = e->next_charge;

    veejay_memcpy(e->src_y, Y, len);

    if (cell_changed || e->grid_w <= 0 || e->grid_h <= 0 || force_update)
        eh_build_contour_force(e, e->src_y, e->prev_y, drift_i, contour_i, pull_i, curl_i, trail_i, speed_i);

    qcols = (w + 3) >> 2;
    qrows = (h + 3) >> 2;

#pragma omp parallel for schedule(static) num_threads(e->n_threads)
    for (qy = 0; qy < qrows; qy++) {
        int yy = qy << 2;
        int th_row = (yy + 4 <= h) ? 4 : (h - yy);
        int tyc_row = yy + (th_row >> 1);
        int fcell = e->grid_cell;
        int fgw = e->grid_w;
        int fgh = e->grid_h;
        int fgy = tyc_row / fcell;
        int fgx_run = 0;
        int qx;
        float fty;
        float fwy0;
        int fgybase;
        if (fgy < 0)
            fgy = 0;
        else if (fgy + 1 >= fgh)
            fgy = fgh - 2;
        fty = (float) (tyc_row - fgy * fcell) / (float) fcell;
        fty = fty * fty * (3.0f - 2.0f * fty);
        fwy0 = 1.0f - fty;
        fgybase = fgy * fgw;
        for (qx = 0; qx < qcols; qx++) {
            int xx = qx << 2;
            int tw = (xx + 4 <= w) ? 4 : (w - xx);
            int th = (yy + 4 <= h) ? 4 : (h - yy);
            int txc = xx + (tw >> 1);
            int tyc = yy + (th >> 1);
            int pc = tyc * w + txc;
            float fx;
            float fy;
            float fm;
            float vmag;
            float vel_x;
            float vel_y;
            float tx;
            float ty;
            float v2;
            float pal_u;
            float pal_v;
            float band;
            int adv_u;
            int adv_v;
            int tile_edge = 0;
            int tile_edge_event;
            int force_charge;
            int motion_gain;
            int use_bilinear;

            while (txc >= (fgx_run + 1) * fcell && fgx_run + 2 < fgw)
                fgx_run++;
            {
                int flx = txc - fgx_run * fcell;
                float ftx = (float) flx / (float) fcell;
                float fwx0;
                int gi00;
                int gi10;
                int gi01;
                int gi11;
                float ax;
                float bx;
                float ay;
                float by;
                float am;
                float bm;
                ftx = ftx * ftx * (3.0f - 2.0f * ftx);
                fwx0 = 1.0f - ftx;
                gi00 = fgybase + fgx_run;
                gi10 = gi00 + 1;
                gi01 = gi00 + fgw;
                gi11 = gi01 + 1;
                ax = e->force_x[gi00] * fwy0 + e->force_x[gi01] * fty;
                bx = e->force_x[gi10] * fwy0 + e->force_x[gi11] * fty;
                ay = e->force_y[gi00] * fwy0 + e->force_y[gi01] * fty;
                by = e->force_y[gi10] * fwy0 + e->force_y[gi11] * fty;
                am = e->force_m[gi00] * fwy0 + e->force_m[gi01] * fty;
                bm = e->force_m[gi10] * fwy0 + e->force_m[gi11] * fty;
                fx = ax * fwx0 + bx * ftx;
                fy = ay * fwx0 + by * ftx;
                fm = am * fwx0 + bm * ftx;
            }
            vmag = eh_absf(fx) + eh_absf(fy);
            tx = -fy;
            ty = fx;
            vel_x = fx * flow_pixels + tx * curl_pixels * fm * curl_dir;
            vel_y = fy * flow_pixels + ty * curl_pixels * fm * curl_dir;

            v2 = vel_x * vel_x + vel_y * vel_y;
            if (v2 > max_v2_global) {
                float sc = max_v_global * eh_fast_rsqrt(v2);
                vel_x *= sc;
                vel_y *= sc;
            }

            if (txc > 0 && txc + 1 < w)
                tile_edge += eh_absi((int) e->src_y[pc + 1] - (int) e->src_y[pc - 1]);
            if (tyc > 0 && tyc + 1 < h)
                tile_edge += eh_absi((int) e->src_y[pc + w] - (int) e->src_y[pc - w]);
            if (tile_edge > 255)
                tile_edge = 255;

            tile_edge_event = e->charge_lut[tile_edge];
            force_charge = (int) (fm * force_charge_scale);
            if (force_charge > 64)
                force_charge = 64;
            motion_gain = motion_gain_global;
            use_bilinear = (vmag > 0.54f || fm > 0.25f || bands_i > 88 || glow_i > 90 || soft_i > 82);

            eh_sample_uv_nearest(old_u, old_v, (float) txc - vel_x, (float) tyc - vel_y, w, h, &adv_u, &adv_v);
            band = eh_lut_sin_fast(e, e->phase + ((float) txc * 0.013f + (float) tyc * 0.009f) * band_scale);
            band = band * 0.5f + 0.5f;
            {
                int src_u0 = (int) U[pc] - 128;
                int src_v0 = (int) V[pc] - 128;
                int pal_idx = (int) ((e->phase * 0.049f + fm * 0.24f + band * 0.10f + (float) e->src_y[pc] * 0.0011f + (float) (src_u0 - src_v0) * 0.0009f) * 256.0f);
                float glow_add = fm * band * (0.9f + global_glow_base * 0.09f);
                float chroma_limit;
                float src_pal = color_t * (0.055f + source_t * 0.055f);
                pal_idx &= 255;
                pal_u = pal_u_lut[pal_idx] + (float) src_u0 * src_pal;
                pal_v = pal_v_lut[pal_idx] + (float) src_v0 * src_pal;
                chroma_limit = chroma_limit_lut[pal_idx] + (eh_absf((float) src_u0) + eh_absf((float) src_v0)) * 0.055f;
                eh_render_tile_sampled(e, Y, U, V, old_y, next_y, next_u, next_v, next_charge,
                                       xx, yy, tw, th, w, h, vel_x, vel_y, fm, src_mix, chroma_src_mix,
                                       global_glow_base, glow_add, chroma_limit,
                                       density_t, trail_t, pal_u, pal_v,
                                       charge_f_lut, wet_lut, detail_lut, glow_lut, fade_lut,
                                       band, adv_u, adv_v,
                                       tile_edge_event, force_charge, motion_gain, use_bilinear);
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
