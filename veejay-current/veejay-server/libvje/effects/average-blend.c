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
#include <veejaycore/vjmem.h>
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
    
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 32;
    ve->defaults[0] = 1;
    
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 128;
    ve->description = "Average Mixer";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Recursions", "Mix Weight"); 
    return ve;
}

void *average_blend_malloc(int w, int h) {
    avgblend_t *t = (avgblend_t*) vj_calloc(sizeof(avgblend_t));
    t->n_threads = vje_advise_num_threads(w * h);
    return (void*)t;
}

void average_blend_free(void *ptr) {
    if(ptr) free(ptr);
}

void average_blend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    avgblend_t *t = (avgblend_t*) ptr;
    const int recursions = args[0];
    const int weight = args[1];
    const int n_threads = t->n_threads;

    const int len = frame->len;
    const int uv_len = frame->uv_len;

    #pragma omp parallel num_threads(n_threads)
    {
        for (int r = 0; r < recursions; r++) {
            
            uint8_t *restrict Y1 = frame->data[0];
            const uint8_t *restrict Y2 = frame2->data[0];
            
            #pragma omp for schedule(static)
            for (int i = 0; i < len; i++) {
                int diff = (int)Y2[i] - (int)Y1[i];
                Y1[i] = (uint8_t)(Y1[i] + ((weight * diff) >> 8));
            }

            uint8_t *restrict U1 = frame->data[1];
            uint8_t *restrict V1 = frame->data[2];
            const uint8_t *restrict U2 = frame2->data[1];
            const uint8_t *restrict V2 = frame2->data[2];

            #pragma omp for schedule(static)
            for (int i = 0; i < uv_len; i++) {
                int diffU = (int)U2[i] - (int)U1[i];
                int diffV = (int)V2[i] - (int)V1[i];
                U1[i] = (uint8_t)(U1[i] + ((weight * diffU) >> 8));
                V1[i] = (uint8_t)(V1[i] + ((weight * diffV) >> 8));
            }

        }
    }
}

void average_blend_applyN(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    average_blend_apply(ptr, frame, frame2, args);
}
