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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <libavutil/pixfmt.h>
#include "rgbchannel.h"

vj_effect *rgbchannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->defaults[0] = 0;
    ve->defaults[0] = 0;
    ve->defaults[0] = 0;
    ve->description = "RGB Channel";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Red", "Green", "Blue");
    return ve;
}

static uint8_t *rgb_ = NULL;
static	VJFrame *rgb_frame_ = NULL;
static void *convert_yuv = NULL;
static void *convert_rgb = NULL;
int	rgbchannel_malloc( int w, int h )
{
	if(!rgb_)
		rgb_ = vj_malloc( sizeof(uint8_t) * w * h * 3 );
	if(!rgb_)
		return 0;
	if(!rgb_frame_)
		rgb_frame_ = yuv_rgb_template( rgb_, w, h , PIX_FMT_RGB24 );
	return 1;
}

void	rgbchannel_free( )
{
	if(rgb_)
		free(rgb_);
	if(rgb_frame_)
		free(rgb_frame_);
	rgb_frame_ = NULL;
	rgb_ = NULL;

	if(convert_rgb)
		yuv_fx_context_destroy( convert_rgb );
	if(convert_yuv)
		yuv_fx_context_destroy( convert_yuv );
	convert_rgb = NULL;
	convert_yuv = NULL;
}

void rgbchannel_apply( VJFrame *frame, int width, int height, int chr, int chg , int chb)
{
	unsigned int x,y,i;

	VJFrame *tmp = yuv_yuv_template( frame->data[0],
					 frame->data[1],
					 frame->data[2],
					 width, height, PIX_FMT_YUV444P );

//	yuv_convert_any_ac( tmp, rgb_frame_, PIX_FMT_YUV444P, PIX_FMT_RGB24 );

	if(!convert_yuv )
		convert_yuv = yuv_fx_context_create( tmp, rgb_frame_, PIX_FMT_YUV444P, PIX_FMT_RGB24 );
	if(!convert_rgb )
		convert_rgb = yuv_fx_context_create( rgb_frame_,tmp, PIX_FMT_RGB24, PIX_FMT_YUV444P );

	yuv_fx_context_process( convert_yuv, tmp, rgb_frame_ );

	int row_stride = width * 3;

	if(chr)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 0; x < row_stride; x += 3 )
			{
				rgb_[ y * row_stride + x ] = 0;
			}
		}
	}
	if(chg)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 1; x < row_stride-1; x += 3 )
			{
				rgb_[ y * row_stride + x ] = 0;
			}
		}
	}
	if(chb)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 2; x < row_stride-2; x += 3 )
			{
				rgb_[ y * row_stride + x ] = 0;
			}
		}
	}

//	yuv_convert_any_ac( rgb_frame_, tmp, PIX_FMT_RGB24, PIX_FMT_YUV444P );

	yuv_fx_context_process( convert_rgb, rgb_frame_, tmp );

	free(tmp);

}
