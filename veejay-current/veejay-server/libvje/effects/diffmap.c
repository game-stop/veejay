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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "diffmap.h"
#include "common.h"
#include "softblur.h"

typedef int (*morph_func)(uint8_t *kernel, uint8_t mt[9] );

vj_effect *differencemap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // threshold
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;  // reverse
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1; // show map
    ve->defaults[0] = 40;
    ve->defaults[1] = 0;
    ve->defaults[2] = 1;
    ve->description = "Map B to A (bitmask)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Reverse", "Show");
    return ve;
}


static uint8_t *binary_img = NULL;
static int nframe = 0;

int		differencemap_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w*h*2) );
	nframe = 0;
	if(!binary_img) return 0;
	return 1;
}

void		differencemap_free(void)
{
	if(binary_img) 
		free(binary_img);
	binary_img = NULL;
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

static int _dilate_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if((kernel[x] * img[x]) > 0 )
			return 1;
	return 0;
}

void differencemap_apply( VJFrame *frame, VJFrame *frame2,int width, int height, int threshold, int reverse,
		int show )
{
	unsigned int i,x,y;
	int len = (width * height);
    	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];

	const uint8_t kernel[9] = { 1,1,1, 1,1,1, 1,1,1 };

//	morph_func	p = _dilate_kernel3x3;
	uint8_t *previous_img = binary_img + len;
//@	take copy of image
	vj_frame_copy1( Y, previous_img, len );

	VJFrame tmp;
	veejay_memcpy(&tmp, frame, sizeof(VJFrame));
	tmp.data[0] = previous_img;
	softblur_apply( &tmp, width,height,0 );

	binarify_1src( binary_img,previous_img,threshold,reverse, width,height);
	//@ clear image

	if(show)
	{
		vj_frame_copy1( binary_img, frame->data[0], len );
		vj_frame_clear1( frame->data[1],128, len);
		vj_frame_clear1(frame->data[2],128, len);
		return;
	}

	veejay_memset( Y, 0, width );
	veejay_memset( Cb, 128, width );
	veejay_memset( Cr, 128, width );

	len -= width;

//	if(!reverse)
//	{
		for(y = width; y < len; y += width  )
		{	
			for(x = 1; x < width-1; x ++)
			{	
				if(binary_img[x+y]) //@ found white pixel
				{
				/*	uint8_t mt[9] = {
					binary_img[x-1+y-width], binary_img[x+y-width], binary_img[x+1+y-width],
					binary_img[x-1+y], 	binary_img[x+y]	    , binary_img[x+1+y],
					binary_img[x-1+y+width], binary_img[x+y+width], binary_img[x+1+y+width]
					};
					if( p( kernel, mt ) ) //@ replace pixel for B
					{
						 Y[x + y] = Y2[x+y];
						Cb[x + y] = Cb2[1][x+y];
						Cr[x + y] = Cr[2][x+y];
					}
					else //@ black
					{
						Y[x + y] = 0;
						Cb[x + y] = 128;
						Cr[x+ y] = 128;
					}*/
					Y[x+y] = Y2[x+y];
					Cb[x+y] = Cb2[x+y];
					Cr[x+y] = Cr2[x+y];
				}
				else
				{
					Y[x+y] = 0;
					Cb[x+y] = 128;
					Cr[x+y] = 128;
				}
			}
		}
//	}
/*	else
	{
		for(y = width; y < len; y += width  )
		{	
			for(x = 1; x < width-1; x ++)
			{	
				if(!binary_img[x+y]) //@ found black pixel
				{
				uint8_t mt[9] = {
					0xff-binary_img[x-1+y-width], 0xff-binary_img[x+y-width], 0xff-binary_img[x+1+y-width],
					0xff-binary_img[x-1+y], 	0xff-binary_img[x+y]	    , 0xff-binary_img[x+1+y],
					0xff-binary_img[x-1+y+width], 0xff-binary_img[x+y+width], 0xff-binary_img[x+1+y+width]
					};
				if( p( kernel, mt ) )
				{
					 Y[x + y] = frame2->data[0][x+y];
					Cb[x + y] = frame2->data[1][x+y];
					Cr[x + y] = frame2->data[2][x+y];
				}
				else
				{
					Y[x + y] = 0;
					Cb[x + y] = 128;
					Cr[x + y] = 128;
				}
			}
		}
	}
//#endif
*/
}
