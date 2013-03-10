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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "dissolve.h"
#include <stdlib.h>

vj_effect *dissolve_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 150;
	ve->parallel = 1;
    ve->description = "Dissolve Overlay";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Opacity" );
    return ve;
}



void dissolve_apply(VJFrame *frame, VJFrame *frame2, int width,
		   int height, int opacity)
{
    unsigned int i;
    unsigned int len = frame->len;
    const int op1 = (opacity > 255) ? 255 : opacity;
    const int op0 = 255 - op1;

 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    for (i = 0; i < len; i++)
    {
	// set pixel as completely transparent or completely solid

	if(Y[i] > opacity) // completely transparent
	{
		Y[i] = (op0 * Y[i] + op1 * Y2[i]) >> 8;
		Cb[i] = (op0 * Cb[i] + op1 * Cb2[i]) >> 8;
		Cr[i] = (op0 * Cr[i] + op1 * Cr2[i]) >> 8;
	}
	else // pixel is solid
	{
		Y[i] = Y2[i];
		Cb[i] = Cb2[i];
		Cr[i] = Cr2[i];
	}

    }
}

void dissolve_free(){}
