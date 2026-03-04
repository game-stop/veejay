/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
#include <config.h>
#include <unistd.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "meanfilter.h"

typedef struct {
	uint8_t *mean;
} mean_t;


vj_effect *meanfilter_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Mean Filter (3x3)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    return ve;
}

void *meanfilter_malloc(int w, int h)
{
	mean_t *m = (mean_t*) vj_malloc(sizeof(mean_t));
	if(!m) 
		return NULL;
	m->mean = (uint8_t*) vj_calloc( (w*h) );
	
	if(m->mean == NULL ) {
		free(m);
		return NULL;
	}

	return (void*) m;
}

void meanfilter_free(void *ptr)
{
	if(!ptr) return;

	mean_t *m = (mean_t*) ptr;
	if(m->mean)
	  free(m->mean);
	free(m);
	m = NULL;
}


static void vje_mean_filter(const uint8_t *restrict src, uint8_t *restrict dst, int w, int h)
{
    int n = vje_advise_num_threads(w*h);
    #pragma omp parallel for num_threads(n)
    for (int y = 1; y < h - 1; y++) {
        int row_idx  = y * w;
        int prev_row = (y - 1) * w;
        int next_row = (y + 1) * w;

        int sum = src[prev_row + 0] + src[prev_row + 1] + src[prev_row + 2]
                + src[row_idx + 0] + src[row_idx + 1] + src[row_idx + 2]
                + src[next_row + 0] + src[next_row + 1] + src[next_row + 2];

        dst[row_idx + 1] = sum / 9;

        for (int x = 2; x < w - 1; x++) {
            int lcol = src[prev_row + x - 2] + src[row_idx + x - 2] + src[next_row + x - 2];
            int rcol = src[prev_row + x + 1] + src[row_idx + x + 1] + src[next_row + x + 1];

            sum = sum - lcol + rcol;
            dst[row_idx + x] = sum / 9;
        }
    }
}

void meanfilter_apply( void *ptr, VJFrame *frame, int *args )
{
	mean_t *m = (mean_t*) ptr;
	vje_mean_filter( frame->data[0], m->mean, frame->width, frame->height );

	veejay_memcpy( frame->data[0], m->mean, frame->len );
}
