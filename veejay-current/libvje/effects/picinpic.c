/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nelburg@looze.net>
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

#include "picinpic.h"
#include <stdlib.h>
#include <stdio.h>
#include <libyuv/yuvconv.h>
#include <libpostproc/img_format.h>

#ifdef USE_SWSCALER
typedef struct
{
	void		*scaler;
	VJFrame		frame;
	sws_template	template;
	void		*sampler;
	int		cached;	
	int		w;
	int		h;
} pic_t;


vj_effect *picinpic_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 64;	/* width of view port */
    ve->defaults[1] = 64;	/* height of viewport */
    ve->defaults[2] = 64;	/* x1 */
    ve->defaults[3] = 64;	/* y1 */

    ve->limits[0][0] = 1;
    ve->limits[1][0] = width;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = height;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = width;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = height;

    ve->description = "Picture in picture";
    ve->sub_format = 1;
    ve->extra_frame = 1;

    ve->has_user = 1;
    ve->user_data = NULL;
    return ve;
}

int	picinpic_malloc(void **d, int w, int h)
{
	int i;
	pic_t *my;
	*d = (void*) vj_malloc(sizeof(pic_t));
	my = (pic_t*) *d;

	my->scaler = NULL;
	memset( &(my->frame), 0, sizeof(VJFrame));
	memset( &(my->template), 0, sizeof(sws_template) );
	my->template.flags = 1;
	my->w = 0;
	my->h = 0;

	return 1;	
}

void picinpic_apply( void *user_data, VJFrame *frame, VJFrame *frame2, int width, int height,
		   int twidth, int theight, int x1, int y1 ) 
{
	int x, y;
	uint8_t *dY = frame->data[0];
	uint8_t *dCb = frame->data[1];
	uint8_t *dCr = frame->data[2];
	uint8_t *sY = frame2->data[0];
	uint8_t *sCb = frame2->data[1];
	uint8_t *sCr = frame2->data[2];
	pic_t	*picture = (pic_t*) user_data;
	int view_width = twidth;
	int view_height = theight;
	int dy = y1;
	int dx = x1;

	if ( (dx + view_width ) > width )
		view_width = width - dx;
	if ( (dy + view_height ) > height )
		view_height = height - dy;

	if(view_width < 8 || view_height < 1  )
		return; // nothing to do

	/* pic in pic, using 444p */
	VJFrame scale_src;	
	vj_get_yuv444_template( &(scale_src), width,height );

	scale_src.data[0] = frame2->data[0];
	scale_src.data[1] = frame2->data[1];	
	scale_src.data[2] = frame2->data[2]; 

	/* Setup preview scaler */	
	if( picture->w != view_width || picture->h != view_height || picture->w == 0 || picture->h == 0)
	{
		int len = (view_width * view_height);
	
		if(picture->scaler)
			yuv_free_swscaler( picture->scaler );
		if(picture->frame.data[0])
			free( picture->frame.data[0] );
		if(picture->frame.data[1])
			free( picture->frame.data[1] );
		if(picture->frame.data[2])
			free( picture->frame.data[2] );
		/* Allocate in picture */
		vj_get_yuv444_template( &(picture->frame), view_width,view_height );

		picture->scaler = yuv_init_swscaler(
			&(scale_src),
			&(picture->frame),
			&(picture->template),
			yuv_sws_get_cpu_flags()
		);
		picture->frame.data[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );
		picture->frame.data[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );
		picture->frame.data[2] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );

		picture->w = view_width;
		picture->h = view_height;

	}

	yuv_convert_and_scale( picture->scaler, scale_src.data, picture->frame.data ); 

	/* Copy the scaled image to output */
	for( y = 0 ; y < picture->h; y ++ )
	{
		for( x = 0 ; x < picture->w; x ++ )
		{
			dY[ (dy + y ) * width + dx + x ] = 
				picture->frame.data[0][ y * picture->w + x];
			dCb[(dy + y ) * width + dx + x ] = 
				picture->frame.data[1][ y * picture->w + x];
			dCr[ (dy + y ) * width + dx + x ] = 
				picture->frame.data[2][ y * picture->w + x];
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
		free( my );
	}
	d=NULL;
}
#endif
