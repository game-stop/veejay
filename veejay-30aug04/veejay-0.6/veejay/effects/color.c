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

#include "color.h"
#include <stdlib.h>
vj_effect *color_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 150;
    ve->defaults[1] = 150;
    ve->defaults[2] = 150;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->sub_format = 0;
    ve->description = "Color Enhance";
    ve->has_internal_data = 0;
    ve->extra_frame = 0;
    return ve;
}


void color_apply(uint8_t *yuv1[3], int width, int height,
		 int opacity_a, int opacity_b,
		 int opacity_c)
{
	const unsigned int len = width * height;
	unsigned int i;
	const unsigned int op_a0 = 255 - opacity_a; 
	const unsigned int op_b0 = 255 - opacity_b;
	const unsigned int op_c0 = 255- opacity_c;

	const unsigned int cb_a = opacity_a * 100;
	const unsigned int cb_c = opacity_c * 212;
	const unsigned int cb_b = opacity_b * 72;

	const unsigned int cr_a = opacity_a * 212;
	const unsigned int cr_b = opacity_b * 58;
	const unsigned int cr_c = opacity_c * 114;

	uint8_t p1,p2,q1,q2;

	const unsigned int uv_len = len / 4;
	
	

	for (i = 0; i < uv_len; i++) {
		p1 = yuv1[1][i];
		p2 = yuv1[2][i];
	
		q1 = (
			((op_a0 * p1 + cb_a)>>8) +
			((op_b0 * p1 + cb_b)>>8) +
			((op_c0 * p1 + cb_c)>>8)) ;
		q2 = (
			((op_a0 * p2 + cr_a)>>8) +
			((op_b0 * p2 + cr_b)>>8) +
			((op_c0 * p2 + cr_c)>>8)) ;

		if( q1 > 512) q1 = q1 / 3;
		else if( q1 > 255) q1 = q1 >> 1;

		if( q2 > 512) q2 = q2 / 3;
		else if (q2 > 255) q1 = q2 >> 1;

 		yuv1[1][i] = q1;
		yuv1[2][i] = q2;

    	}

}
void color_free(){}
