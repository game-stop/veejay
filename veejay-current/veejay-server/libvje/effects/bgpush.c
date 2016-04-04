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
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include "common.h"
#include "bgpush.h"

static uint8_t *frame_data = NULL;
static uint8_t *frame_ptr[4] = { NULL,NULL,NULL,NULL };

vj_effect *bgpush_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 0;
    ve->description = "Background take-frame (singleton)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->global = 1;

    return ve;
}

int bgpush_malloc(int w, int h)
{
	if( frame_data == NULL ) {
		frame_data =  (uint8_t*) vj_malloc( RUP8(w*h*4) );
		if( frame_data == NULL )
			return 0;
		frame_ptr[0] = frame_data;
		frame_ptr[1] = frame_ptr[0] + RUP8(w*h);
		frame_ptr[2] = frame_ptr[1] + RUP8(w*h);
		frame_ptr[3] = frame_ptr[2] + RUP8(w*h);

		veejay_memset( frame_ptr[0], 0, w * h );
		veejay_memset( frame_ptr[1], 128, w * h );
		veejay_memset( frame_ptr[2], 128, w * h );
		veejay_memset( frame_ptr[3], 0, w * h );
	}

	return 1;
}

void bgpush_free()
{
	if( frame_data ) {
		free(frame_data);
		frame_data = NULL;
		frame_ptr[0] = NULL;
		frame_ptr[1] = NULL;
		frame_ptr[2] = NULL;
		frame_ptr[3] = NULL;
	}
}

static int have_bg = 0;

int bgpush_prepare( VJFrame *frame )
{
	if( frame_data == NULL )
		return 0;
	const int uv_len = (frame->ssm ? frame->len : frame->uv_len );
	
	veejay_memcpy( frame_ptr[0], frame->data[0], frame->len );
	veejay_memcpy( frame_ptr[1], frame->data[1], uv_len );
	veejay_memcpy( frame_ptr[2], frame->data[2], uv_len );

	if( frame->stride[3] > 0 )
		veejay_memcpy( frame_ptr[3], frame->data[3], frame->len );

	if( frame->ssm == 0 ) {
		chroma_supersample( SSM_422_444, frame, frame_ptr );
	}

	have_bg = 1;

	return 1;
}

//FIXME: issue #78 , background frame in 4:4:4
//alpha channel should be cleared according to info->settings->alpha_value
void bgpush_apply( VJFrame *frame )
{
	if( have_bg == 0 ) {
		veejay_msg(0, "BG Push: Snap the background frame with VIMS 339 or mask button in reloaded");
	}
}

uint8_t *bgpush_get_bg_frame( unsigned int plane )
{
	if( frame_data == NULL )
		return NULL;
	return frame_ptr[plane];
}
