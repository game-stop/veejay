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
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 1000;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1;
    ve->defaults[0] = 1;
	ve->defaults[1] = 0;
	ve->parallel = 0; //@ cannot run in parallel
    ve->description = "Average";
    ve->sub_format = 1; 
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Number of frames to average", "Mode");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1, "Running Average", "Average" );

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
    int mode = args[1];

    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    average_t *a = (average_t*) ptr;
    double **running_sum = a->running_sum;
    
	if( a->last_params[0] != max_sum || a->last_params[1] != mode || ( mode == 0 && a->frame_count == max_sum) )
	{
		veejay_memset( running_sum[0], 0, sizeof(double) * (len + len + len) );
		a->last_params[0] = max_sum;
		a->last_params[1] = mode;
		a->frame_count = 1;
	}

    int frame_count = a->frame_count;

	if( mode == 0 )
	{
		if( frame_count <= max_sum ) {
			for (i = 0; i < len; i++) {
				running_sum[0][i] += Y[i];
				running_sum[1][i] += Cb[i];
				running_sum[2][i] += Cr[i];
			}
		}

		if( frame_count > 2 ) {

			for (i = 0; i < len; i++) {
				Y[i] = (uint8_t)(running_sum[0][i] / frame_count );
				Cb[i] = (uint8_t)(running_sum[1][i] / frame_count );
				Cr[i] = (uint8_t)(running_sum[2][i] / frame_count );
			}
		}

		if( frame_count <= max_sum )
			frame_count ++;
	}
	else
	{
		for (i = 0; i < len; i++) {
			running_sum[0][i] += Y[i];
			running_sum[1][i] += Cb[i];
			running_sum[2][i] += Cr[i];
		}

		if( frame_count > 2 )
		{
			for (i = 0; i < len; i++) {
				Y[i] = (running_sum[0][i] / frame_count );
				Cb[i] = (running_sum[1][i] / frame_count );
				Cr[i] = (running_sum[2][i] / frame_count );
			}
		}
		frame_count ++;
	}

    a->frame_count = a->frame_count;
}

