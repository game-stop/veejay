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
#include <libvjmem/vjmem.h>
#include "diff.h"
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ffmpeg/avutil.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include <libyuv/yuvconv.h>

#include "softblur.h"
static uint8_t *static_bg = NULL;
static int take_bg_ = 0;
static uint32_t *dt_map = NULL;
static void *shrink_ = NULL;
static sws_template template_;
static VJFrame to_shrink_;
static VJFrame shrinked_;
static int dw_, dh_;

typedef struct
{
	uint32_t *data;
	uint8_t *bitmap;
	uint8_t *current;
} texmap_data;

vj_effect *texmap_init(int width, int height)
{
    //int i,j;
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;	/* reverse */
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;	/* show mask */
    ve->limits[1][2] = 3;
    ve->limits[0][3] = 0;       /* switch to take bg mask */
    ve->limits[1][3] = 1;
    ve->limits[0][4] = 1;	/* thinning */
    ve->limits[1][4] = 100;

    ve->defaults[0] = 30;
    ve->defaults[1] = 0;
    ve->defaults[2] = 2;
    ve->defaults[3] = 0;
    ve->defaults[4] = 5;

    ve->description = "Map B to A (sub bg, texture map))";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 1;
    ve->user_data = NULL;
    return ve;
}

void	texmap_destroy(void)
{
	if(static_bg)
		free(static_bg);
	if(dt_map)
		free(dt_map);
	static_bg = NULL;
	dt_map = NULL;
	
}

#define ru8(num)(((num)+8)&~8)
static int	nearest_div(int val )
{
	int r = val % 8;
	while(r--)
		val--;
	return val;
}
int texmap_malloc(void **d, int width, int height)
{
	texmap_data *my;
	*d = (void*) vj_calloc(sizeof(texmap_data));
	my = (texmap_data*) *d;

	dw_ = nearest_div( width / 8  );
	dh_ = nearest_div( height / 8 );

	my->current = (uint8_t*) vj_calloc( ru8( sizeof(uint8_t) * dw_ * dh_ * 3 ) );
	my->data = (uint32_t*) vj_calloc( ru8(sizeof(uint32_t) * width * height) );
	my->bitmap = (uint8_t*) vj_calloc( ru8(sizeof(uint8_t) * width * height ));
	
	if(static_bg == NULL)	
		static_bg = (uint8_t*) vj_calloc( ru8( width + width * height * sizeof(uint8_t)) );
	if(dt_map == NULL )
		dt_map = (uint32_t*) vj_calloc( ru8(width * height * sizeof(uint32_t) + width ) );

	veejay_memset( &template_, 0, sizeof(sws_template) );

	template_.flags = 1;

	vj_get_yuvgrey_template( &to_shrink_, width, height );
	vj_get_yuvgrey_template( &shrinked_ , dw_, dh_ );

	shrink_ = yuv_init_swscaler(
			&(to_shrink_),
			&(shrinked_),
			&template_ ,
			yuv_sws_get_cpu_flags() );


	return 1;
}

void texmap_free(void *d)
{
	if(d)
	{
		texmap_data *my = (texmap_data*) d;
		if(my->data) free(my->data);
		if(my->current) free(my->current);
		if(my->bitmap) free(my->bitmap);
		free(d);
	}

	if( shrink_ )
	{	
		yuv_free_swscaler( shrink_ );
		shrink_ = NULL;
	}

	d = NULL;
}

void texmap_prepare(void *user, uint8_t *map[3], int width, int height)
{
	if(!static_bg )
	{
		veejay_msg(0,"FX \"Map B to A (substract background mask)\" not initialized");
		return;
	}
	
	veejay_memcpy( static_bg, map[0], (width*height));
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = static_bg;
	tmp.width = width;
	tmp.height = height;
	softblur_apply( &tmp, width,height,0);

	veejay_msg(0, "Snapped and softblurred current frame to use as background mask");
}

static	void	binarify( uint8_t *bm, uint32_t *dst, uint8_t *bg, uint8_t *src,int threshold,int reverse, const int len )
{
	int i;



	if(!reverse)
	{
		for( i = 0; i < len; i ++ )
		{
			if ( abs(bg[i] - src[i]) <= threshold )
			{	dst[i] = 0; bm[i] = 0; }
			else
			{	dst[i] = 1; bm[i] = 1; }
		}

	}
	else
	{
		for( i = 0; i < len; i ++ )
		{
			if ( abs(bg[i] - src[i]) >= threshold )
			{	dst[i] = 0; bm[i] = 0; }
			else
			{	dst[i] = 1; bm[i] = 1; }
		
		}
	}
}

static	void	texmap_centroid()
{
	
}

void texmap_apply(void *ed, VJFrame *frame,
		VJFrame *frame2, int width, int height, 
		int threshold, int reverse,int mode, int take_bg, int feather)
{
    
	unsigned int i;
	const uint32_t len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	uint32_t cx[256];
	uint32_t cy[256];

	veejay_memset( cx,0,sizeof(cx));
	veejay_memset( cy,0,sizeof(cy));

	texmap_data *ud = (texmap_data*) ed;

	if( take_bg != take_bg_ )
	{
		veejay_memcpy( static_bg, frame->data[0], frame->len );
		VJFrame tmp;
		veejay_memset( &tmp, 0, sizeof(VJFrame));
		tmp.data[0] = static_bg;
		tmp.width = width;
		tmp.height = height;
		softblur_apply( &tmp, width,height,0);
		take_bg_ = take_bg;
		return;
	}

	//@ clear distance transform map
	veejay_memset( dt_map, 0 , len * sizeof(uint32_t) );

	//@ todo: optimize with mmx
	binarify( ud->bitmap, ud->data, static_bg, frame->data[0], threshold, reverse,len );

	//@ calculate distance map
	veejay_distance_transform( ud->data, width, height, dt_map );
	
	if(mode==1)
	{
		//@ show difference image in grayscale
		veejay_memcpy( Y, ud->bitmap, len );
		veejay_memset( Cb, 128, len );
		veejay_memset( Cr, 128, len );

		return;
	} else if (mode == 2 )
	{
		//@ show dt map as grayscale image, intensity starts at 128
		for( i = 0; i  < len ; i ++ )
		{
			if( dt_map[i] == feather )	
				Y[i] = 0xff; //@ border white
			else if( dt_map[i] > feather )	{
				Y[i] = 128 + (dt_map[i] % 128); //grayscale value
			} else if ( dt_map[i] == 1 ) {
				Y[i] = 0xff;
			} else {
				Y[i] = 0;	//@ black (background)
			}
			Cb[i] = 128;	
			Cr[i] = 128;
		}
		return;
	} else if( mode ==3 )
	{
		//@ process dt map
		for( i = 0; i < len ;i ++ )
		{
			if( dt_map[ i ] >= feather )
			{
				Y[i] = Y2[i];
				Cb[i] = Cb2[i];
				Cr[i] = Cr2[i];
			}
			else
			{
				Y[i] = 0;
				Cb[i] = 128;
				Cr[i] = 128;
			}
		}
		return;
	}

	to_shrink_.data[0] = ud->bitmap;
	shrinked_.data[0] = ud->current;

	uint8_t blobs[255];

	veejay_memset( blobs, 0, sizeof(blobs) );

	yuv_convert_and_scale_grey( shrink_, &to_shrink_, &shrinked_ );

	uint32_t labels = veejay_component_labeling_8(dw_,dh_, ud->current, blobs, cx,cy );

	for( i = 1; i <= labels ; i ++ )
	{
		if( blobs[i] )
		{
			int radius = (int) ( 0.5 + sqrt( (double) 8.0 * blobs[i]) );
			cx[i] = cx[i] * 8;
			cy[i] = cy[i] * 8;
			viewport_line( Y, cx[i] - radius, cy[i] - radius ,
					  cx[i] + radius, cy[i] - radius ,
					  width, height, 128 );

			viewport_line( Y, cx[i] - radius, cy[i] - radius ,
					  cx[i] - radius, cy[i] + radius ,
					  width, height, 128 );

			viewport_line( Y, cx[i] + radius, cy[i] - radius ,
					  cx[i] + radius, cy[i] + radius ,
					  width, height, 128 );

			viewport_line( Y, cx[i] - radius, cy[i] + radius,
					  cx[i] + radius, cy[i] + radius ,
					  width, height, 128 );
//			veejay_msg( 0, "B %d: %dx%d, weight: %d, pos %d x %d",
//				i, dw_, dh_, blobs[i], cx[i],cy[i]);
		}
	}


}




