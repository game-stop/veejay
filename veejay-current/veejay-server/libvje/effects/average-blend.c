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

#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include <veejay/vj-task.h>
#include "average-blend.h"

extern void ac_average(const uint8_t *src1, const uint8_t *src2, uint8_t *dest, int bytes);

vj_effect *average_blend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 1;
    ve->description = "Average (mixer)"; // expose aclib ac_average as vje fx
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->parallel = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Recursions"); 
    return ve;
}

static void average_blend_apply1( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int average_blend)
{
	unsigned int i;
	for( i = 0; i < average_blend; i ++ ) {
		ac_average( frame->data[0], frame2->data[0], frame->data[0], frame->len );
		ac_average( frame->data[1], frame2->data[1], frame->data[1], frame->uv_len );
		ac_average( frame->data[2], frame2->data[2], frame->data[2], frame->uv_len );
	}
}

static void	average_blend_apply_job( void *arg )
{
	vj_task_arg_t *t = (vj_task_arg_t*) arg;
	unsigned int i;
	for( i = 0; i < t->iparam; i ++ ) {
		ac_average( t->input[0], t->output[0], t->input[0], t->strides[0] );
		ac_average( t->input[1], t->output[1], t->input[1], t->strides[1] );
		ac_average( t->input[2], t->output[2], t->input[2], t->strides[2] );
	}
}

void average_blend_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int average_blend)
{
	average_blend_apply1( frame,frame2,width,height,average_blend );
}

void average_blend_applyN( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int average_blend)
{
	if( vj_task_available() ) {
		vj_task_set_from_frame( frame );
		vj_task_set_int( average_blend );
		vj_task_run( frame->data, frame2->data, NULL, NULL, 3, (performer_job_routine) &average_blend_apply_job );
	} else {
		average_blend_apply1( frame,frame2,width,height,average_blend );
	}
}
	
static void	average_blend_blend_apply1( uint8_t *src1[3], uint8_t *src2[3], int len, int uv_len, int average_blend )
{
}

void	average_blend_blend_apply( uint8_t *src1[3], uint8_t *src2[3], int len, int uv_len, int average_blend )
{
	if( vj_task_available() ) {
		vj_task_set_from_args( len,uv_len );
		vj_task_set_int( average_blend );
		vj_task_run( src1, src2, NULL, NULL, 3, (performer_job_routine) &average_blend_apply_job );
	} else {
		average_blend_blend_apply1( src1,src2,len,uv_len,average_blend );
	}
}

void average_blend_free(){}
