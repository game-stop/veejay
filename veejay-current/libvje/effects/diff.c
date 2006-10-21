/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include "diff.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef struct
{
	int has_bg;
	uint8_t *static_bg[3];
	double *sqrt_table[256];
	uint8_t *data;
} diff_data;

vj_effect *diff_init(int width, int height)
{
    //int i,j;
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;
    ve->limits[0][1] = 0;	/* threshold min */
    ve->limits[1][1] = 25500;
    ve->limits[0][2] = 0;	/* threshold difference min */
    ve->limits[1][2] = 25500;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 3000;
    ve->defaults[2] = 3000;
    ve->defaults[3] = 1;
    ve->description = "Difference Overlay";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 1;
	ve->user_data = NULL;
    return ve;
}



int diff_malloc(void **d, int width, int height)
{
	int i;
	diff_data *my;
	*d = (void*) vj_malloc(sizeof(diff_data));
	my = (diff_data*) *d;
	my->static_bg[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)* width * height);
	memset( my->static_bg[0], 0 , (width*height));
	my->data = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height );
	memset(my->data, 0, width * height);
	for(i=0; i < 256; i ++) 
		my->sqrt_table[i] = (double*)vj_malloc(sizeof(double)* 256);
	
	my->has_bg = 0;
	return 1;
}

void diff_free(void *d)
{
	if(d)
	{
		int i;
		diff_data *my = (diff_data*) d;
		if(my->static_bg[0]) free( my->static_bg[0] );
		if(my->data) free(my->data);
		for(i = 0; i < 256 ; i ++)
			if( my->sqrt_table[i]) free( my->sqrt_table[i]);
		free(d);
	}
	d = NULL;
}

void diff_prepare(void *user, uint8_t *map[3], int width, int height)
{
	diff_data *my = (diff_data*) user;
	int d,e,x,y,len=width*height;
	uint8_t *luma_map = map[0];
   	// map[0] contains luma information of the frame
//	int g_width = 7;
	my->static_bg[0][0] = luma_map[0];	
	// first row, 3x1 average
	for(y=1; y < width; y++)
	{
		my->static_bg[0][y] = ( luma_map[y-1] + luma_map[y] + luma_map[y+1] ) / 3;
	}
	// 3x3 window average
	for(y=width; y < len-width; y+= width)
	{
		// first pixel on row
		my->static_bg[0][y] = luma_map[y];
		for(x=1; x < width-1; x++)
		{
			my->static_bg[0][y+x] = (

				luma_map[x+y-width-1] +
				luma_map[x+y-width]   +
				luma_map[x+y-width+1] +

				luma_map[x+y+width-1] +
				luma_map[x+y+width+1] +
				luma_map[x+y+width] +

				luma_map[x+y-1 ] +
				luma_map[x+y+1 ] + 
				luma_map[x+y]
				) / 9; 
		}
		// last pixel on row
		my->static_bg[0][y+x+1] = luma_map[y+x+1];
	}
	// last row, 3x3 average
	for(y=len-width; y < len; y++)
	{
		my->static_bg[0][y] = (luma_map[y-1] + luma_map[y+1] + luma_map[y] ) /3;
	}
	// calculate distance vector
	for(d=0; d < 256; d ++)
	{
		for(e=0; e < 256;e++ )
		{
			my->sqrt_table[d][e] = sqrt( (d-e) * (d-e) );
		}
	}
	my->has_bg = 1;
}


void diff_apply(void *ed, VJFrame *frame,
		VJFrame *frame2, int width, int height, 
		int K_level, int noise_level,int noise_level2, int mode)
{
    
	unsigned int i;
	double d;
	int x,y;
	int K = 0;
	uint8_t *dst;
	double level1 = (double)noise_level / 100.0;
	double level2 = (double)noise_level2 / 100.0;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	diff_data *ud = (diff_data*) ed;
	uint8_t *map = (uint8_t*) ud->static_bg[0];
	double **tab = (double**) ud->sqrt_table;
 
	dst = ud->data;

	// calculate if pixel is much different (has greater distance)
	// accepted pixels are 0xff 
	if(!ud->has_bg)
	{
		printf("No static bg in has_bg\n");
		return;
	}

	for(i = 0 ; i < len ; i ++ )
	{
		d = tab[ ( map[i]) ][ (Y[i]) ];
		if(d > level1)
		{
			dst[i] = 0xff;
		}
		else
		{
			dst[i] = 0x0;
		}
		d = tab[ map[i]][ (Y2[i]) ];
		if(d > level2)
		{
			dst[i] = 0xf0;
		}
		
	}
	// anti alias frame to remove isolated white pixels

	
	for(y=width;  y < len-width; y+= width)
	{
		for(x=1; x < width-1; x ++)
		{
			if( dst[x+y] >= 0xf0)
			{	// have a bad influence on branch prediction
				// simple 3x3 window where the value of K
				// indicates whether to accept or discard an isolated pixel
		
				K = 1;
				if( dst[x+y-width] >= 0xf0 ) K++;
				if( dst[x+y+width] >= 0xf0 ) K++;
				if( dst[x+y-width+1] >= 0xf0 ) K++;
				if( dst[x+y+width+1] >= 0xf0 ) K++;
				if( dst[x+y+width-1] >= 0xf0 ) K++;
				if( dst[x+y-width-1] >= 0xf0 ) K++;
				if( dst[x+y-1] >= 0xf0) K++;
				if( dst[x+y+1] >= 0xf0) K++;
				if( K <= K_level ) dst[x+y] = 0x0; 
		
			}
		}
	}
	if(mode == 0)
	{

		// apply difference frame  
		for( i = 0; i < len ; i++)
		{
			if(dst[i] == 0xf0)
			{
				Y[i] = Y2[i];
				Cb[i] = Cb2[i];
				Cr[i] = Cr2[i];
			}
		}
	}
	else
	{
		// show different pixels in white
		for( i = 0; i < len ; i++)
		{
			if(dst[i] == 0xf0)
			{
				Y[i] = 200;
			}
			else
			{
				if(dst[i] != 0xff)
				{
					Y[i] = pixel_Y_lo_;
				}
				else
				{
					Y[i] = pixel_Y_hi_;
				}
			}
			Cr[i] = 128;
			Cr[i] = 128;
		}
	}
}




