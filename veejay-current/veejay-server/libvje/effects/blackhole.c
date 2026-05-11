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
#include <stdlib.h>
#include <math.h>

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

static inline int st_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t st_u8(int v)
{
    return (uint8_t) st_clampi(v, 0, 255);
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
    int ix;
    int iy;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);

    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (rows - 1))
        fy = (float) (rows - 1);

    ix = st_floor_to_int(fx + 0.5f);
    iy = st_floor_to_int(fy + 0.5f);

    if (ix < 0)
        ix = 0;
    else if (ix >= w)
        ix = w - 1;

    if (iy < 0)
        iy = 0;
    else if (iy >= rows)
        iy = rows - 1;

    return iy * w + ix;
}

static inline int st_bilinear_sample_clamp(
    const uint8_t *restrict src,
    float fx,
    float fy,
    int w,
    int rows
) {
    int ix;
    int iy;

    int wx;
    int wy;

    int x0;
    int y0;
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
    else if (fy > (float) (rows - 1))
        fy = (float) (rows - 1);

    ix = st_floor_to_int(fx);
    iy = st_floor_to_int(fy);

    wx = (int) ((fx - (float) ix) * 256.0f);
    wy = (int) ((fy - (float) iy) * 256.0f);

    x0 = st_clampi(ix, 0, w - 1);
    y0 = st_clampi(iy, 0, rows - 1);
    x1 = st_clampi(ix + 1, 0, w - 1);
    y1 = st_clampi(iy + 1, 0, rows - 1);

    p00 = src[y0 * w + x0];
    p10 = src[y0 * w + x1];
    p01 = src[y1 * w + x0];
    p11 = src[y1 * w + x1];

    a = p00 * (256 - wx) + p10 * wx;
    b = p01 * (256 - wx) + p11 * wx;

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

    int out_y;
    int out_u;
    int out_v;

    int fb_use;
    int inv_use;

    yv = st_clampi(yv - redshift - shadow + ring, 0, 255);

    fb_use = fb;
    if (local_fb > fb_use)
        fb_use = local_fb;

    fb_use = st_clampi(fb_use, 0, 255);
    inv_use = 255 - fb_use;

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

    int out_y;
    int out_u;
    int out_v;

    int fb_use;
    int inv_use;

    yv = tone_lut[yv];
    yv = st_clampi(yv - redshift - shadow + ring, 0, 255);

    fb_use = fb;
    if (local_fb > fb_use)
        fb_use = local_fb;

    fb_use = st_clampi(fb_use, 0, 255);
    inv_use = 255 - fb_use;

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
    int ix0;
    int iy0;
    int ix1;
    int iy1;

    if (sx00 >= 0.0f && sy00 >= 0.0f &&
        sx00 + 1.0f <= (float) (w - 1) &&
        sy00 + 1.0f <= (float) (rows - 1))
    {
        ix0 = st_floor_to_int(sx00 + 0.5f);
        iy0 = st_floor_to_int(sy00 + 0.5f);

        ix1 = ix0 + 1;
        iy1 = iy0 + 1;

        if (ix1 >= w)
            ix1 = w - 1;

        if (iy1 >= rows)
            iy1 = rows - 1;

        st_apply_pixel_from_index(
            Y, U, V,
            src_y, src_u, src_v,
            fb_y, fb_u, fb_v,
            tone_lut,
            i00,
            iy0 * w + ix0,
            redshift, ring, shadow, fb, local_fb
        );

        if (x_has_1) {
            st_apply_pixel_from_index(
                Y, U, V,
                src_y, src_u, src_v,
                fb_y, fb_u, fb_v,
                tone_lut,
                i00 + 1,
                iy0 * w + ix1,
                redshift, ring, shadow, fb, local_fb
            );
        }

        if (y_has_1) {
            int i01 = i00 + w;

            st_apply_pixel_from_index(
                Y, U, V,
                src_y, src_u, src_v,
                fb_y, fb_u, fb_v,
                tone_lut,
                i01,
                iy1 * w + ix0,
                redshift, ring, shadow, fb, local_fb
            );

            if (x_has_1) {
                st_apply_pixel_from_index(
                    Y, U, V,
                    src_y, src_u, src_v,
                    fb_y, fb_u, fb_v,
                    tone_lut,
                    i01 + 1,
                    iy1 * w + ix1,
                    redshift, ring, shadow, fb, local_fb
                );
            }
        }

        return;
    }

    st_apply_pixel_from_index(
        Y, U, V,
        src_y, src_u, src_v,
        fb_y, fb_u, fb_v,
        tone_lut,
        i00,
        st_nearest_index_clamp(sx00, sy00, w, rows),
        redshift, ring, shadow, fb, local_fb
    );

    if (x_has_1) {
        st_apply_pixel_from_index(
            Y, U, V,
            src_y, src_u, src_v,
            fb_y, fb_u, fb_v,
            tone_lut,
            i00 + 1,
            st_nearest_index_clamp(sx00 + 1.0f, sy00, w, rows),
            redshift, ring, shadow, fb, local_fb
        );
    }

    if (y_has_1) {
        int i01 = i00 + w;

        st_apply_pixel_from_index(
            Y, U, V,
            src_y, src_u, src_v,
            fb_y, fb_u, fb_v,
            tone_lut,
            i01,
            st_nearest_index_clamp(sx00, sy00 + 1.0f, w, rows),
            redshift, ring, shadow, fb, local_fb
        );

        if (x_has_1) {
            st_apply_pixel_from_index(
                Y, U, V,
                src_y, src_u, src_v,
                fb_y, fb_u, fb_v,
                tone_lut,
                i01 + 1,
                st_nearest_index_clamp(sx00 + 1.0f, sy00 + 1.0f, w, rows),
                redshift, ring, shadow, fb, local_fb
            );
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
    st_apply_pixel_bilinear_y(
        Y, U, V,
        src_y, src_u, src_v,
        fb_y, fb_u, fb_v,
        tone_lut,
        i00,
        sx00,
        sy00,
        w,
        rows,
        redshift, ring, shadow, fb, local_fb
    );

    if (x_has_1) {
        st_apply_pixel_bilinear_y(
            Y, U, V,
            src_y, src_u, src_v,
            fb_y, fb_u, fb_v,
            tone_lut,
            i00 + 1,
            sx00 + 1.0f,
            sy00,
            w,
            rows,
            redshift, ring, shadow, fb, local_fb
        );
    }

    if (y_has_1) {
        int i01 = i00 + w;

        st_apply_pixel_bilinear_y(
            Y, U, V,
            src_y, src_u, src_v,
            fb_y, fb_u, fb_v,
            tone_lut,
            i01,
            sx00,
            sy00 + 1.0f,
            w,
            rows,
            redshift, ring, shadow, fb, local_fb
        );

        if (x_has_1) {
            st_apply_pixel_bilinear_y(
                Y, U, V,
                src_y, src_u, src_v,
                fb_y, fb_u, fb_v,
                tone_lut,
                i01 + 1,
                sx00 + 1.0f,
                sy00 + 1.0f,
                w,
                rows,
                redshift, ring, shadow, fb, local_fb
            );
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

    long long sx1 = 0;
    long long sy1 = 0;
    long long sw1 = 0;

    long long sx2 = 0;
    long long sy2 = 0;
    long long sw2 = 0;

    int step;
    int y;
    int x;

    if (saliency <= 0)
        return;

    pole_smooth = st_clampf(pole_smooth, 0.004f, 0.028f);
    step = (saliency > 70) ? 20 : 28;

    for (y = 0; y < h; y += step) {
        int row = y * w;

        for (x = 0; x < w; x += step) {
            int v = src_y[row + x];
            int weight;

            if (v < 48)
                continue;

            weight = (v - 32) * (v - 32);

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

    {
        float cx = (float) w * 0.5f;
        float cy = (float) h * 0.5f;

        float tp1x = -0.45f;
        float tp1y = 0.0f;
        float tp2x = 0.45f;
        float tp2y = 0.0f;

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
}

vj_effect *blackhole_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if (!ve)
        return NULL;

    ve->num_params = BLACKHOLE_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Black Hole Merger / Gravitional Lensing";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_SPEED]    = 350;
    ve->defaults[P_LENS]     = 58;
    ve->defaults[P_FOLDS]    = 3;
    ve->defaults[P_SPIN]     = 40;
    ve->defaults[P_SPIRAL]   = 32;
    ve->defaults[P_FEEDBACK] = 10;
    ve->defaults[P_CORE]     = 24;
    ve->defaults[P_SALIENCY] = 8;
    ve->defaults[P_MERGER]   = 210;
    ve->defaults[P_STRENGTH] = 55;

    ve->limits[0][P_SPEED]    = -2000;
    ve->limits[1][P_SPEED]    = 2000;

    ve->limits[0][P_LENS]     = 0;
    ve->limits[1][P_LENS]     = 100;

    ve->limits[0][P_FOLDS]    = 0;
    ve->limits[1][P_FOLDS]    = 12;

    ve->limits[0][P_SPIN]     = -100;
    ve->limits[1][P_SPIN]     = 100;

    ve->limits[0][P_SPIRAL]   = -100;
    ve->limits[1][P_SPIRAL]   = 100;

    ve->limits[0][P_FEEDBACK] = 0;
    ve->limits[1][P_FEEDBACK] = 100;

    ve->limits[0][P_CORE]     = 1;
    ve->limits[1][P_CORE]     = 100;

    ve->limits[0][P_SALIENCY] = 0;
    ve->limits[1][P_SALIENCY] = 100;

    ve->limits[0][P_MERGER]   = 0;
    ve->limits[1][P_MERGER]   = 300;

    ve->limits[0][P_STRENGTH] = 0;
    ve->limits[1][P_STRENGTH] = 100;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Accretion Speed",
        "Lens Mass",
        "Caustic Folds",
        "Spin Drag",
        "Accretion Pitch",
        "Echo Memory",
        "Core Size",
        "Source Gravity",
        "Merger Cycle",
        "Caustic Strength"
    );

    return ve;
}

void *blackhole_malloc(int w, int h)
{
    blackhole_t *t = NULL;
    unsigned char *base = NULL;

    size_t len = (size_t) w * (size_t) h;
    size_t bytes_y = len;
    size_t total = bytes_y * 6 + sizeof(float) * (size_t) w + 64;
    size_t off = 0;
    size_t i = 0;

    if (w <= 0 || h <= 0 || len == 0)
        return NULL;

    t = (blackhole_t *) vj_calloc(sizeof(blackhole_t));
    if (!t)
        return NULL;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->seeded = 0;
    t->frame = 0;
    t->n_threads = vje_advise_num_threads(w * h);

    if (t->n_threads <= 0)
        t->n_threads = 1;

    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    base = (unsigned char *) t->region;

    t->src_y = (uint8_t *) (base + off);
    off += bytes_y;

    t->src_u = (uint8_t *) (base + off);
    off += bytes_y;

    t->src_v = (uint8_t *) (base + off);
    off += bytes_y;

    t->fb_y = (uint8_t *) (base + off);
    off += bytes_y;

    t->fb_u = (uint8_t *) (base + off);
    off += bytes_y;

    t->fb_v = (uint8_t *) (base + off);
    off += bytes_y;

    off = st_align_size(off, sizeof(float));
    t->xnorm = (float *) (base + off);

    for (i = 0; i < len; i++) {
        t->src_y[i] = 16;
        t->src_u[i] = 128;
        t->src_v[i] = 128;

        t->fb_y[i] = 16;
        t->fb_u[i] = 128;
        t->fb_v[i] = 128;
    }

    {
        float cx = (float) w * 0.5f;
        int x;

        for (x = 0; x < w; x++)
            t->xnorm[x] = ((float) x - cx) / cx;
    }

    for (i = 0; i < 256; i++) {
        float v = (float) i / 255.0f;
        int g = (int) (powf(v, 0.94f) * 255.0f + 0.5f);
        t->gamma_lut[i] = st_u8(g);
        t->tone_lut[i] = t->gamma_lut[i];
    }

    for (i = 0; i < ST_TRIG_LUT_SIZE; i++) {
        float a = ST_TWO_PI * ((float) i / (float) ST_TRIG_LUT_SIZE);
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    t->time = 0.0f;
    t->phase = 0.0f;
    t->orbit = 0.0f;
    t->merger_phase = 0.0f;
    t->ringdown_amp = 0.0f;
    t->ringdown_phase = 0.0f;

    t->p1_x = -0.45f;
    t->p1_y = 0.0f;
    t->p2_x = 0.45f;
    t->p2_y = 0.0f;

    return (void *) t;
}

void blackhole_free(void *ptr)
{
    blackhole_t *t = (blackhole_t *) ptr;

    if (!t)
        return;

    if (t->region)
        free(t->region);

    free(t);
}

void blackhole_apply(void *ptr, VJFrame *frame, int *args)
{
    blackhole_t *t = (blackhole_t *) ptr;

    uint8_t *restrict Y = NULL;
    uint8_t *restrict U = NULL;
    uint8_t *restrict V = NULL;

    uint8_t *restrict src_y = NULL;
    uint8_t *restrict src_u = NULL;
    uint8_t *restrict src_v = NULL;

    uint8_t *restrict fb_y = NULL;
    uint8_t *restrict fb_u = NULL;
    uint8_t *restrict fb_v = NULL;

    uint8_t *restrict tone_lut = NULL;

    int w = 0;
    int h = 0;
    int rows = 0;
    int process_len = 0;

    int speed = 0;
    int lens = 0;
    int folds_i = 0;
    int strength_i = 0;
    int spin_i = 0;
    int spiral_i = 0;
    int feedback_i = 0;
    int core_i = 0;
    int saliency = 0;
    int merger_i = 0;

    float lens_t = 0.0f;
    float lens_curve = 0.0f;
    float lens_bend = 0.0f;

    float speed_abs = 0.0f;
    float speed_amt = 0.0f;

    float merger_t = 0.0f;
    float orbit_curve = 0.0f;
    float inspiral_mix = 0.0f;
    float collision_mix = 0.0f;
    float approach = 0.0f;
    float approach_s = 0.0f;
    float collision_pulse = 0.0f;
    float merge_c = 1.0f;

    float ringdown_amp = 0.0f;
    float ringdown_phase = 0.0f;
    int ringdown_active = 0;

    float strength_t = 0.0f;
    float fold_amount = 0.0f;
    float folds = 0.0f;
    float spiral = 0.0f;
    float spin_param = 0.0f;
    float spin_amount = 0.0f;
    float orbit_dir = 1.0f;

    float influence = 0.0f;
    float influence_damp = 0.0f;
    float pole_smooth = 0.0f;

    float core = 0.0f;
    float core2 = 0.0f;

    float lens_strength = 0.0f;
    float drag_strength = 0.0f;
    float drag_local = 0.0f;
    int has_drag = 0;

    float base_sep = 0.0f;
    float safe_min_sep = 0.0f;
    float orbit_sep = 0.0f;
    float orbit_sep3 = 0.0f;
    float orbit_kepler = 0.0f;
    float orbit_rate = 0.0f;
    float orbit_spin_bias = 0.0f;

    float fold_strength = 0.0f;
    float log_factor = 0.0f;

    float ring_r2 = 0.0f;
    float inv_ring_w = 0.0f;
    float redshift_scale = 0.0f;
    float ring_scale = 0.0f;
    float accretion_glint_scale = 0.0f;
    float merger_glow_scale = 0.0f;
    float event_shadow_scale = 0.0f;
    float ringdown_glow_scale = 0.0f;

    float max_disp = 0.0f;
    float max_disp2 = 0.0f;

    float p1x = 0.0f;
    float p1y = 0.0f;
    float p2x = 0.0f;
    float p2y = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;

    int fb = 0;
    int use_fold = 0;
    int base_bilinear_y = 0;
    int tone_mix = 0;

    int i = 0;
    int qy = 0;

    int qrows = 0;
    int qcols = 0;

    float cx = 0.0f;
    float cy = 0.0f;
    float half_x_step = 0.0f;
    float half_y_step = 0.0f;

    w = t->w;
    h = t->h;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    src_y = t->src_y;
    src_u = t->src_u;
    src_v = t->src_v;

    fb_y = t->fb_y;
    fb_u = t->fb_u;
    fb_v = t->fb_v;

    tone_lut = t->tone_lut;

    process_len = frame->len;
    if (process_len <= 0)
        process_len = t->len;
    if (process_len > t->len)
        process_len = t->len;

    rows = process_len / w;
    if (rows > h)
        rows = h;

    if (rows < 2)
        return;

    process_len = rows * w;

    speed      = st_clampi(args[P_SPEED],    -2000, 2000);
    lens       = st_clampi(args[P_LENS],     0, 100);
    folds_i    = st_clampi(args[P_FOLDS],    0, 12);
    spin_i     = st_clampi(args[P_SPIN],     -100, 100);
    spiral_i   = st_clampi(args[P_SPIRAL],   -100, 100);
    feedback_i = st_clampi(args[P_FEEDBACK], 0, 100);
    core_i     = st_clampi(args[P_CORE],     1, 100);
    saliency   = st_clampi(args[P_SALIENCY], 0, 100);
    merger_i   = st_clampi(args[P_MERGER],   0, 300);
    strength_i = st_clampi(args[P_STRENGTH], 0, 100);

    if (lens <= 0) {
        veejay_memcpy(fb_y, Y, process_len);
        veejay_memcpy(fb_u, U, process_len);
        veejay_memcpy(fb_v, V, process_len);
        t->seeded = 1;
        t->frame++;
        return;
    }

    lens_t = (float) lens * 0.01f;
    lens_curve = st_smooth01(lens_t);

    lens_bend = lens_curve / (1.0f + 0.30f * lens_curve);

    speed_abs = st_absf((float) speed);
    speed_amt = st_clampf(speed_abs / 2000.0f, 0.0f, 1.0f);

    merger_t = st_clampf((float) merger_i / 300.0f, 0.0f, 1.0f);
    orbit_curve = st_smooth01(st_clampf(merger_t / 0.72f, 0.0f, 1.0f));
    inspiral_mix = st_smooth01((merger_t - 0.32f) / 0.44f);
    collision_mix = st_smooth01((merger_t - 0.58f) / 0.42f);

    folds = (float) folds_i;

    strength_t = (float) strength_i * 0.01f;
    fold_amount = st_smooth01(strength_t);

    spiral = (float) spiral_i * 0.010f;
    spin_param = (float) spin_i;
    spin_amount = st_absf(spin_param) * 0.01f;

    orbit_dir = (spin_i < 0) ? -1.0f : 1.0f;

    influence_damp = 1.0f - 0.78f * lens_curve;
    influence_damp = st_clampf(influence_damp, 0.18f, 1.0f);
    influence = (float) saliency * 0.01f * influence_damp;

    pole_smooth = 0.028f - 0.022f * lens_curve;
    pole_smooth = st_clampf(pole_smooth, 0.004f, 0.028f);

    core = 0.014f + (float) core_i * 0.0046f;
    core2 = core * core;

    lens_strength = lens_bend * 0.036f;

    drag_strength = lens_bend * spin_param * 0.0025f;
    drag_local = drag_strength * core2;
    has_drag = (drag_local > 0.0000001f || drag_local < -0.0000001f);

    t->merger_phase = st_wrap_2pi(
        t->merger_phase
        + 0.00010f
        + orbit_curve * 0.00090f
        + collision_mix * collision_mix * 0.00650f
    );

    merge_c = st_lut_cos_interp(t, t->merger_phase);

    approach = (1.0f - merge_c) * 0.5f;
    approach_s = st_smooth01(approach);

    collision_pulse = collision_mix * st_smooth01((approach - 0.55f) / 0.45f);

    t->ringdown_amp *= 0.955f;

    if (collision_pulse > t->ringdown_amp)
        t->ringdown_amp = collision_pulse;

    t->ringdown_phase = st_wrap_2pi(
        t->ringdown_phase
        + 0.040f
        + collision_mix * 0.045f
        + orbit_curve * 0.018f
    );

    ringdown_amp = t->ringdown_amp;
    ringdown_phase = t->ringdown_phase;
    ringdown_active = (ringdown_amp > 0.018f);

    safe_min_sep = 0.078f + core * 0.22f;
    safe_min_sep = st_clampf(safe_min_sep, 0.078f, 0.160f);

    base_sep = 0.43f
             - 0.080f * orbit_curve
             - 0.115f * inspiral_mix * approach_s
             - 0.105f * collision_mix * approach_s;

    base_sep = st_clampf(base_sep, safe_min_sep, 0.43f);

    orbit_sep = base_sep * 2.0f + core * 0.70f;
    orbit_sep = st_clampf(orbit_sep, 0.26f, 1.10f);
    orbit_sep3 = orbit_sep * orbit_sep * orbit_sep;
    orbit_kepler = st_fast_rsqrt(orbit_sep3 + 1.0e-6f);

    orbit_spin_bias = 0.72f + 0.58f * spin_amount;

    orbit_rate = orbit_dir
               * orbit_kepler
               * orbit_spin_bias
               * (0.0020f * orbit_curve + 0.0105f * orbit_curve * orbit_curve);

    if (collision_mix > 0.0f)
        orbit_rate *= 1.0f + 1.80f * collision_pulse;

    fold_strength = fold_amount * (0.006f + lens_bend * 0.042f);
    log_factor = folds * (0.50f + lens_bend * 0.90f);
    fold_strength *= 1.0f + collision_pulse * 0.55f;

    use_fold = (folds_i > 0 && fold_strength > 0.00010f);
    base_bilinear_y = (use_fold && lens > 70);

    fb = (feedback_i * 255) / 100;
    fb = (int) ((float) fb * lens_curve);
    fb += (int) (collision_pulse * 18.0f);
    fb = st_clampi(fb, 0, 255);

    tone_mix = (int) (lens_curve * 255.0f + 0.5f);
    tone_mix = st_clampi(tone_mix, 0, 255);

    for (i = 0; i < 256; i++) {
        int g = t->gamma_lut[i];
        tone_lut[i] = st_u8(i + (((g - i) * tone_mix + 127) / 255));
    }

    ring_r2 = core2 * (5.10f - 1.55f * collision_pulse);
    if (ring_r2 < core2 * 2.25f)
        ring_r2 = core2 * 2.25f;

    inv_ring_w = 1.0f / (core2 * (2.70f + collision_pulse * 1.45f) + 1e-6f);

    redshift_scale = core * (28.0f + collision_pulse * 36.0f) * lens_curve;
    ring_scale = (15.0f + collision_pulse * 36.0f) * lens_curve;

    accretion_glint_scale = (4.0f + 20.0f * fold_amount + 9.0f * lens_curve)
                          * (0.30f + 0.70f * speed_amt);

    merger_glow_scale = collision_pulse * (22.0f + 34.0f * lens_curve);
    ringdown_glow_scale = ringdown_amp * (10.0f + 28.0f * lens_curve);

    event_shadow_scale = 10.0f
                       + 30.0f * lens_curve
                       + 34.0f * collision_pulse
                       + 14.0f * ringdown_amp;

    max_disp = 0.58f + core * 0.12f + collision_pulse * 0.055f;
    max_disp = st_clampf(max_disp, 0.54f, 0.74f);
    max_disp2 = max_disp * max_disp;

    t->time  = st_wrap_2pi(t->time  + (float) speed * 0.00105f);
    t->phase = st_wrap_2pi(t->phase + (float) speed * 0.00052f + spin_param * 0.000022f);
    t->orbit = st_wrap_2pi(t->orbit + orbit_rate);

    cx = (float) w * 0.5f;
    cy = (float) rows * 0.5f;

    half_x_step = 1.0f / (float) w;
    half_y_step = 1.0f / (float) rows;

    qcols = (w + 1) >> 1;
    qrows = (rows + 1) >> 1;

    veejay_memcpy(src_y, Y, process_len);
    veejay_memcpy(src_u, U, process_len);
    veejay_memcpy(src_v, V, process_len);

    if (!t->seeded) {
        veejay_memcpy(fb_y, src_y, process_len);
        veejay_memcpy(fb_u, src_u, process_len);
        veejay_memcpy(fb_v, src_v, process_len);
        t->seeded = 1;
    }

    if (saliency > 0) {
        int saliency_mask = (lens > 65) ? 7 : 3;

        if ((t->frame & saliency_mask) == 0)
            st_update_saliency_poles(t, src_y, rows, saliency, pole_smooth);
    }

    p1x = (-base_sep * (1.0f - influence)) + (t->p1_x * influence);
    p1y = ( 0.00f    * (1.0f - influence)) + (t->p1_y * influence);
    p2x = ( base_sep * (1.0f - influence)) + (t->p2_x * influence);
    p2y = ( 0.00f    * (1.0f - influence)) + (t->p2_y * influence);

    {
        float ca;
        float sa;

        float cpx;
        float cpy;

        float r1x;
        float r1y;
        float r2x;
        float r2y;

        st_lut_sincos_interp(t, t->orbit, &sa, &ca);

        cpx = (p1x + p2x) * 0.5f;
        cpy = (p1y + p2y) * 0.5f;

        r1x = p1x - cpx;
        r1y = p1y - cpy;
        r2x = p2x - cpx;
        r2y = p2y - cpy;

        p1x = cpx + r1x * ca - r1y * sa;
        p1y = cpy + r1x * sa + r1y * ca;
        p2x = cpx + r2x * ca - r2y * sa;
        p2y = cpy + r2x * sa + r2y * ca;
    }

    bx = (p1x + p2x) * 0.5f;
    by = (p1y + p2y) * 0.5f;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (qy = 0; qy < qrows; qy++) {
        int y = qy << 1;
        int row = y * w;
        int y_has_1 = (y + 1 < rows);

        float dy = ((float) y - cy) / cy + half_y_step;

        int qx;

        for (qx = 0; qx < qcols; qx++) {
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

            float vx;
            float vy;

            float qmap_x;
            float qmap_y;

            float sx_center;
            float sy_center;

            float sx00;
            float sy00;

            int redshift;
            int ring;
            int caustic_glint;
            int merger_glow;
            int event_shadow;
            int photon_rim;
            int local_fb;
            int use_bilinear_y;
            int ringdown_glow_i;
            int ringdown_shadow_i;

            float well = (inv1 + inv2) * core;
            float local = (well - 0.48f) * 1.35f;
            float local_core;
            float local_ring;
            float radial_phase = (r21 + r22) * 2.15f + local * 1.60f;

            float rd_env = 0.0f;
            float rd_pos = 0.0f;
            float rd_neg = 0.0f;

            if (has_drag) {
                float inv31 = inv21 * inv1;
                float inv32 = inv22 * inv2;

                float tang_x = (-y1 * inv31 - y2 * inv32) * drag_local;
                float tang_y = ( x1 * inv31 + x2 * inv32) * drag_local;

                vx = grav_x + tang_x;
                vy = grav_y + tang_y;
            }
            else {
                vx = grav_x;
                vy = grav_y;
            }

            if (ringdown_active) {
                float brx = dx - bx;
                float bry = dy - by;
                float br2 = brx * brx + bry * bry;

                if (br2 < 0.5486968f) {
                    float br = br2 * st_fast_rsqrt(br2 + 1.0e-6f);

                    rd_env = 1.0f - br * 1.35f;
                    rd_env = st_clampf(rd_env, 0.0f, 1.0f);

                    if (rd_env > 0.001f) {
                        float rd_wave;

                        rd_env = st_smooth01(rd_env);

                        rd_wave = st_lut_sin_interp(t, ringdown_phase - br * 13.5f);
                        rd_pos = (rd_wave * 0.5f + 0.5f) * rd_env * ringdown_amp;
                        rd_neg = ((-rd_wave) * 0.5f + 0.5f) * rd_env * ringdown_amp;
                    }
                }
            }

            local = st_clampf(local, 0.0f, 1.0f);
            local = st_smooth01(local);

            local_core = st_smooth01((well - 0.82f) * 1.85f);
            local_core = st_clampf(local_core, 0.0f, 1.0f);

            local_ring = local * (1.0f - local_core * 0.68f);
            local_ring = st_clampf(local_ring, 0.0f, 1.0f);

            {
                float v2 = vx * vx + vy * vy;

                if (v2 > max_disp2) {
                    float scale = max_disp * st_fast_rsqrt(v2);
                    vx *= scale;
                    vy *= scale;
                }
            }

            qmap_x = dx + vx;
            qmap_y = dy + vy;

            caustic_glint = 0;

            if (use_fold) {
                float rx = qmap_x - bx;
                float ry = qmap_y - by;
                float rr2 = rx * rx + ry * ry + core2;
                float rinv = st_fast_rsqrt(rr2);

                float theta = st_fast_atan2(ry, rx);
                float log_r = 0.5f * st_fast_log(rr2);

                float spatial_phase = theta * folds + log_r * spiral;
                float wave = st_lut_sin_interp(t, spatial_phase);

                float tx = -ry * rinv;
                float ty =  rx * rinv;
                float nx =  rx * rinv;
                float ny =  ry * rinv;

                float amp = fold_strength * wave;

                qmap_x += tx * amp + nx * amp * 0.33f;
                qmap_y += ty * amp + ny * amp * 0.33f;

                qmap_x += rx * rinv * log_r * fold_strength * 0.14f * log_factor;
                qmap_y += ry * rinv * log_r * fold_strength * 0.14f * log_factor;

                if (local_ring > 0.002f) {
                    float light_phase = t->phase + t->time + radial_phase;
                    float light_wave = st_lut_sin_interp(t, light_phase);
                    float light = light_wave * 0.5f + 0.5f;

                    caustic_glint = (int) (light * local_ring * accretion_glint_scale);
                    caustic_glint = st_clampi(caustic_glint, 0, 40);
                }
            }
            else {
                if (local_ring > 0.002f) {
                    float light_phase = t->phase + t->time + radial_phase;
                    float light_wave = st_lut_sin_interp(t, light_phase);
                    float light = light_wave * 0.5f + 0.5f;

                    caustic_glint = (int) (light * local_ring * (2.0f + 9.0f * lens_curve) * speed_amt);
                    caustic_glint = st_clampi(caustic_glint, 0, 12);
                }
            }

            sx_center = qmap_x * cx + cx;
            sy_center = qmap_y * cy + cy;

            sx00 = sx_center - 0.5f;
            sy00 = sy_center - 0.5f;

            redshift = (int) ((inv1 + inv2) * redshift_scale);
            redshift += (int) (collision_pulse * local * 34.0f);
            redshift += (int) (rd_neg * local * 18.0f);
            redshift = st_clampi(redshift, 0, 104);

            merger_glow = (int) (merger_glow_scale * local_ring);
            merger_glow = st_clampi(merger_glow, 0, 58);

            event_shadow = (int) (event_shadow_scale * local_core);
            event_shadow += (int) (collision_pulse * local_core * 34.0f);
            event_shadow = st_clampi(event_shadow, 0, 96);

            ringdown_shadow_i = (int) (rd_neg * local_core * 28.0f);
            ringdown_shadow_i = st_clampi(ringdown_shadow_i, 0, 32);

            event_shadow += ringdown_shadow_i;
            event_shadow = st_clampi(event_shadow, 0, 112);

            photon_rim = (int) (local_ring * (8.0f + 20.0f * lens_curve + 24.0f * collision_pulse));
            photon_rim = st_clampi(photon_rim, 0, 52);

            ringdown_glow_i = (int) (rd_pos * ringdown_glow_scale * (0.35f + 0.65f * local_ring));
            ringdown_glow_i = st_clampi(ringdown_glow_i, 0, 54);

            {
                float bare1 = r21 - core2;
                float bare2 = r22 - core2;

                float d1 = st_absf(bare1 - ring_r2);
                float d2 = st_absf(bare2 - ring_r2);

                float a1 = 1.0f - d1 * inv_ring_w;
                float a2 = 1.0f - d2 * inv_ring_w;

                a1 = st_clampf(a1, 0.0f, 1.0f);
                a2 = st_clampf(a2, 0.0f, 1.0f);

                ring = (int) ((a1 + a2) * ring_scale);
                ring += photon_rim;
                ring += caustic_glint;
                ring += merger_glow;
                ring += ringdown_glow_i;
                ring = st_clampi(ring, 0, 126);
            }

            local_fb = fb + (int) (collision_pulse * local * 82.0f);

            if (ringdown_active)
                local_fb += (int) (rd_env * ringdown_amp * local * 52.0f);

            local_fb = st_clampi(local_fb, 0, 255);

            use_bilinear_y = base_bilinear_y
                           || (local_core > 0.18f)
                           || (collision_pulse > 0.50f)
                           || (rd_env * ringdown_amp > 0.24f);

            if (use_bilinear_y) {
                st_write_2x2_bilinear_y(
                    Y, U, V,
                    src_y, src_u, src_v,
                    fb_y, fb_u, fb_v,
                    tone_lut,
                    i00,
                    x_has_1,
                    y_has_1,
                    sx00,
                    sy00,
                    w,
                    rows,
                    redshift,
                    ring,
                    event_shadow,
                    fb,
                    local_fb
                );
            }
            else {
                st_write_2x2_nearest_fast(
                    Y, U, V,
                    src_y, src_u, src_v,
                    fb_y, fb_u, fb_v,
                    tone_lut,
                    i00,
                    x_has_1,
                    y_has_1,
                    sx00,
                    sy00,
                    w,
                    rows,
                    redshift,
                    ring,
                    event_shadow,
                    fb,
                    local_fb
                );
            }
        }
    }

    t->frame++;
}