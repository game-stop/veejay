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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "porterduff.h"
#include <stdlib.h>
#include <math.h>
#include "common.h"

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
/*
 * This is a port of the gegl blend operators to veejay
 */

vj_effect *porterduff_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;	/* operator */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 13;

	ve->param_description = vje_build_param_list(ve->num_params, "Operator");
	ve->has_user = 0;
    ve->description = "Porter Duff operations (RGBA)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->rgb_conv = 0;
    ve->parallel = 1;
	ve->rgba_only = 1;
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, 0, ve->limits[1][0], 
		"Dest", "Dest Atop", "Dest In", "Dest Over", "Dest Out", "Src Over", "Src Atop", "Src In", "Src Out", "SVG Multiply", "XOR", "ADD", "SUBTRACT", "DIVIDE" );

	return ve;
} 

static void porterduff_dst( uint8_t *A, uint8_t *B, int n_pixels)
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ ) 
	{
		for( j = 0; j < 3; j ++ )
		{
			aA[j] = bB[j];
		}
		aA[3] = bB[3];
		aA += 4;
		bB += 4;
	}	
}

static void porterduff_atop( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels ; i ++ ) 
	{
		for( j = 0; j < 3; j ++ ) 
		{
			aA[j] = ( (aA[j] * bB[3]) + bB[j] * ( 255 - aA[3] ) ) >> 8;
		}
		aA[3] = bB[3];
		aA += 4;
		bB += 4;
	}
}
static void porterduff_dst_in( uint8_t *A, uint8_t *B, int n_pixels)
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i++ ) 
	{
		for( j = 0; j < 3; j ++ ) 
		{
			aA[j] = ( (aA[j] * bB[3]) >> 8 );
		}
		aA[3] = (aA[3] * bB[3]) >> 8;
		aA += 4;
		bB += 4;
	}
}
static void porterduff_dst_out( uint8_t *A, uint8_t *B, int n_pixels)
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = (aA[3] * ( 0xff - bB[3] )) >> 8;
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = ( aA[j] * ( 0xff - bB[3] ) ) >> 8;
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}

static void porterduff_dst_over( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ((bB[3] + aA[3]) - (bB[3] * aA[3]))>>8;
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = (aA[j] + bB[j]) * (0xff - aA[j]) >> 8; 
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}

static void porterduff_src_over( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = ( ( bB[j] * aA[3]) + aA[j] * ( 0xff - bB[j]) ) >> 8;
		}
		aA += 4;
		bB += 4;
	}
}

static void porterduff_src_atop( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ( (aA[3] * bB[3] ) >> 8 );
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = (bB[j] * aA[j]) >> 8;
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}

static void porterduff_src_in( uint8_t *A, uint8_t *B, int n_pixels)
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ( (aA[3] * bB[3] ) >> 8 );
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = (bB[j] * aA[3]) >> 8;
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}

static void porterduff_src_out( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ( bB[3] * ( 0xff - aA[3])) >> 8;
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = (bB[j] * ( 0xff - aA[3] ) ) >> 8;
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}

static void svg_multiply( uint8_t *A, uint8_t *B, int n_pixels )
{	
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ( bB[3] + aA[3] - bB[3] * aA[3]) >> 8;
		for( j = 0; j < 3 ; j ++ )
		{
			aA[j] = _CLAMP( (( bB[j] * aA[j] + bB[3] * ( 0xff - aA[3]) + aA[j]  * (0xff - bB[3]) ) >> 8), 0, aD );
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}

static void xor( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ( bB[3] + aA[3] - 2 * bB[3] * aA[3]) >> 8;
		for( j = 0; j < 3; j ++ ) 
		{
			aA[j] = (bB[j] * ( 0xff - aA[3]) + aA[j] * ( 0xff - bB[3]) ) >> 8;
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}
static void add( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		for( j = 0; j < 3; j ++ ) 
		{
			unsigned int sum = aA[j] + bB[j];
			aA[j] = ( sum > 255 ? 255: sum );
		}
		aA[3] = (aA[3] < bB[3] ? aA[3] : bB[3]);
		aA += 4;
		bB += 4;
	}
}
static void subtract( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		for( j = 0; j < 3; j ++ ) 
		{
			int sum = aA[j] - bB[j];
			aA[j] = ( sum < 0 ? 0: sum );
		}
		aA[3] = ( (aA[3] - bB[3]) < 0 ? 0 : aA[3] - bB[3]);
		aA += 4;
		bB += 4;
	}
}
/* 
 * FIXME: test
static void screen( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = _CLAMP( bB[3] + aA[3] - ((bB[3] * aA[3]) >> 8), 0 , 255);

		for( j = 0; j < 3; j ++ ) 
		{
			aA[j] = _CLAMP( (bB[j] + aA[j] - bB[j] * aA[j]) >>8,0, aD );
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}



static void difference( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		uint8_t aD = ( bB[3] + aA[3] - bB[3] * aA[3] ) >> 8;
		for( j = 0; j < 3; j ++ ) 
		{
			aA[j] = _CLAMP( bB[j] + aA[j] - 2 * ( MIN( bB[j] * aA[3], aA[j] * bB[3])),0, aD);
		}
		aA[3] = aD;
		aA += 4;
		bB += 4;
	}
}
*/

static void divide( uint8_t *A, uint8_t *B, int n_pixels )
{
	int i,j;
	uint8_t *aA = A;
	uint8_t *bB = B;

	for( i = 0; i < n_pixels; i ++ )
	{
		for( j = 0; j < 3; j ++ ) 
		{
			float d = ( bB[j] == 0 ? 0.0f : aA[j] / bB[j]);
			aA[j] = (uint8_t)( d * 0xff );
		}
		aA += 4;
		bB += 4;
	}
}

void porterduff_apply(VJFrame *frame, VJFrame *frame2, int width,int height, int mode)
{
	switch( mode )  
	{
		case 0:
			porterduff_dst( frame->data[0],frame2->data[0],frame->len );
			break;
		case 1:
			porterduff_atop( frame->data[0],frame2->data[0], frame->len );
			break;
		case 2:
			porterduff_dst_in( frame->data[0],frame2->data[0], frame->len );
			break;
		case 3:
			porterduff_dst_over( frame->data[0],frame2->data[0],frame->len );
			break;
		case 4:
			porterduff_dst_out( frame->data[0],frame2->data[0],frame->len );
			break;
		case 5:
			porterduff_src_over( frame->data[0],frame2->data[0],frame->len );
			break;
		case 6:
			porterduff_src_atop( frame->data[0],frame2->data[0],frame->len );
			break;
		case 7:
			porterduff_src_in( frame->data[0],frame2->data[0],frame->len );
			break;
		case 8:
			porterduff_src_out( frame->data[0],frame2->data[0],frame->len);
			break;
		case 9:
			svg_multiply( frame->data[0], frame2->data[0], frame->len );
			break;
		case 10:
			xor( frame->data[0], frame2->data[0], frame->len);
			break;
		case 11:
			add( frame->data[0], frame2->data[0], frame->len );
			break;
		case 12:
			subtract(frame->data[0],frame2->data[0],frame->len);
			break;
		case 13:
			divide(frame->data[0],frame2->data[0],frame->len);
			break;
	}
}
