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

#include "whiteframe.h"
#include <stdlib.h>
#include "../subsample.h"

vj_effect *whiteframe_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->description = "Replace Pure White";;
    ve->extra_frame = 1;
    ve->sub_frame = 0;
    ve->has_internal_data = 0;
    return ve;
}

/* this method was created for magic motion */
void whiteframe_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		      int height, int val, int degrees)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t p;
    double s, saturation, color;
    double deg = degrees / 100.0;
    int cb, cr;

    /* look for white pixels in luminance channel and swap with yuv2 */
    for (i = 0; i < len; i++) {
	p = yuv1[0][i];
	if (p == 240) {
	    yuv1[0][i] = yuv2[0][i];
	}
    }

    len >>= 2;			/* len = len / 4 */

    for (i = 0; i < len; i++) {
	p = yuv1[1][i];
	if (p == 128) {
	    yuv1[1][i] = yuv2[1][i];
	    yuv2[2][i] = yuv2[2][i];
	}
    }
}
void whitereplace_free(){}
