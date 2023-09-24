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
#include "softblur.h"
#include "diffmap.h"

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

typedef struct {
    uint8_t *binary_img;
} diffmap_t;

void *differencemap_malloc(int w, int h )
{
    diffmap_t *d = (diffmap_t*) vj_calloc(sizeof(diffmap_t));
    if(!d) {
        return NULL;
    }

	d->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8( (w*h*2) + (w*2)) );
	if(!d->binary_img) {
        free(d);
        return NULL;
    }

    return (void*) d;
}

void		differencemap_free(void *ptr)
{
    diffmap_t *d = (diffmap_t*) ptr;
    free(d->binary_img);
    free(d);
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

void differencemap_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int threshold = args[0];
    int reverse = args[1];
	int show = args[2];
    
    diffmap_t *d = (diffmap_t*) ptr;

	unsigned int x,y;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];
    uint8_t *binary_img = d->binary_img;

//	morph_func	p = _dilate_kernel3x3;
	uint8_t *previous_img = binary_img + len;
//@	take copy of image
	vj_frame_copy1( Y, previous_img, len );

	VJFrame tmp;
	veejay_memcpy(&tmp, frame, sizeof(VJFrame));
	tmp.data[0] = previous_img;
	softblur_apply_internal( &tmp, 0);

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

	for(y = width; y < len; y += width  )
	{	
		for(x = 1; x < width-1; x ++)
        {	
			if(binary_img[x+y]) //@ found white pixel
			{
				Y[x+y] = Y2[x+y];
		    	Cb[x+y] = Cb2[x+y];
				Cr[x+y] = Cr2[x+y];
			}
			else
			{
	    		Y[x+y] = pixel_Y_lo_;
				Cb[x+y] = 128;
				Cr[x+y] = 128;
			}
		}
	}
}
