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
#include <omp.h>

#if defined(__SSE__) || defined(__SSE2__) || defined(__AVX__)
    #include <immintrin.h>
    #define HAS_HW_OPT 1
#else
    #define HAS_HW_OPT 0
#endif

#define GAMMA_LUT_SIZE 1024
#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define TO_FP(x) ((int32_t)((x) * FP_ONE))
#define FROM_FP(x) ((float)(x) / FP_ONE)
#define FAST_MAX(a,b) ((a) > (b) ? (a) : (b))

typedef struct {
    uint8_t *dstY, *dstU, *dstV;
    int *histY, *histU, *histV;
    uint8_t gamma_lut[GAMMA_LUT_SIZE];
    double time;
    double phase;
    int width, height;
    int n_threads;
    float p1_x, p1_y;
    float p2_x, p2_y;
} box_topomorph_t;

static inline uint8_t clamp_u8(int i) {
    return (uint8_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
}

vj_effect *topomorph_init(int width, int height) {
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    ve->num_params = 13;
    ve->defaults = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int)*ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int)*ve->num_params);

    ve->defaults[0]=10;   ve->defaults[1]=256; ve->defaults[2]=1;
    ve->defaults[3]=0;   ve->defaults[4]=1;   ve->defaults[5]=60;
    ve->defaults[6]=100; ve->defaults[7]=1;   ve->defaults[8]=0;
    ve->defaults[9]=50;  ve->defaults[10]=1;  ve->defaults[11]=50;
    ve->defaults[12] = 0;
    
    ve->limits[0][0]=-100; ve->limits[1][0]=100;
    ve->limits[0][1]=2;    ve->limits[1][1]=500;
    ve->limits[0][2]=1;    ve->limits[1][2]=20;
    ve->limits[0][3]=-100; ve->limits[1][3]=100;
    ve->limits[0][4]=-100; ve->limits[1][4]=100;
    ve->limits[0][5]=0;    ve->limits[1][5]=100;
    ve->limits[0][6]=-300; ve->limits[1][6]=300;
    ve->limits[0][7]=0;    ve->limits[1][7]=1;
    ve->limits[0][8]=0;    ve->limits[1][8]=2;
    ve->limits[0][9]=0;    ve->limits[1][9]=100;
    ve->limits[0][10]=0;   ve->limits[1][10]=1; 
    ve->limits[0][11]=10;  ve->limits[1][11]=80;
    ve->limits[0][12]=0;  ve->limits[1][12]=1;

    ve->description = "Topological Morph";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, 
        "Speed", "Scale Factor", "Branches", "Swirl", "Rot Speed", "Feedback", 
        "Pitch", "High Quality", "Genus", "Saliency Influence", "Geometry", "Shape P", "Mirror");
    return ve;
}

void *topomorph_malloc(int width, int height) {
    box_topomorph_t *t = (box_topomorph_t*) vj_calloc(sizeof(box_topomorph_t));
    if(!t) return NULL;
    t->width = width; t->height = height;
    int size = width * height;
    t->histY = (int*) vj_calloc(sizeof(int) * size * 3); 
    t->histU = t->histY + size;
    t->histV = t->histU + size;
    t->dstY = (uint8_t*) vj_malloc(size * 3);
    t->dstU = t->dstY + size;
    t->dstV = t->dstU + size;
    for(int i = 0; i < GAMMA_LUT_SIZE; i++) {
        float val = (float)i / (GAMMA_LUT_SIZE - 1);
        t->gamma_lut[i] = clamp_u8(powf(val, 0.85f) * 255.0f);
    }
    t->p1_x = -0.5f; t->p1_y = 0.0f;
    t->p2_x = 0.5f;  t->p2_y = 0.0f;
    t->n_threads = vje_advise_num_threads(size);
    return t;
}

static inline int32_t sample_bilinear(const uint8_t *restrict buf, int32_t u_fp, int32_t v_fp, int w, int h) {
    const int32_t u = u_fp & (FP_ONE - 1);
    const int32_t v = v_fp & (FP_ONE - 1);

    const int x = (int32_t)((int64_t)u * (w - 1)) >> FP_SHIFT;
    const int y = (int32_t)((int64_t)v * (h - 1)) >> FP_SHIFT;

    const int32_t fx = ((int64_t)u * (w - 1)) & (FP_ONE - 1);
    const int32_t fy = ((int64_t)v * (h - 1)) & (FP_ONE - 1);

    const int x1 = (x + 1 >= w) ? 0 : x + 1;
    const int y1 = (y + 1 >= h) ? 0 : y + 1;

    const int p00 = buf[y * w + x];
    const int p10 = buf[y * w + x1];
    const int p01 = buf[y1 * w + x];
    const int p11 = buf[y1 * w + x1];

    const int64_t w00 = (int64_t)(FP_ONE - fx) * (FP_ONE - fy);
    const int64_t w10 = (int64_t)fx * (FP_ONE - fy);
    const int64_t w01 = (int64_t)(FP_ONE - fx) * fy;
    const int64_t w11 = (int64_t)fx * fy;

    return (int32_t)((w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11) >> (FP_SHIFT * 2));
}

static inline int32_t sample_bilinear_uv(const uint8_t *restrict buf, int32_t u_fp, int32_t v_fp, int w, int h) {
    const int32_t u = u_fp & (FP_ONE - 1);
    const int32_t v = v_fp & (FP_ONE - 1);

    const int x = (int32_t)((int64_t)u * (w - 1)) >> FP_SHIFT;
    const int y = (int32_t)((int64_t)v * (h - 1)) >> FP_SHIFT;

    const int32_t fx = ((int64_t)u * (w - 1)) & (FP_ONE - 1);
    const int32_t fy = ((int64_t)v * (h - 1)) & (FP_ONE - 1);

    const int x1 = (x + 1 >= w) ? 0 : x + 1;
    const int y1 = (y + 1 >= h) ? 0 : y + 1;

    const int p00 = buf[y * w + x] - 128;
    const int p10 = buf[y * w + x1] - 128;
    const int p01 = buf[y1 * w + x] - 128;
    const int p11 = buf[y1 * w + x1] - 128;

    const int64_t w00 = (int64_t)(FP_ONE - fx) * (FP_ONE - fy);
    const int64_t w10 = (int64_t)fx * (FP_ONE - fy);
    const int64_t w01 = (int64_t)(FP_ONE - fx) * fy;
    const int64_t w11 = (int64_t)fx * fy;

    return (int32_t)((w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11) >> (FP_SHIFT * 2));
}

static inline float fast_rsqrtf_quake(float number)
{
    const float threehalfs = 1.5f;
    union { float f; uint32_t i; } conv = { number };
    conv.i = 0x5f3759df - (conv.i >> 1);
    float y = conv.f;
    y = y * (threehalfs - (0.5f * number * y * y));
    y = y * (threehalfs - (0.5f * number * y * y));
    return number * y;
}

static inline float fast_sqrtf(float x)
{
#if HAS_HW_OPT
    __m128 v = _mm_load_ss(&x);
    v = _mm_sqrt_ss(v);
    return _mm_cvtss_f32(v);
#else
    return x * fast_rsqrtf_quake(x);
#endif
}

static inline float fast_log_opt(float val) {
    union { float f; uint32_t i; } vx = { val };
    float y = (float)(vx.i) * 1.1920928955078125e-7f;
    return (y - 126.94269504f) * 0.69314718f;
}

static inline float fast_absf(float x)
{
    union { float f; uint32_t i; } u = { x };
    u.i &= 0x7FFFFFFF;
    return u.f;
}

static inline float fast_atan2_opt(float y, float x)
{
    const float PI      = 3.141592654f;
    const float PI_2    = 1.570796327f;
    const float EPSILON = 1e-8f;

    float ax = fast_absf(x);
    float ay = fast_absf(y);

    float max = ax > ay ? ax : ay;
    float min = ax > ay ? ay : ax;

    float a = min / (max + EPSILON);
    float s = a * a;

    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;

    if (ay > ax)
        r = PI_2 - r;

    if (x < 0.0f)
        r = PI - r;

    return (y < 0.0f) ? -r : r;
}

static inline float fast_log2f(float x)
{
    union { float f; uint32_t i; } u = { x };
    return (float)u.i * 1.1920928955078125e-7f - 127.0f;
}

static inline float fast_exp2f(float x)
{
    union { uint32_t i; float f; } u;
    u.i = (uint32_t)((x + 127.0f) * 8388608.0f);
    return u.f;
}

static inline float fast_powf_fallback(float a, float b)
{
    return fast_exp2f(b * fast_log2f(a));
}

#if HAS_HW_OPT
static inline float fast_powf_hw(float a, float b)
{
    float loga = fast_log2f(a);
    float x = b * loga;
    return fast_exp2f(x);
}
#endif

static inline float fast_powf(float a, float b)
{
    if (b == 2.0f) return a * a;
    if (b == 3.0f) return a * a * a;
    if (b == 0.5f) {
        return fast_sqrtf(a);
    }
    if (b == -0.5f) {
        return fast_rsqrtf_quake(a);
    }

#if HAS_HW_OPT
    return fast_powf_hw(a, b);
#else
    return fast_powf_fallback(a, b);
#endif
}

static inline float get_radius_smooth_opt(float X, float Y, int p_exponent) {
    float ax = fabsf(X) + 1e-6f;
    float ay = fabsf(Y) + 1e-6f;

    if (p_exponent == 20) return fast_sqrtf(ax*ax + ay*ay);
    if (p_exponent == 10) return ax + ay;
    if (p_exponent >= 80) return fmaxf(ax, ay);

    float p = p_exponent * 0.1f;

    float p_log_x = p * fast_log2f(ax);
    float p_log_y = p * fast_log2f(ay);

    float max_log = fmaxf(p_log_x, p_log_y);
    float sum_exp = fast_exp2f(p_log_x - max_log) + fast_exp2f(p_log_y - max_log);

    return fast_exp2f((max_log + fast_log2f(sum_exp)) / p);
}

static void update_saliency_poles(box_topomorph_t *t, uint8_t *srcY) {
    int w = t->width, h = t->height;
    int64_t sx1=0, sy1=0, sw1=0, sx2=0, sy2=0, sw2=0;
    for(int y=0; y<h; y+=8) {
        for(int x=0; x<w; x+=8) {
            int val = srcY[y*w+x];
            if (val < 64) continue;
            int weight = val*val;
            if (x < w/2) { sx1+=x*weight; sy1+=y*weight; sw1+=weight; }
            else { sx2+=x*weight; sy2+=y*weight; sw2+=weight; }
        }
    }
    float cx = w*0.5f, cy = h*0.5f;
    float tp1x = sw1 ? (sx1/sw1-cx)/cx : -0.5f, tp1y = sw1 ? (sy1/sw1-cy)/cy : 0;
    float tp2x = sw2 ? (sx2/sw2-cx)/cx : 0.5f, tp2y = sw2 ? (sy2/sw2-cy)/cy : 0;
    t->p1_x += (tp1x - t->p1_x)*0.05f; t->p1_y += (tp1y - t->p1_y)*0.05f;
    t->p2_x += (tp2x - t->p2_x)*0.05f; t->p2_y += (tp2y - t->p2_y)*0.05f;
}

static void process_core_no_mirror(box_topomorph_t *t, VJFrame *frame, int *args, int genus) {
    const int w = t->width;
    const int h = t->height;
    const int size = w * h;

    t->time += args[0] * 0.000725f;
    t->phase += args[4] * 0.000725f;

    const float branches = (float)args[2];
    const float swirl    = args[3] * 0.01f;
    const float zoom     = 0.8f + (args[1] / 500.0f) * 12.0f;
    const float factor   = branches / zoom;
    const float inv_2pi  = 0.15915494f;
    const float pitch    = args[6] * 0.01f;
    const float influence = args[9] * 0.01f;

    const int geometry   = args[10];
    const float shape_p  = (float)args[11];
    const int use_high_quality = (args[7] == 1);

    const int32_t fb     = TO_FP(args[5] * 0.01f);
    const int32_t inv_fb = FP_ONE - fb;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float inv_cx = 1.0f / cx;
    const float inv_cy = 1.0f / cy;

    const float p1x = t->p1_x * influence, p1y = t->p1_y * influence;
    const float p2x = t->p2_x * influence, p2y = t->p2_y * influence;
    const float p_avg_x = (p1x + p2x) * 0.5f;
    const float p_avg_y = (p1y + p2y) * 0.5f;

    #pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int y = 0; y < h; y++) {
        const float dy = (y - cy) * inv_cy;
        const int row_offset = y * w;

        for (int x = 0; x < w; x++) {
            const float dx = (x - cx) * inv_cx;
            const int i = row_offset + x;
            float X, Y;

            if (geometry == 0) {
                if (genus == 0) {
                    X = dx - p_avg_x;
                    Y = dy - p_avg_y;
                } else if (genus == 1) {
                    float X1 = dx - p1x, Y1 = dy - p1y;
                    float X2 = dx - p2x, Y2 = dy - p2y;
                    X = X1 * X2 - Y1 * Y2;
                    Y = X1 * Y2 + Y1 * X2;
                } else {
                    float X1 = dx - p1x, Y1 = dy - p1y;
                    float X2 = dx - p2x, Y2 = dy - p2y;
                    float X3 = dx + p_avg_x, Y3 = dy + p_avg_y;
                    float tx = X1 * X2 - Y1 * Y2;
                    float ty = X1 * Y2 + Y1 * X2;
                    X = tx * X3 - ty * Y3;
                    Y = tx * Y3 + ty * X3;
                }
            } else {
                if (genus == 0) {
                    X = dx - p_avg_x;
                    Y = dy - p_avg_y;
                } else if (genus == 1) {
                    float X1 = dx - p1x, Y1 = dy - p1y;
                    float X2 = dx - p2x, Y2 = dy - p2y;
                    X = X1 * X2;
                    Y = Y1 * Y2;
                } else {
                    float X1 = dx - p1x, Y1 = dy - p1y;
                    float X2 = dx - p2x, Y2 = dy - p2y;
                    float X3 = dx + p_avg_x, Y3 = dy + p_avg_y;
                    X = X1 * X2 * X3;
                    Y = Y1 * Y2 * Y3;
                }
            }

            float r = fmaxf(get_radius_smooth_opt(X, Y, (int)shape_p), 1e-6f);
            float theta = fast_atan2_opt(Y, X);
            float log_r = fast_log_opt(r);

            float angle_comp = theta * inv_2pi * branches;
            float log_factor = log_r * factor;
            float v_f = log_factor + angle_comp + t->time;
            float u_f = angle_comp - (log_factor * pitch) + t->phase + (swirl * log_r);

            int32_t u_fp = (int32_t)(u_f * 65536.0f);
            int32_t v_fp = (int32_t)(v_f * 65536.0f);

            int64_t accY, accU, accV;
            if (use_high_quality) {
                accY = (int64_t)sample_bilinear(srcY, u_fp, v_fp, w, h) << FP_SHIFT;
                accU = (int64_t)sample_bilinear_uv(srcU, u_fp, v_fp, w, h) << FP_SHIFT;
                accV = (int64_t)sample_bilinear_uv(srcV, u_fp, v_fp, w, h) << FP_SHIFT;
            } else {
                int32_t um = u_fp & 0xFFFF;
                int32_t vm = v_fp & 0xFFFF;
                int tx = ((um >> 8) * (w - 1)) >> 8;
                int ty = ((vm >> 8) * (h - 1)) >> 8;
                accY = (int64_t)srcY[ty * w + tx] << FP_SHIFT;
                accU = (int64_t)(srcU[ty * w + tx] - 128) << FP_SHIFT;
                accV = (int64_t)(srcV[ty * w + tx] - 128) << FP_SHIFT;
            }

            t->histY[i] = (int32_t)(((accY * inv_fb) + ((int64_t)t->histY[i] * fb) + 32768) >> FP_SHIFT);
            t->histU[i] = (int32_t)(((accU * inv_fb) + ((int64_t)t->histU[i] * fb) + 32768) >> FP_SHIFT);
            t->histV[i] = (int32_t)(((accV * inv_fb) + ((int64_t)t->histV[i] * fb) + 32768) >> FP_SHIFT);

            int y_val = t->histY[i] >> FP_SHIFT;
            int lut_idx = (y_val < 0) ? 0 : (y_val > 255) ? 255 : y_val;
            t->dstY[i] = t->gamma_lut[(lut_idx * 1023) / 255];
            t->dstU[i] = clamp_u8(((t->histU[i] >> FP_SHIFT) * 1056 >> 10) + 128);
            t->dstV[i] = clamp_u8(((t->histV[i] >> FP_SHIFT) * 1056 >> 10) + 128);
        }
    }

    veejay_memcpy(srcY, t->dstY, size);
    veejay_memcpy(srcU, t->dstU, size);
    veejay_memcpy(srcV, t->dstV, size);
}

static void process_core(box_topomorph_t *t, VJFrame *frame, int *args, int genus) {
    const int w = t->width, h = t->height, size = w * h;
    const int half_w = w >> 1, half_h = h >> 1;
    const float inv_hw = 1.0f / (float)half_w;
    const float inv_hh = 1.0f / (float)half_h;

    t->time += args[0] * 0.0005f;
    t->phase += args[4] * 0.0005f;

    const float branches = (float)args[2], swirl = args[3] * 0.01f;
    const float zoom = 0.8f + (args[1] / 500.0f) * 12.0f;
    const float factor = branches / zoom, inv_2pi = 0.15915494f;
    const float pitch = args[6] * 0.01f, influence = args[9] * 0.01f;
    const float rotation = args[4] * 0.01f;

    const int geometry = args[10];
    const float shape_p = (float)args[11];
    const int use_high_quality = (args[7] == 1);

    const int32_t fb = TO_FP(args[5] * 0.01f), inv_fb = FP_ONE - fb;
    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    const float p1x = t->p1_x * influence, p1y = t->p1_y * influence;
    const float p2x = t->p2_x * influence, p2y = t->p2_y * influence;
    const float p_avg_x = (p1x + p2x) * 0.5f;
    const float p_avg_y = (p1y + p2y) * 0.5f;

    #pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int y = 0; y < half_h; y++) {
        const float dy = (float)y * inv_hh;
        const int row_top = (half_h - 1 - y) * w;
        const int row_bot = (half_h + y) * w;

        for (int x = 0; x < half_w; x++) {
            const float dx = (float)x * inv_hw;
            const float d_r = fast_sqrtf(dx*dx + dy*dy + 1e-6f);
            const float d_t = fast_atan2_opt(dy, dx) + rotation;
            const float m_dx = d_r * cosf(d_t);
            const float m_dy = d_r * sinf(d_t);

            float X, Y;

            if (geometry == 0) {
                if (genus == 0) {
                    X = m_dx - p_avg_x;
                    Y = m_dy - p_avg_y;
                } else if (genus == 1) {
                    float X1 = m_dx - p1x, Y1 = m_dy - p1y;
                    float X2 = m_dx - p2x, Y2 = m_dy - p2y;
                    X = X1*X2 - Y1*Y2;
                    Y = X1*Y2 + Y1*X2;
                } else {
                    float X1 = m_dx - p1x, Y1 = m_dy - p1y;
                    float X2 = m_dx - p2x, Y2 = m_dy - p2y;
                    float X3 = m_dx + p_avg_x, Y3 = m_dy + p_avg_y;
                    float tx = X1*X2 - Y1*Y2, ty = X1*Y2 + Y1*X2;
                    X = tx*X3 - ty*Y3;
                    Y = tx*Y3 + ty*X3;
                }
            } else {
                if (genus == 0) {
                    X = m_dx - p_avg_x;
                    Y = m_dy - p_avg_y;
                } else if (genus == 1) {
                    float X1 = m_dx - p1x, Y1 = m_dy - p1y;
                    float X2 = m_dx - p2x, Y2 = m_dy - p2y;
                    X = X1 * X2;
                    Y = Y1 * Y2;
                } else {
                    float X1 = m_dx - p1x, Y1 = m_dy - p1y;
                    float X2 = m_dx - p2x, Y2 = m_dy - p2y;
                    float X3 = m_dx + p_avg_x, Y3 = m_dy + p_avg_y;
                    X = X1 * X2 * X3;
                    Y = Y1 * Y2 * Y3;
                }
            }

            const float mag_sq = X*X + Y*Y + 1e-6f;
            const float scale = fast_powf(mag_sq, -0.1f);
            X *= scale; Y *= scale;

            const float r = get_radius_smooth_opt(X, Y, shape_p);
            const float theta = fast_atan2_opt(Y, X);
            const float log_r = fast_log_opt(r);

            const float angle_part = theta * inv_2pi * branches;
            const float log_factor = log_r * factor;
            const float v_f = log_factor + angle_part + t->time;
            const float u_f = angle_part - (log_factor * pitch) + t->phase + (swirl * log_r);

            const int32_t u_fp = (int32_t)(u_f * 65536.0f);
            const int32_t v_fp = (int32_t)(v_f * 65536.0f);

            int64_t accY, accU, accV;
            if (use_high_quality) {
                accY = (int64_t)sample_bilinear(srcY, u_fp, v_fp, w, h) << FP_SHIFT;
                accU = (int64_t)sample_bilinear_uv(srcU, u_fp, v_fp, w, h) << FP_SHIFT;
                accV = (int64_t)sample_bilinear_uv(srcV, u_fp, v_fp, w, h) << FP_SHIFT;
            } else {
                int32_t um = u_fp & 0xFFFF;
                int32_t vm = v_fp & 0xFFFF;
                int tx = ((um >> 8) * (w - 1)) >> 8;
                int ty = ((vm >> 8) * (h - 1)) >> 8;
                accY = (int64_t)srcY[ty * w + tx] << FP_SHIFT;
                accU = (int64_t)(srcU[ty * w + tx] - 128) << FP_SHIFT;
                accV = (int64_t)(srcV[ty * w + tx] - 128) << FP_SHIFT;
            }

            const int ix = half_w + x;
            const int mx = half_w - 1 - x;
            const int idx[4] = { row_bot + ix, row_bot + mx, row_top + ix, row_top + mx };

            for(int k=0; k<4; k++) {
                const int i = idx[k];

                t->histY[i] = (int32_t)(((accY * inv_fb) + ((int64_t)t->histY[i] * fb) + 32768) >> FP_SHIFT);
                t->histU[i] = (int32_t)(((accU * inv_fb) + ((int64_t)t->histU[i] * fb) + 32768) >> FP_SHIFT);
                t->histV[i] = (int32_t)(((accV * inv_fb) + ((int64_t)t->histV[i] * fb) + 32768) >> FP_SHIFT);

                int y_val = t->histY[i] >> 16;
                int lut_idx = (y_val < 0) ? 0 : (y_val > 255) ? 255 : y_val;
                t->dstY[i] = t->gamma_lut[(lut_idx * 1023) / 255];
                t->dstU[i] = clamp_u8(((t->histU[i] >> 16) * 1056 >> 10) + 128);
                t->dstV[i] = clamp_u8(((t->histV[i] >> 16) * 1056 >> 10) + 128);
            }
        }
    }

    veejay_memcpy(srcY, t->dstY, size);
    veejay_memcpy(srcU, t->dstU, size);
    veejay_memcpy(srcV, t->dstV, size);
}

void topomorph_apply(void *ptr, VJFrame *frame, int *args) {
    update_saliency_poles((box_topomorph_t*)ptr, frame->data[0]);
    if(args[12] == 1) {
        process_core((box_topomorph_t*)ptr, frame, args, args[8]);
    }
    else {
        process_core_no_mirror((box_topomorph_t*)ptr,frame,args,args[8]);
    }
}

void topomorph_free(void *ptr){
    box_topomorph_t *t = (box_topomorph_t*)ptr;
    if (t) { free(t->histY); free(t->dstY); free(t); }
}