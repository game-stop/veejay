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
#include <stdlib.h>
#include "chromium.h"
#include "common.h"


vj_effect *chromium_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->defaults[0] = 0;
    ve->description = "Chromium";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;
    return ve;
}

void chromium_apply(uint8_t *yuv[3], int width, int height, int m )
{
	const int len = (width * height) /4;
	unsigned int i;
	double tmp;
	uint8_t t;
	switch(m)
	{
		case 0:
		for( i = 0; i < len ; i++)
		{
			yuv[1][i] = 0xff - yuv[1][i];
		}
		break;
		case 1:
		for( i = 0; i < len ; i++ )
		{
			yuv[2][i] = 0xff - yuv[2][i];
		}
		break;
		case 2:
		for( i = 0; i < len; i++)
		{
			yuv[1][i] = 0xff - yuv[1][i];
			yuv[2][i] = 0xff - yuv[2][i];
		}
		break;
		case 3:
		// swap cb/cr
		for (i = 0; i < len ; i ++ )
		{
			tmp = yuv[1][i];
			yuv[1][i] = yuv[2][i];
			yuv[2][i] = tmp;
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
			yuv[1][i] = t;
		}
		break;
*/
	}
 
}
