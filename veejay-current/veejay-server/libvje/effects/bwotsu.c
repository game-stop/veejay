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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "bwotsu.h"

vj_effect *bwotsu_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 3;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 0;
	ve->defaults[1] = 0xff;
	ve->defaults[2] = 0;

	ve->limits[0][0] = 0;
	ve->limits[1][0] = 1;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 0xff;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1;
    
	ve->description = "Black and White Mask by Otsu's method";
    
	ve->sub_format = -1;
	ve->extra_frame = 0;
	ve->has_user =0;
	ve->parallel = 1;
	
	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

	ve->param_description = vje_build_param_list( ve->num_params, "To Alpha", "Skew", "Invert" );

	return ve;
}

//@see https://en.wikipedia.org/wiki/Otsu's_method
static uint32_t	bwotsu( uint32_t *H, const int N )
{
	uint32_t threshold = 0;
	double wF, wB=0.0, mB, mF, between, max = 0.0;
	double sum = 0.0, sumB=0.0;
	uint32_t i;

	for( i = 0; i < 256; i++ ) 
	{
		wB += H[i];
		if( wB == 0 )
			continue;
		wF = N - wB;
		if( wF == 0 )
			break;
		sumB += ( i * H[i] );
		mB = sumB / wB;
		mF = (sum - sumB) / wF;
		between = wB * wF * pow( mB - mF , 2 );
		if( between > max ) {
			max = between;
			threshold = i;
		}
	}
	return threshold;
}

void bwotsu_apply(void *ptr, VJFrame *frame, int *args) {
    int mode = args[0];
    int skew = args[1];
    int invert = args[2];

	uint32_t Histogram[256];
	unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *A = frame->data[3];

	veejay_memset( Histogram, 0, sizeof( Histogram ) );

	if( skew != 0xff )
	{
		uint8_t Lookup[256];
		__init_lookup_table( Lookup, 256, 0.0f, 255.0f, 0.0f, (float)skew ); 
		for( i = 0; i < len; i ++ )
		{
			Histogram[ Lookup[ Y[i] ] ] += 1;
		}	
	}
	else
	{
		for( i = 0; i < len; i++ ) 
		{
			Histogram[ Y[i] ] += 1;
		}
	}
	
	uint32_t threshold = bwotsu( Histogram, len );

	uint8_t l = 0;
	uint8_t h = 0xff;

	if( invert ) {
			l = 0xff;
			h = 0;
	}

	switch( mode ) {
		case 0:
			for( i = 0; i < len; i ++ )
			{
				if( Y[i] < threshold )
					Y[i] = l;
				else
					Y[i] = h;
			}
			veejay_memset( frame->data[1], 128, (frame->ssm ? len : frame->uv_len) );
			veejay_memset( frame->data[2], 128, (frame->ssm ? len : frame->uv_len) );

			break;
		case 1:
			for( i = 0; i < len; i ++ )
			{
				if( Y[i] < threshold )
					A[i] = l;
				else
					A[i] = h;
			}
			break;
	}
}

