/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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

/*
	This effect uses libpostproc , it should be enabled at compile time
	(--with-swscaler) if you want to use this Effect.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "picinpic.h"
#include <libyuv/yuvconv.h>
#include <libavutil/pixfmt.h>
#include "common.h" 
extern void    vj_get_yuv444_template(VJFrame *src, int w, int h);
typedef struct
{
	void		*scaler;
	VJFrame		*frame;
	sws_template	template;
	void		*sampler;
	int		cached;	
	int		w;
	int		h;
} pic_t;

static	int	nearest_div(int val);

vj_effect *picinpic_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = width/8;	/* width of view port */
    ve->defaults[1] = height/8;	/* height of viewport */
    ve->defaults[2] = width/2;	/* x1 */
    ve->defaults[3] = height/2;	/* y1 */

    ve->limits[0][0] = 8;
    ve->limits[1][0] = width;
    ve->limits[0][1] = 8;
    ve->limits[1][1] = height;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = width;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = height;

    ve->description = "Picture in picture";
    ve->sub_format = 1;
    ve->extra_frame = 1;

    ve->has_user = 1;
    ve->user_data = NULL;

	ve->param_description = vje_build_param_list( ve->num_params, "Width", "Height", "X offset", "Y offset" );
    return ve;
}

int	picinpic_malloc(void **d, int w, int h)
{
	int i;
	pic_t *my;
	*d = (void*) vj_calloc(sizeof(pic_t));
	my = (pic_t*) *d;

	my->scaler = NULL;
	my->template.flags = 1;
	my->w = 0;
	my->h = 0;
	my->frame = NULL;
	return 1;	
}

static int	nearest_div(int val )
{
	int r = val % 8;
	while(r--)
		val--;
	return val;
}

void picinpic_apply( void *user_data, VJFrame *frame, VJFrame *frame2, int width, int height,
		   int twidth, int theight, int x1, int y1 ) 
{
	int x, y;
	pic_t	*picture = (pic_t*) user_data;
	int view_width = nearest_div(twidth);
	int view_height = nearest_div(theight);
	int dy = nearest_div(y1);
	int dx = nearest_div(x1);

	//@ round view_width to nearest multiple of 2,4,8


	if ( (dx + view_width ) > width )
		view_width = width - dx;
	if ( (dy + view_height ) > height )
		view_height = height - dy;

	if(view_width < 8 || view_height < 1  )
		return; // nothing to do

	// sub_format up is always 4:4:4 planar
	int pixfmt = (frame->format == PIX_FMT_YUVJ422P ? PIX_FMT_YUVJ444P: PIX_FMT_YUV444P);
	VJFrame src;
	veejay_memcpy(&src,frame2,sizeof(VJFrame));
	src.format = pixfmt;
	src.stride[1] = src.width;
	src.stride[2] = src.width;

	if( picture->w != view_width || picture->h != view_height || picture->w == 0 || picture->h == 0)
	{
		if(picture->frame) {
			for( x = 0; x < 3; x ++ ) {
				if(picture->frame->data[x])
					free(picture->frame->data[x]);
				picture->frame->data[x] = NULL;
			}
			free(picture->frame);
			picture->frame = NULL;
		}	
		if( picture->scaler ) {
			yuv_free_swscaler( picture->scaler );
			picture->scaler = NULL;
		}

		picture->w = view_width;
		picture->h = view_height;
	}

	if( picture->frame == NULL ) {
		picture->frame = yuv_yuv_template( NULL,NULL,NULL ,view_width,view_height,pixfmt );
		// needs seperately allocated planes
		picture->frame->data[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8( view_width * view_height ));
		picture->frame->data[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8( view_width * view_height ));
		picture->frame->data[2] = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8( view_width * view_height ));
	}

	if( picture->scaler == NULL ) {
		picture->scaler = yuv_init_swscaler(
			&src,
			picture->frame,
			&(picture->template),
			yuv_sws_get_cpu_flags() );
		if(picture->scaler == NULL ) {
			return;
		}
	}

	yuv_convert_and_scale( picture->scaler, &src, picture->frame );

	uint8_t *sY = picture->frame->data[0];
	uint8_t *sCb = picture->frame->data[1];
	uint8_t *sCr = picture->frame->data[2];

	uint8_t *dY = frame->data[0];
	uint8_t *dCb = frame->data[1];
	uint8_t *dCr = frame->data[2]; 
	
	/* Copy the scaled image to output */
	for( y = 0 ; y < picture->h-1; y ++ )
	{
		for( x = 0 ; x < picture->w-1; x ++ )
		{
			dY[ (dy + y ) * width + dx + x ] =  sY[ y * picture->w + x];
			dCb[(dy + y ) * width + dx + x ] =  sCb[ y * picture->w + x];
			dCr[ (dy + y ) * width + dx + x ] = sCr[ y * picture->w + x];
		}
	}

}

void picinpic_free(void *d)
{
	if(d)
	{
		pic_t *my = (pic_t*) d;
		if(my->scaler)
			yuv_free_swscaler( my->scaler );
		int i;

		if(my->frame) {
			for( i = 0; i < 3; i ++ ) {
				if(my->frame->data[i])
					free(my->frame->data[i]);
				my->frame->data[i] = NULL;
			}
			free(my->frame);
		}

		free( my );
	}
	d=NULL;
}
