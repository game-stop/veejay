/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include "overclock.h"
#include <stdlib.h>

#define OVERCLOCK_PARAMS 2

#define P_RADIUS 0
#define P_VALUE  1

typedef struct {
    uint8_t *oc_buf;
    uint32_t seed;
    int *task_y;
    int *task_r;
    int num_tasks;
} overclock_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t overclock_next_u32(overclock_t *o)
{
    o->seed = o->seed * 1664525u + 1013904223u;
    return o->seed;
}

static inline int overclock_rand_bounded(overclock_t *o, int max)
{
    return (int)(((uint64_t)overclock_next_u32(o) * (uint64_t)max) >> 32);
}

vj_effect *overclock_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = OVERCLOCK_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int radius_hi = h >> 3;

    if(radius_hi < 2)
        radius_hi = 2;

    ve->limits[0][P_RADIUS] = 2;
    ve->limits[1][P_RADIUS] = radius_hi;
    ve->defaults[P_RADIUS] = radius_hi < 5 ? radius_hi : 5;

    ve->limits[0][P_VALUE] = 1;
    ve->limits[1][P_VALUE] = 90;
    ve->defaults[P_VALUE] = 2;

    ve->description = "Radial Cubics";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Value");

    int r_hi = h / 14;

    if(r_hi > 32)
        r_hi = 32;
    if(r_hi < 2)
        r_hi = 2;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, r_hi, 82, 100, 10, 520, 0, 1, 160, VJ_BEAT_COST_MODERATE, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 2, 72, 90, 100, 0, 460, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void)w;

    return ve;
}

void overclock_free(void *ptr)
{
    overclock_t *o = (overclock_t*) ptr;
    if(!o)
        return;

    if(o->oc_buf) free(o->oc_buf);
    if(o->task_y) free(o->task_y);
    if(o->task_r) free(o->task_r);
    free(o);
}

void *overclock_malloc(int w, int h)
{
    overclock_t *o = (overclock_t*) vj_calloc(sizeof(overclock_t));

    if(!o)
        return NULL;

    const int len = w * h;

    o->oc_buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len);
    
    o->task_y = (int*) vj_malloc(sizeof(int) * (size_t)h);
    o->task_r = (int*) vj_malloc(sizeof(int) * (size_t)h);

    if(!o->oc_buf || !o->task_y || !o->task_r) {
        if(o->oc_buf) free(o->oc_buf);
        if(o->task_y) free(o->task_y);
        if(o->task_r) free(o->task_r);
        free(o);
        return NULL;
    }

    o->seed = 0x0c0ffeeu ^ (uint32_t)w ^ ((uint32_t)h << 16);

    return (void*) o;
}

void overclock_apply(void *ptr, VJFrame *frame, int *args)
{
    overclock_t *o = (overclock_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int radius_hi = (height >> 3) < 2 ? 2 : (height >> 3);
    const int n = clampi(args[P_RADIUS], 2, radius_hi);
    const int radius = args[P_VALUE];
    const int max_block = width < height ? width : height;
    int N = n << 1;

    if(N > max_block)
        N = max_block;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = o->oc_buf;

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        veejay_blur2(B + (y * width), Y + (y * width), width, radius, 1, 1, 1);
    }

    #pragma omp single
    {
        o->num_tasks = 0;
        for(int y = N; y < height - N; ) {
            o->task_r[o->num_tasks] = 1 + overclock_rand_bounded(o, N);
            o->task_y[o->num_tasks] = y;
            o->num_tasks++;
            y += 1 + overclock_rand_bounded(o, N);
        }
    } 
    
    const int total_tasks = o->num_tasks;

    #pragma omp for schedule(dynamic)
    for(int i = 0; i < total_tasks; i++) {
        const int y = o->task_y[i];
        const int r = o->task_r[i];

        for(int x = 0; x < width; x += r) {
            const int bw = x + N <= width ? N : width - x;
            const int bh = y + N <= height ? N : height - y;
            const int area = bw * bh;
            int sum = 0;

            if(area <= 0)
                continue;

            for(int dy = 0; dy < bh; dy++) {
                const int row = (y + dy) * width + x;

                for(int dx = 0; dx < bw; dx++)
                    sum += B[row + dx];
            }

            const uint8_t t = (uint8_t)(sum / area);

            for(int dy = 0; dy < bh; dy++) {
                const int row = (y + dy) * width + x;

                for(int dx = 0; dx < bw; dx++) {
                    const int idx = row + dx;

                    Y[idx] = B[idx] > Y[idx] ? (uint8_t)((Y[idx] + t) >> 1) : t;
                }
            }
        }
    }
}