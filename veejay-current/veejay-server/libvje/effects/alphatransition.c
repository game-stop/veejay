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
#include <libvjmem/vjmem.h>
#include <veejay/vj-task.h>
#include "alphatransition.h"

/* almost the same as masktransition.c, but adding threshold and direction parameters */

vj_effect *alphatransition_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);		/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 0xff;

    ve->defaults[0] = 0; 
    ve->defaults[1] = 50;
	ve->defaults[2] = 0;
	ve->defaults[3] = 30;

    ve->description = "Alpha: Transition Mask";
	ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->parallel = 1;
	ve->alpha = FLAG_ALPHA_SRC_A;
		 
	ve->param_description = vje_build_param_list(ve->num_params, "Time Index", "Smooth", "Direction", "Threshold" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2, "Channel A", "Channel B" );

    return ve;
}

static inline void alpha_blend1(uint8_t *Y,
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
static inline void alpha_blend2(uint8_t *Y,
						const uint8_t *Y2,
						const uint8_t *AA,
						size_t w)
{
	size_t j;
	for( j = 0; j < w; j ++ )
	{
		Y[j] = (((0xff-AA[j]) * Y[j]) + (AA[j] * Y2[j])) >> 8;
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
									const uint8_t *a1, const size_t len, const size_t w, 
									unsigned int time_index, const unsigned int dur, const int direction, const int threshold )
{
	uint8_t lookup[256];
	uint8_t AA[ RUP8(w+7) ];

	const uint8_t *T  = (const uint8_t*) lookup;
	const uint8_t *aA = a0;

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
	
		if( lookup[i] < threshold )
			lookup[i] = 0;
	}

	if( direction == 0 )
	{
		for( i = 0; i < len; i += w )
		{
			/* unroll the lookup table so we can vectorize */
			expand_lookup_table( AA, T, aA + i, w );

			alpha_blend1( Y + i, Y2 + i, AA, w );
			alpha_blend1( Cb+ i, Cb2+ i, AA, w );
			alpha_blend1( Cr+ i, Cr2+ i, AA, w );
		}
	} 
	else
	{
		for( i = 0; i < len; i += w )
		{
			/* unroll the lookup table so we can vectorize */
			expand_lookup_table( AA, T, aA + i, w );

			alpha_blend2( Y + i, Y2 + i, AA, w );
			alpha_blend2( Cb+ i, Cb2+ i, AA, w );
			alpha_blend2( Cr+ i, Cr2+ i, AA, w );
		}

	}
}

void alphatransition_apply( VJFrame *frame, VJFrame *frame2, int time_index, int duration, int direction, int threshold  )
{
	alpha_blend_transition(
		frame->data[0],frame->data[1],frame->data[2],frame->data[3],
		frame2->data[0],frame2->data[1],frame2->data[2],frame->data[3],
		frame->len,
		frame->width,
		time_index,
		duration + 1,
	    direction,
		threshold
	);
}


