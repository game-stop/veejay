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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "average.h"
#include "common.h"

static double *running_sum[4] = { NULL, NULL, NULL, NULL };
static int last_params[2] = { 0,0 };
static int frame_count = -1;

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

int	average_malloc(int width, int height)
{
	running_sum[0] = (double*) vj_calloc( sizeof(double) * RUP8(width * height * 3 ));
	if(!running_sum[0])
		return 0;
	running_sum[1] = running_sum[0] + RUP8(width*height);
	running_sum[2] = running_sum[1] + RUP8(width*height);
	return 1;
}

void average_free() 
{
	if(running_sum[0]) {
		free(running_sum[0]);
		running_sum[0] = NULL;
	}
	last_params[0] = 0;
	last_params[1] = 0;
	frame_count = -1;
}	

void average_apply(VJFrame *frame, int max_sum, int mode)
{
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    
	if( last_params[0] != max_sum || last_params[1] != mode || ( mode == 0 && frame_count == max_sum) )
	{
		veejay_memset( running_sum[0], 0, sizeof(double) * (len + len + len) );
		last_params[0] = max_sum;
		last_params[1] = mode;
		frame_count = 1;
	}

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
}

