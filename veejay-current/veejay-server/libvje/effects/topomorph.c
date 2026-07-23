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
#include <math.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "topomorph.h"

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define TO_FP(x) ((int32_t)((x) * (float)FP_ONE))

#define TOPOMORPH_PARAMS 12

#define P_SPEED       0
#define P_SCALE       1
#define P_BRANCHES    2
#define P_SWIRL       3
#define P_ROT_SPEED   4
#define P_FEEDBACK    5
#define P_PITCH       6
#define P_TOPO_MODE   7
#define P_SALIENCY    8
#define P_SHAPE_P     9
#define P_MIRROR     10
#define P_WARP_DRIVE 11

#define TOPOMORPH_INTERNAL_PARAMS 15
#define P_WARP_ENV   12
#define P_WARP_KICK  13
#define P_WARP_PHASE 14

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
    uint8_t *region;
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
    int warp_phase;
    float eff_speed;
    float eff_scale;
    float eff_swirl;
    float eff_rot_speed;
    float eff_feedback;
    float eff_pitch;
    float eff_saliency;
    float eff_warp_drive;
    int eff_initialized;
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

static inline int clampi(const int v, const int lo, const int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int topo_push_towards(const int v, const int target, const int beat_push, const int strength)
{
    const int q = clampi((beat_push * strength + 500) / 1000, 0, 1000);
    return v + (((target - v) * q + ((target >= v) ? 500 : -500)) / 1000);
}

static inline int topo_push_abs(const int v, const int target_abs, const int beat_push, const int strength)
{
    const int q = clampi((beat_push * strength + 500) / 1000, 0, 1000);
    const int sign = (v < 0) ? -1 : 1;
    const int av = (v < 0) ? -v : v;
    int out = av + (((target_abs - av) * q + ((target_abs >= av) ? 500 : -500)) / 1000);
    if(out < 0)
        out = 0;
    return sign * out;
}


static inline int topo_soft_luma_i(const int y)
{
    if (y > 232) {
        int v = 232 + ((y - 232) >> 2);
        return (v > 244) ? 244 : v;
    }

    return y < 0 ? 0 : y;
}






static inline int topo_smooth_i(float *restrict state, const int target, const float attack, const float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;
    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline void topo_limit_chroma_pair_i(int *restrict u, int *restrict v)
{
    int au = (*u < 0) ? -*u : *u;
    int av = (*v < 0) ? -*v : *v;
    int m = (au > av) ? au : av;

    if (m > 112) {
        int lim = 112 + ((m - 112) >> 2);

        if (lim > 126)
            lim = 126;

        *u = (*u * lim + ((*u >= 0) ? (m >> 1) : -(m >> 1))) / m;
        *v = (*v * lim + ((*v >= 0) ? (m >> 1) : -(m >> 1))) / m;
    }
}

static inline int topo_radius_kind(const int p)
{
    return (p <= 10) ? 0 : ((p == 20) ? 1 : ((p >= 80) ? 2 : 3));
}

static inline int topo_lut_index_wrap(const float angle)
{
    return ((int)(angle * TOPO_TRIG_MUL)) & TOPO_TRIG_LUT_MASK;
}

static inline float topo_lut_sin(const box_topomorph_t *restrict t, const float angle)
{
    return t->sin_lut[topo_lut_index_wrap(angle)];
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
    const int yv = topo_soft_luma_i(clamp_i32_255(t->histY[i] >> FP_SHIFT));
    int uv = t->histU[i] >> FP_SHIFT;
    int vv = t->histV[i] >> FP_SHIFT;
    topo_limit_chroma_pair_i(&uv, &vv);
    t->dstY[i] = t->gamma8_lut[yv];
    t->dstU[i] = clamp_u8_i32(((uv * 1024) >> 10) + 128);
    t->dstV[i] = clamp_u8_i32(((vv * 1024) >> 10) + 128);
}

#define DEFINE_NO_MIRROR_KERNEL(MODE_ID, RADIUS_ID, RADIUS_FN, SAMPLE_ID, SAMPLE_FN, MAP_FN) \
static void topo_nm_m##MODE_ID##_r##RADIUS_ID##_s##SAMPLE_ID(box_topomorph_t *restrict t, VJFrame *restrict frame, const int *restrict args) \
{ \
    const int w = t->width; \
    const int h = t->height; \
    const int size = w * h; \
    t->time += (double)args[P_SPEED] * 0.0000725 + (double)args[P_WARP_ENV] * 0.0000100 + (double)args[P_WARP_KICK] * 0.0000200; \
    t->phase += (double)args[P_ROT_SPEED] * 0.0000725 + (double)args[P_WARP_ENV] * 0.0000075 + (double)args[P_WARP_KICK] * 0.0000140; \
    const float branches = (float)args[P_BRANCHES]; \
    const float swirl = (float)args[P_SWIRL] * 0.001f; \
    const float zoom = 0.8f + ((float)args[P_SCALE] * 0.024f); \
    const float factor = branches / zoom; \
    const float pitch = (float)args[P_PITCH] * 0.001f; \
    const float influence = (float)args[P_SALIENCY] * 0.001f; \
    const int32_t fb = TO_FP((float)args[P_FEEDBACK] * 0.001f); \
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
    (void)args[P_WARP_PHASE]; \
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
    t->time += (double)args[P_SPEED] * 0.00005 + (double)args[P_WARP_ENV] * 0.0000075 + (double)args[P_WARP_KICK] * 0.0000150; \
    t->phase += (double)args[P_ROT_SPEED] * 0.00005 + (double)args[P_WARP_ENV] * 0.0000060 + (double)args[P_WARP_KICK] * 0.0000110; \
    const float branches = (float)args[P_BRANCHES]; \
    const float swirl = (float)args[P_SWIRL] * 0.001f; \
    const float zoom = 0.8f + ((float)args[P_SCALE] * 0.024f); \
    const float factor = branches / zoom; \
    const float pitch = (float)args[P_PITCH] * 0.001f; \
    const float influence = (float)args[P_SALIENCY] * 0.001f; \
    const int32_t fb = TO_FP((float)args[P_FEEDBACK] * 0.001f); \
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
    (void)args[P_WARP_PHASE]; \
    float rs, rc; \
    topo_lut_sincos(t, (float)args[P_ROT_SPEED] * 0.001f, &rs, &rc); \
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

    ve->num_params = TOPOMORPH_PARAMS;
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

    ve->defaults[P_SPEED]       = 100;
    ve->defaults[P_SCALE]       = 256;
    ve->defaults[P_BRANCHES]    = 1;
    ve->defaults[P_SWIRL]       = 0;
    ve->defaults[P_ROT_SPEED]   = 10;
    ve->defaults[P_FEEDBACK]    = 600;
    ve->defaults[P_PITCH]       = 1000;
    ve->defaults[P_TOPO_MODE]   = 3;
    ve->defaults[P_SALIENCY]    = 500;
    ve->defaults[P_SHAPE_P]     = 50;
    ve->defaults[P_MIRROR]      = 0;
    ve->defaults[P_WARP_DRIVE]  = 0;

    ve->limits[0][P_SPEED] = -1000;       ve->limits[1][P_SPEED] = 1000;
    ve->limits[0][P_SCALE] = 2;           ve->limits[1][P_SCALE] = 500;
    ve->limits[0][P_BRANCHES] = 1;        ve->limits[1][P_BRANCHES] = 20;
    ve->limits[0][P_SWIRL] = -1000;       ve->limits[1][P_SWIRL] = 1000;
    ve->limits[0][P_ROT_SPEED] = -1000;   ve->limits[1][P_ROT_SPEED] = 1000;
    ve->limits[0][P_FEEDBACK] = 0;        ve->limits[1][P_FEEDBACK] = 1000;
    ve->limits[0][P_PITCH] = -3000;       ve->limits[1][P_PITCH] = 3000;
    ve->limits[0][P_TOPO_MODE] = 0;       ve->limits[1][P_TOPO_MODE] = 5;
    ve->limits[0][P_SALIENCY] = 0;        ve->limits[1][P_SALIENCY] = 1000;
    ve->limits[0][P_SHAPE_P] = 10;        ve->limits[1][P_SHAPE_P] = 80;
    ve->limits[0][P_MIRROR] = 0;      ve->limits[1][P_MIRROR] = 1;
    ve->limits[0][P_WARP_DRIVE] = 0;  ve->limits[1][P_WARP_DRIVE] = 1000;

    ve->description = "Topological Morph";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params,
        "Speed",
        "Scale Factor",
        "Branches",
        "Swirl",
        "Rot Speed",
        "Feedback",
        "Pitch",
        "Topology Mode",
        "Saliency Influence",
        "Shape P",
        "Mirror",
        "Warp Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_TOPO_MODE],
        P_TOPO_MODE,
        "Complex Genus 0",
        "Complex Genus 1",
        "Complex Genus 2",
        "Box Product Genus 0",
        "Box Product Genus 1",
        "Box Product Genus 2"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MIRROR],
        P_MIRROR,
        "Full Frame",
        "Mirrored"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -420, 420, 90, 100, 0, 320, 0, 2, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 54, 360, 86, 100, 8, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -1000, 1000, 94, 100, 0, 300, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -420, 420, 84, 100, 0, 320, 0, 2, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 300, 900, 72, 96, 240, 1700, 0, 5, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -2400, 2400, 72, 96, 40, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 160, 1000, 90, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 850, 90, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *topomorph_malloc(int width, int height)
{
    box_topomorph_t *t = (box_topomorph_t*) vj_calloc(sizeof(box_topomorph_t));
    if (!t)
        return NULL;

    const int size = width * height;
    const size_t hist_bytes = sizeof(int32_t) * (size_t)size * 3u;
    const size_t dst_bytes = (size_t)size * 3u;
    const size_t total = hist_bytes + dst_bytes + 32u;

    t->region = (uint8_t*) vj_calloc(total);
    if(!t->region) {
        free(t);
        return NULL;
    }

    uint8_t *p = (uint8_t*)(((uintptr_t)t->region + 15u) & ~(uintptr_t)15u);

    t->width = width;
    t->height = height;
    t->n_threads = vje_advise_num_threads(size);

    t->histY = (int32_t*)p;
    t->histU = t->histY + size;
    t->histV = t->histU + size;

    p += hist_bytes;

    t->dstY = p;
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
    t->warp_phase = 0;
    t->eff_speed = 0.0f;
    t->eff_scale = 0.0f;
    t->eff_swirl = 0.0f;
    t->eff_rot_speed = 0.0f;
    t->eff_feedback = 0.0f;
    t->eff_pitch = 0.0f;
    t->eff_saliency = 0.0f;
    t->eff_warp_drive = 0.0f;
    t->eff_initialized = 0;

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
                                   const int *restrict args)
{
    const int mode = clampi(args[P_TOPO_MODE], 0, 5);
    const int radius = topo_radius_kind(args[P_SHAPE_P]);
    const int sampler = 1;
    topo_no_mirror_kernels[mode][radius][sampler](t, frame, args);
}

static void process_core_mirror(box_topomorph_t *restrict t,
                                VJFrame *restrict frame,
                                const int *restrict args)
{
    if ((t->width & 1) || (t->height & 1)) {
        process_core_no_mirror(t, frame, args);
        return;
    }

    const int mode = clampi(args[P_TOPO_MODE], 0, 5);
    const int radius = topo_radius_kind(args[P_SHAPE_P]);
    const int sampler = 1;
    topo_mirror_kernels[mode][radius][sampler](t, frame, args);
}

void topomorph_apply(void *ptr, VJFrame *frame, int *args)
{
    box_topomorph_t *t = (box_topomorph_t*) ptr;
    int eff[TOPOMORPH_INTERNAL_PARAMS];

    eff[P_SPEED]      = args[P_SPEED];
    eff[P_SCALE]      = args[P_SCALE];
    eff[P_BRANCHES]   = args[P_BRANCHES];
    eff[P_SWIRL]      = args[P_SWIRL];
    eff[P_ROT_SPEED]  = args[P_ROT_SPEED];
    eff[P_FEEDBACK]   = args[P_FEEDBACK];
    eff[P_PITCH]      = args[P_PITCH];
    eff[P_TOPO_MODE]  = args[P_TOPO_MODE];
    eff[P_SALIENCY]   = args[P_SALIENCY];
    eff[P_SHAPE_P]    = args[P_SHAPE_P];
    eff[P_MIRROR]     = args[P_MIRROR];
    eff[P_WARP_DRIVE] = args[P_WARP_DRIVE];

    if(!t->eff_initialized) {
        t->eff_speed = (float)eff[P_SPEED];
        t->eff_scale = (float)eff[P_SCALE];
        t->eff_swirl = (float)eff[P_SWIRL];
        t->eff_rot_speed = (float)eff[P_ROT_SPEED];
        t->eff_feedback = (float)eff[P_FEEDBACK];
        t->eff_pitch = (float)eff[P_PITCH];
        t->eff_saliency = (float)eff[P_SALIENCY];
        t->eff_warp_drive = (float)eff[P_WARP_DRIVE];
        t->eff_initialized = 1;
    } else {
        const float warp_s = (float)eff[P_WARP_DRIVE] * 0.001f;
        const float fast = 0.095f + warp_s * 0.060f;
        const float slow = 0.040f + warp_s * 0.040f;

        eff[P_SPEED]      = topo_smooth_i(&t->eff_speed,      eff[P_SPEED],      fast,        slow);
        eff[P_SCALE]      = topo_smooth_i(&t->eff_scale,      eff[P_SCALE],      fast * 0.84f, slow);
        eff[P_SWIRL]      = topo_smooth_i(&t->eff_swirl,      eff[P_SWIRL],      fast,        slow);
        eff[P_ROT_SPEED]  = topo_smooth_i(&t->eff_rot_speed,  eff[P_ROT_SPEED],  fast * 0.84f, slow);
        eff[P_FEEDBACK]   = topo_smooth_i(&t->eff_feedback,   eff[P_FEEDBACK],   fast * 0.42f, slow * 0.78f);
        eff[P_PITCH]      = topo_smooth_i(&t->eff_pitch,      eff[P_PITCH],      fast * 0.76f, slow);
        eff[P_SALIENCY]   = topo_smooth_i(&t->eff_saliency,   eff[P_SALIENCY],   fast * 0.68f, slow);
        eff[P_WARP_DRIVE] = topo_smooth_i(&t->eff_warp_drive, eff[P_WARP_DRIVE], fast * 1.16f, slow);
    }

    {
        const int mode = clampi(eff[P_TOPO_MODE], 0, 5);
        const int complex_mode = (mode <= 2);
        const int warp = eff[P_WARP_DRIVE];

        const int travel_q = clampi((warp * 82 + 50) / 100, 0, 1000);
        const int warp_q = clampi((warp * 92 + 50) / 100, 0, 1000);
        const int pulse_q = warp;

        if(travel_q > 0) {
            const int speed_target = complex_mode ? 820 : 680;
            const int rot_target = complex_mode ? 760 : 620;
            const int pitch_target = complex_mode ? 1600 : 1280;
            const int q = clampi((travel_q * 680 + pulse_q * 320 + 500) / 1000, 0, 1000);

            eff[P_SPEED] = clampi(topo_push_abs(eff[P_SPEED], speed_target, q, 54), -1000, 1000);
            eff[P_ROT_SPEED] = clampi(topo_push_abs(eff[P_ROT_SPEED], rot_target, q, 44), -1000, 1000);
            eff[P_PITCH] = clampi(topo_push_abs(eff[P_PITCH], pitch_target, q, 34), -3000, 3000);
        }

        if(warp_q > 0) {
            const int swirl_target = complex_mode ? 780 : 640;
            const int scale_target = complex_mode ? 118 : 150;
            const int saliency_target = complex_mode ? 930 : 960;
            const int feedback_target = warp > 760 ? 520 : 640;
            const int q = clampi((warp_q * 720 + pulse_q * 280 + 500) / 1000, 0, 1000);

            eff[P_SCALE] = clampi(topo_push_towards(eff[P_SCALE], scale_target, q, 46), 2, 500);
            eff[P_SWIRL] = clampi(topo_push_abs(eff[P_SWIRL], swirl_target, q, 56), -1000, 1000);
            eff[P_FEEDBACK] = clampi(topo_push_towards(eff[P_FEEDBACK], feedback_target, q, 22), 0, 860);
            eff[P_SALIENCY] = clampi(topo_push_towards(eff[P_SALIENCY], saliency_target, q, 38), 0, 1000);
        }

        if(warp > 0) {
            const int dir = (eff[P_SPEED] < 0) ? -1 : 1;

            eff[P_SPEED] += dir * ((warp * 54 + 500) / 1000);
            eff[P_ROT_SPEED] += dir * ((warp * 38 + 500) / 1000);
            eff[P_SCALE] -= (warp * 10 + 500) / 1000;
            eff[P_FEEDBACK] -= (warp * 14 + 500) / 1000;
        }
    }

    eff[P_SPEED]      = clampi(eff[P_SPEED], -1000, 1000);
    eff[P_SCALE]      = clampi(eff[P_SCALE], 2, 500);
    eff[P_SWIRL]      = clampi(eff[P_SWIRL], -1000, 1000);
    eff[P_ROT_SPEED]  = clampi(eff[P_ROT_SPEED], -1000, 1000);
    eff[P_FEEDBACK]   = clampi(eff[P_FEEDBACK], 0, 860);
    eff[P_PITCH]      = clampi(eff[P_PITCH], -3000, 3000);
    eff[P_SALIENCY]   = clampi(eff[P_SALIENCY], 0, 1000);
    eff[P_WARP_DRIVE] = clampi(eff[P_WARP_DRIVE], 0, 1000);

    t->warp_phase = (t->warp_phase + 3 + ((eff[P_WARP_DRIVE] * 20 + 500) / 1000)) & 1023;
    eff[P_WARP_ENV] = eff[P_WARP_DRIVE];
    eff[P_WARP_KICK] = 0;
    eff[P_WARP_PHASE] = t->warp_phase;

    topo_rebuild_shape_lut(t, eff[P_SHAPE_P]);

    if(eff[P_SALIENCY] > 0)
        update_saliency_poles(t, frame->data[0]);

    if(eff[P_MIRROR] == 1)
        process_core_mirror(t, frame, eff);
    else
        process_core_no_mirror(t, frame, eff);
}
void topomorph_free(void *ptr)
{
    box_topomorph_t *t = (box_topomorph_t*) ptr;

    free(t->region);
    free(t);
}
