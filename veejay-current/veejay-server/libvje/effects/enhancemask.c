/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "enhancemask.h"
#include <stdlib.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "enhancemask.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int n_threads;
    uint8_t *buf[3];
} enhancemask_t;

vj_effect *enhancemask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 120; // Strength
    ve->defaults[1] = 8;   // Threshold
    ve->defaults[2] = 50;  // Halo Clamp

    ve->limits[0][0] = 0;    ve->limits[1][0] = 4096;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 64;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 128;

    ve->description = "Sharpen";
    ve->param_description = vje_build_param_list(
        ve->num_params, "Strength", "Grain Threshold", "Halo Clamp"
    );

    return ve;
}

void *enhancemask_malloc(int w, int h) {
    enhancemask_t *e = (enhancemask_t*) vj_calloc(sizeof(enhancemask_t));
    if(!e) return NULL;
    e->n_threads = vje_advise_num_threads(w*h);
    e->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
    if(!e->buf[0]) {
        free(e);
        return NULL;
    }

    return (void*) e;
}
void enhancemask_free(void *ptr) {
    enhancemask_t *e = (enhancemask_t*) ptr;
    if(e) {
        if(e->buf[0]) free(e->buf[0]);
        free(e);
    }
}

static inline uint8_t clamp_u8(int x) {
    if (x < 0) return 0;
    if (x > 255) return 255;
    return (uint8_t)x;
}

void enhancemask_apply(void *ptr, VJFrame *frame, int *s)
{
    enhancemask_t *e = (enhancemask_t*) ptr;
    const int width  = frame->width;
    const int height = frame->height;
    const int n_threads = e->n_threads;
    
    uint8_t *dst = frame->data[0];
    const int amount    = s[0];
    const int threshold = s[1];
    const int limit     = s[2];

    if (amount <= 0) return;

    const int stride = width;
    veejay_memcpy(e->buf[0], frame->data[0], frame->len);
    #pragma omp parallel num_threads(n_threads)
    {
        uint8_t *src = e->buf[0];
        #pragma omp for schedule(static)
        for (int y = 1; y < height - 1; y++)
        {
            const uint8_t *p_prev = src + (y - 1) * stride;
            const uint8_t *p_curr = src + y * stride;
            const uint8_t *p_next = src + (y + 1) * stride;
            uint8_t *p_out = dst + y * stride;

            for (int x = 1; x < width - 1; x++)
            {
                const int blur = (
                    p_prev[x-1] + (p_prev[x] << 1) + p_prev[x+1] +
                    (p_curr[x-1] << 1) + (p_curr[x] << 2) + (p_curr[x+1] << 1) +
                    p_next[x-1] + (p_next[x] << 1) + p_next[x+1]
                ) >> 4;

                const int detail = (int)p_curr[x] - blur;

                int abs_d = (detail ^ (detail >> 31)) - (detail >> 31);
                int mask = -(abs_d >= threshold);
                int final_detail = (detail * (mask & 1));

                int boost = (final_detail * amount) >> 7;
                boost = (boost > limit) ? limit : (boost < -limit ? -limit : boost);

                p_out[x] = (uint8_t)clamp_u8((int)p_curr[x] + boost);
            }
        }
    }
}