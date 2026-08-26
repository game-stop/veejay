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
#include "hyperfluid.h"

#define HYPERFLUID_PARAMS 10
#define HF_TRIG_LUT_SIZE 1024
#define HF_TRIG_LUT_MASK 1023
#define HF_TWO_PI 6.28318530718f
#define HF_INV_TWO_PI 0.15915494309f
#define HF_LUT_SCALE 162.974661726f

#define P_BAND       0
#define P_WIDTH      1
#define P_SCALE      2
#define P_SPLIT      3
#define P_ORDER      4
#define P_SWELL      5
#define P_FLOW       6
#define P_GLOW       7
#define P_CHROMA     8
#define P_SPEED      9

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;
    void *region;
    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;
    uint8_t *blur1;
    uint8_t *blur2;
    uint8_t *blur3;
    uint8_t *tmp;
    uint8_t *activity;
    uint8_t *envelope;
    float sin_lut[HF_TRIG_LUT_SIZE];
    float cos_lut[HF_TRIG_LUT_SIZE];
    float phase;
} hyperfluid_t;

typedef struct {
    float w0, w1, w2;
    float inv_width;
    float edge_norm;
    float order_t_coeff;
    float order_a_coeff;
    float swell_px;
    float flow_px;
    float glow_t;
    float chroma_t;
    float swell_t;
    float flow_t;
    float contour_x_step;
    float contour_y_step;
    float hue_x_step;
    float hue_y_step;
    int ssm;
    int uv_w;
} hf_render_consts_t;

static inline int hf_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float hf_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float hf_smooth01(float x)
{
    x = hf_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static inline float hf_absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float hf_fast_rsqrt(float x)
{
    union { float f; unsigned int i; } u;
    float y;

    u.f = x;
    u.i = 0x5f3759dfU - (u.i >> 1);
    y = u.f;
    return y * (1.5f - 0.5f * x * y * y);
}

static inline float hf_wrap_2pi(float v)
{
    if (v >= HF_TWO_PI || v < 0.0f) {
        int k = (int) (v * HF_INV_TWO_PI);
        v -= (float) k * HF_TWO_PI;
        if (v < 0.0f)
            v += HF_TWO_PI;
        else if (v >= HF_TWO_PI)
            v -= HF_TWO_PI;
    }
    return v;
}

static inline float hf_lut_sin(const hyperfluid_t *t, float phase)
{
    float fidx = phase * HF_LUT_SCALE + 1048576.0f;
    int i0 = (int) fidx;
    float frac = fidx - (float) i0;
    int idx = i0 & HF_TRIG_LUT_MASK;
    int idx1 = (idx + 1) & HF_TRIG_LUT_MASK;
    return t->sin_lut[idx] + (t->sin_lut[idx1] - t->sin_lut[idx]) * frac;
}

static inline float hf_lut_cos(const hyperfluid_t *t, float phase)
{
    float fidx = phase * HF_LUT_SCALE + 1048576.0f;
    int i0 = (int) fidx;
    float frac = fidx - (float) i0;
    int idx = i0 & HF_TRIG_LUT_MASK;
    int idx1 = (idx + 1) & HF_TRIG_LUT_MASK;
    return t->cos_lut[idx] + (t->cos_lut[idx1] - t->cos_lut[idx]) * frac;
}

static inline float hf_poly_curve(float x, int order)
{
    float p = x;
    x = hf_clampf(x, 0.0f, 1.0f);
    p = x;
    for (int k = 1; k < order; k++)
        p *= x;

    return p * ((float) (order + 1) - (float) order * x);
}

static inline float hf_time_step(int speed)
{
    if (speed == 0)
        return 0.0f;
    float u = hf_clampf(hf_absf((float) speed) * 0.001f, 0.0f, 1.0f);
    float mag = 0.00022f * (exp2f(11.0f * u) - 1.0f);
    return speed < 0 ? -mag : mag;
}

static void hf_box_blur_horizontal(
    hyperfluid_t *t,
    const uint8_t *restrict src,
    uint8_t *restrict dst,
    int radius
) {
    const int w = t->w;
    const int h = t->h;
    const int window = radius * 2 + 1;
    const int recip = (65536 + window / 2) / window;

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + y * w;
        uint8_t *out = dst + y * w;
        int sum = row[0] * (radius + 1);

        for (int x = 1; x <= radius; x++)
            sum += row[x < w ? x : w - 1];

        for (int x = 0; x < w; x++) {
            int subx = x - radius; 
            int addx = x + radius + 1;
            out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
            
            subx = subx > 0 ? subx : 0;
            addx = addx < w ? addx : w - 1;
            sum += row[addx] - row[subx];
        }
    }
}

static void hf_box_blur3(
    hyperfluid_t *t,
    const uint8_t *restrict src,
    uint8_t *restrict dst1,
    uint8_t *restrict dst2,
    uint8_t *restrict dst3,
    int r1,
    int r2,
    int r3
) {
    const int w = t->w;
    const int h = t->h;
    const int win1 = r1 * 2 + 1;
    const int win2 = r2 * 2 + 1;
    const int win3 = r3 * 2 + 1;
    const int recip1 = (65536 + win1 / 2) / win1;
    const int recip2 = (65536 + win2 / 2) / win2;
    const int recip3 = (65536 + win3 / 2) / win3;
    uint8_t *restrict tmp1 = t->tmp;
    uint8_t *restrict tmp2 = t->activity;
    uint8_t *restrict tmp3 = t->envelope;

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + y * w;
        uint8_t *out1 = tmp1 + y * w;
        uint8_t *out2 = tmp2 + y * w;
        uint8_t *out3 = tmp3 + y * w;
        int sum1 = row[0] * (r1 + 1);
        int sum2 = row[0] * (r2 + 1);
        int sum3 = row[0] * (r3 + 1);

        for (int x = 1; x <= r3; x++) {
            int v = row[x < w ? x : w - 1];
            if (x <= r1) sum1 += v;
            if (x <= r2) sum2 += v;
            sum3 += v;
        }

        for (int x = 0; x < w; x++) {
            int sub1 = x - r1, add1 = x + r1 + 1;
            int sub2 = x - r2, add2 = x + r2 + 1;
            int sub3 = x - r3, add3 = x + r3 + 1;

            out1[x] = (uint8_t) ((sum1 * recip1 + 32768) >> 16);
            out2[x] = (uint8_t) ((sum2 * recip2 + 32768) >> 16);
            out3[x] = (uint8_t) ((sum3 * recip3 + 32768) >> 16);

            sub1 = sub1 > 0 ? sub1 : 0; add1 = add1 < w ? add1 : w - 1;
            sub2 = sub2 > 0 ? sub2 : 0; add2 = add2 < w ? add2 : w - 1;
            sub3 = sub3 > 0 ? sub3 : 0; add3 = add3 < w ? add3 : w - 1;

            sum1 += row[add1] - row[sub1];
            sum2 += row[add2] - row[sub2];
            sum3 += row[add3] - row[sub3];
        }
    }

#pragma omp for schedule(static)
    for (int x = 0; x < w; x++) {
        int sum1 = tmp1[x] * (r1 + 1);
        int sum2 = tmp2[x] * (r2 + 1);
        int sum3 = tmp3[x] * (r3 + 1);

        for (int yy = 1; yy <= r3; yy++) {
            int sy = yy < h ? yy : h - 1;
            if (yy <= r1) sum1 += tmp1[sy * w + x];
            if (yy <= r2) sum2 += tmp2[sy * w + x];
            sum3 += tmp3[sy * w + x];
        }

        for (int yy = 0; yy < h; yy++) {
            int sub1 = yy - r1, add1 = yy + r1 + 1;
            int sub2 = yy - r2, add2 = yy + r2 + 1;
            int sub3 = yy - r3, add3 = yy + r3 + 1;

            dst1[yy * w + x] = (uint8_t) ((sum1 * recip1 + 32768) >> 16);
            dst2[yy * w + x] = (uint8_t) ((sum2 * recip2 + 32768) >> 16);
            dst3[yy * w + x] = (uint8_t) ((sum3 * recip3 + 32768) >> 16);

            sub1 = sub1 > 0 ? sub1 : 0; add1 = add1 < h ? add1 : h - 1;
            sub2 = sub2 > 0 ? sub2 : 0; add2 = add2 < h ? add2 : h - 1;
            sub3 = sub3 > 0 ? sub3 : 0; add3 = add3 < h ? add3 : h - 1;

            sum1 += tmp1[add1 * w + x] - tmp1[sub1 * w + x];
            sum2 += tmp2[add2 * w + x] - tmp2[sub2 * w + x];
            sum3 += tmp3[add3 * w + x] - tmp3[sub3 * w + x];
        }
    }
}

static inline void hf_sample_bilinear_y(
    const uint8_t *restrict Y,
    const uint8_t *restrict U,
    const uint8_t *restrict V,
    float fx,
    float fy,
    int w,
    int h,
    int *oy,
    int *ou,
    int *ov,
    const hf_render_consts_t *rc
) {
    int x0 = hf_clampi((int) fx, 0, w - 1);
    int y0 = hf_clampi((int) fy, 0, h - 1);
    int x1 = hf_clampi(x0 + 1, 0, w - 1);
    int y1 = hf_clampi(y0 + 1, 0, h - 1);

    int wx = (int) ((fx - (float) x0) * 256.0f);
    int wy = (int) ((fy - (float) y0) * 256.0f);

    int p00 = Y[y0 * w + x0];
    int p10 = Y[y0 * w + x1];
    int p01 = Y[y1 * w + x0];
    int p11 = Y[y1 * w + x1];

    int a = p00 * (256 - wx) + p10 * wx;
    int b = p01 * (256 - wx) + p11 * wx;
    *oy = ((a * (256 - wy) + b * wy) + 32768) >> 16;

    int ni_uv = rc->ssm ? (y0 * w + x0) : (y0 * rc->uv_w + (x0 >> 1));
    *ou = U[ni_uv];
    *ov = V[ni_uv];
}

static inline int hf_nearest_index(float fx, float fy, int w, int h)
{
    int x = hf_clampi((int) (fx + 0.5f), 0, w - 1);
    int y = hf_clampi((int) (fy + 0.5f), 0, h - 1);
    return y * w + x;
}

static inline void hf_rgb_to_yuv(float r, float g, float b, int *y, int *u, int *v)
{
    int yy = (int) (0.299000f * r + 0.587000f * g + 0.114000f * b + 0.5f);
    int uu = (int) (128.0f - 0.168736f * r - 0.331264f * g + 0.500000f * b + 0.5f);
    int vv = (int) (128.0f + 0.500000f * r - 0.418688f * g - 0.081312f * b + 0.5f);

    *y = hf_clampi(yy, 0, 255);
    *u = hf_clampi(uu, 0, 255);
    *v = hf_clampi(vv, 0, 255);
}

static inline void hf_spectral_uv(float phase, float *u, float *v)
{
    float h = hf_wrap_2pi(phase) * HF_INV_TWO_PI;
    float h6 = h * 6.0f;
    float r = hf_clampf(hf_absf(h6 - 3.0f) - 1.0f, 0.0f, 1.0f);
    float g = hf_clampf(2.0f - hf_absf(h6 - 2.0f), 0.0f, 1.0f);
    float b = hf_clampf(2.0f - hf_absf(h6 - 4.0f), 0.0f, 1.0f);

    *u = (-0.168736f * r - 0.331264f * g + 0.500000f * b) * 2.0f;
    *v = ( 0.500000f * r - 0.418688f * g - 0.081312f * b) * 2.0f;
}

vj_effect *hyperfluid_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve)
        return NULL;

    ve->num_params = HYPERFLUID_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Hyper-Fluid Iridescence";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_BAND]   = 190;
    ve->defaults[P_WIDTH]  = 94;
    ve->defaults[P_SCALE]  = 4;
    ve->defaults[P_SPLIT]  = 18;
    ve->defaults[P_ORDER]  = 3;
    ve->defaults[P_SWELL]  = 36;
    ve->defaults[P_FLOW]   = 22;
    ve->defaults[P_GLOW]   = 44;
    ve->defaults[P_CHROMA] = 76;
    ve->defaults[P_SPEED]  = 120;

    ve->limits[0][P_BAND]   = 0;
    ve->limits[1][P_BAND]   = 255;
    ve->limits[0][P_WIDTH]  = 4;
    ve->limits[1][P_WIDTH]  = 192;
    ve->limits[0][P_SCALE]  = 1;
    ve->limits[1][P_SCALE]  = 6;
    ve->limits[0][P_SPLIT]  = -100;
    ve->limits[1][P_SPLIT]  = 100;
    ve->limits[0][P_ORDER]  = 2;
    ve->limits[1][P_ORDER]  = 7;
    ve->limits[0][P_SWELL]  = 0;
    ve->limits[1][P_SWELL]  = 100;
    ve->limits[0][P_FLOW]   = -100;
    ve->limits[1][P_FLOW]   = 100;
    ve->limits[0][P_GLOW]   = 0;
    ve->limits[1][P_GLOW]   = 100;
    ve->limits[0][P_CHROMA] = 0;
    ve->limits[1][P_CHROMA] = 100;
    ve->limits[0][P_SPEED]  = -1000;
    ve->limits[1][P_SPEED]  = 1000;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Luma Band",
        "Band Width",
        "Micro Scale",
        "Scale Split",
        "Curve Order",
        "Boundary Swell",
        "Liquid Flow",
        "Luminescence",
        "Iridescence",
        "Flow Speed"
    );

    return ve;
}

void *hyperfluid_malloc(int w, int h)
{
    hyperfluid_t *t = (hyperfluid_t *) vj_calloc(sizeof(hyperfluid_t));
    if (!t) return NULL;

    size_t len = (size_t) w * (size_t) h;
    size_t total = len * 9;
    size_t off = 0;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    uint8_t *base = (uint8_t *) t->region;
    t->src_y = base + off; off += len;
    t->src_u = base + off; off += len;
    t->src_v = base + off; off += len;
    t->blur1 = base + off; off += len;
    t->blur2 = base + off; off += len;
    t->blur3 = base + off; off += len;
    t->tmp = base + off; off += len;
    t->activity = base + off; off += len;
    t->envelope = base + off;

    for (int i = 0; i < HF_TRIG_LUT_SIZE; i++) {
        float a = HF_TWO_PI * (float) i / (float) HF_TRIG_LUT_SIZE;
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    t->phase = 0.0f;
    return t;
}

void hyperfluid_free(void *ptr)
{
    hyperfluid_t *t = (hyperfluid_t *) ptr;

    if (!t)
        return;

    if (t->region)
        free(t->region);
    free(t);
}

void hyperfluid_apply(void *ptr, VJFrame *frame, int *args)
{
    hyperfluid_t *t = (hyperfluid_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    uint8_t *restrict b1 = t->blur1;
    uint8_t *restrict b2 = t->blur2;
    uint8_t *restrict b3 = t->blur3;
    uint8_t *restrict activity = t->activity;
    uint8_t *restrict envelope = t->envelope;
    
    const int w = t->w;
    const int h = t->h;
    const int len = t->len;
    const int actual_uv_len = frame->ssm ? len : frame->uv_len;
    const int band = args[P_BAND];
    const int width = args[P_WIDTH];
    const int scale = args[P_SCALE];
    const int split = args[P_SPLIT];
    const int order = args[P_ORDER];

    #pragma omp single
    {
        veejay_memcpy(src_y, Y, (size_t)len);
        veejay_memcpy(src_u, U, (size_t)actual_uv_len);
        veejay_memcpy(src_v, V, (size_t)actual_uv_len);
    }
    
    hf_render_consts_t rc;
    rc.swell_t = (float) args[P_SWELL] * 0.01f;
    rc.flow_t = (float) args[P_FLOW] * 0.01f;
    rc.glow_t = (float) args[P_GLOW] * 0.01f;
    rc.chroma_t = (float) args[P_CHROMA] * 0.01f;
    
    float min_dim = (float) (w < h ? w : h);
    float split_t = ((float) split + 100.0f) * 0.005f;
    float im = 1.0f - split_t;
    rc.w0 = im * im;
    rc.w2 = split_t * split_t;
    rc.w1 = 1.0f - rc.w0 - rc.w2;
    
    rc.edge_norm = 1.0f / (10.5f + (float) width * 0.115f);
    rc.swell_px = min_dim * 0.082f * rc.swell_t * rc.swell_t;
    rc.flow_px = min_dim * 0.058f * rc.flow_t * hf_absf(rc.flow_t);
    rc.order_t_coeff = (float) (order - 2) * 0.20f;
    rc.order_a_coeff = 1.08f + rc.order_t_coeff * 0.08f;
    rc.inv_width = 1.0f / (float) width;
    
    rc.contour_x_step = 0.0024f + 0.00025f * (float) scale;
    rc.contour_y_step = 0.0017f + 0.00018f * (float) scale;
    rc.hue_x_step = HF_TWO_PI * 1.35f / (float) w;
    rc.hue_y_step = HF_TWO_PI * 0.65f / (float) h;
    rc.ssm = frame->ssm;
    rc.uv_w = frame->ssm ? w : w / 2;
    
    const int r1 = scale;
    const int r2 = scale * 3 + 1;
    const int r3 = scale * 9 + 2;
    const int envelope_radius = 2 + scale * 2 + width / 64;

    hf_box_blur3(t, src_y, b1, b2, b3, r1, r2, r3);

    #pragma omp for schedule(static)
    for (int i = 0; i < len; i++) {
        float d0 = (float) src_y[i] - (float) b1[i];
        float d1 = ((float) b1[i] - (float) b2[i]) * 1.30f;
        float d2 = ((float) b2[i] - (float) b3[i]) * 1.65f;
        float detail = d0 * rc.w0 + d1 * rc.w1 + d2 * rc.w2;
        float lum = ((float) src_y[i] + (float) b3[i]) * 0.5f;
        
        float gate = 1.0f - hf_absf(lum - (float) band) * rc.inv_width;
        float x = hf_absf(detail) * rc.edge_norm;
        float support = hf_absf(d1) * 0.030f + hf_absf(d2) * 0.020f;

        gate = hf_smooth01(gate);
        support = hf_smooth01(hf_clampf(support, 0.0f, 1.0f));
        x = hf_clampf(x * (1.0f + rc.order_t_coeff * 0.05f), 0.0f, 1.0f);
        
        float a = hf_poly_curve(x, order);
        a = hf_smooth01(a * rc.order_a_coeff) * gate;
        a *= 0.36f + 0.64f * support;
        
        activity[i] = (uint8_t) hf_clampi((int) (a * 255.0f + 0.5f), 0, 255);
    }

    hf_box_blur_horizontal(t, activity, envelope, envelope_radius);

    #pragma omp single
    {
        t->phase = hf_wrap_2pi(t->phase + hf_time_step(args[P_SPEED]));
    }
    
    const float phase = t->phase;

    #pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int ym = y > 0 ? y - 1 : 0;
        int yp = y + 1 < h ? y + 1 : h - 1;
        int ev1 = envelope_radius > 1 ? envelope_radius >> 1 : 1;
        int ev2 = envelope_radius;
        int ey1m = y > ev1 ? y - ev1 : 0;
        int ey1p = y + ev1 < h ? y + ev1 : h - 1;
        int ey2m = y > ev2 ? y - ev2 : 0;
        int ey2p = y + ev2 < h ? y + ev2 : h - 1;
        
        int row = y * w;
        int rowm = ym * w;
        int rowp = yp * w;
        int erow1m = ey1m * w;
        int erow1p = ey1p * w;
        int erow2m = ey2m * w;
        int erow2p = ey2p * w;

        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0;
            int xp = x + 1 < w ? x + 1 : w - 1;
            int idx = row + x;
            
            float core = (float) activity[idx] * 0.00392156862f; // 1.0f / 255.0f
            int env_sum = (int) envelope[idx] * 4
                        + (int) envelope[erow1m + x] * 2
                        + (int) envelope[erow1p + x] * 2
                        + (int) envelope[erow2m + x]
                        + (int) envelope[erow2p + x];
                        
            float membrane = hf_smooth01(hf_clampf((float) env_sum * 0.00116279069f, 0.0f, 1.0f)); // 1.0f / 860.0f
            float rim = hf_clampf(membrane - core * 0.36f, 0.0f, 1.0f);
            float spread = hf_clampf(core * 0.24f + membrane * 0.96f, 0.0f, 1.0f);
            float deform = hf_clampf(core * 0.30f + rim * 0.96f, 0.0f, 1.0f);

            if (spread < 0.012f) {
                Y[idx] = src_y[idx];
                
                int uv_idx = rc.ssm ? idx : (y * rc.uv_w + (x >> 1));
                U[uv_idx] = src_u[uv_idx];
                V[uv_idx] = src_v[uv_idx];
                continue;
            }

            float d0 = (float) src_y[idx] - (float) b1[idx];
            float d1 = ((float) b1[idx] - (float) b2[idx]) * 1.30f;
            float d2 = ((float) b2[idx] - (float) b3[idx]) * 1.65f;
            float detail = d0 * rc.w0 + d1 * rc.w1 + d2 * rc.w2;
            
            float gx1 = (float) b1[row + xp] - (float) b1[row + xm];
            float gy1 = (float) b1[rowp + x] - (float) b1[rowm + x];
            float gx2 = (float) b2[row + xp] - (float) b2[row + xm];
            float gy2 = (float) b2[rowp + x] - (float) b2[rowm + x];
            float gx3 = (float) b3[row + xp] - (float) b3[row + xm];
            float gy3 = (float) b3[rowp + x] - (float) b3[rowm + x];
            
            float gx = gx1 * rc.w0 + gx2 * rc.w1 + gx3 * rc.w2;
            float gy = gy1 * rc.w0 + gy2 * rc.w1 + gy3 * rc.w2;
            float g2 = gx * gx + gy * gy + 1.0e-5f;
            float rinv = hf_fast_rsqrt(g2);
            float nx = gx * rinv;
            float ny = gy * rinv;
            float tx = -ny;
            float ty = nx;
            float sign = detail < 0.0f ? -1.0f : 1.0f;
            
            float contour_phase = phase
                                + (float) x * rc.contour_x_step
                                + (float) y * rc.contour_y_step
                                + (float) b3[idx] * 0.0105f
                                + nx * 0.72f + ny * 0.48f;
                                
            float wave = hf_lut_sin(t, contour_phase);
            float wave_q = hf_lut_cos(t, contour_phase * 0.61f + 1.37f);
            float normal = sign * rc.swell_px * deform * (0.80f + 0.20f * wave);
            float tangent = rc.flow_px * deform * (0.58f + 0.42f * wave_q);
            float sx = (float) x - nx * normal - tx * tangent;
            float sy = (float) y - ny * normal - ty * tangent;
            
            float control_mix = hf_clampf(0.22f + rc.swell_t * 0.46f + hf_absf(rc.flow_t) * 0.30f, 0.0f, 1.0f);
            float warp_mix = hf_smooth01(hf_clampf(deform * (0.62f + control_mix * 0.58f), 0.0f, 1.0f));
            
            float film_phase = phase * 0.11f
                            + (float) x * rc.hue_x_step
                            + (float) y * rc.hue_y_step
                            + (float) b3[idx] * 0.0048f
                            + membrane * 2.55f
                            + membrane * membrane * 1.15f
                            + rim * 0.72f
                            + nx * 0.31f + ny * 0.22f;
                            
            float hu, hv;
            float pearl = 0.5f + 0.5f * hf_lut_cos(t, film_phase * 0.52f + phase * 0.23f + 0.9f);
            float glow = rc.glow_t * hf_clampf(core * 0.18f + membrane * 0.38f + rim * 0.48f, 0.0f, 1.0f);
            float color_shell = hf_smooth01(hf_clampf(membrane * 0.76f + rim * 0.58f, 0.0f, 1.0f));
            float source_chroma = hf_absf((float) src_u[idx] - 128.0f) + hf_absf((float) src_v[idx] - 128.0f);
            
            float neutrality = 1.0f - hf_smooth01(hf_clampf(source_chroma * 0.00961538461f, 0.0f, 1.0f)); // 1.0f / 104.0f
            float iridescence = rc.chroma_t * color_shell * (0.62f + pearl * 0.38f) * (0.42f + neutrality * 0.58f);
            float prism_px = 2.25f * rc.chroma_t * color_shell * (0.55f + neutrality * 0.45f);
            
            int oy, ou, ov;
            hf_spectral_uv(film_phase, &hu, &hv);
            hf_sample_bilinear_y(src_y, src_u, src_v, sx, sy, w, h, &oy, &ou, &ov, &rc);

            if (prism_px > 0.05f) {
                int ip = hf_nearest_index(sx + nx * prism_px, sy + ny * prism_px, w, h);
                int ic = hf_nearest_index(sx, sy, w, h);
                int im_idx = hf_nearest_index(sx - nx * prism_px, sy - ny * prism_px, w, h);
                
                int ip_uv = rc.ssm ? ip : ((ip / w) * rc.uv_w + ((ip % w) >> 1));
                int ic_uv = rc.ssm ? ic : ((ic / w) * rc.uv_w + ((ic % w) >> 1));
                int im_uv = rc.ssm ? im_idx : ((im_idx / w) * rc.uv_w + ((im_idx % w) >> 1));

                float rp = (float) src_y[ip] + 1.402000f * ((float) src_v[ip_uv] - 128.0f);
                float uf = (float) src_u[ic_uv] - 128.0f;
                float vf = (float) src_v[ic_uv] - 128.0f;
                float gc = (float) src_y[ic] - 0.344136f * uf - 0.714136f * vf;
                float bm = (float) src_y[im_idx] + 1.772000f * ((float) src_u[im_uv] - 128.0f);
                
                int py, pu, pv;
                rp = hf_clampf(rp, 0.0f, 255.0f);
                gc = hf_clampf(gc, 0.0f, 255.0f);
                bm = hf_clampf(bm, 0.0f, 255.0f);
                
                hf_rgb_to_yuv(rp, gc, bm, &py, &pu, &pv);
                oy += (int) ((float) (py - oy) * color_shell * 0.14f);
                ou = pu;
                ov = pv;
            }

            int base_y = (int) ((float) src_y[idx] * (1.0f - warp_mix) + (float) oy * warp_mix + 0.5f);
            int base_u = (int) ((float) src_u[idx] * (1.0f - warp_mix) + (float) ou * warp_mix + 0.5f);
            int base_v = (int) ((float) src_v[idx] * (1.0f - warp_mix) + (float) ov * warp_mix + 0.5f);

            int yy = base_y + (int) ((float) (255 - base_y) * glow * (0.09f + 0.22f * rc.glow_t) * (0.80f + pearl * 0.20f));
            yy += (int) (rim * rc.glow_t * (2.0f + pearl * 5.0f));

            int uu = 128 + (int) ((float) (base_u - 128) * (1.0f - iridescence * 0.025f));
            int vv = 128 + (int) ((float) (base_v - 128) * (1.0f - iridescence * 0.025f));
            uu += (int) (hu * iridescence * (54.0f + pearl * 18.0f));
            vv += (int) (hv * iridescence * (54.0f + pearl * 18.0f));

            Y[idx] = (uint8_t) hf_clampi(yy, 0, 255);
            
            int uv_idx = rc.ssm ? idx : (y * rc.uv_w + (x >> 1));
            U[uv_idx] = (uint8_t) hf_clampi(uu, 0, 255);
            V[uv_idx] = (uint8_t) hf_clampi(vv, 0, 255);
        }
    }
}