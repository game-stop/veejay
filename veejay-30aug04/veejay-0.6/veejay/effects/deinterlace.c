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

#include "common.h"
#include "deinterlace.h"
#include <stdlib.h>

vj_effect *deinterlace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 0;

    ve->description = "Deinterlace (yuvkineco)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;
    return ve;
}

void deinterlace_apply(uint8_t * yuv[3], int width, int height, int val)
{
	const unsigned int uv_width = width >> 1;
	const unsigned int uv_height = height >> 1;

	deinterlace( yuv[0], width,height,val);
	deinterlace( yuv[1], uv_width,uv_height,val);
	deinterlace( yuv[2], uv_width,uv_height,val);
}

void deinterlace_free(){}
