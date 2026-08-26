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
#include "blackhole.h"

#define BLACKHOLE_PARAMS 10

#define P_SPEED     0
#define P_LENS      1
#define P_FOLDS     2
#define P_SPIN      3
#define P_SPIRAL    4
#define P_FEEDBACK  5
#define P_CORE      6
#define P_SALIENCY  7
#define P_MERGER    8
#define P_STRENGTH  9

#define ST_TRIG_LUT_SIZE 1024
#define ST_TRIG_LUT_MASK 1023
#define ST_TWO_PI        6.28318530718f
#define ST_INV_TWO_PI    0.15915494309f

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int frame;
    int n_threads;

    void *region;

    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;

    uint8_t *fb_y;
    uint8_t *fb_u;
    uint8_t *fb_v;

    float *xnorm;

    uint8_t gamma_lut[256];
    uint8_t tone_lut[256];

    float sin_lut[ST_TRIG_LUT_SIZE];
    float cos_lut[ST_TRIG_LUT_SIZE];

    float time;
    float phase;
    float orbit;
    float merger_phase;

    float ringdown_amp;
    float ringdown_phase;

    float p1_x;
    float p1_y;
    float p2_x;
    float p2_y;
} blackhole_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t st_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline float st_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float st_absf(float x)
{
    union { float f; unsigned int i; } u;
    u.f = x;
    u.i &= 0x7fffffffU;
    return u.f;
}

static inline float st_smooth01(float t)
{
    t = st_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline int st_floor_to_int(float v)
{
    int i = (int) v;
    if (v < (float) i)
        i--;
    return i;
}

static inline float st_wrap_2pi(float v)
{
    if (v >= ST_TWO_PI || v < 0.0f) {
        int k = (int) (v * ST_INV_TWO_PI);
        v -= (float) k * ST_TWO_PI;
        if (v < 0.0f)
            v += ST_TWO_PI;
        else if (v >= ST_TWO_PI)
            v -= ST_TWO_PI;
    }
    return v;
}

static inline float st_lut_sin_interp(const blackhole_t *t, float phase)
{
    float fidx = phase * ((float) ST_TRIG_LUT_SIZE * ST_INV_TWO_PI);
    int idx0 = st_floor_to_int(fidx);
    float frac = fidx - (float) idx0;

    int i0 = idx0 & ST_TRIG_LUT_MASK;
    int i1 = (i0 + 1) & ST_TRIG_LUT_MASK;

    float a = t->sin_lut[i0];
    float b = t->sin_lut[i1];

    return a + (b - a) * frac;
}

static inline float st_lut_cos_interp(const blackhole_t *t, float phase)
{
    float fidx = phase * ((float) ST_TRIG_LUT_SIZE * ST_INV_TWO_PI);
    int idx0 = st_floor_to_int(fidx);
    float frac = fidx - (float) idx0;

    int i0 = idx0 & ST_TRIG_LUT_MASK;
    int i1 = (i0 + 1) & ST_TRIG_LUT_MASK;

    float a = t->cos_lut[i0];
    float b = t->cos_lut[i1];

    return a + (b - a) * frac;
}

static inline void st_lut_sincos_interp(
    const blackhole_t *t,
    float phase,
    float *s,
    float *c
) {
    float fidx = phase * ((float) ST_TRIG_LUT_SIZE * ST_INV_TWO_PI);
    int idx0 = st_floor_to_int(fidx);
    float frac = fidx - (float) idx0;

    int i0 = idx0 & ST_TRIG_LUT_MASK;
    int i1 = (i0 + 1) & ST_TRIG_LUT_MASK;

    float sa = t->sin_lut[i0];
    float sb = t->sin_lut[i1];

    float ca = t->cos_lut[i0];
    float cb = t->cos_lut[i1];

    *s = sa + (sb - sa) * frac;
    *c = ca + (cb - ca) * frac;
}

static inline float st_fast_rsqrt(float x)
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

static inline float st_fast_log(float val)
{
    union { float f; unsigned int i; } vx;

    if (val <= 1.0e-12f)
        val = 1.0e-12f;

    vx.f = val;

    return ((float) vx.i * 1.1920928955078125e-7f - 126.94269504f) * 0.69314718f;
}

static inline float st_fast_atan2(float y, float x)
{
    const float PI = 3.141592654f;
    const float PI_2 = 1.570796327f;
    const float EPSILON = 1e-8f;

    float ax = st_absf(x);
    float ay = st_absf(y);

    float mx = ax > ay ? ax : ay;
    float mn = ax > ay ? ay : ax;

    float a = mn / (mx + EPSILON);
    float s = a * a;

    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;

    if (ay > ax)
        r = PI_2 - r;

    if (x < 0.0f)
        r = PI - r;

    return (y < 0.0f) ? -r : r;
}

static inline int st_nearest_index_clamp(float fx, float fy, int w, int rows)
{
    int ix, iy;

    if (fx < 0.0f) fx = 0.0f;
    else if (fx > (float) (w - 1)) fx = (float) (w - 1);

    if (fy < 0.0f) fy = 0.0f;
    else if (fy > (float) (rows - 1)) fy = (float) (rows - 1);

    ix = st_floor_to_int(fx + 0.5f);
    iy = st_floor_to_int(fy + 0.5f);

    if (ix < 0) ix = 0;
    else if (ix >= w) ix = w - 1;

    if (iy < 0) iy = 0;
    else if (iy >= rows) iy = rows - 1;

    return iy * w + ix;
}

static inline int st_bilinear_sample_clamp(
    const uint8_t *restrict src,
    float fx,
    float fy,
    int w,
    int rows
) {
    int ix = st_floor_to_int(fx);
    int iy = st_floor_to_int(fy);

    if (fx < 0.0f) fx = 0.0f;
    else if (fx > (float) (w - 1)) fx = (float) (w - 1);

    if (fy < 0.0f) fy = 0.0f;
    else if (fy > (float) (rows - 1)) fy = (float) (rows - 1);

    int wx = (int) ((fx - (float) ix) * 256.0f);
    int wy = (int) ((fy - (float) iy) * 256.0f);

    int x0 = clampi(ix, 0, w - 1);
    int y0 = clampi(iy, 0, rows - 1);
    int x1 = clampi(ix + 1, 0, w - 1);
    int y1 = clampi(iy + 1, 0, rows - 1);

    int p00 = src[y0 * w + x0];
    int p10 = src[y0 * w + x1];
    int p01 = src[y1 * w + x0];
    int p11 = src[y1 * w + x1];

    int a = p00 * (256 - wx) + p10 * wx;
    int b = p01 * (256 - wx) + p11 * wx;

    return ((a * (256 - wy) + b * wy) + 32768) >> 16;
}

static inline size_t st_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

static inline void st_apply_pixel_from_index(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    uint8_t *restrict fb_y,
    uint8_t *restrict fb_u,
    uint8_t *restrict fb_v,
    const uint8_t *restrict tone_lut,
    int dst_i,
    int src_i,
    int redshift,
    int ring,
    int shadow,
    int fb,
    int local_fb
) {
    int yv = tone_lut[src_y[src_i]];
    int uv = src_u[src_i];
    int vv = src_v[src_i];

    yv = clampi(yv - redshift - shadow + ring, 0, 255);

    int fb_use = fb;
    if (local_fb > fb_use) fb_use = local_fb;
    fb_use = clampi(fb_use, 0, 255);
    
    int inv_use = 255 - fb_use;

    int out_y, out_u, out_v;

    if (fb_use > 0) {
        out_y = (yv * inv_use + (int) fb_y[dst_i] * fb_use + 127) / 255;
        out_u = (uv * inv_use + (int) fb_u[dst_i] * fb_use + 127) / 255;
        out_v = (vv * inv_use + (int) fb_v[dst_i] * fb_use + 127) / 255;
    }
    else {
        out_y = yv;
        out_u = uv;
        out_v = vv;
    }

    Y[dst_i] = (uint8_t) out_y;
    U[dst_i] = (uint8_t) out_u;
    V[dst_i] = (uint8_t) out_v;

    fb_y[dst_i] = (uint8_t) out_y;
    fb_u[dst_i] = (uint8_t) out_u;
    fb_v[dst_i] = (uint8_t) out_v;
}

static inline void st_apply_pixel_bilinear_y(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    uint8_t *restrict fb_y,
    uint8_t *restrict fb_u,
    uint8_t *restrict fb_v,
    const uint8_t *restrict tone_lut,
    int dst_i,
    float sx,
    float sy,
    int w,
    int rows,
    int redshift,
    int ring,
    int shadow,
    int fb,
    int local_fb
) {
    int src_i = st_nearest_index_clamp(sx, sy, w, rows);
    int yv = st_bilinear_sample_clamp(src_y, sx, sy, w, rows);
    int uv = src_u[src_i];
    int vv = src_v[src_i];

    yv = tone_lut[yv];
    yv = clampi(yv - redshift - shadow + ring, 0, 255);

    int fb_use = fb;
    if (local_fb > fb_use) fb_use = local_fb;
    fb_use = clampi(fb_use, 0, 255);
    
    int inv_use = 255 - fb_use;

    int out_y, out_u, out_v;

    if (fb_use > 0) {
        out_y = (yv * inv_use + (int) fb_y[dst_i] * fb_use + 127) / 255;
        out_u = (uv * inv_use + (int) fb_u[dst_i] * fb_use + 127) / 255;
        out_v = (vv * inv_use + (int) fb_v[dst_i] * fb_use + 127) / 255;
    }
    else {
        out_y = yv;
        out_u = uv;
        out_v = vv;
    }

    Y[dst_i] = (uint8_t) out_y;
    U[dst_i] = (uint8_t) out_u;
    V[dst_i] = (uint8_t) out_v;

    fb_y[dst_i] = (uint8_t) out_y;
    fb_u[dst_i] = (uint8_t) out_u;
    fb_v[dst_i] = (uint8_t) out_v;
}

static inline void st_write_2x2_nearest_fast(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    uint8_t *restrict fb_y,
    uint8_t *restrict fb_u,
    uint8_t *restrict fb_v,
    const uint8_t *restrict tone_lut,
    int i00,
    int x_has_1,
    int y_has_1,
    float sx00,
    float sy00,
    int w,
    int rows,
    int redshift,
    int ring,
    int shadow,
    int fb,
    int local_fb
) {
    if (sx00 >= 0.0f && sy00 >= 0.0f &&
        sx00 + 1.0f <= (float) (w - 1) &&
        sy00 + 1.0f <= (float) (rows - 1))
    {
        int ix0 = st_floor_to_int(sx00 + 0.5f);
        int iy0 = st_floor_to_int(sy00 + 0.5f);
        int ix1 = ix0 + 1;
        int iy1 = iy0 + 1;

        if (ix1 >= w) ix1 = w - 1;
        if (iy1 >= rows) iy1 = rows - 1;

        st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00, iy0 * w + ix0, redshift, ring, shadow, fb, local_fb);

        if (x_has_1) {
            st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00 + 1, iy0 * w + ix1, redshift, ring, shadow, fb, local_fb);
        }

        if (y_has_1) {
            int i01 = i00 + w;
            st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i01, iy1 * w + ix0, redshift, ring, shadow, fb, local_fb);

            if (x_has_1) {
                st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i01 + 1, iy1 * w + ix1, redshift, ring, shadow, fb, local_fb);
            }
        }
        return;
    }

    st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00, st_nearest_index_clamp(sx00, sy00, w, rows), redshift, ring, shadow, fb, local_fb);

    if (x_has_1) {
        st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00 + 1, st_nearest_index_clamp(sx00 + 1.0f, sy00, w, rows), redshift, ring, shadow, fb, local_fb);
    }

    if (y_has_1) {
        int i01 = i00 + w;
        st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i01, st_nearest_index_clamp(sx00, sy00 + 1.0f, w, rows), redshift, ring, shadow, fb, local_fb);

        if (x_has_1) {
            st_apply_pixel_from_index(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i01 + 1, st_nearest_index_clamp(sx00 + 1.0f, sy00 + 1.0f, w, rows), redshift, ring, shadow, fb, local_fb);
        }
    }
}

static inline void st_write_2x2_bilinear_y(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    uint8_t *restrict fb_y,
    uint8_t *restrict fb_u,
    uint8_t *restrict fb_v,
    const uint8_t *restrict tone_lut,
    int i00,
    int x_has_1,
    int y_has_1,
    float sx00,
    float sy00,
    int w,
    int rows,
    int redshift,
    int ring,
    int shadow,
    int fb,
    int local_fb
) {
    st_apply_pixel_bilinear_y(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00, sx00, sy00, w, rows, redshift, ring, shadow, fb, local_fb);

    if (x_has_1) {
        st_apply_pixel_bilinear_y(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00 + 1, sx00 + 1.0f, sy00, w, rows, redshift, ring, shadow, fb, local_fb);
    }

    if (y_has_1) {
        int i01 = i00 + w;
        st_apply_pixel_bilinear_y(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i01, sx00, sy00 + 1.0f, w, rows, redshift, ring, shadow, fb, local_fb);

        if (x_has_1) {
            st_apply_pixel_bilinear_y(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i01 + 1, sx00 + 1.0f, sy00 + 1.0f, w, rows, redshift, ring, shadow, fb, local_fb);
        }
    }
}

static void st_update_saliency_poles(
    blackhole_t *t,
    const uint8_t *src_y,
    int rows,
    int saliency,
    float pole_smooth
) {
    const int w = t->w;
    const int h = rows;
    const int half = w >> 1;

    long long sx1 = 0, sy1 = 0, sw1 = 0;
    long long sx2 = 0, sy2 = 0, sw2 = 0;

    int step = (saliency > 70) ? 20 : 28;
    pole_smooth = st_clampf(pole_smooth, 0.004f, 0.028f);

    for (int y = 0; y < h; y += step) {
        int row = y * w;
        for (int x = 0; x < w; x += step) {
            int v = src_y[row + x];
            if (v < 48) continue;

            int weight = (v - 32) * (v - 32);
            if (x < half) {
                sx1 += (long long) x * (long long) weight;
                sy1 += (long long) y * (long long) weight;
                sw1 += weight;
            }
            else {
                sx2 += (long long) x * (long long) weight;
                sy2 += (long long) y * (long long) weight;
                sw2 += weight;
            }
        }
    }

    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;

    float tp1x = -0.45f, tp1y = 0.0f;
    float tp2x = 0.45f, tp2y = 0.0f;

    if (sw1 > 0) {
        tp1x = (((float) sx1 / (float) sw1) - cx) / cx;
        tp1y = (((float) sy1 / (float) sw1) - cy) / cy;
    }
    if (sw2 > 0) {
        tp2x = (((float) sx2 / (float) sw2) - cx) / cx;
        tp2y = (((float) sy2 / (float) sw2) - cy) / cy;
    }

    t->p1_x += (tp1x - t->p1_x) * pole_smooth;
    t->p1_y += (tp1y - t->p1_y) * pole_smooth;
    t->p2_x += (tp2x - t->p2_x) * pole_smooth;
    t->p2_y += (tp2y - t->p2_y) * pole_smooth;
}

vj_effect *blackhole_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve) return NULL;

    ve->num_params = BLACKHOLE_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Black Hole Merger / Gravitational Lensing";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_SPEED]    = 350;  ve->limits[0][P_SPEED]    = -2000; ve->limits[1][P_SPEED]    = 2000;
    ve->defaults[P_LENS]     = 58;   ve->limits[0][P_LENS]     = 0;     ve->limits[1][P_LENS]     = 100;
    ve->defaults[P_FOLDS]    = 3;    ve->limits[0][P_FOLDS]    = 0;     ve->limits[1][P_FOLDS]    = 12;
    ve->defaults[P_SPIN]     = 40;   ve->limits[0][P_SPIN]     = -100;  ve->limits[1][P_SPIN]     = 100;
    ve->defaults[P_SPIRAL]   = 32;   ve->limits[0][P_SPIRAL]   = -100;  ve->limits[1][P_SPIRAL]   = 100;
    ve->defaults[P_FEEDBACK] = 10;   ve->limits[0][P_FEEDBACK] = 0;     ve->limits[1][P_FEEDBACK] = 100;
    ve->defaults[P_CORE]     = 24;   ve->limits[0][P_CORE]     = 1;     ve->limits[1][P_CORE]     = 100;
    ve->defaults[P_SALIENCY] = 8;    ve->limits[0][P_SALIENCY] = 0;     ve->limits[1][P_SALIENCY] = 100;
    ve->defaults[P_MERGER]   = 210;  ve->limits[0][P_MERGER]   = 0;     ve->limits[1][P_MERGER]   = 300;
    ve->defaults[P_STRENGTH] = 55;   ve->limits[0][P_STRENGTH] = 0;     ve->limits[1][P_STRENGTH] = 100;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Accretion Speed", "Lens Mass", "Caustic Folds", "Spin Drag", "Accretion Pitch",
        "Echo Memory", "Core Size", "Source Gravity", "Merger Cycle", "Caustic Strength"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, -1400, 1400, 78, 100, 18, 280, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 18, 100, 72, 100, 22, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_IMPULSE | VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_BEAT_PULSE, VJ_BEAT_OP_IMPULSE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, 9, 56, 92, 0, 520, 90, 1, 80, VJ_BEAT_COST_MODERATE, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, -100, 100, 70, 100, 24, 360, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_SMOOTHSTEP, -100, 100, 62, 96, 36, 520, 0, 1, 0, VJ_BEAT_COST_MODERATE, 80, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LOG, 0, 82, 64, 96, 48, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 8, 72, 44, 80, 90, 1600, 0, 1, 0, VJ_BEAT_COST_MODERATE, 58, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ONSET, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 90, 62, 96, 24, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, 0, 300, 72, 100, 20, 360, 0, 1, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 8, 100, 78, 100, 18, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 99, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *blackhole_malloc(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;

    blackhole_t *t = (blackhole_t *) vj_calloc(sizeof(blackhole_t));
    if (!t) return NULL;

    size_t len = (size_t) w * (size_t) h;
    size_t bytes_y = len;
    size_t total = bytes_y * 6 + sizeof(float) * (size_t) w + 64;
    size_t off = 0;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->seeded = 0;
    t->frame = 0;

    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    unsigned char *base = (unsigned char *) t->region;

    t->src_y = (uint8_t *) (base + off); off += bytes_y;
    t->src_u = (uint8_t *) (base + off); off += bytes_y;
    t->src_v = (uint8_t *) (base + off); off += bytes_y;
    t->fb_y  = (uint8_t *) (base + off); off += bytes_y;
    t->fb_u  = (uint8_t *) (base + off); off += bytes_y;
    t->fb_v  = (uint8_t *) (base + off); off += bytes_y;

    off = st_align_size(off, sizeof(float));
    t->xnorm = (float *) (base + off);

    for (size_t i = 0; i < len; i++) {
        t->src_y[i] = 16; t->src_u[i] = 128; t->src_v[i] = 128;
        t->fb_y[i]  = 16; t->fb_u[i]  = 128; t->fb_v[i]  = 128;
    }

    float cx = (float) w * 0.5f;
    for (int x = 0; x < w; x++) {
        t->xnorm[x] = ((float) x - cx) / cx;
    }

    for (int i = 0; i < 256; i++) {
        float v = (float) i / 255.0f;
        int g = (int) (powf(v, 0.94f) * 255.0f + 0.5f);
        t->gamma_lut[i] = st_u8(g);
        t->tone_lut[i] = t->gamma_lut[i];
    }

    for (int i = 0; i < ST_TRIG_LUT_SIZE; i++) {
        float a = ST_TWO_PI * ((float) i / (float) ST_TRIG_LUT_SIZE);
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    t->time = 0.0f; t->phase = 0.0f; t->orbit = 0.0f;
    t->merger_phase = 0.0f; t->ringdown_amp = 0.0f; t->ringdown_phase = 0.0f;
    t->p1_x = -0.45f; t->p1_y = 0.0f; t->p2_x = 0.45f; t->p2_y = 0.0f;

    return (void *) t;
}

void blackhole_free(void *ptr)
{
    blackhole_t *t = (blackhole_t *) ptr;
    if (!t) return;
    if (t->region) free(t->region);
    free(t);
}

void blackhole_apply(void *ptr, VJFrame *frame, int *args)
{
    blackhole_t *t = (blackhole_t *) ptr;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    uint8_t *restrict fb_y = t->fb_y;
    uint8_t *restrict fb_u = t->fb_u;
    uint8_t *restrict fb_v = t->fb_v;
    uint8_t *restrict tone_lut = t->tone_lut;

    int w = t->w;
    int h = t->h;
    int process_len = frame->len;
    
    if (process_len <= 0) process_len = t->len;
    if (process_len > t->len) process_len = t->len;

    int rows = process_len / w;
    if (rows > h) rows = h;

    int speed = args[P_SPEED];
    int lens = args[P_LENS];
    int folds_i = args[P_FOLDS];
    int strength_i = args[P_STRENGTH];
    int spin_i = args[P_SPIN];
    int spiral_i = args[P_SPIRAL];
    int feedback_i = args[P_FEEDBACK];
    int core_i = args[P_CORE];
    int saliency = args[P_SALIENCY];
    int merger_i = args[P_MERGER];

    int skip_processing = 0;

    if (lens <= 0 || rows < 2) {
        skip_processing = 1;
    }

    if (skip_processing) {
        #pragma omp single
        {
            if (lens <= 0) {
                veejay_memcpy(fb_y, Y, process_len);
                veejay_memcpy(fb_u, U, process_len);
                veejay_memcpy(fb_v, V, process_len);
            }
            t->seeded = 1;
            t->frame++;
        }
        return;
    }

    float lens_t = (float) lens * 0.01f;
    float lens_curve = st_smooth01(lens_t);
    float lens_bend = lens_curve / (1.0f + 0.30f * lens_curve);

    float speed_abs = st_absf((float) speed);
    float speed_amt = st_clampf(speed_abs / 2000.0f, 0.0f, 1.0f);

    float merger_t = st_clampf((float) merger_i / 300.0f, 0.0f, 1.0f);
    float orbit_curve = st_smooth01(st_clampf(merger_t / 0.72f, 0.0f, 1.0f));
    float inspiral_mix = st_smooth01((merger_t - 0.32f) / 0.44f);
    float collision_mix = st_smooth01((merger_t - 0.58f) / 0.42f);

    float folds = (float) folds_i;
    float strength_t = (float) strength_i * 0.01f;
    float fold_amount = st_smooth01(strength_t);
    float spiral = (float) spiral_i * 0.010f;
    float spin_param = (float) spin_i;
    float spin_amount = st_absf(spin_param) * 0.01f;
    float orbit_dir = (spin_i < 0) ? -1.0f : 1.0f;

    float core = 0.014f + (float) core_i * 0.0046f;
    float core2 = core * core;
    float lens_strength = lens_bend * 0.036f;

    float drag_strength = lens_bend * spin_param * 0.0025f;
    float drag_local = drag_strength * core2;
    int has_drag = (drag_local > 0.0000001f || drag_local < -0.0000001f);

    float new_merger_phase = st_wrap_2pi(t->merger_phase + 0.00010f + orbit_curve * 0.00090f + collision_mix * collision_mix * 0.00650f);
    float merge_c = st_lut_cos_interp(t, new_merger_phase);
    float approach = (1.0f - merge_c) * 0.5f;
    float approach_s = st_smooth01(approach);
    float collision_pulse = collision_mix * st_smooth01((approach - 0.55f) / 0.45f);

    float new_ringdown_amp = t->ringdown_amp * 0.955f;
    if (collision_pulse > new_ringdown_amp) new_ringdown_amp = collision_pulse;
    float new_ringdown_phase = st_wrap_2pi(t->ringdown_phase + 0.040f + collision_mix * 0.045f + orbit_curve * 0.018f);

    float safe_min_sep = st_clampf(0.078f + core * 0.22f, 0.078f, 0.160f);
    float base_sep = st_clampf(0.43f - 0.080f * orbit_curve - 0.115f * inspiral_mix * approach_s - 0.105f * collision_mix * approach_s, safe_min_sep, 0.43f);
    
    float orbit_sep = st_clampf(base_sep * 2.0f + core * 0.70f, 0.26f, 1.10f);
    float orbit_sep3 = orbit_sep * orbit_sep * orbit_sep;
    float orbit_kepler = st_fast_rsqrt(orbit_sep3 + 1.0e-6f);
    float orbit_spin_bias = 0.72f + 0.58f * spin_amount;
    float orbit_rate = orbit_dir * orbit_kepler * orbit_spin_bias * (0.0020f * orbit_curve + 0.0105f * orbit_curve * orbit_curve);
    if (collision_mix > 0.0f) orbit_rate *= 1.0f + 1.80f * collision_pulse;

    float new_time = st_wrap_2pi(t->time + (float) speed * 0.00105f);
    float new_phase = st_wrap_2pi(t->phase + (float) speed * 0.00052f + spin_param * 0.000022f);
    float new_orbit = st_wrap_2pi(t->orbit + orbit_rate);

    int tone_mix = clampi((int)(lens_curve * 255.0f + 0.5f), 0, 255);

    #pragma omp single
    {
        veejay_memcpy(src_y, Y, process_len);
        veejay_memcpy(src_u, U, process_len);
        veejay_memcpy(src_v, V, process_len);

        if (!t->seeded) {
            veejay_memcpy(fb_y, src_y, process_len);
            veejay_memcpy(fb_u, src_u, process_len);
            veejay_memcpy(fb_v, src_v, process_len);
            t->seeded = 1;
        }

        t->merger_phase = new_merger_phase;
        t->ringdown_amp = new_ringdown_amp;
        t->ringdown_phase = new_ringdown_phase;
        t->time = new_time;
        t->phase = new_phase;
        t->orbit = new_orbit;

        for (int i = 0; i < 256; i++) {
            int g = t->gamma_lut[i];
            tone_lut[i] = st_u8(i + (((g - i) * tone_mix + 127) / 255));
        }

        if (saliency > 0) {
            int saliency_mask = (lens > 65) ? 7 : 3;
            if ((t->frame & saliency_mask) == 0) {
                float pole_smooth = st_clampf(0.028f - 0.022f * lens_curve, 0.004f, 0.028f);
                st_update_saliency_poles(t, src_y, rows, saliency, pole_smooth);
            }
        }
    }

    float influence_damp = st_clampf(1.0f - 0.78f * lens_curve, 0.18f, 1.0f);
    float influence = (float) saliency * 0.01f * influence_damp;

    float p1x = (-base_sep * (1.0f - influence)) + (t->p1_x * influence);
    float p1y = ( 0.00f    * (1.0f - influence)) + (t->p1_y * influence);
    float p2x = ( base_sep * (1.0f - influence)) + (t->p2_x * influence);
    float p2y = ( 0.00f    * (1.0f - influence)) + (t->p2_y * influence);

    float ca, sa;
    st_lut_sincos_interp(t, new_orbit, &sa, &ca);
    float cpx = (p1x + p2x) * 0.5f;
    float cpy = (p1y + p2y) * 0.5f;
    float r1x = p1x - cpx; float r1y = p1y - cpy;
    float r2x = p2x - cpx; float r2y = p2y - cpy;
    p1x = cpx + r1x * ca - r1y * sa;
    p1y = cpy + r1x * sa + r1y * ca;
    p2x = cpx + r2x * ca - r2y * sa;
    p2y = cpy + r2x * sa + r2y * ca;

    float bx = (p1x + p2x) * 0.5f;
    float by = (p1y + p2y) * 0.5f;

    int ringdown_active = (new_ringdown_amp > 0.018f);

    float fold_strength = fold_amount * (0.006f + lens_bend * 0.042f) * (1.0f + collision_pulse * 0.55f);
    float log_factor = folds * (0.50f + lens_bend * 0.90f);
    int use_fold = (folds_i > 0 && fold_strength > 0.00010f);
    int base_bilinear_y = (use_fold && lens > 70);

    int fb = clampi((int)((float)((feedback_i * 255) / 100) * lens_curve) + (int)(collision_pulse * 18.0f), 0, 255);

    float ring_r2 = core2 * (5.10f - 1.55f * collision_pulse);
    if (ring_r2 < core2 * 2.25f) ring_r2 = core2 * 2.25f;
    float inv_ring_w = 1.0f / (core2 * (2.70f + collision_pulse * 1.45f) + 1e-6f);

    float redshift_scale = core * (28.0f + collision_pulse * 36.0f) * lens_curve;
    float ring_scale = (15.0f + collision_pulse * 36.0f) * lens_curve;
    float accretion_glint_scale = (4.0f + 20.0f * fold_amount + 9.0f * lens_curve) * (0.30f + 0.70f * speed_amt);
    float merger_glow_scale = collision_pulse * (22.0f + 34.0f * lens_curve);
    float ringdown_glow_scale = new_ringdown_amp * (10.0f + 28.0f * lens_curve);
    float event_shadow_scale = 10.0f + 30.0f * lens_curve + 34.0f * collision_pulse + 14.0f * new_ringdown_amp;

    float max_disp = st_clampf(0.58f + core * 0.12f + collision_pulse * 0.055f, 0.54f, 0.74f);
    float max_disp2 = max_disp * max_disp;

    float cx = (float) w * 0.5f;
    float cy = (float) rows * 0.5f;
    float half_x_step = 1.0f / (float) w;
    float half_y_step = 1.0f / (float) rows;
    int qcols = (w + 1) >> 1;
    int qrows = (rows + 1) >> 1;

    #pragma omp for schedule(static)
    for (int qy = 0; qy < qrows; qy++) {
        int y = qy << 1;
        int row = y * w;
        int y_has_1 = (y + 1 < rows);
        float dy = ((float) y - cy) / cy + half_y_step;

        for (int qx = 0; qx < qcols; qx++) {
            int x = qx << 1;
            int i00 = row + x;
            int x_has_1 = (x + 1 < w);
            float dx = t->xnorm[x] + half_x_step;

            float x1 = dx - p1x;
            float y1 = dy - p1y;
            float x2 = dx - p2x;
            float y2 = dy - p2y;

            float r21 = x1 * x1 + y1 * y1 + core2;
            float r22 = x2 * x2 + y2 * y2 + core2;

            float inv1 = st_fast_rsqrt(r21);
            float inv2 = st_fast_rsqrt(r22);
            float inv21 = inv1 * inv1;
            float inv22 = inv2 * inv2;

            float grav_x = -(x1 * inv21 + x2 * inv22) * lens_strength;
            float grav_y = -(y1 * inv21 + y2 * inv22) * lens_strength;
            float vx = grav_x, vy = grav_y;

            if (has_drag) {
                float inv31 = inv21 * inv1;
                float inv32 = inv22 * inv2;
                vx += (-y1 * inv31 - y2 * inv32) * drag_local;
                vy += ( x1 * inv31 + x2 * inv32) * drag_local;
            }

            float well = (inv1 + inv2) * core;
            float local = st_clampf(st_smooth01((well - 0.48f) * 1.35f), 0.0f, 1.0f);
            float local_core = st_clampf(st_smooth01((well - 0.82f) * 1.85f), 0.0f, 1.0f);
            float local_ring = st_clampf(local * (1.0f - local_core * 0.68f), 0.0f, 1.0f);
            float radial_phase = (r21 + r22) * 2.15f + local * 1.60f;

            if (ringdown_active) {
                float brx = dx - bx;
                float bry = dy - by;
                float br2 = brx * brx + bry * bry;
                if (br2 < 0.5486968f) {
                    float br = br2 * st_fast_rsqrt(br2 + 1.0e-6f);
                    float rd_env = st_clampf(1.0f - br * 1.35f, 0.0f, 1.0f);
                    if (rd_env > 0.001f) {
                        rd_env = st_smooth01(rd_env);
                    }
                }
            }

            float v2 = vx * vx + vy * vy;
            if (v2 > max_disp2) {
                float scale = max_disp * st_fast_rsqrt(v2);
                vx *= scale;
                vy *= scale;
            }

            float qmap_x = dx + vx;
            float qmap_y = dy + vy;
            int caustic_glint = 0;

            if (use_fold) {
                float rx = qmap_x - bx;
                float ry = qmap_y - by;
                float rr2 = rx * rx + ry * ry + core2;
                float rinv = st_fast_rsqrt(rr2);
                float theta = st_fast_atan2(ry, rx);
                float log_r = 0.5f * st_fast_log(rr2);
                float spatial_phase = theta * folds + log_r * spiral;
                float wave = st_lut_sin_interp(t, spatial_phase);
                float amp = fold_strength * wave;
                
                qmap_x += (-ry * rinv) * amp + (rx * rinv) * amp * 0.33f + rx * rinv * log_r * fold_strength * 0.14f * log_factor;
                qmap_y += ( rx * rinv) * amp + (ry * rinv) * amp * 0.33f + ry * rinv * log_r * fold_strength * 0.14f * log_factor;

                if (local_ring > 0.002f) {
                    float light = (st_lut_sin_interp(t, new_phase + new_time + radial_phase) * 0.5f + 0.5f);
                    caustic_glint = clampi((int)(light * local_ring * accretion_glint_scale), 0, 40);
                }
            } else {
                if (local_ring > 0.002f) {
                    float light = (st_lut_sin_interp(t, new_phase + new_time + radial_phase) * 0.5f + 0.5f);
                    caustic_glint = clampi((int)(light * local_ring * (2.0f + 9.0f * lens_curve) * speed_amt), 0, 12);
                }
            }

            float sx_center = qmap_x * cx + cx;
            float sy_center = qmap_y * cy + cy;
            float sx00 = sx_center - 0.5f;
            float sy00 = sy_center - 0.5f;

            int redshift = clampi((int)((inv1 + inv2) * redshift_scale) + (int)(collision_pulse * local * 34.0f), 0, 104);
            int merger_glow = clampi((int)(merger_glow_scale * local_ring), 0, 58);
            int event_shadow = clampi((int)(event_shadow_scale * local_core) + (int)(collision_pulse * local_core * 34.0f), 0, 112);
            int photon_rim = clampi((int)(local_ring * (8.0f + 20.0f * lens_curve + 24.0f * collision_pulse)), 0, 52);
            
            float bare1 = r21 - core2;
            float bare2 = r22 - core2;
            float a1 = st_clampf(1.0f - st_absf(bare1 - ring_r2) * inv_ring_w, 0.0f, 1.0f);
            float a2 = st_clampf(1.0f - st_absf(bare2 - ring_r2) * inv_ring_w, 0.0f, 1.0f);
            int ring = clampi((int)((a1 + a2) * ring_scale) + photon_rim + caustic_glint + merger_glow, 0, 126);

            int local_fb = clampi(fb + (int)(collision_pulse * local * 82.0f), 0, 255);
            int use_bilinear_y = base_bilinear_y || (local_core > 0.18f) || (collision_pulse > 0.50f);

            if (use_bilinear_y) {
                st_write_2x2_bilinear_y(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00, x_has_1, y_has_1, sx00, sy00, w, rows, redshift, ring, event_shadow, fb, local_fb);
            } else {
                st_write_2x2_nearest_fast(Y, U, V, src_y, src_u, src_v, fb_y, fb_u, fb_v, tone_lut, i00, x_has_1, y_has_1, sx00, sy00, w, rows, redshift, ring, event_shadow, fb, local_fb);
            }
        }
    }

    #pragma omp single
    {
        t->frame++;
    }
}