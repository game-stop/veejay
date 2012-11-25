/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
/*
	This effects overlays 2 images , 
	It allows the user to set the transparency per channel.
	Result will vary over different color spaces.

 */
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "tripplicity.h"
vj_effect *tripplicity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;	
    ve->limits[1][0] = 255; // opacity Y
    ve->limits[0][1] = 0;   
    ve->limits[1][1] = 255; // opacity Cb
    ve->limits[0][2] = 0;	
    ve->limits[1][2] = 255; // opacity Cr
    ve->defaults[0] = 150;
    ve->defaults[1] = 150;
    ve->defaults[2] = 150;

    ve->description = "Normal Overlay (per Channel)";
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->has_user = 0;
 	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Opacity Y", "Opacity Cb", "Opacity Cr" );

    return ve;
}



void tripplicity_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacityL, int opacityCb, int opacityCr)
{
    unsigned int i;
    const unsigned int len =  frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    const uint8_t *Y2 = frame2->data[0];
 	const uint8_t *Cb2= frame2->data[1];
	const uint8_t *Cr2= frame2->data[2];
    const uint8_t op1  = (opacityL > 255) ? 255 : opacityL;
    const uint8_t op0  = 255 - op1;
    const uint8_t opCb1= (opacityCb > 255) ? 255: opacityCb;
    const uint8_t opCb0= 255 - opCb1;
    const uint8_t opCr1= (opacityCr > 255) ? 255: opacityCr;
    const uint8_t opCr0= 255 - opCr1;

 
    for (i = 0; i < len; i++)
    {
		Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
    }

    for (i = 0; i < uv_len; i++)
    {
		Cb[i] = (opCb0 * Cb[i] + opCb1 * Cb2[i]) >> 8;
		Cr[i] = (opCr0 * Cr[i] + opCr1 * Cr2[i]) >> 8;
    }
 
}

