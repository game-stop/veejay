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
#include <config.h>
#include "opacity.h"

vj_effect *opacity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;
    ve->description = "Normal Overlay";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
 
    return ve;
}



void opacity_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacity)
{
    unsigned int i, op0, op1;
    unsigned int len =  frame->len;
    unsigned int uv_len = frame->uv_len;

  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;



    for (i = 0; i < len; i++) {
	Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
    }

    for (i = 0; i < uv_len; i++) {
	Cb[i] = (op0 * Cb[i] + op1 * Cb2[i]) >> 8;
	Cr[i] = (op0 * Cr[i] + op1 * Cr2[i]) >> 8;
    }
 
}

void opacity_free(){}
