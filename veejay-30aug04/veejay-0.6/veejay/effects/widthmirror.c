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

#include "widthmirror.h"
#include <stdlib.h>
#include <stdio.h>
vj_effect *widthmirror_init(int max_width,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 2;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = max_width;

    ve->description = "Width Mirror";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

void widthmirror_apply(uint8_t * yuv[3], int width, int height,
		       int width_div)
{
    unsigned int r, c;


    const unsigned int len = (width * height);
    const unsigned int uv_len = len / 4;
    const unsigned int uv_width = width / 2;
    const unsigned int uv_height = height/2;
    const unsigned int uv_width_div = width_div;
    int p1;
    uint8_t x1, x2, x3;

    if (width_div >= width || width_div < 2)
	width_div = 2;

    for (r = width; r < len; r += width) {
	unsigned int divisor = width / width_div;
	for (c = 0; c < width; c++) {
	    if (divisor - c < 0)
		p1 = c - divisor + r;
	    else
		p1 = divisor - c + r;
	    x1 = yuv[0][c + r];
	    yuv[0][p1] = x1;
	    yuv[0][width - c + r] = x1;
	}
    }
    for (r = uv_width; r < uv_len; r += uv_width) {
	unsigned int divisor = uv_width / uv_width_div;
	for (c = 0; c < uv_width; c++) {
	    if (divisor - c < 0)
		p1 = c - divisor + r;
	    else
		p1 = divisor - c + r;
	
	    x2 = yuv[1][c + r];
	    yuv[1][p1] = x2;
	    yuv[1][uv_width - c + r] = x2;

	    x3 = yuv[2][c + r];
	    yuv[2][p1] = x3;
	    yuv[2][uv_width - c + r] = x3;
	
	}
    }





}
void widthmirror_free(){}
