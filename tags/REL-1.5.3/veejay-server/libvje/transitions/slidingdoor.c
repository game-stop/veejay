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
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "transblend.h"
#include <libvje/effects/common.h>
#include <stdlib.h>

vj_effect *slidingdoor_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->defaults = (int *) vj_calloc(sizeof(int) * 1);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * 1);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * 1);	/* max */
 /*   ve->defaults[0] = 1; 
    ve->defaults[1] = 1;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = height / 16;
*/
    ve->sub_format = 1;

  /*  ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
*/
    ve->description = "AlphaLuma Overlay";
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = NULL;
    return ve;
}



void slidingdoor_apply( VJFrame *frame, VJFrame *frame2, int width,
		       int height, int size)
{

	//@ alpha luma
	unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	const uint8_t *Y2 = frame2->data[0];
     	uint8_t *Cb= frame->data[1];
        uint8_t *Cr= frame->data[2];
        const uint8_t *Cb2= frame2->data[1];
        const uint8_t *Cr2= frame2->data[2];


	for( i = 0; i < len ; i ++ )
	{
		unsigned int op0 = Y2[i];
		unsigned int op1 = 255 - op0;
		Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
		Cb[i] = (op0 * Cb[i] + op1 * Cb2[i]) >> 8;
		Cr[i] = (op0 * Cr[i] + op1 * Cr2[i]) >> 8;
	}
/*
	frameborder_yuvdata(
		frame->data[0],
		frame->data[1],
		frame->data[2],
		frame2->data[0],
		frame2->data[1],
		frame2->data[2],
		width,height,
		size*16,
		size*16,
		0,
		0,
		frame->shift_h,
		frame->shift_v );*/

}
void slidingdoor_free(){}
