/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "average.h"

typedef struct {
    double *running_sum[4];
    int last_params[2];
    int frame_count;
} average_t;

vj_effect *average_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 1000;
    ve->defaults[0] = 1;
	ve->parallel = 1; 
    ve->description = "Running Average";
    ve->sub_format = 1; 
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Number of frames" );

	return ve;
}

void *average_malloc(int width, int height)
{
    average_t *a = (average_t*) vj_calloc(sizeof(average_t));
    if(!a) {
        return NULL;
    }
	a->running_sum[0] = (double*) vj_calloc( sizeof(double) * (width * height * 3 ));
	if(!a->running_sum[0]) {
		free(a);
        return NULL;
    }
	a->running_sum[1] = a->running_sum[0] + (width*height);
	a->running_sum[2] = a->running_sum[1] + (width*height);
	a->frame_count = 1;
    return (void*) a;
}

void average_free(void *ptr) 
{
    average_t *a = (average_t*) ptr;
	free(a->running_sum[0]);
    free(a);
}	

void average_apply(void *ptr, VJFrame *frame, int *args) {
    int max_sum = args[0];
    double weight = 1.0 / max_sum;

    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    average_t *a = (average_t *)ptr;
    double *running_sum[3];
    running_sum[0] = a->running_sum[0] + frame->offset;
    running_sum[1] = a->running_sum[1] + frame->offset;
    running_sum[2] = a->running_sum[2] + frame->offset;

    for (int i = 0; i < len; i++) {
        running_sum[0][i] = (1 - weight) * running_sum[0][i] + weight * Y[i];
        running_sum[1][i] = (1 - weight) * running_sum[1][i] + weight * (Cb[i] - 128);
        running_sum[2][i] = (1 - weight) * running_sum[2][i] + weight * (Cr[i] - 128);
    }

    for (int i = 0; i < len; i++) {
        Y[i] = (uint8_t)running_sum[0][i];
        Cb[i] = (uint8_t)(128 + running_sum[1][i]);
        Cr[i] = (uint8_t)(128 + running_sum[2][i]);
    }
}

