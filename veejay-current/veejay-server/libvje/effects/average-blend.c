/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#pragma GCC optimize ("unroll-loops","tree-vectorize")
#include "common.h"
#include "average-blend.h"

typedef struct {
    int n_threads;
} avgblend_t;

vj_effect *average_blend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = 32;  ve->defaults[0] = 1;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 128;

    ve->description = "Average Mixer";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Recursions", "Mix Weight");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 1, 4, 54, 86, 120, 700, 0, 1, 240, VJ_BEAT_COST_MODERATE, 46, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 16, 240, 90, 100, 0, 260, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *average_blend_malloc(int w, int h)
{
    avgblend_t *t = (avgblend_t *) vj_calloc(sizeof(avgblend_t));

    if(!t)
        return NULL;

    t->n_threads = vje_advise_num_threads(w * h);

    return t;
}

void average_blend_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

void average_blend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    avgblend_t *t = (avgblend_t *) ptr;

    const int recursions = args[0] < 1 ? 1 : args[0];
    const int weight = args[1];
    const int n_threads = t->n_threads;
    const int len = frame->len;

    uint8_t *restrict Y1 = frame->data[0];
    uint8_t *restrict U1 = frame->data[1];
    uint8_t *restrict V1 = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

    #pragma omp parallel num_threads(n_threads)
    {
        for(int r = 0; r < recursions; r++)
        {
            #pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
            {
                const int y = Y1[i];
                const int u = U1[i];
                const int v = V1[i];

                Y1[i] = (uint8_t)(y + ((weight * ((int)Y2[i] - y)) >> 8));
                U1[i] = (uint8_t)(u + ((weight * ((int)U2[i] - u)) >> 8));
                V1[i] = (uint8_t)(v + ((weight * ((int)V2[i] - v)) >> 8));
            }
        }
    }
}

void average_blend_applyN(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    average_blend_apply(ptr, frame, frame2, args);
}
