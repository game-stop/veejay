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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <libvje/effects/common.h>
#include "colormap.h"

vj_effect *colormap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->defaults[0] = 46;
    ve->defaults[1] = 109;
    ve->defaults[2] = 92;

    ve->description = "Color mapping";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Red","Green","Blue" );
    return ve;
}

static uint8_t u_[256];
static uint8_t v_[256];

void colormap_apply( VJFrame *frame, int width, int height, int r, int g, int b)
{
    int i;
    int len = (width * height);
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
	int dummy = 0;
	for(i = 1; i < 256; i ++ )
	{
		COLOR_rgb2yuv( (r % i),(g % i),(b % i), dummy, u_[i-1],v_[i-1]);

	}
    
    
/*    for (i = 0; i < len; i++) {
	*(Y) = val - *(Y);
	*(Y)++;
    }*/

    for (i = 0; i < len; i++) {
//	*(Cb) = val - *(Cb);
  //      *(Cb)++;
    //    *(Cr) = val - *(Cr);
//	*(Cr)++;
	*(Cb) = u_[ (*Y) ];
	*(Cr) = v_[ (*Y) ];	
	*(Cb)++;
	*(Cr)++;
	*(Y)++;
    }
}
