/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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

/*  Blend
    (add, substract,mult, divide) between source image and constant value
    cat libvje/internal.h |grep BLEND
    Most of the blending parameters ignore p3 and p4 (default values 128)

 */

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "constantblend.h"
#include "common.h"
vj_effect *constantblend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 31;
    ve->limits[0][1] = 1;  // scale from 0.0 to 5.0 (only luma)
    ve->limits[1][1] = 500;
    ve->limits[0][2] = pixel_Y_lo_;
    ve->limits[1][2] = pixel_Y_hi_;
    ve->defaults[0] = 1;   // blend type (additive)
    ve->defaults[1] = 110; // scale before blend
    ve->defaults[2] = 16;  // constant Y
    ve->description = "Constant Luminance Blend";
    ve->parallel = 1;
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Threshold", "Constant" );
    return ve;
}

void constantblend_apply( VJFrame *frame, int width, int height, 
	int type, int scale, int valY )
{
	unsigned int i;
	const int len = (width * height);
	const uint8_t y = (uint8_t) valY;
	const float   s = ((float) scale / 100.0 );

	pix_func_Y	blend_y    = get_pix_func_Y( type );

	uint8_t *Y = frame->data[0];

	for (i = 0; i < len; i++)
	{
		int tmp_val =(int)( ((float) *(Y)) * s);
		*(Y)++ = blend_y( (uint8_t) ( (uint8_t) tmp_val ) , y ); 
   	}

}
