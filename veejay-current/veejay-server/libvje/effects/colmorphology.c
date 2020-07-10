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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "colmorphology.h"

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
	ve->limits[1][2] = 1; // type
	
    ve->defaults[0] = 140;
    ve->defaults[1] = 1;
    ve->defaults[2] = 0;

    ve->description = "Colored Morphology";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold","Kernel", "Dilate or Erode");
    return ve;
}

typedef struct {
    uint8_t *binary_img;
} colmorph_t;

void *colmorphology_malloc(int w, int h )
{
    colmorph_t *c = (colmorph_t*) vj_calloc(sizeof(colmorph_t));
    if(!c) {
        return NULL;
    }
	c->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h) );
    if(!c->binary_img) {
        free(c);
        return NULL;
    }

    return (void*) c;
}

void		colmorphology_free(void *ptr)
{
    colmorph_t *c = (colmorph_t*) ptr;
    free(c->binary_img);
    free(c);
}

static inline uint8_t _dilate_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if((kernel[x] * img[x]) > 0 )
			return pixel_Y_hi_;
	return pixel_Y_lo_;
}


static inline uint8_t _erode_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if(kernel[x] && img[x] == 0 )
			return pixel_Y_lo_;
	return pixel_Y_hi_;
}

void colmorphology_apply( void *ptr, VJFrame *frame, int *args ) {
    int threshold = args[0];
    int type = args[1];
    int passes = args[2];

    colmorph_t *c = (colmorph_t*) ptr;

    unsigned int i,x,y;
	const unsigned int width = frame->width;
	int len = frame->len;
	uint8_t *Y = frame->data[0];
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

    uint8_t *binary_img = c->binary_img;

	for( i = 0; i < len; i ++ )
	{
		binary_img[i] = (  Y[i] < threshold ? 0: 0xff );
	}

	len -= width;

	/* compute dilation of binary image with kernel */

	if( passes == 0 ) {
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
					Y[x+y] = _dilate_kernel3x3(kernels[type], mt);
				}
			}
		}
	}
	else {
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
					Y[x+y] = _erode_kernel3x3( kernels[type], mt );
				}
			}
		}
	}
}
