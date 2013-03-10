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
#include "rawval.h"
#include <stdlib.h>
#include <math.h>
vj_effect *rawval_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
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
	ve->parallel = 1;
    ve->sub_format = 0;
    ve->description = "Raw Chroma Pixel Replacement";
	ve->has_user = 0;
    ve->extra_frame = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Old Cb", "Old Cr", "New Cb", "New Cr" );
    return ve;
}



void rawval_apply( VJFrame *frame, int width, int height,
		  const int color_cb, const int color_cr,
		  const int new_color_cb, const int new_color_cr)
{
    unsigned int i;
	int uv_len = frame->uv_len;
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	if( frame->ssm )
		uv_len = frame->len;

    for (i = 0; i < uv_len; i++) {
	if (Cb[i] >= new_color_cb)
	    Cb[i] = color_cb;
	if (Cr[i] >= new_color_cr)
	    Cr[i] = color_cr;
    }
}
void rawval_free(){}
