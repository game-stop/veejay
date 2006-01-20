/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nelburg@looze.net>
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
#include "morphology.h"
#include <stdlib.h>

typedef uint8_t (*morph_func)(uint8_t *kernel, uint8_t mt[9] );

vj_effect *morphology_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // threshold
    ve->limits[1][0] = 255;
	ve->limits[0][1] = 0; // morpology operator (dilate,erode, ... )
	ve->limits[1][1] = 8;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1; // passes
    ve->defaults[0] = 140;
	ve->defaults[1] = 0;
	ve->defaults[2] = 0;
    ve->description = "Morphology (experimental)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
    return ve;
}


static uint8_t *binary_img;

int		morphology_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!binary_img) return 0;
	return 1;
}

void		morphology_free(void)
{
	if(binary_img)
		free(binary_img);
	binary_img = NULL;
}

static uint8_t _dilate_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if((kernel[x] * img[x]) > 0 )
			return 235;
	return 16;
}


static uint8_t _erode_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if(kernel[x] && img[x] == 0 )
			return 16;
	return 235;
}

morph_func	_morphology_function(int i)
{
	if( i == 0 )
		return _dilate_kernel3x3;
	if( i == 1 )
		return _erode_kernel3x3;
}


void morphology_apply( VJFrame *frame, int width, int height, int threshold, int type, int passes )
{
    unsigned int i,x,y;
    int len = (width * height);
	int c = 0,t=0,k=0;
    int uv_len = frame->uv_len;
	uint8_t pixel;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t kernels[4][9] ={
		 { 1,1,1, 1,1,1 ,1,1,1 },//0
		 { 0,1,0, 1,1,1, 0,1,0 },//1
		 { 0,0,0, 1,1,1, 0,0,0 },//2
		 { 0,1,0, 0,1,0, 0,1,0 },//3
		 { 0,0,1, 0,1,0, 1,0,0 },//4	
		 { 1,0,0, 0,1,0, 0,0,1 },	
		 { 1,1,1, 0,0,0, 0,0,0 },
		 { 0,0,0, 0,0,0, 1,1,1 }
		};

	morph_func	p = _morphology_function(passes);

	/* threshold image  --> binary img
		0 = bg
	      255 = fg
	 */
	for( i = 0; i < len; i ++ )
	{	binary_img[i] = (  Y[i] < threshold ? 0: 235 );t++;}

	memset( Cb, 128, uv_len );
	memset( Cr, 128, uv_len );	

	len -= width;
	if(type > 7 ) return;


	/* compute dilation of binary image with kernel */
	for(y = width; y < len; y += width  )
	{	
		for(x = 1; x < width-1; x ++)
		{	
			if(binary_img[x+y] == 0)
			{
				uint8_t mt[9] = {
					binary_img[x-1+y-width], binary_img[x+y-width], binary_img[x+1+y-width],
					binary_img[x-1+y], 	binary_img[x+y]	    , binary_img[x+1+y],
					binary_img[x-1+y+width], binary_img[x+y+width], binary_img[x+1+y+width]
					};
				/*
				uint8_t it[9] = {
					Y[x-1+y-width], Y[x+y-width], Y[x+1+y-width],
					Y[x-1+y], 	Y[x+y]	    , Y[x+1+y],
					Y[x-1+y+width], Y[x+y+width], Y[x+1+y+width]

					};
				*/		

				Y[x+y] = p( kernels[type], mt );
				c++;
			}
		}
	}
	len -=width;
}
