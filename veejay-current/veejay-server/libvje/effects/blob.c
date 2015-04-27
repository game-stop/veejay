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

/* Copyright (C) 2002-2003 W.P. van Paassen - peter@paassen.tmfweb.nl

   This program is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*

	blob , originally from the demo effect collection
	easily modified to videoblob
	p0: radius
	p1: number of blobs
	p2: speed 
	p3: shape (rect,circle)

	the number of blobs and size of radius determine amount of work for cpu.

*/

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "common.h"
#include "blob.h"

typedef struct 
{
	short x;
	short y;
} blob_t;

#define DEFAULT_RADIUS 16
#define DEFAULT_NUM 50

#define	BLOB_RECT 0
#define BLOB_CIRCLE 1

static blob_t 	*blobs_;
static uint8_t 	**blob_;
static uint8_t 	*blob_image_;
static int		blob_ready_	 = 0;
static int		blob_radius_ 	 = 16;
static int		blob_dradius_ 	 = 0;
static int		blob_sradius_ 	 = 0;
static int		blob_num_	 = 50;
static int		blob_type_	 = 1;

vj_effect *blob_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;  // radius
    ve->limits[0][1] = 1; 
    ve->limits[1][1] = 100;  // num blobs
    ve->limits[0][2] = 1;
	ve->limits[1][2] = 100;   // speed
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 1;	// shape
    ve->defaults[0] = DEFAULT_RADIUS;
    ve->defaults[1] = DEFAULT_NUM;
	ve->defaults[2] = 50;
	ve->defaults[3] = 1;
    ve->description = "Video Blobs";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user =0;
	ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Blobs", "Speed" , "Shape");
    return ve;
}
static void	blob_init_( blob_t *b , int w , int h)
{
	b->x = (w >> 1) - blob_radius_;
	b->y = (h >> 1) - blob_radius_;
}

int	blob_malloc(int w, int h)
{
	int j,i;
	int dist_sqrt;

	if(blob_radius_ <= 0)
		return 0;

	blob_dradius_ = blob_radius_ * 2;
	blob_sradius_ = blob_radius_ * blob_radius_;

	blob_ = (uint8_t**) vj_malloc(sizeof(uint8_t*) * blob_dradius_ );
	for(i = 0; i < blob_dradius_ ; i ++ )
	{
		blob_[i] = (uint8_t*) vj_calloc(sizeof(uint8_t) * blob_dradius_ );
		if(!blob_[i]) return 0;
	}

	blobs_ = (blob_t*) vj_calloc(sizeof(blob_t) * blob_num_ );
	if(!blobs_ ) return 0;

	blob_image_ = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!blob_image_) return 0;
		
	for( i = -blob_radius_ ; i < blob_radius_ ; ++ i )
	{
		for( j = -blob_radius_ ; j < blob_radius_ ; ++ j ) 
		{
			dist_sqrt = i * i + j * j;
			if( dist_sqrt < blob_sradius_ )
			{
				blob_[i + blob_radius_][j + blob_radius_] = 0xff;
			}
			else
			{
				blob_[i + blob_radius_][j + blob_radius_ ] = 0x0; // was 0
			}
		}
	}

	for( i = 0; i < blob_num_ ; i ++ )
	{
		blob_init_( blobs_ + i , w , h );
	}

	veejay_memset( blob_image_ , 0 , w * h );

	blob_ready_  = 1;

	return 1;
}


void blob_free()
{
	int i;
	for (i = 0; i < blob_dradius_ ; i ++ )
		if( blob_[i] ) free( blob_[i] );
	if(blobs_)
		free(blobs_);
	if(blob_image_)
		free(blob_image_);
	blobs_ = NULL;
	blob_image_ = NULL;
}

typedef void (*blob_func)(int s, int width);

static void	blob_render_circle(int s, int width)
{
	int i,j;
	for( i = 0; i < blob_dradius_ ; ++ i )	
	{
		for( j = 0; j < blob_dradius_ ; ++ j)
		{
			if( blob_image_[ s + j ] + blob_[i][j] > 255 ) 
				    blob_image_[s + j] = 0xff;
			else
					blob_image_[s + j] += blob_[i][j];
		}
		s += width;
	}
}

static void	blob_render_rect(int s, int width)
{
	int i,j;
	for( i = 0; i < blob_dradius_ ; ++ i )	
	{
		for( j = 0; j < blob_dradius_ ; ++ j)
		{
			blob_image_[s + j] = 0xff;
		}
		s += width;
	}
}

static blob_func	blob_render(void)
{
	if( blob_type_ == BLOB_RECT)
		return &blob_render_rect;
	return &blob_render_circle;
}

void blob_apply(VJFrame *frame,
			   int width, int height, int radius, int num, int speed, int shape)
{
    const int len = frame->len;
	uint8_t *srcY = frame->data[0];
	uint8_t *srcCb= frame->data[1];
	uint8_t *srcCr= frame->data[2];
	int i,k;
	int s;
	double max = speed / 10.0;
	blob_func f = blob_render();

	blob_type_ = shape;

	if( radius != blob_radius_ || num != blob_num_ )
	{ // reinitialize
			blob_radius_ = radius;
			blob_num_ 	 = num;
			blob_free();
			blob_malloc(width,height);
	}

	// move blob
	for( i = 0; i < blob_num_; i ++)
	{
		blobs_[i].x += -2 + (int) ( max * (rand()/(RAND_MAX+1.0)));
		blobs_[i].y += -2 + (int) ( max * (rand()/(RAND_MAX+1.0)));
	}

	// fill blob
	for( k = 0; k < blob_num_ ; k ++ )
	{
		if( (blobs_[k].x > 0) &&
			(blobs_[k].x < (width - blob_dradius_)) &&
			(blobs_[k].y > 0) &&
			(blobs_[k].y < (height - blob_dradius_)) )
		{
			s = blobs_[k].x + blobs_[k].y * width;
			f(s,width);
		}
		else
		{
			blob_init_( blobs_ + k,width ,height );
		}
	}

	// project blob onto video frame
	for(i = 0; i < len ; i ++ )
	{
		if( blob_image_[i]  == 0x0 )
		{
			srcY[i] = 16;
			srcCb[i] = 128;
			srcCr[i] = 128;
		}
		blob_image_[i] = 0x0;
	}
}
