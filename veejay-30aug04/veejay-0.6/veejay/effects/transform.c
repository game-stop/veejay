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
#include <stdlib.h>
#include <stdio.h>
#include "transform.h"
#include "common.h"

vj_effect *transform_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    //ve->defaults[1] = 1;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = (height / 16);

    //ve->limits[0][1] = 1;
    //ve->limits[1][1] = 4096;

    ve->description = "Transform Cubics";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void transform_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		     int height, const int size)
{
    unsigned int ty, tx, y, x;
    const unsigned int uv_height = height >> 1;
    const unsigned int uv_width = width >> 1;

    /* Luminance */
    for (y = 1; y < height; y++) {
	ty = y % size - (size >> 1);
	if ((y / size) % 2) {
	    ty = y - ty;
	} else {
	    ty = y + ty;
	}
	if (ty < 0)
	    ty = 0;

	if (ty >= height)
	    ty = height - 1;

	for (x = 1; x < width; x++) {
	    tx = x % size - (size >> 1);
	    if ((x / size) % 2) {
		tx = x - tx;
	    } else {
		tx = x + tx;
	    }
	    if (tx < 0)
		tx = 0;
	    if (tx >= width)
		tx = width - 1;
	    yuv1[0][x + (y * width)] = yuv2[0][(ty * width) + tx];
	}
    }
    /* Chroma */
    for (y = 1; y < uv_height; y++) {
	ty = y % size - (size >> 1);
	if ((y / size) % 2) {
	    ty = y - ty;
	} else {
	    ty = y + ty;
	}
	if (ty < 0)
	    ty = 0;

	if (ty >= uv_height)
	    ty = uv_height - 1;

	for (x = 1; x < uv_width; x++) {
	    tx = x % size - (size >> 1);
	    if ((x / size) % 2) {
		tx = x - tx;
	    } else {
		tx = x + tx;
	    }
	    if (tx < 0)
		tx = 0;
	    if (tx >= uv_width)
		tx = uv_width - 1;
	    yuv1[1][x + (y * uv_width)] = yuv2[1][(ty * uv_width) + tx];
	    yuv1[2][x + (y * uv_width)] = yuv2[2][(ty * uv_width) + tx];
	}
    }
}
void transform_free(){}
