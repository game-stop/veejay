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
#include <libvjmem/vjmem.h>
#include <veejay/vj-viewport.h>
#include "zoom.h"

vj_effect *zoom_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = width/2;
    ve->defaults[1] = height/2;
    ve->defaults[2] = 50;
    ve->defaults[3] = 1;
	ve->defaults[4] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = width;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 10;
    ve->limits[1][2] = 100;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

	ve->limits[0][4] = 0;
	ve->limits[1][4] = 1;

    ve->description = "Zoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	
	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

	ve->param_description = vje_build_param_list( ve->num_params, "Width", "Height", "Factor", "Mode", "Update Alpha" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3, "Forward", "Reverse" );


    return ve;
}
static int zoom_[4] = { 0,0,0,0 };
static void *zoom_vp_ = NULL;

static uint8_t *zoom_private_[4] = { NULL, NULL, NULL, NULL };

int	zoom_malloc(int width, int height)
{
	int i;
	for( i = 0; i < 4; i ++ ) {	
		zoom_private_[i] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(width*height+width));
		if(!zoom_private_[i])
			return 0;
	}

	return 1;
}

void zoom_free() {
	int i;
	for( i = 0; i < 4; i ++ ) {	
	if( zoom_private_[i] )
		free(zoom_private_[i] );
	zoom_private_[i] = NULL;
	}
	if( zoom_vp_ )
		viewport_destroy( zoom_vp_ );
	zoom_vp_ = NULL;
}

void zoom_apply( VJFrame *frame, int x, int y, int factor, int dir, int alpha)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

	if( zoom_[0] != x || zoom_[1] != y || zoom_[2] != factor || !zoom_vp_ || dir != zoom_[3])
	{
		if( zoom_vp_ )
			viewport_destroy( zoom_vp_ );
		zoom_vp_ = viewport_fx_zoom_init( VP_QUADZOOM, width,height,x,y,factor, dir );
		if(!zoom_vp_ )
			return;
		zoom_[0] = x; zoom_[1] = y; zoom_[2] = factor; zoom_[3] = dir;
	}

	int strides[4] = { len, len, len, (alpha ? len : 0 ) };
	vj_frame_copy( frame->data, zoom_private_, strides );

	if(alpha == 0) {
		viewport_process_dynamic( zoom_vp_, zoom_private_, frame->data );
	}
	else {
		viewport_process_dynamic_alpha( zoom_vp_, zoom_private_, frame->data );
	}
	
}

