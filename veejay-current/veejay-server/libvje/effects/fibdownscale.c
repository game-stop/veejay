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
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "fibdownscale.h"

vj_effect *fibdownscale_init(int w, int h)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->description = "Fibonacci Downscaler";
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->limits[0][0] = 0;
    ve->limits[0][1] = 1;
    ve->limits[1][0] = 1;
    ve->limits[1][1] = 8;
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Fib" );
    ve->has_user =0;
    return ve;
}

void fibdownscale_apply(VJFrame *frame, VJFrame *frame2, int width,
			int height, int n)
{
    if (n == 0)
	_fibdownscale_apply(frame, frame2, width, height);
    if (n == 1)
	_fibrectangle_apply(frame, frame2, width, height);
}

void _fibdownscale_apply(VJFrame *frame, VJFrame *frame2, int width,
			 int height)
{

    unsigned i, f1;
    unsigned int len = frame->len / 2;
    unsigned int uv_len = frame->uv_len / 2;

 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];




    /* do fib over half of image. (now we have 2 squares in upper half) */
    for (i = 2; i < len; i++) {
		f1 = (i + 1) + (i - 1);
		Y[i] = Y2[f1];
    }

    /* copy over first half (we could use veejay_memcpy) */
//    for (i = len; i < (width * height); i++) {
//	Y[i] = Y[i - len];
  //  }
    veejay_memcpy( Y + len, Y, len ); 

    /* do the same thing for UV to get correct image */

    for (i = 2; i < uv_len; i++) {
	f1 = (i + 1) + (i - 1);
	Cb[i] = Cb2[f1];
	Cr[i] = Cr2[f1];
    }

	veejay_memcpy( Cb + uv_len, Cb , uv_len );
	veejay_memcpy( Cr + uv_len, Cr , uv_len );
}

void _fibrectangle_apply(VJFrame *frame, VJFrame *frame2, int width,
			 int height)
{
    unsigned int i, f1;
	const uint32_t len = frame->len /2;
	const uint32_t uv_len = frame->uv_len/2;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    for (i = 2; i < len; i++) {
		f1 = (i - 1) + (i - 2);
//		if (f1 < len)
		    Y[i] = Y2[f1];
//	else
//	    Y[i] = Y[(f1 - len)];
    }
  
  for (i = 2; i < uv_len; i++) {

	f1 = (i - 1) + (i - 2);
//	if (f1 < len) {
	    Cb[i] = Cb2[f1];
	    Cr[i] = Cr2[f1];
//	} else {
//	    Cb[i] = Cb2[f1 - len];
//	    Cr[i] = Cr2[f1 - len];
//	}
    }
}
void fibdownscale_free(){}
