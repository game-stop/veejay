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
#include "xraysolarization.h"
#include <veejaycore/vjmem.h>

#define XRAY_PARAMS 10
#define P_BLACK       0
#define P_WHITE       1
#define P_CDFMIX      2
#define P_LOCAL       3
#define P_TEXTURE     4
#define P_SOLAR       5
#define P_ISOLATION   6
#define P_SCALE       7
#define P_POLARITY    8
#define P_SHIMMER     9

typedef struct {
    int w;
    int h;
    int len;
    void *region;
    uint8_t *src_y;
    uint8_t *blur1;
    uint8_t *blur2;
    uint8_t *tmp1;
    uint8_t *tmp2;
    uint32_t hist[256];
    uint8_t cdf_lut[256];
    float phase;

    float cached_local_gain;
    float cached_texture_gain;
    float cached_isolate_gain;
    float cached_cdf_gain;
    float cached_solar_gain;
    float cached_threshold_base;
} xraysolarization_t;

static inline int xr_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float xr_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float xr_absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float xr_smooth01(float x)
{
    x = xr_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static inline float xr_time_step(int shimmer)
{
    if (shimmer == 0)
        return 0.0f;
    float u = xr_clampf(xr_absf((float) shimmer) * 0.001f, 0.0f, 1.0f);
    float mag = 0.00018f * (exp2f(10.0f * u) - 1.0f);
    return shimmer < 0 ? -mag : mag;
}

static void xr_box_blur2(
    xraysolarization_t *t,
    const uint8_t *restrict src,
    uint8_t *restrict dst1,
    uint8_t *restrict dst2,
    int r1,
    int r2
) {
    const int w = t->w;
    const int h = t->h;
    const int win1 = r1 * 2 + 1;
    const int win2 = r2 * 2 + 1;
    const int recip1 = (65536 + win1 / 2) / win1;
    const int recip2 = (65536 + win2 / 2) / win2;
    uint8_t *restrict tmp1 = t->tmp1;
    uint8_t *restrict tmp2 = t->tmp2;
    int y;

    #pragma omp for schedule(static)
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)w;
        uint8_t *out1 = tmp1 + (size_t)y * (size_t)w;
        uint8_t *out2 = tmp2 + (size_t)y * (size_t)w;
        int sum1 = row[0] * (r1 + 1);
        int sum2 = row[0] * (r2 + 1);
        int x;
        for (x = 1; x <= r2; x++) {
            int v = row[x < w ? x : w - 1];
            if (x <= r1) sum1 += v;
            sum2 += v;
        }
        for (x = 0; x < w; x++) {
            int sub1 = x - r1, add1 = x + r1 + 1;
            int sub2 = x - r2, add2 = x + r2 + 1;
            out1[x] = (uint8_t) ((sum1 * recip1 + 32768) >> 16);
            out2[x] = (uint8_t) ((sum2 * recip2 + 32768) >> 16);
            if (sub1 < 0) sub1 = 0;
            if (add1 >= w) add1 = w - 1;
            if (sub2 < 0) sub2 = 0;
            if (add2 >= w) add2 = w - 1;
            sum1 += row[add1] - row[sub1];
            sum2 += row[add2] - row[sub2];
        }
    }

    #pragma omp barrier

    #pragma omp for schedule(static)
    for (int x = 0; x < w; x++) {
        int sum1 = tmp1[x] * (r1 + 1);
        int sum2 = tmp2[x] * (r2 + 1);
        int yy;
        for (yy = 1; yy <= r2; yy++) {
            int sy = yy < h ? yy : h - 1;
            if (yy <= r1) sum1 += tmp1[(size_t)sy * (size_t)w + x];
            sum2 += tmp2[(size_t)sy * (size_t)w + x];
        }
        for (yy = 0; yy < h; yy++) {
            int sub1 = yy - r1, add1 = yy + r1 + 1;
            int sub2 = yy - r2, add2 = yy + r2 + 1;
            dst1[(size_t)yy * (size_t)w + x] = (uint8_t) ((sum1 * recip1 + 32768) >> 16);
            dst2[(size_t)yy * (size_t)w + x] = (uint8_t) ((sum2 * recip2 + 32768) >> 16);
            if (sub1 < 0) sub1 = 0;
            if (add1 >= h) add1 = h - 1;
            if (sub2 < 0) sub2 = 0;
            if (add2 >= h) add2 = h - 1;
            sum1 += tmp1[(size_t)add1*(size_t)w+x] - tmp1[(size_t)sub1*(size_t)w+x];
            sum2 += tmp2[(size_t)add2*(size_t)w+x] - tmp2[(size_t)sub2*(size_t)w+x];
        }
    }
}

static void xr_build_cdf(xraysolarization_t *t, const uint8_t *src_y, int len)
{
    uint64_t accum = 0;
    uint32_t min_cdf = 0;
    uint32_t total = (uint32_t) len;
    uint64_t denom;
    int i;

    for (i = 0; i < 256; i++)
        t->hist[i] = 0;

    for (i = 0; i < len; i++)
        t->hist[src_y[i]]++;

    for (i = 0; i < 256; i++) {
        accum += t->hist[i];
        if (min_cdf == 0 && accum > 0)
            min_cdf = (uint32_t) accum;
        t->cdf_lut[i] = 0;
    }

    if (total == 0)
        return;

    denom = (uint64_t) total - (uint64_t) min_cdf;
    if (denom == 0) {
        for (i = 0; i < 256; i++)
            t->cdf_lut[i] = (uint8_t) i;
        return;
    }

    accum = 0;
    for (i = 0; i < 256; i++) {
        uint64_t num;
        int v;
        accum += t->hist[i];
        if (accum <= min_cdf) {
            t->cdf_lut[i] = 0;
            continue;
        }
        num = (accum - min_cdf) * 255ULL;
        v = (int) ((num + denom / 2ULL) / denom);
        t->cdf_lut[i] = (uint8_t) xr_clampi(v, 0, 255);
    }
}

vj_effect *xraysolarization_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    (void) w;
    (void) h;
    if (!ve)
        return NULL;

    ve->num_params = XRAY_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "X-Ray Solarization";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_BLACK]     = 6;
    ve->defaults[P_WHITE]     = 248;
    ve->defaults[P_CDFMIX]    = 92;
    ve->defaults[P_LOCAL]     = 68;
    ve->defaults[P_TEXTURE]   = 84;
    ve->defaults[P_SOLAR]     = 62;
    ve->defaults[P_ISOLATION] = 72;
    ve->defaults[P_SCALE]     = 6;
    ve->defaults[P_POLARITY]  = 18;
    ve->defaults[P_SHIMMER]   = 0;

    ve->limits[0][P_BLACK]     = 0;   ve->limits[1][P_BLACK]     = 120;
    ve->limits[0][P_WHITE]     = 136; ve->limits[1][P_WHITE]     = 255;
    ve->limits[0][P_CDFMIX]    = 0;   ve->limits[1][P_CDFMIX]    = 100;
    ve->limits[0][P_LOCAL]     = 0;   ve->limits[1][P_LOCAL]     = 100;
    ve->limits[0][P_TEXTURE]   = 0;   ve->limits[1][P_TEXTURE]   = 100;
    ve->limits[0][P_SOLAR]     = 0;   ve->limits[1][P_SOLAR]     = 100;
    ve->limits[0][P_ISOLATION] = 0;   ve->limits[1][P_ISOLATION] = 100;
    ve->limits[0][P_SCALE]     = 1;   ve->limits[1][P_SCALE]     = 32;
    ve->limits[0][P_POLARITY]  = -100;ve->limits[1][P_POLARITY]  = 100;
    ve->limits[0][P_SHIMMER]   = -1000;ve->limits[1][P_SHIMMER]  = 1000;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Black Clamp",
        "White Clamp",
        "CDF Mix",
        "Local Contrast",
        "Texture Boost",
        "Solarization",
        "Gradient Isolation",
        "Detail Scale",
        "Polarity",
        "Threshold Shimmer"
    );

    return ve;
}

void *xraysolarization_malloc(int w, int h)
{
    size_t len = (size_t) w * (size_t) h;
    size_t total = len * 5u;
    size_t off = 0;

    xraysolarization_t *t = (xraysolarization_t *) vj_calloc(sizeof(xraysolarization_t));
    if (!t)
        return NULL;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->phase = 0.0f;

    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    unsigned char *base = (unsigned char *) t->region;
    t->src_y = (uint8_t *) (base + off); off += len;
    t->blur1 = (uint8_t *) (base + off); off += len;
    t->blur2 = (uint8_t *) (base + off); off += len;
    t->tmp1  = (uint8_t *) (base + off); off += len;
    t->tmp2  = (uint8_t *) (base + off);

    return (void *) t;
}

void xraysolarization_free(void *ptr)
{
    xraysolarization_t *t = (xraysolarization_t *) ptr;
    if (!t)
        return;
    if (t->region)
        free(t->region);
    free(t);
}

void xraysolarization_apply(void *ptr, VJFrame *frame, int *args)
{
    xraysolarization_t *t = (xraysolarization_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict src_y = t->src_y;
    const uint8_t *restrict blur1 = t->blur1;
    const uint8_t *restrict blur2 = t->blur2;
    const uint8_t *restrict cdf_lut = t->cdf_lut;

    int len = frame->len;
    int black = args[P_BLACK];
    int white = args[P_WHITE];
    int cdfmix = args[P_CDFMIX];
    int local = args[P_LOCAL];
    int texture = args[P_TEXTURE];
    int solar = args[P_SOLAR];
    int isolation = args[P_ISOLATION];
    int scale = args[P_SCALE];
    int polarity = args[P_POLARITY];
    int shimmer = args[P_SHIMMER];

    if (len <= 0 || len > t->len)
        len = t->len;

    #pragma omp single
    {
        veejay_memcpy(t->src_y, Y, (size_t)len);
        xr_build_cdf(t, t->src_y, len);
    }
    
    if (white <= black)
        white = black + 1;

    const int r1 = xr_clampi(scale, 1, 32);
    const int r2 = xr_clampi(r1 * 3, 2, 96);
    xr_box_blur2(t, t->src_y, t->blur1, t->blur2, r1, r2);

    #pragma omp single
    {
        t->cached_local_gain = (float) local * 0.040f;
        t->cached_texture_gain = (float) texture * 0.060f;
        t->cached_isolate_gain = (float) isolation * 0.010f;
        t->cached_cdf_gain = (float) cdfmix * 0.01f;
        t->cached_solar_gain = (float) solar * 0.01f;
        float polarity_bias = (float) polarity * 0.010f;

        t->phase += xr_time_step(shimmer);
        if (t->phase > 6.28318530718f)
            t->phase -= 6.28318530718f;
        else if (t->phase < 0.0f)
            t->phase += 6.28318530718f;

        t->cached_threshold_base = 0.50f + polarity_bias * 0.34f + sinf(t->phase) * t->cached_solar_gain * 0.17f;
        t->cached_threshold_base = xr_clampf(t->cached_threshold_base, 0.06f, 0.94f);
    }

    const float local_gain = t->cached_local_gain;
    const float texture_gain = t->cached_texture_gain;
    const float isolate_gain = t->cached_isolate_gain;
    const float cdf_gain = t->cached_cdf_gain;
    const float solar_gain = t->cached_solar_gain;
    const float threshold_base = t->cached_threshold_base;

    const float inv_wb = 1.0f / (float)(white - black);
    const float f_black = (float)black;
    const float isolate_scale = 0.72f + isolate_gain * 1.65f;
    const float tex_coeff1 = 0.32f + solar_gain * 0.58f;
    const float tex_coeff2 = solar_gain * 0.18f;
    const float tex_coeff3 = solar_gain * 0.78f;
    const float detail_coeff = 0.08f + solar_gain * 0.22f;
    const float isolation_coeff = 0.35f + isolate_gain * 0.85f;

    #pragma omp for schedule(static)
    for (int i = 0; i < len; i++) {
        int y0 = src_y[i];
        int eq = cdf_lut[y0];
        int smooth_y = blur2[i];
        int smooth_eq = cdf_lut[smooth_y];
        int local_detail = y0 - (int) blur1[i];
        int micro = (int) blur1[i] - (int) blur2[i];

        float detail = (float) local_detail * local_gain + (float) micro * texture_gain;
        int grad = xr_clampi((int) xr_absf((float) local_detail) * 2 + (int) xr_absf((float) micro) * 3, 0, 255);
        float gate = xr_smooth01(((float) grad * (1.0f / 255.0f)) * isolate_scale);

        float base = (float) y0 + ((float) eq - (float) y0) * cdf_gain;
        float smooth_base = (float) smooth_y + ((float) smooth_eq - (float) smooth_y) * cdf_gain;
        float enhanced = base + detail * (0.28f + gate * 1.28f);

        enhanced = (float) xr_clampi((int) (enhanced + 0.5f), black, white);
        smooth_base = (float) xr_clampi((int) (smooth_base + 0.5f), black, white);

        float n = (enhanced - f_black) * inv_wb;
        float smooth_n = (smooth_base - f_black) * inv_wb;
        n = xr_clampf(n, 0.0f, 1.0f);
        smooth_n = xr_clampf(smooth_n, 0.0f, 1.0f);

        float softness = 0.20f - solar_gain * 0.145f - gate * solar_gain * 0.025f;
        softness = xr_clampf(softness, 0.028f, 0.20f);

        float hard = xr_smooth01((n - (threshold_base - softness)) / (softness * 2.0f));
        float solar_mid = 1.0f - xr_absf(2.0f * n - 1.0f);

        float texture_result = n + (hard - n) * tex_coeff1;
        texture_result += solar_mid * gate * tex_coeff2;

        if (n > threshold_base) {
            float inv = 1.0f - texture_result;
            texture_result += (inv - texture_result) * gate * tex_coeff3;
        }

        texture_result += (detail * (1.0f / 255.0f)) * gate * detail_coeff;
        texture_result = xr_clampf(texture_result, 0.0f, 1.0f);

        float isolation_mix = xr_smooth01(gate * isolation_coeff);
        float result = smooth_n + (texture_result - smooth_n) * isolation_mix;
        result = xr_clampf(result, 0.0f, 1.0f);

        Y[i] = (uint8_t) xr_clampi((int) (result * 255.0f + 0.5f), 0, 255);
    }
}