/* 
 * Linux VeeJay
 *
 * Copyright(C)2009 Niels Elburg <nwelburg@gmail.com>
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

/*  "weed"-plugin partially ported from LiVES (C) G. Finch (Salsaman) 2009
 *
 *  weed-plugins/multi_transitions.c?revision=286
 *
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "iris.h"

#define IRIS_PARAMS 2

#define P_VALUE 0
#define P_SHAPE 1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *iris_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = IRIS_PARAMS;
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

    ve->limits[0][P_VALUE] = 0; ve->limits[1][P_VALUE] = 100; ve->defaults[P_VALUE] = 1;
    ve->limits[0][P_SHAPE] = 0; ve->limits[1][P_SHAPE] = 1;   ve->defaults[P_SHAPE] = 0;

    ve->description = "Iris Transition (Circle,Rect)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(ve->num_params, "Value", "Shape");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_SHAPE], P_SHAPE, "Circle", "Rectangle");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    5,                  96,                 16, 62,  700, 2800, 0,    86,
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

static void iris_copy_frame(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y0 = frame->data[0];
    uint8_t *restrict Cb0 = frame->data[1];
    uint8_t *restrict Cr0 = frame->data[2];
    const uint8_t *restrict Y1 = frame2->data[0];
    const uint8_t *restrict Cb1 = frame2->data[1];
    const uint8_t *restrict Cr1 = frame2->data[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        Y0[i] = Y1[i];
        Cb0[i] = Cb1[i];
        Cr0[i] = Cr1[i];
    }
}

void iris_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int val = args[P_VALUE];
    const int shape = args[P_SHAPE];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    if(val <= 0) {
        iris_copy_frame(frame, frame2, n_threads);
        return;
    }

    if(val >= 100)
        return;

    uint8_t *restrict Y0 = frame->data[0];
    uint8_t *restrict Cb0 = frame->data[1];
    uint8_t *restrict Cr0 = frame->data[2];

    const uint8_t *restrict Y1 = frame2->data[0];
    const uint8_t *restrict Cb1 = frame2->data[1];
    const uint8_t *restrict Cr1 = frame2->data[2];

    const int half_w = width >> 1;
    const int half_h = height >> 1;

    if(shape == 0) {
        const long long max_dist_sq = (long long)half_h * (long long)half_h + (long long)half_w * (long long)half_w;
        const long long vv = (long long)val * (long long)val;
        const long long threshold_sq = (max_dist_sq * vv) / 10000LL;

#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int y = 0; y < height; y++) {
            const int dy = y - half_h;
            const long long dy_sq = (long long)dy * (long long)dy;
            const int row = y * width;

#pragma omp simd
            for(int x = 0; x < width; x++) {
                const int dx = x - half_w;
                const long long dist_sq = dy_sq + (long long)dx * (long long)dx;
                const int mask = -(dist_sq > threshold_sq);
                const int idx = row + x;

                Y0[idx]  = (uint8_t)((Y1[idx]  & mask) | (Y0[idx]  & ~mask));
                Cb0[idx] = (uint8_t)((Cb1[idx] & mask) | (Cb0[idx] & ~mask));
                Cr0[idx] = (uint8_t)((Cr1[idx] & mask) | (Cr0[idx] & ~mask));
            }
        }
    }
    else {
        const int inv = 100 - val;
        const int x_bound = (half_w * inv) / 100;
        const int y_bound = (half_h * inv) / 100;
        const int x_hi = width - x_bound;
        const int y_hi = height - y_bound;

#pragma omp parallel for schedule(static) num_threads(n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const int row_mask = (y < y_bound) | (y >= y_hi);

#pragma omp simd
            for(int x = 0; x < width; x++) {
                const int mask = -((x < x_bound) | (x >= x_hi) | row_mask);
                const int idx = row + x;

                Y0[idx]  = (uint8_t)((Y1[idx]  & mask) | (Y0[idx]  & ~mask));
                Cb0[idx] = (uint8_t)((Cb1[idx] & mask) | (Cb0[idx] & ~mask));
                Cr0[idx] = (uint8_t)((Cr1[idx] & mask) | (Cr0[idx] & ~mask));
            }
        }
    }
}
