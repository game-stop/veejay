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
#include "common.h"
#include "complexsync.h"
#include <stdlib.h>

static uint8_t *c_outofsync_buffer[4] = { NULL,NULL,NULL, NULL };

vj_effect *complexsync_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height-1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = (25 * 10);
    ve->defaults[0] = 36;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;
    ve->description = "Out of Sync -Replace selection-";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;	
    ve->param_description = vje_build_param_list( ve->num_params, "Vertical size", "Mode", "Framespeed" );
    return ve;
}

int complexsync_malloc(int width, int height)
{
   c_outofsync_buffer[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(width*height*3) );
   c_outofsync_buffer[1] = c_outofsync_buffer[0] + RUP8(width*height);
   c_outofsync_buffer[2] = c_outofsync_buffer[1] + RUP8(width*height);
  
   vj_frame_clear1( c_outofsync_buffer[0] , pixel_Y_lo_ , RUP8(width*height));
   vj_frame_clear1( c_outofsync_buffer[1] , 128, RUP8(width*height*2) );
   return 1;

}

void complexsync_free() {
	 if(c_outofsync_buffer[0])
	    free(c_outofsync_buffer[0]);
   	 c_outofsync_buffer[0] = NULL;
}

void complexsync_apply(VJFrame *frame, VJFrame *frame2, int width, int height, int val)
{

	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	int region = width * val;

	int strides[4] = { region, region, region, 0 };
	int planes[4] = { len, len, len, 0 };
	
	int i;

	vj_frame_copy( frame->data, c_outofsync_buffer, planes );
	vj_frame_copy( frame2->data, frame->data, planes );

        if( (len - region) > 0)
	{
		uint8_t *dest[4] = { Y + region, Cb + region, Cr + region, NULL };
		int dst_strides[4] = { len - region, len - region, len - region,0 };

		vj_frame_copy( c_outofsync_buffer, dest, dst_strides );
	}
}
