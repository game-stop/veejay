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
static unsigned int bg_n = 0;
static uint8_t instance = 0;
static int auto_hist = 1;

vj_effect *bgsubtract_init(int width, int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;	/* threshold */
	ve->limits[1][0] = 255;
	ve->limits[0][1] = 0;	/* mode */
	ve->limits[1][1] = 2;
	ve->limits[0][2] = 0;	/* enable/disable */
	ve->limits[1][2] = 1;
	ve->limits[0][3] = 0;   /* alpha */
	ve->limits[1][3] = 2;

	ve->defaults[0] = 45;
	ve->defaults[1] = 0;
	ve->defaults[2] = 0;
	ve->defaults[3] = 0;
	ve->defaults[4] = 0;

	ve->description = "Subtract Background";
	ve->extra_frame = 0;
	ve->sub_format = -1;
	ve->has_user = 1;
	ve->user_data = NULL;
	ve->sub_format = 0;
	ve->parallel = 0;
	ve->global = 1; /* this effect is not freed when switching samples */

	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "BG Method","Enable", "To Alpha");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1,"Static BG","CMA BG","Average BG" );
	vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2,"Generate/Show BG","Subtract Background" );
	vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3,"Create Mask","Create Alpha Mask", "Do Nothing" );

	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

	const char *hist = getenv( "VEEJAY_BG_AUTO_HISTOGRAM_EQ" );
	if( hist ) {
		auto_hist = atoi( hist );
	}

	return ve;
}

int bgsubtract_instances()
{
	return instance;
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
	
	instance = 1;
	
	veejay_msg( VEEJAY_MSG_INFO,
			"You can enable/disable the histogram equalizer by setting env var VEEJAY_BG_AUTO_HISTOGRAM_EQ" );
	veejay_msg( VEEJAY_MSG_INFO,
			"Histogram equalization is %s", (auto_hist ? "enabled" : "disabled" ));

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

	bg_ssm = 0;
	instance = 0;
}

int bgsubtract_prepare(VJFrame *frame)
{
	if(!static_bg__ )
	{
		return 0;
	}
	
	if( auto_hist )
		vje_histogram_auto_eq( frame );

	//@ copy the iamge
	veejay_memcpy( bg_frame__[0], frame->data[0], frame->len );
	
	if( frame->ssm ) {
		veejay_memcpy( bg_frame__[1], frame->data[1], frame->len );
		veejay_memcpy( bg_frame__[2], frame->data[2], frame->len );
		bg_ssm = 1;
	}
	else {
		// if data is not subsampled, upsample chroma planes now 
		veejay_memcpy( bg_frame__[1], frame->data[1], frame->uv_len );
		veejay_memcpy( bg_frame__[2], frame->data[2], frame->uv_len );
		chroma_supersample( SSM_422_444, frame, bg_frame__ );
		bg_ssm = 1;
	}

	bg_n = 0;

	veejay_msg(2, "Subtract background: Snapped background frame (4:4:4 = %d)", bg_ssm);
	return 1;
}

/* always returns chroma planes in 4:4:4 */
uint8_t* bgsubtract_get_bg_frame(unsigned int plane)
{
	if( static_bg__ == NULL )
		return NULL;
	return bg_frame__[ plane ];
}

static void bgsubtract_cma_frame( const int len, uint8_t *I, uint8_t *O )
{
	int i;
	for( i = 0; i < len; i ++ )
	{
		O[i] = ((I[i] + (bg_n * O[i])) / (bg_n+1)); 
	}
}
static void bgsubtract_avg_frame( const int len, uint8_t *I, uint8_t *O)
{
	int i;
	for( i = 0; i < len; i ++ )
	{
		O[i] = (O[i] + I[i]) >> 1;
	}
}

static void bgsubtract_show_bg( VJFrame *frame )
{
	veejay_memcpy( frame->data[0], bg_frame__[0], frame->len );
	if( bg_ssm && frame->ssm ) {
		veejay_memcpy( frame->data[1], bg_frame__[1], frame->len );
		veejay_memcpy( frame->data[2], bg_frame__[2], frame->len );
	} else { /* subsampling does not match */
		veejay_memset( frame->data[1], 128, frame->uv_len );
		veejay_memset( frame->data[2], 128, frame->uv_len );
	}	
}

void bgsubtract_apply(VJFrame *frame,int width, int height, int threshold, int method, int enabled, int alpha )
{
	const int uv_len = (frame->ssm ? frame->len : frame->uv_len );

	if( auto_hist )
		vje_histogram_auto_eq( frame );

	if( enabled == 0 ) {
		switch( method ) {
			case 0:
				bgsubtract_show_bg( frame );
				break;
			case 1:
				bgsubtract_cma_frame( frame->len, frame->data[0], bg_frame__[0] );
				bgsubtract_show_bg( frame );
				bg_n ++;
				break;
			case 2:
				bgsubtract_avg_frame( frame->len, frame->data[0], bg_frame__[0] );
				bgsubtract_show_bg( frame );
				break;	
			default:
			break;
		}
	}
	else {
		if( alpha == 0 ) {
			veejay_memset( frame->data[1], 128, uv_len );
			veejay_memset( frame->data[2], 128, uv_len );
			vje_diff_plane( bg_frame__[0], frame->data[0], frame->data[0], threshold, frame->len );
		}
		else {
			vje_diff_plane( bg_frame__[0], frame->data[0], frame->data[3], threshold, frame->len );
		}
	}
}
