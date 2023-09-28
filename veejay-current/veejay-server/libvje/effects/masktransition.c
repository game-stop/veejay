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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "masktransition.h"

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
	ve->parallel = 1;
	ve->alpha = FLAG_ALPHA_SRC_A;
		 
	ve->param_description = vje_build_param_list(ve->num_params, "Time Index", "Smooth" );
    return ve;
}

static inline void alpha_blend(uint8_t *Y,
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


static void	alpha_blend_transition( uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *a0,
									const uint8_t *Y2, const uint8_t *Cb2, const uint8_t *Cr2,
									const uint8_t *a1, const size_t len, const size_t w, unsigned int time_index, const unsigned int dur, const int alpha_select )
{
	uint8_t lookup[256];
	const uint8_t *T  = (const uint8_t*) lookup;
	const uint8_t *aA = (alpha_select == 0 ? a0 : a1);
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

	uint8_t AA[ w + 16 ];

	for( i = 0; i < len; i += w )
	{
		/* unroll the lookup table so we can vectorize */
		expand_lookup_table( AA, T, aA + i, w );

		alpha_blend( Y + i, Y2 + i, AA, w );
		alpha_blend( Cb+ i, Cb2+ i, AA, w );
		alpha_blend( Cr+ i, Cr2+ i, AA, w );
	}
}

#define SMOOTH_DEFAULT 256
#define USE_FROM_A	   0
#define USE_FROM_B	   1

static void alpha_transition_apply_job( void *arg )
{
	vj_task_arg_t *t = (vj_task_arg_t*) arg;
	alpha_blend_transition(
		t->input[0],t->input[1],t->input[2], t->input[3],
		t->output[0],t->output[1],t->output[2], t->output[3],
		t->strides[0],
		t->width,
		t->iparams[0],
		SMOOTH_DEFAULT,
		USE_FROM_A
	);
}

void	alpha_transition_apply( VJFrame *frame, uint8_t *B[4], int time_index )
{
	alpha_blend_transition(
		frame->data[0],frame->data[1],frame->data[2], frame->data[3],
		B[0],B[1],B[2],B[3],
		frame->len,
		frame->width,
		time_index,
		SMOOTH_DEFAULT,
	    USE_FROM_A
	);
}

void masktransition_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int time_index = args[0];
    int duration = args[1];

	alpha_blend_transition(
		frame->data[0],frame->data[1],frame->data[2],frame->data[3],
		frame2->data[0],frame2->data[1],frame2->data[2],frame->data[3],
		frame->len,
		frame->width,
		time_index,
		duration + 1,
	    USE_FROM_A
	);
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


