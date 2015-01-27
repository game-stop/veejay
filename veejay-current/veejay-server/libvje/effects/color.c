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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "color.h"
vj_effect *color_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
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
	ve->param_description = vje_build_param_list( ve->num_params, "Intensity Y", "Intensity U", "Intensity V" ); 
	ve->has_user = 0;
    ve->extra_frame = 0;
	ve->parallel = 1;
    return ve;
}


void color_apply(VJFrame *frame, int width, int height,
		 int opacity_a, int opacity_b,
		 int opacity_c)
{
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

	int p1,p2,q1,q2;

	const int uv_len = frame->uv_len;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	for (i = 0; i < uv_len; i++) {
		p1 = Cb[i];
		p2 = Cr[i];
	
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

 		Cb[i] = q1;
		Cr[i] = q2;

    	}

}
void color_free(){}
