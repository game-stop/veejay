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



void opacity_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		   int height, int opacity)
{
    unsigned int i, op0, op1;
    unsigned int len = width * height;
    unsigned int uv_len = len / 4;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    for (i = 0; i < len; i++) {
	yuv1[0][i] = (op0 * yuv1[0][i] + op1 * yuv2[0][i]) >> 8;
    }

    for (i = 0; i < uv_len; i++) {
	yuv1[1][i] = (op0 * yuv1[1][i] + op1 * yuv2[1][i]) >> 8;
	yuv1[2][i] = (op0 * yuv1[2][i] + op1 * yuv2[2][i]) >> 8;
    }
 
}

void opacity_free(){}
