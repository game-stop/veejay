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

#include "rawval.h"
#include <stdlib.h>
#include <math.h>
vj_effect *rawval_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 232;
    ve->defaults[1] = 16;
    ve->defaults[2] = 16;
    ve->defaults[3] = 16;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->sub_format = 0;
    ve->description = "Raw Chroma Pixel Replacement";
   ve->has_internal_data = 0;
    ve->extra_frame = 0;
    return ve;
}



static int *conemap = NULL;
static uint8_t *conebuf = NULL;
void rawval_apply(uint8_t * yuv1[3], int width, int height,
		  const int color_cb, const int color_cr,
		  const int new_color_cb, const int new_color_cr)
{
    unsigned int len = width * height;
    unsigned int i;

    len >>= 2;
    for (i = 0; i < len; i++) {
	if (yuv1[2][i] >= new_color_cb)
	    yuv1[2][i] = color_cb;
	if (yuv1[1][i] >= new_color_cr)
	    yuv1[1][i] = color_cr;
    }
}
void rawval_free(){}
