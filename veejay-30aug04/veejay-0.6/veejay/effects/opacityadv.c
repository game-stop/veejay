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

#include "opacityadv.h"
#include "../../config.h"
#include <stdlib.h>
#include "../subsample.h"


vj_effect *opacityadv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[0] = 150;
    ve->defaults[1] = 40;
    ve->defaults[2] = 176;
    ve->description = "Overlay by Threshold Range";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
    return ve;
}

void opacityadv_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		      int height, int opacity, int threshold,
		      int threshold2)
{

    unsigned int x, y, len = width * height;
    uint8_t a1, a2, d;
    unsigned int op0, op1;
    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    for (y = 0; y < len; y += width) {
	for (x = 0; x < width; x++) {
	    a1 = yuv1[0][x + y];
	    a2 = yuv2[0][x + y];
	    /*
	       if(a2 > a1) {
	       d = abs(a2-a1);
	       if (d > threshold && d < threshold2) {
	       yuv1[0][x+y] = (op0 * a1 + op1 * a2 )/255;
	       yuv1[1][x+y] = (op0 * yuv1[1][x+y] + op1 * yuv2[1][x+y])/255;
	       yuv1[2][x+y] = (op0 * yuv1[2][x+y] + op1 * yuv2[2][x+y])/255;
	       }
	       } */
	    if (a1 >= threshold && a1 <= threshold2) {
		yuv1[0][x + y] = (op0 * a1 + op1 * a2) >> 8;

		yuv1[1][x + y] =
		    (op0 * yuv1[1][x + y] + op1 * yuv2[1][x + y]) >> 8;
		yuv1[2][x + y] =
		    (op0 * yuv1[2][x + y] + op1 * yuv2[2][x + y]) >> 8;
	    }
	}
    }
}
void opacityadv_free(){}
