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
#include "whiteframe.h"

vj_effect *whiteframe_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->defaults = NULL;	/* default values */
    ve->limits[0] = NULL;	/* min */
    ve->limits[1] = NULL;	/* max */
    ve->description = "Replace Pure White";;
    ve->extra_frame = 1;
    ve->sub_format = 0;
	ve->has_user = 0;
	ve->parallel = 1;	
	ve->param_description = NULL;
    return ve;
}

/* this method was created for magic motion */
void whiteframe_apply( VJFrame *frame, VJFrame *frame2, int width,
		      int height)
{
    unsigned int i;
    const int len = frame->len;
	const int uv_len = frame->uv_len;
    uint8_t p;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];
    /* look for white pixels in luminance channel and swap with yuv2 */
    for (i = 0; i < len; i++)
	{
		p = Y[i];
		if (p >= 235)
		{
		    Y[i] = Y2[i];
		}
    }

    for (i = 0; i < uv_len; i++)
	{
		p = Cb[i];
		if (p == 128)
		{
		    Cb[i] = Cb2[i];
		    Cr[i] = Cr2[i];
		}
    }
}
void whitereplace_free(){}
