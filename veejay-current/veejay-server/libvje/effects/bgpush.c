/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
#include "common.h"
#include "bgpush.h"

static uint8_t *frame = NULL;
static uint8_t *frame_ptr[4] = { NULL,NULL,NULL,NULL };

vj_effect *bgpush_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->defaults[0] = 1;
    ve->description = "Push current frame to buffer";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list( ve->num_params, "Push Frame" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "Off" , "On" );

    return ve;
}

int bgpush_malloc(int w, int h)
{
	if( frame == NULL ) {
		frame =  (uint8_t*) vj_calloc( RUP8(w*h*4) );
		if( frame == NULL )
			return 0;
		frame_ptr[0] = frame;
		frame_ptr[1] = frame_ptr[0] + RUP8(w*h);
		frame_ptr[2] = frame_ptr[1] + RUP8(w*h);
		frame_ptr[3] = frame_ptr[2] + RUP8(w*h);
	}

	return 1;
}

void bgpush_free()
{
	if( frame ) {
		free(frame);
		frame = NULL;
		frame_ptr[0] = NULL;
		frame_ptr[1] = NULL;
		frame_ptr[2] = NULL;
		frame_ptr[3] = NULL;
	}
}


void bgpush_apply( VJFrame *frame, int mode )
{
	if( mode == 0 )
		return;

	const int uv_len = (frame->ssm ? frame->len : frame->len );
	veejay_memcpy( frame_ptr[0], frame->data[0], frame->len );
	veejay_memcpy( frame_ptr[1], frame->data[1], uv_len );
	veejay_memcpy( frame_ptr[2], frame->data[2], uv_len );
	if( frame->stride[3] > 0 )
		veejay_memcpy( frame_ptr[3], frame->data[3], frame->len );

}

uint8_t *bgpush_get_bg_frame( unsigned int plane )
{
	return frame_ptr[plane];
}
