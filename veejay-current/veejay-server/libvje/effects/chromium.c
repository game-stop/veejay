/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "chromium.h"
#include "common.h"


vj_effect *chromium_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->defaults[0] = 0;
    ve->description = "Chromium";
   	ve->parallel = 1;
	ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode" );
    return ve;
}

void chromium_apply(VJFrame *frame, int width, int height, int m )
{
	const int len = frame->uv_len;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	unsigned int i;
	double tmp;
	switch(m)
	{
		case 0:
		for( i = 0; i < len ; i++)
		{
			Cb[i] = 0xff - Cb[i];
		}
		break;
		case 1:
		for( i = 0; i < len ; i++ )
		{
			Cr[i] = 0xff - Cr[i];
		}
		break;
		case 2:
		for( i = 0; i < len; i++)
		{
			Cb[i] = 0xff - Cb[i];
			Cr[i] = 0xff - Cr[i];
		}
		break;
		case 3:
		// swap cb/cr
		for (i = 0; i < len ; i ++ )
		{
			tmp = Cb[i];
			Cb[i] = Cr[i];
			Cr[i] = tmp;
		}
		break;
/*
		case 4:
		// U - blue - Y , V = Red - Y
		// U = 0.492 * ( B - Y )
		for( i = 0; i < len; i++)
		{
			tmp = 0.492 * (VJ_EFFECT_CB_YELLOW - yuv[0][i * 2]);
			t = 128 + (uint8_t) tmp;
			Cb[i] = t;
		}
		break;
*/
	}
 
}
