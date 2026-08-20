/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "meanfilter.h"

typedef struct {
    uint8_t *mean;
    int n_threads;
} mean_t;

vj_effect *meanfilter_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->description = "Mean Filter (3x3)";
    ve->sub_format = -1;
    return ve;
}

void *meanfilter_malloc(int w, int h)
{
    mean_t *m = (mean_t*) vj_malloc(sizeof(mean_t));

    if(!m)
        return NULL;

    m->mean = (uint8_t*) vj_malloc((size_t)w * (size_t)h);

    if(!m->mean) {
        free(m);
        return NULL;
    }

    m->n_threads = vje_advise_num_threads(w * h);

    return (void*) m;
}

void meanfilter_free(void *ptr)
{
    mean_t *m = (mean_t*) ptr;

    free(m->mean);
    free(m);
}

static void vje_mean_filter(const uint8_t *restrict src,
                            uint8_t *restrict dst,
                            int w,
                            int h,
                            int n_threads)
{
#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 1; y < h - 1; y++) {
        const int row = y * w;
        const int prev = row - w;
        const int next = row + w;
        int sum = 0;

        for(int x = 1; x < w - 1; x++) {
            if(x == 1) {
                sum = src[prev] + src[prev + 1] + src[prev + 2] +
                      src[row]  + src[row  + 1] + src[row  + 2] +
                      src[next] + src[next + 1] + src[next + 2];
            }
            else {
                const int lcol = src[prev + x - 2] + src[row + x - 2] + src[next + x - 2];
                const int rcol = src[prev + x + 1] + src[row + x + 1] + src[next + x + 1];

                sum += rcol - lcol;
            }

            dst[row + x] = (uint8_t)(sum / 9);
        }
    }
}

void meanfilter_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) args;

    mean_t *m = (mean_t*) ptr;

    veejay_memcpy(m->mean, frame->data[0], frame->len);
    vje_mean_filter(m->mean, frame->data[0], frame->width, frame->height, m->n_threads);
}
