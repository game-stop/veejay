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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <stdlib.h>
#include "frameborder.h"
#include "common.h"

vj_effect *frameborder_init(int width, int height)
{

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = width/8;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height / 2;
	ve->has_user = 0;
    ve->description = "Frame Border Translation";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Size");
    return ve;
}


void frameborder_apply(VJFrame *frame, VJFrame *frame2, int width,
		       int height, int size)
{
    frameborder_yuvdata(frame->data[0], frame->data[1], frame->data[2], frame2->data[0], frame2->data[1],
			frame2->data[2], width, height, (size), (size), (size),
			(size),frame->shift_h,frame->shift_v);

}
void frameborder_free(){}
