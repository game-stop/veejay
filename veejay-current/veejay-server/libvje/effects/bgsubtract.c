/* 
 * Linux VeeJay
 *
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
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
#include "bgsubtract.h"
#include "common.h"
#include <math.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include "softblur.h"
#include <libsubsample/subsample.h>

static uint8_t *static_bg__ = NULL;
static uint8_t *bg_frame__[4] = { NULL,NULL,NULL,NULL };
static int bg_ssm = 0;

vj_effect *bgsubtract_init(int width, int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 3;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;	/* threshold */
	ve->limits[1][0] = 255;
	ve->limits[0][1] = 0;	/* mode */
	ve->limits[1][1] = 1;
	ve->limits[0][2] = 0;	/* alpha */
	ve->limits[1][2] = 1;

	ve->defaults[0] = 45;
	ve->defaults[1] = 1;
	ve->defaults[2] = 0;

	ve->description = "Subtract background";
	ve->extra_frame = 0;
	ve->sub_format = -1;
	ve->has_user = 1;
	ve->user_data = NULL;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode", "To Alpha");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1,"Show Difference", "Show Static BG" );

	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

	return ve;
}

int bgsubtract_malloc(int width, int height)
{
	if(static_bg__ == NULL){
		static_bg__ = (uint8_t*) vj_malloc( RUP8(width*height)*4);
		bg_frame__[0] = static_bg__;
		bg_frame__[1] = bg_frame__[0] + RUP8(width*height);
		bg_frame__[2] = bg_frame__[1] + RUP8(width*height);
		bg_frame__[3] = bg_frame__[2] + RUP8(width*height);
	}

	return 1;
}

void bgsubtract_free()
{
	if( static_bg__ ) {
		free(static_bg__ );

		bg_frame__[0] = NULL;
		bg_frame__[1] = NULL;
		bg_frame__[2] = NULL;
		bg_frame__[3] = NULL;
		static_bg__ = NULL;
	}
}

int bgsubtract_prepare(VJFrame *frame)
{
	if(!static_bg__ )
	{
		return 0;
	}
	
	//@ copy the iamge
	veejay_memcpy( bg_frame__[0], frame->data[0], frame->len );
	if( frame->ssm ) {
		veejay_memcpy( bg_frame__[1], frame->data[1], frame->len );
		veejay_memcpy( bg_frame__[2], frame->data[2], frame->len );
	}
	else {
		// if data is subsampled, super sample it now 
		veejay_memcpy( bg_frame__[1], frame->data[1], frame->uv_len );
		veejay_memcpy( bg_frame__[2], frame->data[2], frame->uv_len );
		chroma_supersample( SSM_422_444, frame, bg_frame__ );
	}

	bg_ssm = 1;
	
	veejay_msg(2, "Subtract background: Snapped background frame");
	return 1;
}

uint8_t* bgsubtract_get_bg_frame(unsigned int plane)
{
	return bg_frame__[ plane ];
}

void bgsubtract_apply(VJFrame *frame,int width, int height, int threshold, int mode, int alpha )
{
	const int uv_len = (frame->ssm ? frame->len : frame->uv_len );

	if( mode ) {
		veejay_memcpy( frame->data[0], bg_frame__[0], frame->len );
		if( frame->ssm ) {
			veejay_memcpy( frame->data[1], bg_frame__[1], frame->len );
			veejay_memcpy( frame->data[2], bg_frame__[2], frame->len );
		} else { /* if chain is still subsampled, copy only greyscale image */
			veejay_memset( frame->data[1], 128, frame->uv_len );
			veejay_memset( frame->data[2], 128, frame->uv_len );
		}	
		return;
	}

	if( alpha == 0 ) {
		veejay_memset( frame->data[1], 128, uv_len );
		veejay_memset( frame->data[2], 128, uv_len );
		vje_diff_plane( bg_frame__[0], frame->data[0], frame->data[0], threshold, frame->len );
	}
	else {
		vje_diff_plane( bg_frame__[0], frame->data[0], frame->data[3], threshold, frame->len );
	}
}
