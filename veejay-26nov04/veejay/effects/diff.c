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
#include "vj-effect.h"
#include "vj-common.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static int has_bg = 0;
static uint8_t *static_bg[3]; // static background image, for all instances of this effect
static int has_static_bg = 0;
static double *sqrt_table[256];

vj_effect *diff_init(int width, int height)
{
    int i,j;
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
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
    ve->has_internal_data = 0;
    ve->static_bg = 1;  
    // temporary buffer space for difference overlay
	ve->vjed = NULL;
//    ve->vjed = (vj_effect_data*) malloc(sizeof(vj_effect_data));
//    ve->vjed->data = NULL;
//    ve->vjed->internal_params = NULL;
    return ve;
}



int diff_malloc(vj_effect_data *d, int width, int height)
{
	if( d == NULL )
	{
		d = (vj_effect_data*) vj_malloc( sizeof(vj_effect_data) ) ;
		if(!d ) return 0;
		d->data = NULL;
		d->internal_params = NULL;
	}	
	if(d)
	{
		int i;
		d->data = (uint8_t*) vj_malloc(sizeof(uint8_t)*width*height);
	//	d->internal_params = (void*) malloc(sizeof(void) * 4);
		if(!d->data) return 0;
		static_bg[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height);
		memset( static_bg[0],0, width * height);
		for(i=0; i < 256; i ++) 
			sqrt_table[i] = (double*)vj_malloc(sizeof(double)* 256);

		return 1;
	}
	return 0;
}

void diff_free(vj_effect_data *d)
{
	if(d)
	{
		int i;   
		if(d->data) free(d->data);
		if(static_bg[0]) free(static_bg[0]);
	//	if(d->internal_params) free(d->internal_params);
		for(i = 0; i < 256 ; i ++)
			if( sqrt_table[i]) free(sqrt_table[i]);
		free(d);
	}
}

int   diff_can_apply()
{
	if(!has_static_bg) return 0;
	return 1;
}


int *diff_get_gaussian_table(int g_width)
{
	int *table = (int*)malloc(sizeof(int) * g_width);
	if(!table) return NULL;
	table[0] = 1;
	table[1] = 6;
	table[2] = 15;
	table[3] = 20;
	table[4] = 15;
	table[5] = 6;
	table[6] = 1;
	return table;
}


void diff_set_background(uint8_t *map[3], int width, int height)
{
	int d,e,x,y,len=width*height;
	uint8_t *luma_map = map[0];
   	// map[0] contains luma information of the frame
	int tmp,k,sum;
//	int g_width = 7;
//	int *g_table = diff_get_gaussian_table(g_width);	
	static_bg[0][0] = luma_map[0];	
	// first row, 3x1 average
	for(y=1; y < width; y++)
	{
		static_bg[0][y] = ( luma_map[y-1] + luma_map[y] + luma_map[y+1] ) / 3;
	}
	// 3x3 window average
	for(y=width; y < len-width; y+= width)
	{
		// first pixel on row
		static_bg[0][y] = luma_map[y];
		for(x=1; x < width-1; x++)
		{
			static_bg[0][y+x] = (

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
			/*	
			for(k=0; k < g_width; k++)
			{
				tmp = luma_map[x+y+k];
				sum += tmp * g_table[k];
			
			}
			sum = sum / 64;
			static_bg[0][x+y] = (uint8_t)sum;
			*/
		}
		// last pixel on row
		static_bg[0][y+x+1] = luma_map[y+x+1];
	}
	// last row, 3x3 average
	for(y=len-width; y < len; y++)
	{
		static_bg[0][y] = (luma_map[y-1] + luma_map[y+1] + luma_map[y] ) /3;
	}
	//if(g_table) free(g_table);

	// calculate distance vector
	for(d=0; d < 256; d ++)
	{
		for(e=0; e < 256;e++ )
		{
			sqrt_table[d][e] = sqrt( (d-e) * (d-e) );
		}
	}
	has_static_bg = 1;
	veejay_msg(VEEJAY_MSG_INFO, "[Effect]: Diff has static background map");
}


void diff_apply(vj_effect_data *ed, VJFrame *frame,
		VJFrame *frame2, int width, int height, 
		int K_level, int noise_level,int noise_level2, int mode)
{
    
	unsigned int i;
	unsigned int ps=0;
	unsigned int tmp;
	double d;
	int x,y;
	int sum,k,K = 0;
	double nn;
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

	if(ed==NULL) return;

	dst = ed->data;

	// calculate if pixel is much different (has greater distance)
	// accepted pixels are 0xff 
	if(!diff_can_apply())
		return 0;
	
	for(i = 0 ; i < len ; i ++ )
	{
		d = sqrt_table[ (static_bg[0][i]) ][ (Y[i]) ];
		if(d > level1)
		{
			dst[i] = 0xff;
		}
		else
		{
			dst[i] = 0x0;
		}
		d = sqrt_table[ (static_bg[0][i])][ (Y2[i]) ];
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
				Cr[i] = Cb2[i];
				Cr[i] = Cr[i];
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
					Y[i] = 16;
				}
				else
				{
					Y[i] = 235;
				}
			}
			Cr[i] = 128;
			Cr[i] = 128;
		}
	}
}




