/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdlib.h>
#include "chromapalette.h"
#include "common.h"


vj_effect *chromapalette_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 16;
    ve->limits[1][0] = 240;
	ve->limits[0][1] = 16;
	ve->limits[1][1] = 240;
    ve->defaults[0] = 35;
	ve->defaults[1] = 56;
    ve->description = "Chrominance palette";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;
    return ve;
}

void chromapalette_apply(uint8_t *yuv[3], int width, int height, int color_cb, int color_cr )
{
	const int len = (width * height);
	unsigned int i;
	double tmp;
	uint8_t t;
	uint8_t *Y = yuv[0];
	uint8_t *Cb = yuv[1];
	uint8_t *Cr = yuv[2];
	uint8_t U;
	uint8_t V;
    const float cb_mul = 0.492;
	const float cr_mul = 0.877;
	/*

				chrominance is defined as the difference between a color and a reference value luminance

				U = blue - Y
			    V = red - Y

				this effect does

				U = color_cb - Y
			 	V = color_cr - Y
				
			 	4:2:0 is supersampled to 4:4:4 so there is a chroma value for every Y

	*/
    if(color_cb == 0 && color_cr == 0 ) return;

	if(color_cb != 0 && color_cr != 0)
	{
		for( i = 0 ; i < len ; i ++ )
		{
				U = 128 + (int)( (float) (color_cb - Y[i]) * cb_mul );
				if(U < 16) U = 16; else if ( U > 240 ) U = 240;
				V = 128 + (int)( (float) (color_cr - Y[i]) * cr_mul );
				if(V < 16) V = 16; else if ( V > 240 ) V = 240;
				Cb[i] = U;
				Cr[i] = V;
		}
	}
	if(color_cr == 0 )
	{
		for( i = 0 ; i < len ; i ++ )
		{
				V = 128 + (int)( (float) (color_cr - Y[i]) * cr_mul );
				if( V < 16 ) V = 16; else if ( V > 240 ) V = 240;
				Cr[i] = V;
		}
	}
	if(color_cb == 0 )
	{
		for( i = 0 ; i < len ; i ++ )
		{
				U = 128 + (int)( (float) (color_cb - Y[i]) * cb_mul );
				if( U < 16 ) U = 16; else if ( U > 240 ) U = 240;
				Cb[i] = U;
		}
	}

 
}
