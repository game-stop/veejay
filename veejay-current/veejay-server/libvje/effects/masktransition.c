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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "masktransition.h"
#include <veejay/vj-task.h>
#include "common.h"

vj_effect *masktransition_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000;
    ve->defaults[0] = 0; 
    ve->defaults[1] = 50;
    ve->description = "Alpha: Transition Map";
	ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->parallel = 0;
	ve->alpha = FLAG_ALPHA_SRC_A;
		 
	ve->param_description = vje_build_param_list(ve->num_params, "Time Index", "Smooth" );
    return ve;
}

static void alpha_blend(uint8_t *Y,
						const uint8_t *Y2,
						const uint8_t *AA,
						size_t w)
{
	size_t j;
	for( j = 0; j < w; j ++ )
	{
		Y[j] = ((AA[j] * Y[j]) + ((0xff-AA[j]) * Y2[j])) >> 8;
	}
}


static inline void expand_lookup_table( uint8_t *AA, const uint8_t *lookup, const uint8_t *aA, const size_t w )
{
	size_t j;		
	for( j = 0; j < w; j ++ )
	{
		AA[j] = lookup[ aA[j] ];
	}
}


static void	alpha_blend_transition( uint8_t *Y, uint8_t *Cb, uint8_t *Cr,
									const uint8_t *Y2, const uint8_t *Cb2, const uint8_t *Cr2,
									const uint8_t *aA, const size_t len, const size_t w, unsigned int time_index, const unsigned int dur )
{
	uint8_t lookup[256];
	size_t i;

	/* precalc lookup table for vectorization */
	for( i = 0; i < 256; i ++ )
	{
		if( time_index < aA[i] )
			lookup[i] = 0;
		else if ( time_index >= (aA[i] + dur) )
			lookup[i] = 0xff;
		else
			lookup[i] = 0xff * ( (double) (time_index - i ) / dur );
	}

	uint8_t AA[ RUP8(w) ];

	for( i = 0; i < len; i += w )
	{
		/* unroll the lookup table so we can vectorize */
		expand_lookup_table( AA, lookup, aA + i, w );

		alpha_blend( Y + i, Y2 + i, AA, w );
		alpha_blend( Cb+ i, Cb2+ i, AA, w );
		alpha_blend( Cr+ i, Cr2+ i, AA, w );

/*	c equivalent:		
 
 		op0 = lookup[ aA[i] ];
		op1 = 0xff - op0;

		Y[i] = ((op0 * Y[i]) + (op1 * Y2[i])) >> 8;
		Cb[i]= ((op0 * Cb[i])+ (op1 * Cb2[i]))>> 8;
		Cr[i]= ((op0 * Cr[i])+ (op1 * Cr2[i]))>> 8; 
*/
	}
}

static void alpha_transition_apply_job( void *arg )
{
	vj_task_arg_t *t = (vj_task_arg_t*) arg;

	alpha_blend_transition(
			t->input[0],t->input[1],t->input[2],
			t->output[0],t->output[1],t->output[2],
			t->input[3],
			t->strides[0],
			t->width,
			t->iparam,
			255
		);
}

/* fixme */
void	alpha_transition_apply( VJFrame *frame, uint8_t *B[4], int time_index )
{
/*	if(vj_task_available() ) {
		vj_task_set_from_frame( frame );
		vj_task_set_int( time_index );
		vj_task_run( frame->data, B, NULL, NULL, 4, (performer_job_routine) &alpha_transition_apply_job );
	} else { 
*/
		VJFrame Bframe;
		veejay_memcpy(&Bframe,frame,sizeof(VJFrame));
		Bframe.data[0] = B[0];
		Bframe.data[1] = B[1];
		Bframe.data[2] = B[2];
		Bframe.data[3] = B[3];
		masktransition_apply( frame, &Bframe,frame->width,frame->height,time_index,255 );
//	}
}


void masktransition_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int time_index, int duration  )
{
	alpha_blend_transition(
			frame->data[0],frame->data[1],frame->data[2],
			frame2->data[0],frame2->data[1],frame2->data[2],
			frame->data[3],
			frame->len,
			frame->width,
			time_index,
			duration + 1 );
}

/*
	for( i = 0; i < len; i ++ ) {
		if( time_index < aA[i] )
			op0 = 0;
		else if (time_index >= ( aA[i] + dur ) )
			op0	= 0xff;
		else 
			op0 = 0xff * ( (double)( time_index - aA[i] ) / dur); 
		
		op1 = 0xff - op0;

		Y[i] = ((op0 * Y[i]) + (op1 * Y2[i])) >> 8;
		Cb[i]= ((op0 * Cb[i])+ (op1 * Cb2[i]))>> 8;
		Cr[i]= ((op0 * Cr[i])+ (op1 * Cr2[i]))>> 8;
	}
*/


