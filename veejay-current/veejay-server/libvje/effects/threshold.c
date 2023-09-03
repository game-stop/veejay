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
#include "threshold.h"

typedef int (*morph_func)(uint8_t *kernel, uint8_t mt[9] );

vj_effect *threshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // threshold
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;  // reverse
    ve->limits[1][1] = 1;

    ve->defaults[0] = 40;
    ve->defaults[1] = 0;
    ve->description = "Map B from threshold mask";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Reverse" );
    return ve;
}

typedef struct {
    uint8_t *binary_img;
} threshold_t;


void	*threshold_malloc(int w, int h )
{
    threshold_t *t = (threshold_t*) vj_calloc( sizeof(threshold_t) );
    if(!t) {
        return NULL;
    }
	t->binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h) );
	if(!t->binary_img) {
        free(t);
        return NULL;
    }
	return (void*) t;
}

void		threshold_free(void *ptr)
{
    threshold_t *t = (threshold_t*) ptr;
	free(t->binary_img);
	free(t);
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

void threshold_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args )
{
	unsigned int y,x;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];

    int threshold = args[0];
    int reverse = args[1];

    threshold_t *t = (threshold_t*) ptr;

    uint8_t *binary_img = t->binary_img;

	softblur_apply_internal( frame, 0);

	binarify_1src( binary_img,Y,threshold,0, width,height);

//	len -= width;

	if(!reverse)
	{
		for(y = 0; y < len; y += width  )
		{	
			for(x = 0; x < width; x ++)
			{	
				if(binary_img[x+y]) //@ found white pixel
				{
					Y[x+y] = Y2[x+y];
					Cb[x+y] = Cb2[x+y];
					Cr[x+y] = Cr2[x+y];

				}
				else //@ black
				{
					Y[x + y] = 0;
					Cb[x + y] = 128;
					Cr[x + y] = 128;
				}
			}
		}
	}
	else
	{
		for(y = 0; y < len; y += width  )
		{	
			for(x = 0; x < width; x ++)
			{	
				if(binary_img[x+y] == 0x0) //@ found black pixel
				{
					Y[x+y] = Y2[x+y];
					Cb[x+y]= Cb2[x+y];
					Cr[x+y]= Cr2[x+y];
				}
				else
				{ 
					Y[x+y] = 0x0;
					Cb[x+y] = 128; 
					Cr[x+y] = 128;
				}
			}
		}
	}
}
