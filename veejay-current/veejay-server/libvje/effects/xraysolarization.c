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
#include <omp.h>

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
    uint32_t *hist_workers;
    uint8_t cdf_lut[256];
    float phase;
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
    float u;
    float mag;

    if (shimmer == 0)
        return 0.0f;

    u = xr_clampf(xr_absf((float) shimmer) * 0.001f, 0.0f, 1.0f);
    mag = 0.00018f * (exp2f(10.0f * u) - 1.0f);
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
        const uint8_t *row = src + y * w;
        uint8_t *out1 = tmp1 + y * w;
        uint8_t *out2 = tmp2 + y * w;
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
#pragma omp for schedule(static)
    for (int x = 0; x < w; x++) {
        int sum1 = tmp1[x] * (r1 + 1);
        int sum2 = tmp2[x] * (r2 + 1);
        int yy;
        for (yy = 1; yy <= r2; yy++) {
            int sy = yy < h ? yy : h - 1;
            if (yy <= r1) sum1 += tmp1[sy * w + x];
            sum2 += tmp2[sy * w + x];
        }
        for (yy = 0; yy < h; yy++) {
            int sub1 = yy - r1, add1 = yy + r1 + 1;
            int sub2 = yy - r2, add2 = yy + r2 + 1;
            dst1[yy * w + x] = (uint8_t) ((sum1 * recip1 + 32768) >> 16);
            dst2[yy * w + x] = (uint8_t) ((sum2 * recip2 + 32768) >> 16);
            if (sub1 < 0) sub1 = 0;
            if (add1 >= h) add1 = h - 1;
            if (sub2 < 0) sub2 = 0;
            if (add2 >= h) add2 = h - 1;
            sum1 += tmp1[add1*w+x] - tmp1[sub1*w+x];
            sum2 += tmp2[add2*w+x] - tmp2[sub2*w+x];
        }
    }
}

static void xr_build_cdf(xraysolarization_t *t, const uint8_t *src_y, int len)
{
    const int tid = omp_get_thread_num();
    const int worker_count = omp_get_num_threads();
    uint32_t *worker_hist = t->hist_workers + (size_t) tid * 256;
    uint64_t accum = 0;
    uint32_t min_cdf = 0;
    uint32_t total = (uint32_t) len;
    uint64_t denom;
    int i;

    for (i = 0; i < 256; i++)
        worker_hist[i] = 0;

#pragma omp for schedule(static)
    for (i = 0; i < len; i++) {
        worker_hist[src_y[i]]++;
    }

#pragma omp single
    {
        for (i = 0; i < 256; i++)
            t->hist[i] = 0;
        for (int worker = 0; worker < worker_count; worker++) {
            const uint32_t *hist = t->hist_workers + (size_t) worker * 256;
            for (i = 0; i < 256; i++)
                t->hist[i] += hist[i];
        }
        for (i = 0; i < 256; i++) {
            accum += t->hist[i];
            if (min_cdf == 0 && accum > 0)
                min_cdf = (uint32_t) accum;
            t->cdf_lut[i] = 0;
        }

        if (total > 0) {
            denom = (uint64_t) total - (uint64_t) min_cdf;
            if (denom == 0) {
                for (i = 0; i < 256; i++)
                    t->cdf_lut[i] = (uint8_t) i;
            }
            else {
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
        }
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

    ve->limits[0][P_BLACK]     = 0;
    ve->limits[1][P_BLACK]     = 120;
    ve->limits[0][P_WHITE]     = 136;
    ve->limits[1][P_WHITE]     = 255;
    ve->limits[0][P_CDFMIX]    = 0;
    ve->limits[1][P_CDFMIX]    = 100;
    ve->limits[0][P_LOCAL]     = 0;
    ve->limits[1][P_LOCAL]     = 100;
    ve->limits[0][P_TEXTURE]   = 0;
    ve->limits[1][P_TEXTURE]   = 100;
    ve->limits[0][P_SOLAR]     = 0;
    ve->limits[1][P_SOLAR]     = 100;
    ve->limits[0][P_ISOLATION] = 0;
    ve->limits[1][P_ISOLATION] = 100;
    ve->limits[0][P_SCALE]     = 1;
    ve->limits[1][P_SCALE]     = 32;
    ve->limits[0][P_POLARITY]  = -100;
    ve->limits[1][P_POLARITY]  = 100;
    ve->limits[0][P_SHIMMER]   = -1000;
    ve->limits[1][P_SHIMMER]   = 1000;

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

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SNARE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SNARE_PULSE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 56, 90, 22, 38, 20, 220, 0, 1, 0, VJ_BEAT_COST_CHEAP, 150, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_HAT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HAT_PULSE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 72, 100, 18, 32, 16, 120, 0, 1, 0, VJ_BEAT_COST_CHEAP, 120, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BEAT_GATE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 48, 88, 30, 48, 20, 220, 0, 1, 0, VJ_BEAT_COST_CHEAP, 175, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *xraysolarization_malloc(int w, int h)
{
    xraysolarization_t *t = NULL;
    unsigned char *base = NULL;
    size_t len = (size_t) w * (size_t) h;
    size_t hist_bytes;
    size_t total;
    size_t off = 0;
    int hist_workers_count;

    if (w <= 0 || h <= 0 || len == 0)
        return NULL;

    hist_workers_count = omp_get_max_threads();
    if (hist_workers_count < 1)
        hist_workers_count = 1;
    hist_bytes = (size_t) hist_workers_count * 256 * sizeof(uint32_t);
    total = len * 5 + hist_bytes + sizeof(uint32_t) - 1;

    t = (xraysolarization_t *) vj_calloc(sizeof(xraysolarization_t));
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

    base = (unsigned char *) t->region;
    t->src_y = (uint8_t *) (base + off); off += len;
    t->blur1 = (uint8_t *) (base + off); off += len;
    t->blur2 = (uint8_t *) (base + off); off += len;
    t->tmp1  = (uint8_t *) (base + off); off += len;
    t->tmp2  = (uint8_t *) (base + off);
    off += len;
    off = (off + sizeof(uint32_t) - 1) & ~(sizeof(uint32_t) - 1);
    t->hist_workers = (uint32_t *) (base + off);

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
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict blur1 = t->blur1;
    uint8_t *restrict blur2 = t->blur2;
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
    float local_gain;
    float texture_gain;
    float isolate_gain;
    float cdf_gain;
    float solar_gain;
    float threshold_base;
    float polarity_bias;
    int r1;
    int r2;
    int i;

    if (len <= 0 || len > t->len)
        len = t->len;

#pragma omp single
    veejay_memcpy(src_y, Y, len);

    xr_build_cdf(t, src_y, len);

    if (white <= black)
        white = black + 1;

    r1 = xr_clampi(scale, 1, 32);
    r2 = xr_clampi(r1 * 3, 2, 96);

    xr_box_blur2(t, src_y, blur1, blur2, r1, r2);

    local_gain = (float) local * 0.040f;
    texture_gain = (float) texture * 0.060f;
    isolate_gain = (float) isolation * 0.010f;
    cdf_gain = (float) cdfmix * 0.01f;
    solar_gain = (float) solar * 0.01f;
    polarity_bias = (float) polarity * 0.010f;

    t->phase += xr_time_step(shimmer);
    if (t->phase > 6.28318530718f)
        t->phase -= 6.28318530718f;
    else if (t->phase < 0.0f)
        t->phase += 6.28318530718f;

    threshold_base = 0.50f + polarity_bias * 0.34f + sinf(t->phase) * solar_gain * 0.17f;
    threshold_base = xr_clampf(threshold_base, 0.06f, 0.94f);

#pragma omp for schedule(static)
    for (i = 0; i < len; i++) {
        int y0 = src_y[i];
        int eq = t->cdf_lut[y0];
        int smooth_y = blur2[i];
        int smooth_eq = t->cdf_lut[smooth_y];
        int local_detail = y0 - (int) blur1[i];
        int micro = (int) blur1[i] - (int) blur2[i];
        float detail = (float) local_detail * local_gain + (float) micro * texture_gain;
        int grad = xr_clampi((int) xr_absf((float) local_detail) * 2 + (int) xr_absf((float) micro) * 3, 0, 255);
        float grad_n = (float) grad * (1.0f / 255.0f);
        float gate = xr_smooth01(grad_n * (0.72f + isolate_gain * 1.65f));
        float base = (float) y0 + ((float) eq - (float) y0) * cdf_gain;
        float smooth_base = (float) smooth_y + ((float) smooth_eq - (float) smooth_y) * cdf_gain;
        float enhanced = base + detail * (0.28f + gate * 1.28f);
        float n;
        float smooth_n;
        float softness;
        float hard;
        float solar_mid;
        float texture_result;
        float isolation_mix;
        float result;
        int outy;

        enhanced = (float) xr_clampi((int) (enhanced + 0.5f), black, white);
        smooth_base = (float) xr_clampi((int) (smooth_base + 0.5f), black, white);

        n = (enhanced - (float) black) / (float) (white - black);
        smooth_n = (smooth_base - (float) black) / (float) (white - black);
        n = xr_clampf(n, 0.0f, 1.0f);
        smooth_n = xr_clampf(smooth_n, 0.0f, 1.0f);

        softness = 0.20f - solar_gain * 0.145f - gate * solar_gain * 0.025f;
        softness = xr_clampf(softness, 0.028f, 0.20f);
        hard = xr_smooth01((n - (threshold_base - softness)) / (softness * 2.0f));
        solar_mid = 1.0f - xr_absf(2.0f * n - 1.0f);
        texture_result = n + (hard - n) * (0.32f + solar_gain * 0.58f);
        texture_result += solar_mid * gate * solar_gain * 0.18f;

        if (n > threshold_base) {
            float inv = 1.0f - texture_result;
            texture_result += (inv - texture_result) * gate * solar_gain * 0.78f;
        }

        texture_result += (detail / 255.0f) * gate * (0.08f + solar_gain * 0.22f);
        texture_result = xr_clampf(texture_result, 0.0f, 1.0f);

        isolation_mix = xr_smooth01(gate * (0.35f + isolate_gain * 0.85f));
        result = smooth_n + (texture_result - smooth_n) * isolation_mix;
        result = xr_clampf(result, 0.0f, 1.0f);

        outy = xr_clampi((int) (result * 255.0f + 0.5f), 0, 255);
        Y[i] = (uint8_t) outy;
        U[i] = 128;
        V[i] = 128;
    }
}
