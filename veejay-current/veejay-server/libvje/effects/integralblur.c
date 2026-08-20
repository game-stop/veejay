/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include "integralblur.h"

#define INTEGRALBLUR_PARAMS 4

#define P_RADIUS        0
#define P_ITERATIONS    1
#define P_BLUR_AMOUNT   2
#define P_CHROMA_AMOUNT 3

typedef struct {
    uint8_t  *planes;
    uint8_t  *orig;
    uint8_t  *mask;
    uint8_t  *tmp;
    uint32_t *integral;

    int width;
    int stride;
    int height;
    int max_radius;
    int n_threads;
} integralblur_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t ib_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline int ib_param1000_to_range(int v, int lo, int hi)
{
    v = clampi(v, 0, 1000);
    if(hi <= lo)
        return lo;
    return lo + (((hi - lo) * v + 500) / 1000);
}

static inline int ib_range_to_param1000(int v, int lo, int hi)
{
    v = clampi(v, lo, hi);
    if(hi <= lo)
        return 0;
    return ((v - lo) * 1000 + ((hi - lo) >> 1)) / (hi - lo);
}

static inline int ib_mix_u8(int a, int b, int q8)
{
    return a + (((b - a) * q8 + ((b >= a) ? 127 : -127)) / 255);
}

vj_effect *integralblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    int max_radius = MIN(w, h) / 4;
    int soft_radius_hi;

    if(!ve)
        return NULL;

    if(max_radius < 1)
        max_radius = 1;

    soft_radius_hi = MIN(max_radius, 18);

    ve->num_params = INTEGRALBLUR_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_RADIUS] = 0;        ve->limits[1][P_RADIUS] = 1000;        ve->defaults[P_RADIUS] = ib_range_to_param1000(3, 1, max_radius);
    ve->limits[0][P_ITERATIONS] = 1;    ve->limits[1][P_ITERATIONS] = 6;       ve->defaults[P_ITERATIONS] = 1;
    ve->limits[0][P_BLUR_AMOUNT] = 0;   ve->limits[1][P_BLUR_AMOUNT] = 1000;   ve->defaults[P_BLUR_AMOUNT] = 1000;
    ve->limits[0][P_CHROMA_AMOUNT] = 0; ve->limits[1][P_CHROMA_AMOUNT] = 1000; ve->defaults[P_CHROMA_AMOUNT] = 850;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Iterations",
        "Blur Amount",
        "Chroma Blur"
    );

    ve->description = "Integral Blur";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, ib_range_to_param1000(soft_radius_hi, 1, max_radius), 76, 98, 15, 620, 0, 5, 160, VJ_BEAT_COST_MODERATE, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, 4, 62, 90, 0, 720, 0, 1, 450, VJ_BEAT_COST_MODERATE, 58, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 200, 1000, 88, 100, 10, 480, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 100, 1000, 74, 98, 30, 760, 0, 5, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *integralblur_malloc(int width, int height)
{
    integralblur_t *f = (integralblur_t*) vj_calloc(sizeof(integralblur_t));

    if(!f)
        return NULL;

    const size_t len = (size_t)width * (size_t)height;
    const size_t integral_len = (size_t)(width + 1) * (size_t)(height + 1);

    f->width = width;
    f->height = height;
    f->stride = width + 1;
    f->max_radius = MIN(width, height) / 4;

    if(f->max_radius < 1)
        f->max_radius = 1;

    f->planes = (uint8_t*) vj_malloc(len * 3u);
    f->integral = (uint32_t*) vj_malloc(sizeof(uint32_t) * integral_len);

    if(!f->planes || !f->integral) {
        if(f->planes)
            free(f->planes);
        if(f->integral)
            free(f->integral);
        free(f);
        return NULL;
    }

    f->orig = f->planes;
    f->mask = f->orig + len;
    f->tmp  = f->mask + len;
    f->n_threads = vje_advise_num_threads(width * height);

    return f;
}

void integralblur_free(void *ptr)
{
    integralblur_t *f = (integralblur_t*) ptr;

    free(f->planes);
    free(f->integral);
    free(f);
}

static void build_integral(integralblur_t *f, uint8_t *src)
{
    const int w = f->width;
    const int h = f->height;
    const int stride = f->stride;
    uint32_t *restrict I = f->integral;

    veejay_memset(I, 0, sizeof(uint32_t) * (size_t)(w + 1));

    for(int y = 1; y <= h; y++) {
        const uint8_t *restrict src_row = src + (size_t)(y - 1) * (size_t)w;
        uint32_t *restrict cur = I + (size_t)y * (size_t)stride;
        const uint32_t *restrict prev = cur - stride;
        uint32_t sum = 0;

        cur[0] = 0;

        for(int x = 1; x <= w; x++) {
            sum += src_row[x - 1];
            cur[x] = sum + prev[x];
        }
    }
}

static void box_blur(integralblur_t *f, uint8_t *src, uint8_t *dst, int radius)
{
    const int w = f->width;
    const int h = f->height;
    const int stride = f->stride;
    const uint32_t *restrict I = f->integral;

    build_integral(f, src);

#pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for(int y = 0; y < h; y++) {
        int y0 = y - radius;
        int y1 = y + radius;

        if(y0 < 0)
            y0 = 0;
        if(y1 >= h)
            y1 = h - 1;

        const int iy0 = y0;
        const int iy1 = y1 + 1;
        const int y_area = y1 - y0 + 1;
        const uint32_t *restrict row_i0 = I + (size_t)iy0 * (size_t)stride;
        const uint32_t *restrict row_i1 = I + (size_t)iy1 * (size_t)stride;
        uint8_t *restrict out = dst + (size_t)y * (size_t)w;

        for(int x = 0; x < w; x++) {
            int x0 = x - radius;
            int x1 = x + radius;

            if(x0 < 0)
                x0 = 0;
            if(x1 >= w)
                x1 = w - 1;

            const int ix0 = x0;
            const int ix1 = x1 + 1;
            const uint32_t sum = row_i1[ix1] - row_i0[ix1] - row_i1[ix0] + row_i0[ix0];
            const int area = (x1 - x0 + 1) * y_area;

            out[x] = (uint8_t)((sum + (uint32_t)(area >> 1)) / (uint32_t)area);
        }
    }
}

static uint8_t *integralblur_blur_plane(integralblur_t *f, uint8_t *plane, int len, int radius, int iter)
{
    uint8_t *src = f->mask;
    uint8_t *dst = f->tmp;

    veejay_memcpy(src, plane, len);

    if(iter < 2) {
        for(int i = 0; i < iter; i++) {
            box_blur(f, src, dst, radius);

            uint8_t *swap = src;
            src = dst;
            dst = swap;
        }
    }
    else {
        const int n1 = iter >> 1;
        const int n2 = iter - n1;
        int r1 = (int)((float)radius * sqrtf((float)n1) + 0.5f);
        int r2 = (int)((float)radius * sqrtf((float)n2) + 0.5f);

        if(r1 < 1)
            r1 = radius;
        if(r2 < 1)
            r2 = radius;
        if(r1 > f->max_radius)
            r1 = f->max_radius;
        if(r2 > f->max_radius)
            r2 = f->max_radius;

        box_blur(f, src, dst, r1);
        box_blur(f, dst, src, r2);
    }

    return src;
}

static void integralblur_mix_plane(integralblur_t *f, uint8_t *plane, uint8_t *blurred, int len, int mix_q8)
{
    const uint8_t *restrict orig = f->orig;
    const uint8_t *restrict blur = blurred;
    uint8_t *restrict out = plane;

    if(mix_q8 >= 255) {
        if(out != blur)
            veejay_memcpy(out, blur, len);
        return;
    }

#pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for(int i = 0; i < len; i++)
        out[i] = ib_u8(ib_mix_u8(orig[i], blur[i], mix_q8));
}

void integralblur_apply(void *ptr, VJFrame *frame, int *args)
{
    integralblur_t *f = (integralblur_t*) ptr;

    const int radius_param = args[P_RADIUS];
    const int iter = args[P_ITERATIONS];
    const int blur_amount = args[P_BLUR_AMOUNT];
    const int chroma_amount = args[P_CHROMA_AMOUNT];
    const int radius = ib_param1000_to_range(radius_param, 1, f->max_radius);
    const int mix_q8 = clampi((blur_amount * 255 + 500) / 1000, 0, 255);
    const int chroma_mix_q8 = clampi((mix_q8 * chroma_amount + 500) / 1000, 0, 255);
    const int len = frame->len;

    if(mix_q8 <= 0 && chroma_mix_q8 <= 0)
        return;

    for(int p = 0; p < 3; p++) {
        const int plane_mix = (p == 0) ? mix_q8 : chroma_mix_q8;

        if(plane_mix > 0) {
            uint8_t *blurred;

            veejay_memcpy(f->orig, frame->data[p], len);
            blurred = integralblur_blur_plane(f, frame->data[p], len, radius, iter);
            integralblur_mix_plane(f, frame->data[p], blurred, len, plane_mix);
        }
    }
}
