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
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include "diffimg.h"
#include "common.h"

vj_effect *diffimg_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 6;/* type */
    ve->defaults[1] = 15;	/* min */
    ve->defaults[2] = 240;	/* max */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 7;
    /* 0,179,0253,0127 */
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;

    ve->limits[1][2] = 255;
    ve->limits[0][2] = 1;
	ve->param_description = vje_build_param_list( ve->num_params,"Mode", "Min threshold", "Max threshold" );
    ve->description = "Enhanced Magic Blend";
    ve->extra_frame = 0;
    ve->sub_format = 0;
	ve->has_user = 0;
    return ve;
}



void diffimg_apply(
	    VJFrame *frame,
		int width,
		int height,
		int type,
		int threshold_min,
		int threshold_max
		)
{
	unsigned int i;
	const int len = frame->len - width - 2;
	int d,m;
	uint8_t y,yb;
 	uint8_t *Y = frame->data[0];

	_pf _pff = _get_pf(type);

	for(i=0; i < len; i++)
	{
		y = Y[i];
		if( y < 16 ) y = 16; else if (y > pixel_Y_hi_) y = pixel_Y_hi_;
		yb = y;
		if(y >= threshold_min && y <= threshold_max)
		{
			m = (Y[i+1] + Y[i+width] + Y[i+width+1]+2) >> 2;
			d = Y[i] - m;
			d *= 500;
			d /= 100;
			m = m + d;
			y = ((((y << 1) - (255 - m))>>1) + Y[i])>>1;
			if(y < 16) y = 16; else if (y>pixel_Y_hi_) y = pixel_Y_hi_;
			Y[i] = _pff(y,yb);
		}
	}
}
void diffimg_free(){}
