/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *             2015 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include <libavutil/avutil.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/yuvconv.h>
#include "softblur.h"
#include "diff.h"
#include "contourextract.h"

static uint8_t *static_bg = NULL;
static uint32_t *dt_map = NULL;
static void *shrink_ = NULL;
static sws_template template_;
static VJFrame to_shrink_;
static VJFrame shrinked_;
static int dw_, dh_;
static int x_[255];
static int y_[255];
static void *proj_[255];

typedef struct
{
	uint32_t *data;
	uint8_t *bitmap;
	uint8_t *current;
} contourextract_data;

typedef struct
{
	int x;
	int y;
} point_t;

static 	point_t **points = NULL;


vj_effect *contourextract_init(int width, int height)
{
    //int i,j;
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;	/* reverse */
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;	/* show thresholded image / contour */
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;	/* Take background */
    ve->limits[1][3] = 1;
    ve->limits[0][4] = 1;	/* thinning */
    ve->limits[1][4] = 100;
    ve->limits[0][5] = 1;	/* minimum blob weight */
    ve->limits[1][5] = 5000;
    
    ve->defaults[0] = 30;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 3;
    ve->defaults[5] = 200;
    
    ve->description = "Contour extraction";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 1;
    ve->user_data = NULL;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode", "Show image/contour", "Take background", "Thinning", "Min weight" );

	ve->hints = vje_init_value_hint_list (ve->num_params);
	vje_build_value_hint_list (ve->hints, ve->limits[1][1],1,
	                           "Normal",	//0
	                           "Reverse");	//1

	vje_build_value_hint_list (ve->hints, ve->limits[1][2],2,
	                           "Image",		//0
	                           "Contour");	//1

	vje_build_value_hint_list (ve->hints, ve->limits[1][3],3,
	                           "Do not take background",		//0
	                           "Take background");	//1

    return ve;
}

void	contourextract_destroy(void)
{
	if(static_bg)
		free(static_bg);
	if(dt_map)
		free(dt_map);
	static_bg = NULL;
	dt_map = NULL;
	
}

static int	nearest_div(int val )
{
	int r = val % 8;
	while(r--)
		val--;
	return val;
}
int contourextract_malloc(void **d, int width, int height)
{
	contourextract_data *my;
	*d = (void*) vj_calloc(sizeof(contourextract_data));
	my = (contourextract_data*) *d;

	dw_ = nearest_div( width / 8  );
	dh_ = nearest_div( height / 8 );

	my->current = (uint8_t*) vj_calloc( sizeof(uint8_t) * ( dw_ * dh_ * 3 ));
	my->bitmap = (uint8_t*) vj_calloc( sizeof(uint8_t) * ( width * height ));
	
	if(static_bg == NULL)	
		static_bg = (uint8_t*) vj_calloc( sizeof(uint8_t) * ( (width * height + (width*2)) )   );
	if(dt_map == NULL )
		dt_map = (uint32_t*) vj_calloc( sizeof(uint32_t) * ( width * height ));

	veejay_memset( &template_, 0, sizeof(sws_template) );
	veejay_memset( proj_, 0, sizeof(proj_) );
	
	template_.flags = 1;

	vj_get_yuvgrey_template( &to_shrink_, width, height );
	vj_get_yuvgrey_template( &shrinked_ , dw_, dh_ );

	shrink_ = yuv_init_swscaler(
			&(to_shrink_),
			&(shrinked_),
			&template_ ,
			yuv_sws_get_cpu_flags() );

	points = (point_t**) vj_calloc( sizeof(point_t*) * 12000 );
	int i;
	for( i = 0; i < 12000; i ++ )
	{
		points[i] = (point_t*) vj_calloc( sizeof(point_t) );
	}

	veejay_memset( x_, 0, sizeof(x_) );
	veejay_memset( y_, 0, sizeof(y_) );
	
	return 1;
}

void contourextract_free(void *d)
{
	if(d)
	{
		contourextract_data *my = (contourextract_data*) d;
		if(my->current) free(my->current);
		if(my->bitmap) free(my->bitmap);
		free(d);
	}

	if( shrink_ )
	{	
		yuv_free_swscaler( shrink_ );
		shrink_ = NULL;
	}

	int i;
	for( i = 0; i < 255; i++ )
		if( proj_[i] )
			viewport_destroy( proj_[i] );
	for( i = 0; i < 12000; i ++ )
	{
		if(points[i])
			free(points[i]);
	}
	free(points);

	d = NULL;
}

int contourextract_prepare(uint8_t *map[4], int width, int height)
{
	if(!static_bg )
	{
		return 0;
	}

	vj_frame_copy1( map[0], static_bg, (width*height));	
	
	VJFrame tmp;
	veejay_memset( &tmp, 0, sizeof(VJFrame));
	tmp.data[0] = static_bg;
	tmp.width = width;
	tmp.height = height;
	softblur_apply_internal( &tmp, 0);

	veejay_msg(2, "Contour extraction: Snapped background frame");
	return 1;
}

void contourextract_apply(void *ed, VJFrame *frame, int threshold, int reverse,
                          int mode, int take_bg, int feather, int min_blob_weight)
{
	unsigned int i;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	uint32_t cx[256];
	uint32_t cy[256];
	uint32_t xsize[256];
	uint32_t ysize[256];
	uint32_t blobs[255];

	veejay_memset( cx,0,sizeof(cx));
	veejay_memset( cy,0,sizeof(cy));
	veejay_memset( xsize,0,sizeof(xsize));
	veejay_memset( ysize,0,sizeof(ysize));
	veejay_memset( blobs, 0, sizeof(blobs) );

	contourextract_data *ud = (contourextract_data*) ed;
	
	//@ clear distance transform map
	veejay_memset( dt_map, 0 , len * sizeof(uint32_t) );

	binarify_1src( ud->bitmap, frame->data[0], threshold, reverse, width, height );

	if(mode==1)
	{
		//@ show difference image in grayscale
		vj_frame_copy1( ud->bitmap, Y, len );
		vj_frame_clear1( Cb, 128, uv_len );
		vj_frame_clear1( Cr, 128, uv_len );
		return;
	}

	//@ calculate distance map
	veejay_distance_transform8( ud->bitmap, width, height, dt_map );

	to_shrink_.data[0] = ud->bitmap;
	shrinked_.data[0] = ud->current;

	yuv_convert_and_scale_grey( shrink_, &to_shrink_, &shrinked_ );

	uint32_t labels = 
		veejay_component_labeling_8(dw_,dh_, shrinked_.data[0], blobs, cx,cy,xsize,ysize,min_blob_weight);

	veejay_memset( Y, 0, len );
	veejay_memset( Cb , 128, uv_len);
	veejay_memset( Cr , 128, uv_len );  

	int num_objects = 0;
	for( i = 1 ; i <= labels; i ++ )
		if( blobs[i] ) 
			num_objects ++;
	
	
	//@ Iterate over blob's bounding boxes and extract contours
	//@ use snippet below to get center of blob --> parameter extraction TODO
	/*
	for( i = 1; i <= labels; i ++ )
	{
		if( blobs[i] > 0 )
		{
			int nx = cx[i] * sx;
			int ny = cy[i] * sy;
			int size_x = xsize[i] * sx;
			int size_y = ysize[i] * sy * 0.5; 
			int x1 = nx - size_x;
			int y1 = ny - size_y;
			int x2 = nx + size_y;
			int y2 = ny + size_y;
			int n_points = 0;
			int center = 0;

			if( x1 < 0 ) x1 = 0; else if ( x1 > width ) x1 = width;
			if( x2 < 0 ) x2 = 0; else if ( x2 > width ) x2 = width;
			if( y1 < 0 ) y1 = 0; else if ( y1 >= height ) y1 = height -1;
			if( y2 < 0 ) y2 = 0; else if ( y2 >= height ) y2 = height -1;


			for( k = y1; k < y2; k ++ )
			{
				for( j = x1; j < x2; j ++ )
				{
					//@ use distance transform map to find centroid (fuzzy)
					if( dt_map[ (k * width + j) ] > center )
					{
						center = dt_map[ (k* width +j) ];
					}
					if( dt_map[ (k * width + j) ] == feather )
					{
						Y[ (k * width +j)] = 0xff;
						points[ n_points ]->x = j;
						points[ n_points ]->y = k;
						n_points++;
						if( n_points >= 11999 )
						{
							veejay_msg(0, "Too many points in contour");	
							return;
						}
					}
				}
			}
		}
	}
	*/

}




