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

    return (void*) m;
}

void meanfilter_free(void *ptr)
{
    mean_t *m = (mean_t*) ptr;

    free(m->mean);
    free(m);
}

void meanfilter_apply(void *ptr, VJFrame *frame, int *args)
{
    mean_t *m = (mean_t*) ptr;
    (void) args;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    #pragma omp single
    {
        veejay_memcpy(m->mean, frame->data[0], len);
    }

    #pragma omp for schedule(static)
    for(int y = 1; y < h - 1; y++) {
        const int row = y * w;
        const int prev = row - w;
        const int next = row + w;
        int sum = 0;

        for(int x = 1; x < w - 1; x++) {
            if(x == 1) {
                sum = m->mean[prev] + m->mean[prev + 1] + m->mean[prev + 2] +
                      m->mean[row]  + m->mean[row  + 1] + m->mean[row  + 2] +
                      m->mean[next] + m->mean[next + 1] + m->mean[next + 2];
            }
            else {
                const int lcol = m->mean[prev + x - 2] + m->mean[row + x - 2] + m->mean[next + x - 2];
                const int rcol = m->mean[prev + x + 1] + m->mean[row + x + 1] + m->mean[next + x + 1];

                sum += rcol - lcol;
            }

            frame->data[0][row + x] = (uint8_t)(sum / 9);
        }
    }
}