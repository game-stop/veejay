/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "borders.h"

vj_effect *borders_init(int width,int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */

	ve->defaults[0] = 41;
	ve->defaults[1] = 0;
	ve->limits[0][0] = 1;
	ve->limits[1][0] = (height / 2);
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 7;
	ve->description = "Colored Border Translation";
	ve->sub_format = 0;
	ve->extra_frame = 0;
	ve->has_user = 0;	
	ve->param_description = vje_build_param_list( ve->num_params, "Size", "Color");

	ve->hints = vje_init_value_hint_list (ve->num_params);
	vje_build_value_hint_list (ve->hints, ve->limits[1][1],1,
	                           "White", "Black", "Green", "Light Blue", "Rose",
	                           "Blue", "Red", "Yellow");

	return ve;
}

void borders_apply( void *ptr, VJFrame *frame, int *args ) {
    int size = args[0];
    int color = args[1];

	blackborder_yuvdata(frame->data[0], frame->data[1], frame->data[2],
						frame->width, frame->height, (size), (size), (size), (size),
						frame->shift_h, frame->shift_v,color);

}
