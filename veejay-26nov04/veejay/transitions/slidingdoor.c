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

#include "transblend.h"
#include "../effects/common.h"
#include <stdlib.h>

vj_effect *slidingdoor_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1; /* hori or verti */
    ve->defaults[1] = 1; /* initial size */
    ve->defaults[2] = 1; /* auto or not auto */
    //ve->defaults[3] = 25; /* max frames */

    ve->limits[0][1] = 1;
    ve->limits[1][1] = height / 16;

    ve->sub_format = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    //ve->limits[0][3] = 1;
    //ve->limits[1][3] = (25 * 120);

    ve->description = "Transition Sliding Door";
    ve->has_internal_data = 0;
    ve->extra_frame = 1;
    return ve;
}



void slidingdoor_apply( VJFrame *frame, VJFrame *frame2, int width,
		       int height, int n, int size)
{

	if( n== 0)
	{
		frameborder_yuvdata( frame->data[0],frame->data[1],frame->data[2],
							   frame2->data[0],frame2->data[1],frame2->data[2],
							   width,height, size*16, size*16, 0, 0, frame->shift_h, frame->shift_v );
	}
	else
	{
		frameborder_yuvdata( frame->data[0],frame->data[1],frame->data[2],
							   frame2->data[0],frame2->data[1],frame2->data[2],
							   width,height, 0,0, size*16,size*16, frame->shift_h, frame->shift_v );

	}

}
void slidingdoor_free(){}
