/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include "colmorphology.h"
#include "common.h"
typedef uint8_t (*morph_func)(uint8_t *kernel, uint8_t mt[9] );

vj_effect *colmorphology_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // threshold
    ve->limits[1][0] = 255;
    
	ve->limits[0][1] = 0; // morpology operator (dilate,erode, ... )
	ve->limits[1][1] = 8;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1; // passes
	
    ve->defaults[0] = 140;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;

    ve->description = "Colored Morphology";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold","Operator mode","Repeat");
    return ve;
}


static uint8_t *binary_img;

int		colmorphology_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!binary_img) return 0;
	return 1;
}

void		colmorphology_free(void)
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
			return pixel_Y_hi_;
	return pixel_Y_lo_;
}


static uint8_t _erode_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if(kernel[x] && img[x] == 0 )
			return pixel_Y_lo_;
	return pixel_Y_hi_;
}

static morph_func	_morphology_function(int i)
{
	if( i == 0 )
		return _dilate_kernel3x3;
	return _erode_kernel3x3;
}


void colmorphology_apply( VJFrame *frame, int width, int height, int threshold, int type, int passes )
{
	unsigned int i,x,y;
	int len = (width * height);
	int t=0;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t kernels[8][9] ={
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

	for( i = 0; i < len; i ++ )
	{
		binary_img[i] = (  Y[i] < threshold ? 0: 0xff );
		t++;
	}

	len -= width;

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
				Y[x+y] = p( kernels[type], mt );
			}
		}
	}
}
