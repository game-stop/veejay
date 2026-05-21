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
#include <math.h>
#include <stdint.h>
#include <omp.h>

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define TO_FP(x) ((int32_t)((x) * (float)FP_ONE))

#define TOPO_TRIG_LUT_SIZE 4096
#define TOPO_TRIG_LUT_MASK (TOPO_TRIG_LUT_SIZE - 1)
#define TOPO_ATAN_LUT_SIZE 2049
#define TOPO_SQRT_LUT_SIZE 4096
#define TOPO_LOG_LUT_SIZE 4096
#define TOPO_SHAPE_LUT_SIZE 1024
#define TOPO_SCALE_LUT_SIZE 4096

#define TOPO_PI 3.14159265358979323846f
#define TOPO_HALF_PI 1.57079632679489661923f
#define TOPO_TWO_PI 6.28318530717958647692f
#define TOPO_INV_2PI 0.15915494309189533577f
#define TOPO_TRIG_MUL ((float)TOPO_TRIG_LUT_SIZE / TOPO_TWO_PI)

#define TOPO_SQRT_MAX 32.0f
#define TOPO_LOG_MIN 1.0e-6f
#define TOPO_LOG_MAX 32.0f
#define TOPO_SCALE_MIN 1.0e-6f
#define TOPO_SCALE_MAX 32.0f

typedef struct {
    uint8_t *dstY, *dstU, *dstV;
    int32_t *histY, *histU, *histV;
    uint8_t gamma8_lut[256];
    float sin_lut[TOPO_TRIG_LUT_SIZE];
    float cos_lut[TOPO_TRIG_LUT_SIZE];
    float atan_lut[TOPO_ATAN_LUT_SIZE];
    float sqrt_lut[TOPO_SQRT_LUT_SIZE];
    float log_lut[TOPO_LOG_LUT_SIZE];
    float shape_lut[TOPO_SHAPE_LUT_SIZE];
    float mag_scale_lut[TOPO_SCALE_LUT_SIZE];
    double time;
    double phase;
    int width, height;
    int n_threads;
    int cached_shape_p;
    float p1_x, p1_y;
    float p2_x, p2_y;
} box_topomorph_t;

static inline uint8_t clamp_u8_i32(const int v)
{
    return (uint8_t)((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

static inline int clamp_i32_255(const int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

static inline float fast_absf(const float x)
{
    union { float f; uint32_t i; } u = { x };
    u.i &= 0x7fffffffu;
    return u.f;
}

static inline int genus_clamp(const int g)
{
    return (g < 0) ? 0 : ((g > 2) ? 2 : g);
}

static inline int topo_radius_kind(const int p)
{
    return (p <= 10) ? 0 : ((p == 20) ? 1 : ((p >= 80) ? 2 : 3));
}

static inline int topo_lut_index_wrap(const float angle)
{
    return ((int)(angle * TOPO_TRIG_MUL)) & TOPO_TRIG_LUT_MASK;
}

static inline void topo_lut_sincos(const box_topomorph_t *restrict t,
                                   const float angle,
                                   float *restrict s,
                                   float *restrict c)
{
    const int idx = topo_lut_index_wrap(angle);
    *s = t->sin_lut[idx];
    *c = t->cos_lut[idx];
}

static inline float topo_lut_atan2(const box_topomorph_t *restrict t,
                                   const float y,
                                   const float x)
{
    const float ax = fast_absf(x);
    const float ay = fast_absf(y);
    const float hi0 = (ax > ay) ? ax : ay;
    const float lo = (ax > ay) ? ay : ax;
    const float hi = (hi0 > 1.0e-12f) ? hi0 : 1.0e-12f;
    const float pos = (lo / hi) * (float)(TOPO_ATAN_LUT_SIZE - 1);
    int idx = (int)pos;
    idx = (idx >= (TOPO_ATAN_LUT_SIZE - 1)) ? (TOPO_ATAN_LUT_SIZE - 2) : idx;
    const float f = pos - (float)idx;
    float a = t->atan_lut[idx] + (t->atan_lut[idx + 1] - t->atan_lut[idx]) * f;
    a = (ay > ax) ? (TOPO_HALF_PI - a) : a;
    a = (x < 0.0f) ? (TOPO_PI - a) : a;
    return (y < 0.0f) ? -a : a;
}

static inline float topo_lut_sqrt(const box_topomorph_t *restrict t, const float x0)
{
    const float x1 = (x0 < 0.0f) ? 0.0f : x0;
    const float x = (x1 > TOPO_SQRT_MAX) ? TOPO_SQRT_MAX : x1;
    const float pos = x * ((float)(TOPO_SQRT_LUT_SIZE - 1) / TOPO_SQRT_MAX);
    int idx = (int)pos;
    idx = (idx >= (TOPO_SQRT_LUT_SIZE - 1)) ? (TOPO_SQRT_LUT_SIZE - 2) : idx;
    const float f = pos - (float)idx;
    return t->sqrt_lut[idx] + (t->sqrt_lut[idx + 1] - t->sqrt_lut[idx]) * f;
}

static inline float topo_lut_log(const box_topomorph_t *restrict t, const float x0)
{
    const float x1 = (x0 < TOPO_LOG_MIN) ? TOPO_LOG_MIN : x0;
    const float x = (x1 > TOPO_LOG_MAX) ? TOPO_LOG_MAX : x1;
    const float pos = (x - TOPO_LOG_MIN) * ((float)(TOPO_LOG_LUT_SIZE - 1) / (TOPO_LOG_MAX - TOPO_LOG_MIN));
    int idx = (int)pos;
    idx = (idx >= (TOPO_LOG_LUT_SIZE - 1)) ? (TOPO_LOG_LUT_SIZE - 2) : idx;
    const float f = pos - (float)idx;
    return t->log_lut[idx] + (t->log_lut[idx + 1] - t->log_lut[idx]) * f;
}

static inline float topo_lut_mag_scale(const box_topomorph_t *restrict t, const float x0)
{
    const float x1 = (x0 < TOPO_SCALE_MIN) ? TOPO_SCALE_MIN : x0;
    const float x = (x1 > TOPO_SCALE_MAX) ? TOPO_SCALE_MAX : x1;
    const float pos = (x - TOPO_SCALE_MIN) * ((float)(TOPO_SCALE_LUT_SIZE - 1) / (TOPO_SCALE_MAX - TOPO_SCALE_MIN));
    int idx = (int)pos;
    idx = (idx >= (TOPO_SCALE_LUT_SIZE - 1)) ? (TOPO_SCALE_LUT_SIZE - 2) : idx;
    const float f = pos - (float)idx;
    return t->mag_scale_lut[idx] + (t->mag_scale_lut[idx + 1] - t->mag_scale_lut[idx]) * f;
}

static inline float topo_radius_10(const box_topomorph_t *restrict t, const float x, const float y)
{
    (void)t;
    return fast_absf(x) + fast_absf(y) + 2.0e-6f;
}

static inline float topo_radius_20(const box_topomorph_t *restrict t, const float x, const float y)
{
    const float ax = fast_absf(x) + 1.0e-6f;
    const float ay = fast_absf(y) + 1.0e-6f;
    return topo_lut_sqrt(t, ax * ax + ay * ay);
}

static inline float topo_radius_80(const box_topomorph_t *restrict t, const float x, const float y)
{
    (void)t;
    const float ax = fast_absf(x) + 1.0e-6f;
    const float ay = fast_absf(y) + 1.0e-6f;
    return (ax > ay) ? ax : ay;
}

static inline float topo_radius_custom(const box_topomorph_t *restrict t, const float x, const float y)
{
    const float ax = fast_absf(x) + 1.0e-6f;
    const float ay = fast_absf(y) + 1.0e-6f;
    const float hi = (ax > ay) ? ax : ay;
    const float lo = (ax > ay) ? ay : ax;
    const float pos = (lo / hi) * (float)(TOPO_SHAPE_LUT_SIZE - 1);
    int idx = (int)pos;
    idx = (idx >= (TOPO_SHAPE_LUT_SIZE - 1)) ? (TOPO_SHAPE_LUT_SIZE - 2) : idx;
    const float f = pos - (float)idx;
    const float m = t->shape_lut[idx] + (t->shape_lut[idx + 1] - t->shape_lut[idx]) * f;
    return hi * m;
}

static void topo_rebuild_shape_lut(box_topomorph_t *restrict t, const int shape_p)
{
    const int p_exp = (shape_p < 10) ? 10 : ((shape_p > 80) ? 80 : shape_p);

    if (t->cached_shape_p == p_exp)
        return;

    const float p = (float)p_exp * 0.1f;

    for (int i = 0; i < TOPO_SHAPE_LUT_SIZE; i++) {
        const float q = (float)i / (float)(TOPO_SHAPE_LUT_SIZE - 1);
        t->shape_lut[i] = (p_exp == 10) ? (1.0f + q) :
                          ((p_exp == 20) ? sqrtf(1.0f + q * q) :
                          ((p_exp >= 80) ? 1.0f :
                          powf(1.0f + powf(q, p), 1.0f / p)));
    }

    t->cached_shape_p = p_exp;
}

#define MAP_0(sx, sy, X, Y) do { \
    (X) = (sx) - pax; \
    (Y) = (sy) - pay; \
} while (0)

#define MAP_1(sx, sy, X, Y) do { \
    const float x1__ = (sx) - p1x; \
    const float y1__ = (sy) - p1y; \
    const float x2__ = (sx) - p2x; \
    const float y2__ = (sy) - p2y; \
    (X) = x1__ * x2__ - y1__ * y2__; \
    (Y) = x1__ * y2__ + y1__ * x2__; \
} while (0)

#define MAP_2(sx, sy, X, Y) do { \
    const float x1__ = (sx) - p1x; \
    const float y1__ = (sy) - p1y; \
    const float x2__ = (sx) - p2x; \
    const float y2__ = (sy) - p2y; \
    const float x3__ = (sx) + pax; \
    const float y3__ = (sy) + pay; \
    const float tx__ = x1__ * x2__ - y1__ * y2__; \
    const float ty__ = x1__ * y2__ + y1__ * x2__; \
    (X) = tx__ * x3__ - ty__ * y3__; \
    (Y) = tx__ * y3__ + ty__ * x3__; \
} while (0)

#define MAP_3 MAP_0

#define MAP_4(sx, sy, X, Y) do { \
    const float x1__ = (sx) - p1x; \
    const float y1__ = (sy) - p1y; \
    const float x2__ = (sx) - p2x; \
    const float y2__ = (sy) - p2y; \
    (X) = x1__ * x2__; \
    (Y) = y1__ * y2__; \
} while (0)

#define MAP_5(sx, sy, X, Y) do { \
    const float x1__ = (sx) - p1x; \
    const float y1__ = (sy) - p1y; \
    const float x2__ = (sx) - p2x; \
    const float y2__ = (sy) - p2y; \
    const float x3__ = (sx) + pax; \
    const float y3__ = (sy) + pay; \
    (X) = x1__ * x2__ * x3__; \
    (Y) = y1__ * y2__ * y3__; \
} while (0)

static inline void sample_bilinear_yuv(const uint8_t *restrict ybuf,
                                       const uint8_t *restrict ubuf,
                                       const uint8_t *restrict vbuf,
                                       const int32_t u_fp,
                                       const int32_t v_fp,
                                       const int w,
                                       const int h,
                                       int *restrict outY,
                                       int *restrict outU,
                                       int *restrict outV)
{
    const uint32_t uu = ((uint32_t)u_fp) & 0xffffu;
    const uint32_t vv = ((uint32_t)v_fp) & 0xffffu;
    const uint32_t xfp = uu * (uint32_t)(w - 1);
    const uint32_t yfp = vv * (uint32_t)(h - 1);
    const int x = (int)(xfp >> FP_SHIFT);
    const int y = (int)(yfp >> FP_SHIFT);
    const int fx = (int)(xfp & 0xffffu);
    const int fy = (int)(yfp & 0xffffu);
    const int x1 = (x + 1 >= w) ? 0 : (x + 1);
    const int y1 = (y + 1 >= h) ? 0 : (y + 1);
    const int o0 = y * w;
    const int o1 = y1 * w;
    int p00, p10, p01, p11, a, b;

    p00 = ybuf[o0 + x];
    p10 = ybuf[o0 + x1];
    p01 = ybuf[o1 + x];
    p11 = ybuf[o1 + x1];
    a = p00 + (((p10 - p00) * fx + 32768) >> FP_SHIFT);
    b = p01 + (((p11 - p01) * fx + 32768) >> FP_SHIFT);
    *outY = a + (((b - a) * fy + 32768) >> FP_SHIFT);

    p00 = ubuf[o0 + x];
    p10 = ubuf[o0 + x1];
    p01 = ubuf[o1 + x];
    p11 = ubuf[o1 + x1];
    a = p00 + (((p10 - p00) * fx + 32768) >> FP_SHIFT);
    b = p01 + (((p11 - p01) * fx + 32768) >> FP_SHIFT);
    *outU = a + (((b - a) * fy + 32768) >> FP_SHIFT) - 128;

    p00 = vbuf[o0 + x];
    p10 = vbuf[o0 + x1];
    p01 = vbuf[o1 + x];
    p11 = vbuf[o1 + x1];
    a = p00 + (((p10 - p00) * fx + 32768) >> FP_SHIFT);
    b = p01 + (((p11 - p01) * fx + 32768) >> FP_SHIFT);
    *outV = a + (((b - a) * fy + 32768) >> FP_SHIFT) - 128;
}

static inline void sample_nearest_yuv(const uint8_t *restrict ybuf,
                                      const uint8_t *restrict ubuf,
                                      const uint8_t *restrict vbuf,
                                      const int32_t u_fp,
                                      const int32_t v_fp,
                                      const int w,
                                      const int h,
                                      int *restrict outY,
                                      int *restrict outU,
                                      int *restrict outV)
{
    const uint32_t uu = ((uint32_t)u_fp) & 0xffffu;
    const uint32_t vv = ((uint32_t)v_fp) & 0xffffu;
    const int x = (int)(((uu >> 8) * (uint32_t)(w - 1)) >> 8);
    const int y = (int)(((vv >> 8) * (uint32_t)(h - 1)) >> 8);
    const int i = y * w + x;
    *outY = ybuf[i];
    *outU = (int)ubuf[i] - 128;
    *outV = (int)vbuf[i] - 128;
}

static inline void blend_store_pixel(box_topomorph_t *restrict t,
                                     const int i,
                                     const int sy,
                                     const int su,
                                     const int sv,
                                     const int32_t fb,
                                     const int32_t inv_fb)
{
    const int64_t accY = (int64_t)sy * FP_ONE;
    const int64_t accU = (int64_t)su * FP_ONE;
    const int64_t accV = (int64_t)sv * FP_ONE;
    t->histY[i] = (int32_t)(((accY * inv_fb) + ((int64_t)t->histY[i] * fb) + 32768) >> FP_SHIFT);
    t->histU[i] = (int32_t)(((accU * inv_fb) + ((int64_t)t->histU[i] * fb) + 32768) >> FP_SHIFT);
    t->histV[i] = (int32_t)(((accV * inv_fb) + ((int64_t)t->histV[i] * fb) + 32768) >> FP_SHIFT);
    const int yv = clamp_i32_255(t->histY[i] >> FP_SHIFT);
    const int uv = t->histU[i] >> FP_SHIFT;
    const int vv = t->histV[i] >> FP_SHIFT;
    t->dstY[i] = t->gamma8_lut[yv];
    t->dstU[i] = clamp_u8_i32(((uv * 1056) >> 10) + 128);
    t->dstV[i] = clamp_u8_i32(((vv * 1056) >> 10) + 128);
}

#define DEFINE_NO_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, SAMPLE_ID, SAMPLE_FN, MAP_FN) \
static void topo_nm_m##MODE_ID##_r##RADIUS_ID##_s##SAMPLE_ID(box_topomorph_t *restrict t, VJFrame *restrict frame, const int *restrict args) \
{ \
    const int w = t->width; \
    const int h = t->height; \
    const int size = w * h; \
    t->time += (double)args[0] * 0.000725; \
    t->phase += (double)args[4] * 0.000725; \
    const float branches = (float)args[2]; \
    const float swirl = (float)args[3] * 0.01f; \
    const float zoom = 0.8f + ((float)args[1] * 0.024f); \
    const float factor = branches / zoom; \
    const float pitch = (float)args[6] * 0.01f; \
    const float influence = (float)args[9] * 0.01f; \
    const int32_t fb = TO_FP((float)args[5] * 0.01f); \
    const int32_t inv_fb = FP_ONE - fb; \
    const uint8_t *restrict srcY = frame->data[0]; \
    const uint8_t *restrict srcU = frame->data[1]; \
    const uint8_t *restrict srcV = frame->data[2]; \
    uint8_t *restrict outY = frame->data[0]; \
    uint8_t *restrict outU = frame->data[1]; \
    uint8_t *restrict outV = frame->data[2]; \
    const float inv_cx = 2.0f / (float)w; \
    const float inv_cy = 2.0f / (float)h; \
    const float p1x = t->p1_x * influence; \
    const float p1y = t->p1_y * influence; \
    const float p2x = t->p2_x * influence; \
    const float p2y = t->p2_y * influence; \
    const float pax = (p1x + p2x) * 0.5f; \
    const float pay = (p1y + p2y) * 0.5f; \
    (void)pax; \
    (void)pay; \
    const float time_f = (float)t->time; \
    const float phase_f = (float)t->phase; \
    _Pragma("omp parallel for schedule(static) num_threads(t->n_threads)") \
    for (int y = 0; y < h; y++) { \
        const int row = y * w; \
        const float dy = (float)y * inv_cy - 1.0f; \
        for (int x = 0; x < w; x++) { \
            const float dx = (float)x * inv_cx - 1.0f; \
            const int i = row + x; \
            float X, Y; \
            MAP_FN(dx, dy, X, Y); \
            const float r = RADIUS_FN(t, X, Y); \
            const float theta = topo_lut_atan2(t, Y, X); \
            const float log_r = topo_lut_log(t, r); \
            const float angle_part = theta * TOPO_INV_2PI * branches; \
            const float log_factor = log_r * factor; \
            const float vf = log_factor + angle_part + time_f; \
            const float uf = angle_part - (log_factor * pitch) + phase_f + (swirl * log_r); \
            const int32_t u_fp = (int32_t)(uf * (float)FP_ONE); \
            const int32_t v_fp = (int32_t)(vf * (float)FP_ONE); \
            int py, pu, pv; \
            SAMPLE_FN(srcY, srcU, srcV, u_fp, v_fp, w, h, &py, &pu, &pv); \
            blend_store_pixel(t, i, py, pu, pv, fb, inv_fb); \
        } \
    } \
    veejay_memcpy(outY, t->dstY, size); \
    veejay_memcpy(outU, t->dstU, size); \
    veejay_memcpy(outV, t->dstV, size); \
}

#define DEFINE_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, SAMPLE_ID, SAMPLE_FN, MAP_FN) \
static void topo_m_m##MODE_ID##_r##RADIUS_ID##_s##SAMPLE_ID(box_topomorph_t *restrict t, VJFrame *restrict frame, const int *restrict args) \
{ \
    const int w = t->width; \
    const int h = t->height; \
    const int size = w * h; \
    const int half_w = w >> 1; \
    const int half_h = h >> 1; \
    t->time += (double)args[0] * 0.0005; \
    t->phase += (double)args[4] * 0.0005; \
    const float branches = (float)args[2]; \
    const float swirl = (float)args[3] * 0.01f; \
    const float zoom = 0.8f + ((float)args[1] * 0.024f); \
    const float factor = branches / zoom; \
    const float pitch = (float)args[6] * 0.01f; \
    const float influence = (float)args[9] * 0.01f; \
    const int32_t fb = TO_FP((float)args[5] * 0.01f); \
    const int32_t inv_fb = FP_ONE - fb; \
    const uint8_t *restrict srcY = frame->data[0]; \
    const uint8_t *restrict srcU = frame->data[1]; \
    const uint8_t *restrict srcV = frame->data[2]; \
    uint8_t *restrict outY = frame->data[0]; \
    uint8_t *restrict outU = frame->data[1]; \
    uint8_t *restrict outV = frame->data[2]; \
    const float inv_hw = 1.0f / (float)half_w; \
    const float inv_hh = 1.0f / (float)half_h; \
    const float p1x = t->p1_x * influence; \
    const float p1y = t->p1_y * influence; \
    const float p2x = t->p2_x * influence; \
    const float p2y = t->p2_y * influence; \
    const float pax = (p1x + p2x) * 0.5f; \
    const float pay = (p1y + p2y) * 0.5f; \
    (void)pax; \
    (void)pay; \
    const float time_f = (float)t->time; \
    const float phase_f = (float)t->phase; \
    float rs, rc; \
    topo_lut_sincos(t, (float)args[4] * 0.01f, &rs, &rc); \
    _Pragma("omp parallel for schedule(static) num_threads(t->n_threads)") \
    for (int y = 0; y < half_h; y++) { \
        const float dy = (float)y * inv_hh; \
        const int row_top = (half_h - 1 - y) * w; \
        const int row_bot = (half_h + y) * w; \
        for (int x = 0; x < half_w; x++) { \
            const float dx = (float)x * inv_hw; \
            const float sx = dx * rc - dy * rs; \
            const float sy = dx * rs + dy * rc; \
            float X, Y; \
            MAP_FN(sx, sy, X, Y); \
            const float mag_sq = X * X + Y * Y + 1.0e-6f; \
            const float scale = topo_lut_mag_scale(t, mag_sq); \
            X *= scale; \
            Y *= scale; \
            const float r = RADIUS_FN(t, X, Y); \
            const float theta = topo_lut_atan2(t, Y, X); \
            const float log_r = topo_lut_log(t, r); \
            const float angle_part = theta * TOPO_INV_2PI * branches; \
            const float log_factor = log_r * factor; \
            const float vf = log_factor + angle_part + time_f; \
            const float uf = angle_part - (log_factor * pitch) + phase_f + (swirl * log_r); \
            const int32_t u_fp = (int32_t)(uf * (float)FP_ONE); \
            const int32_t v_fp = (int32_t)(vf * (float)FP_ONE); \
            int py, pu, pv; \
            SAMPLE_FN(srcY, srcU, srcV, u_fp, v_fp, w, h, &py, &pu, &pv); \
            const int ix = half_w + x; \
            const int mx = half_w - 1 - x; \
            blend_store_pixel(t, row_bot + ix, py, pu, pv, fb, inv_fb); \
            blend_store_pixel(t, row_bot + mx, py, pu, pv, fb, inv_fb); \
            blend_store_pixel(t, row_top + ix, py, pu, pv, fb, inv_fb); \
            blend_store_pixel(t, row_top + mx, py, pu, pv, fb, inv_fb); \
        } \
    } \
    veejay_memcpy(outY, t->dstY, size); \
    veejay_memcpy(outU, t->dstU, size); \
    veejay_memcpy(outV, t->dstV, size); \
}

#define DEFINE_MODE_RADIUS(MODE_ID, MAP_FN, RADIUS_ID, RADIUS_FN) \
DEFINE_NO_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, 0, sample_nearest_yuv, MAP_FN) \
DEFINE_NO_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, 1, sample_bilinear_yuv, MAP_FN) \
DEFINE_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, 0, sample_nearest_yuv, MAP_FN) \
DEFINE_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, 1, sample_bilinear_yuv, MAP_FN)

#define DEFINE_MODE_ALL_RADIUS(MODE_ID, MAP_FN) \
DEFINE_MODE_RADIUS(MODE_ID, MAP_FN, 0, topo_radius_10) \
DEFINE_MODE_RADIUS(MODE_ID, MAP_FN, 1, topo_radius_20) \
DEFINE_MODE_RADIUS(MODE_ID, MAP_FN, 2, topo_radius_80) \
DEFINE_MODE_RADIUS(MODE_ID, MAP_FN, 3, topo_radius_custom)

DEFINE_MODE_ALL_RADIUS(0, MAP_0)
DEFINE_MODE_ALL_RADIUS(1, MAP_1)
DEFINE_MODE_ALL_RADIUS(2, MAP_2)
DEFINE_MODE_ALL_RADIUS(3, MAP_3)
DEFINE_MODE_ALL_RADIUS(4, MAP_4)
DEFINE_MODE_ALL_RADIUS(5, MAP_5)

typedef void (*topo_kernel_fn)(box_topomorph_t *restrict, VJFrame *restrict, const int *restrict);

static topo_kernel_fn topo_no_mirror_kernels[6][4][2] = {
    {
        { topo_nm_m0_r0_s0, topo_nm_m0_r0_s1 },
        { topo_nm_m0_r1_s0, topo_nm_m0_r1_s1 },
        { topo_nm_m0_r2_s0, topo_nm_m0_r2_s1 },
        { topo_nm_m0_r3_s0, topo_nm_m0_r3_s1 }
    },
    {
        { topo_nm_m1_r0_s0, topo_nm_m1_r0_s1 },
        { topo_nm_m1_r1_s0, topo_nm_m1_r1_s1 },
        { topo_nm_m1_r2_s0, topo_nm_m1_r2_s1 },
        { topo_nm_m1_r3_s0, topo_nm_m1_r3_s1 }
    },
    {
        { topo_nm_m2_r0_s0, topo_nm_m2_r0_s1 },
        { topo_nm_m2_r1_s0, topo_nm_m2_r1_s1 },
        { topo_nm_m2_r2_s0, topo_nm_m2_r2_s1 },
        { topo_nm_m2_r3_s0, topo_nm_m2_r3_s1 }
    },
    {
        { topo_nm_m3_r0_s0, topo_nm_m3_r0_s1 },
        { topo_nm_m3_r1_s0, topo_nm_m3_r1_s1 },
        { topo_nm_m3_r2_s0, topo_nm_m3_r2_s1 },
        { topo_nm_m3_r3_s0, topo_nm_m3_r3_s1 }
    },
    {
        { topo_nm_m4_r0_s0, topo_nm_m4_r0_s1 },
        { topo_nm_m4_r1_s0, topo_nm_m4_r1_s1 },
        { topo_nm_m4_r2_s0, topo_nm_m4_r2_s1 },
        { topo_nm_m4_r3_s0, topo_nm_m4_r3_s1 }
    },
    {
        { topo_nm_m5_r0_s0, topo_nm_m5_r0_s1 },
        { topo_nm_m5_r1_s0, topo_nm_m5_r1_s1 },
        { topo_nm_m5_r2_s0, topo_nm_m5_r2_s1 },
        { topo_nm_m5_r3_s0, topo_nm_m5_r3_s1 }
    }
};

static topo_kernel_fn topo_mirror_kernels[6][4][2] = {
    {
        { topo_m_m0_r0_s0, topo_m_m0_r0_s1 },
        { topo_m_m0_r1_s0, topo_m_m0_r1_s1 },
        { topo_m_m0_r2_s0, topo_m_m0_r2_s1 },
        { topo_m_m0_r3_s0, topo_m_m0_r3_s1 }
    },
    {
        { topo_m_m1_r0_s0, topo_m_m1_r0_s1 },
        { topo_m_m1_r1_s0, topo_m_m1_r1_s1 },
        { topo_m_m1_r2_s0, topo_m_m1_r2_s1 },
        { topo_m_m1_r3_s0, topo_m_m1_r3_s1 }
    },
    {
        { topo_m_m2_r0_s0, topo_m_m2_r0_s1 },
        { topo_m_m2_r1_s0, topo_m_m2_r1_s1 },
        { topo_m_m2_r2_s0, topo_m_m2_r2_s1 },
        { topo_m_m2_r3_s0, topo_m_m2_r3_s1 }
    },
    {
        { topo_m_m3_r0_s0, topo_m_m3_r0_s1 },
        { topo_m_m3_r1_s0, topo_m_m3_r1_s1 },
        { topo_m_m3_r2_s0, topo_m_m3_r2_s1 },
        { topo_m_m3_r3_s0, topo_m_m3_r3_s1 }
    },
    {
        { topo_m_m4_r0_s0, topo_m_m4_r0_s1 },
        { topo_m_m4_r1_s0, topo_m_m4_r1_s1 },
        { topo_m_m4_r2_s0, topo_m_m4_r2_s1 },
        { topo_m_m4_r3_s0, topo_m_m4_r3_s1 }
    },
    {
        { topo_m_m5_r0_s0, topo_m_m5_r0_s1 },
        { topo_m_m5_r1_s0, topo_m_m5_r1_s1 },
        { topo_m_m5_r2_s0, topo_m_m5_r2_s1 },
        { topo_m_m5_r3_s0, topo_m_m5_r3_s1 }
    }
};

vj_effect *topomorph_init(int width, int height)
{
    (void)width;
    (void)height;

    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    if (!ve)
        return NULL;

    ve->num_params = 13;
    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    if (!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if (ve->defaults) free(ve->defaults);
        if (ve->limits[0]) free(ve->limits[0]);
        if (ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[0] = 10;
    ve->defaults[1] = 256;
    ve->defaults[2] = 1;
    ve->defaults[3] = 0;
    ve->defaults[4] = 1;
    ve->defaults[5] = 60;
    ve->defaults[6] = 100;
    ve->defaults[7] = 1;
    ve->defaults[8] = 0;
    ve->defaults[9] = 50;
    ve->defaults[10] = 1;
    ve->defaults[11] = 50;
    ve->defaults[12] = 0;

    ve->limits[0][0] = -100; ve->limits[1][0] = 100;
    ve->limits[0][1] = 2; ve->limits[1][1] = 500;
    ve->limits[0][2] = 1; ve->limits[1][2] = 20;
    ve->limits[0][3] = -100; ve->limits[1][3] = 100;
    ve->limits[0][4] = -100; ve->limits[1][4] = 100;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100;
    ve->limits[0][6] = -300; ve->limits[1][6] = 300;
    ve->limits[0][7] = 0; ve->limits[1][7] = 1;
    ve->limits[0][8] = 0; ve->limits[1][8] = 2;
    ve->limits[0][9] = 0; ve->limits[1][9] = 100;
    ve->limits[0][10] = 0; ve->limits[1][10] = 1;
    ve->limits[0][11] = 10; ve->limits[1][11] = 80;
    ve->limits[0][12] = 0; ve->limits[1][12] = 1;

    ve->description = "Topological Morph";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Speed", "Scale Factor", "Branches", "Swirl", "Rot Speed", "Feedback",
        "Pitch", "High Quality", "Genus", "Saliency Influence", "Geometry", "Shape P", "Mirror");

    return ve;
}

void *topomorph_malloc(int width, int height)
{
    box_topomorph_t *t = (box_topomorph_t*) vj_calloc(sizeof(box_topomorph_t));
    if (!t)
        return NULL;

    const int size = width * height;

    t->width = width;
    t->height = height;
    t->n_threads = vje_advise_num_threads(size);
    t->n_threads = (t->n_threads <= 0) ? 1 : t->n_threads;

    t->histY = (int32_t*) vj_calloc(sizeof(int32_t) * size * 3);
    t->dstY = (uint8_t*) vj_malloc(size * 3);

    if (!t->histY || !t->dstY) {
        if (t->histY) free(t->histY);
        if (t->dstY) free(t->dstY);
        free(t);
        return NULL;
    }

    t->histU = t->histY + size;
    t->histV = t->histU + size;
    t->dstU = t->dstY + size;
    t->dstV = t->dstU + size;

    for (int i = 0; i < 256; i++) {
        const float v = (float)i / 255.0f;
        t->gamma8_lut[i] = clamp_u8_i32((int)(powf(v, 0.85f) * 255.0f + 0.5f));
    }

    for (int i = 0; i < TOPO_TRIG_LUT_SIZE; i++) {
        const float a = TOPO_TWO_PI * (float)i / (float)TOPO_TRIG_LUT_SIZE;
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    for (int i = 0; i < TOPO_ATAN_LUT_SIZE; i++) {
        const float q = (float)i / (float)(TOPO_ATAN_LUT_SIZE - 1);
        t->atan_lut[i] = atanf(q);
    }

    for (int i = 0; i < TOPO_SQRT_LUT_SIZE; i++) {
        const float x = TOPO_SQRT_MAX * (float)i / (float)(TOPO_SQRT_LUT_SIZE - 1);
        t->sqrt_lut[i] = sqrtf(x);
    }

    for (int i = 0; i < TOPO_LOG_LUT_SIZE; i++) {
        const float x = TOPO_LOG_MIN + (TOPO_LOG_MAX - TOPO_LOG_MIN) * (float)i / (float)(TOPO_LOG_LUT_SIZE - 1);
        t->log_lut[i] = logf(x);
    }

    for (int i = 0; i < TOPO_SCALE_LUT_SIZE; i++) {
        const float x = TOPO_SCALE_MIN + (TOPO_SCALE_MAX - TOPO_SCALE_MIN) * (float)i / (float)(TOPO_SCALE_LUT_SIZE - 1);
        t->mag_scale_lut[i] = powf(x, -0.1f);
    }

    t->cached_shape_p = -1;
    topo_rebuild_shape_lut(t, 50);

    t->p1_x = -0.5f;
    t->p1_y = 0.0f;
    t->p2_x = 0.5f;
    t->p2_y = 0.0f;

    return t;
}

static void update_saliency_poles(box_topomorph_t *restrict t, const uint8_t *restrict srcY)
{
    const int w = t->width;
    const int h = t->height;
    const int half = w >> 1;
    int64_t sx1 = 0, sy1 = 0, sw1 = 0;
    int64_t sx2 = 0, sy2 = 0, sw2 = 0;

    for (int y = 0; y < h; y += 8) {
        const int row = y * w;
        for (int x = 0; x < w; x += 8) {
            const int val = srcY[row + x];
            if (val < 64)
                continue;

            const int weight = val * val;

            if (x < half) {
                sx1 += (int64_t)x * weight;
                sy1 += (int64_t)y * weight;
                sw1 += weight;
            } else {
                sx2 += (int64_t)x * weight;
                sy2 += (int64_t)y * weight;
                sw2 += weight;
            }
        }
    }

    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float inv_cx = 1.0f / cx;
    const float inv_cy = 1.0f / cy;
    const float tp1x = sw1 ? (((float)sx1 / (float)sw1) - cx) * inv_cx : -0.5f;
    const float tp1y = sw1 ? (((float)sy1 / (float)sw1) - cy) * inv_cy : 0.0f;
    const float tp2x = sw2 ? (((float)sx2 / (float)sw2) - cx) * inv_cx : 0.5f;
    const float tp2y = sw2 ? (((float)sy2 / (float)sw2) - cy) * inv_cy : 0.0f;

    t->p1_x += (tp1x - t->p1_x) * 0.05f;
    t->p1_y += (tp1y - t->p1_y) * 0.05f;
    t->p2_x += (tp2x - t->p2_x) * 0.05f;
    t->p2_y += (tp2y - t->p2_y) * 0.05f;
}

static void process_core_no_mirror(box_topomorph_t *restrict t,
                                   VJFrame *restrict frame,
                                   const int *restrict args,
                                   const int genus)
{
    const int mode = ((args[10] != 0) ? 3 : 0) + genus_clamp(genus);
    const int radius = topo_radius_kind(args[11]);
    const int sampler = (args[7] != 0) ? 1 : 0;
    topo_no_mirror_kernels[mode][radius][sampler](t, frame, args);
}

static void process_core_mirror(box_topomorph_t *restrict t,
                                VJFrame *restrict frame,
                                const int *restrict args,
                                const int genus)
{
    if ((t->width & 1) || (t->height & 1)) {
        process_core_no_mirror(t, frame, args, genus);
        return;
    }

    const int mode = ((args[10] != 0) ? 3 : 0) + genus_clamp(genus);
    const int radius = topo_radius_kind(args[11]);
    const int sampler = (args[7] != 0) ? 1 : 0;
    topo_mirror_kernels[mode][radius][sampler](t, frame, args);
}

void topomorph_apply(void *ptr, VJFrame *frame, int *args)
{
    box_topomorph_t *t = (box_topomorph_t*) ptr;
    if (!t || !frame || !args)
        return;

    topo_rebuild_shape_lut(t, args[11]);
    update_saliency_poles(t, frame->data[0]);

    if (args[12] == 1)
        process_core_mirror(t, frame, args, args[8]);
    else
        process_core_no_mirror(t, frame, args, args[8]);
}

void topomorph_free(void *ptr)
{
    box_topomorph_t *t = (box_topomorph_t*) ptr;
    if (t) {
        if (t->histY)
            free(t->histY);
        if (t->dstY)
            free(t->dstY);
        free(t);
    }
}
