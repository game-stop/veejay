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
#include <math.h>
#include <stdio.h>
#include "common.h"
vj_effect *whiteframe_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 100;	/* saturation value */
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 36000;	/* degrees */
    ve->defaults[0] = 1;
    ve->defaults[1] = 100;
    ve->defaults[2] = 180;
    ve->description = "Replace Pure White";;
    ve->extra_frame = 1;
    ve->sub_format = 0;
    ve->has_internal_data = 0;
    return ve;
}

/* this method was created for magic motion */
void whiteframe_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		      int height, int val, int degrees, int type)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t p, q;
    double s, saturation, color;
    double deg = degrees / 100.0;
    int cb, cr;
    double res = 0;
    /* look for white pixels in luminance channel and swap with yuv2 */
    for (i = 0; i < len; i++) {
	p = yuv1[0][i];
	if (p > 234) {
	    yuv1[0][i] = yuv2[0][i];
	}
    }

    s = val / 100.0;
    len >>= 2;			/* len = len / 4 */
    /* saturate the difference frame */
    for (i = 0; i < len; i++) {
	p = yuv1[1][i];
	q = yuv1[2][i];
	if (p != 128 && q != 128) { // skip neutrals   
	    cb = p - 128;
	    cr = yuv1[2][i] - 128;
	    if (cb != 0 && cr != 0) {
		fast_sqrt( saturation, (double)(cb * cb) + (cr * cr));
		color = asin(cb / saturation);
		if (cr < 0)
		    color = M_PI - color;
		color += (deg * M_PI) / 180.0;
		saturation *= s;
		fast_sin( res, color );
		yuv1[1][i] = res * saturation + 128;
		yuv1[2][i] = res * saturation + 128;
	    }
	} else {
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }
}
void whiteframe_free(){}
