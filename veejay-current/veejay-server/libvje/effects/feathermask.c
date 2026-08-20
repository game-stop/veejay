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
#include "feathermask.h"

typedef struct {
    uint8_t  *mask;
    uint8_t  *tmp;
    uint32_t *integral;
    int width;
    int height;
    int n_threads;
} feathermask_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *feathermask_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 2;
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

    ve->limits[0][0] = 1; ve->limits[1][0] = 32; ve->defaults[0] = 3;
    ve->limits[0][1] = 1; ve->limits[1][1] = 4;  ve->defaults[1] = 1;

    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Iterations");
    ve->description = "Alpha: Feather Mask";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;
    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 24, 66, 92, 20, 620, 0, 1, 240, VJ_BEAT_COST_MODERATE, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, 4, 48, 82, 0, 700, 0, 1, 800, VJ_BEAT_COST_EXPENSIVE, 42, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *feathermask_malloc(int width, int height)
{
    feathermask_t *f = (feathermask_t*) vj_calloc(sizeof(feathermask_t));

    if(!f)
        return NULL;

    const size_t len = (size_t)width * (size_t)height;
    const size_t ilen = (size_t)(width + 1) * (size_t)(height + 1);

    f->width = width;
    f->height = height;
    f->mask = (uint8_t*) vj_malloc(len);
    f->tmp = (uint8_t*) vj_malloc(len);
    f->integral = (uint32_t*) vj_malloc(sizeof(uint32_t) * ilen);

    if(!f->mask || !f->tmp || !f->integral) {
        free(f->mask);
        free(f->tmp);
        free(f->integral);
        free(f);
        return NULL;
    }

    f->n_threads = vje_advise_num_threads(width * height);
    return f;
}

void feathermask_free(void *ptr)
{
    feathermask_t *f = (feathermask_t*) ptr;

    free(f->mask);
    free(f->tmp);
    free(f->integral);
    free(f);
}

static void feathermask_build_integral(feathermask_t *f, const uint8_t *restrict src)
{
    const int w = f->width;
    const int h = f->height;
    const int stride = w + 1;
    uint32_t *restrict I = f->integral;

    veejay_memset(I, 0, sizeof(uint32_t) * (size_t)stride);

    for(int y = 1; y <= h; y++) {
        const uint8_t *restrict src_row = src + (size_t)(y - 1) * (size_t)w;
        const int row = y * stride;
        const int prev = row - stride;
        uint32_t row_sum = 0;

        I[row] = 0;

        for(int x = 1; x <= w; x++) {
            row_sum += src_row[x - 1];
            I[row + x] = I[prev + x] + row_sum;
        }
    }
}

static inline uint32_t feathermask_box_sum(const uint32_t *restrict I,
                                           int stride,
                                           int x0,
                                           int y0,
                                           int x1,
                                           int y1)
{
    return I[y1 * stride + x1] - I[y0 * stride + x1] -
           I[y1 * stride + x0] + I[y0 * stride + x0];
}

static void feathermask_box_blur(feathermask_t *f,
                                 const uint8_t *restrict src,
                                 uint8_t *restrict dst,
                                 int radius)
{
    const int w = f->width;
    const int h = f->height;
    const int stride = w + 1;
    const uint32_t *restrict I = f->integral;

    feathermask_build_integral(f, src);

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
        uint8_t *restrict dst_row = dst + (size_t)y * (size_t)w;

        for(int x = 0; x < w; x++) {
            int x0 = x - radius;
            int x1 = x + radius;

            if(x0 < 0)
                x0 = 0;
            if(x1 >= w)
                x1 = w - 1;

            const int ix0 = x0;
            const int ix1 = x1 + 1;
            const int area = (x1 - x0 + 1) * y_area;
            const uint32_t sum = feathermask_box_sum(I, stride, ix0, iy0, ix1, iy1);

            dst_row[x] = (uint8_t)((sum + (uint32_t)(area >> 1)) / (uint32_t)area);
        }
    }
}

void feathermask_apply(void *ptr, VJFrame *frame, int *args)
{
    feathermask_t *f = (feathermask_t*) ptr;

    const int len = frame->len;
    const int radius = args[0];
    const int iter = args[1];

    veejay_memcpy(f->mask, frame->data[3], len);

    uint8_t *restrict src = f->mask;
    uint8_t *restrict dst = f->tmp;

    for(int i = 0; i < iter; i++) {
        feathermask_box_blur(f, src, dst, radius);

        uint8_t *swap = src;
        src = dst;
        dst = swap;
    }

    veejay_memcpy(frame->data[3], src, len);
}
