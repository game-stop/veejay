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

#include "common.h"
#include <veejaycore/vjmem.h>
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

typedef struct {
    blob_t *blobs_;
    uint8_t	**blob_;
    uint8_t	*blob_image_;
    int	blob_ready_	;
    int	blob_radius_;// 16
    int	blob_dradius_;
    int	blob_sradius_;
    int	blob_num_;//50
    int blob_type_;// 1
} blobs_t;

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
static void	blob_init_( blobs_t *g, blob_t *b , int w , int h)
{
	b->x = (w >> 1) - g->blob_radius_;
	b->y = (h >> 1) - g->blob_radius_;
}

static int blob_reinit(blobs_t *b, int radius, int num, int w, int h)
{
    int i,j;
    if( b->blob_ ) {
        for( i = 0; i < b->blob_dradius_; i ++ ) {
            if( b->blob_[i] )
                free(b->blob_[i]);
        }
        free(b->blob_);
    }
    if( b->blobs_ ) {
        free( b->blobs_ );
    }

    b->blob_radius_ = radius;
    b->blob_num_ = num;
	b->blob_dradius_ = b->blob_radius_ * 2;
	b->blob_sradius_ = b->blob_radius_ * b->blob_radius_;

    b->blob_ = (uint8_t**) vj_calloc(sizeof(uint8_t*) * b->blob_dradius_ );
    if(!b->blob_) { 
        return 0;
    }

	for(i = 0; i < b->blob_dradius_ ; i ++ )
	{
		b->blob_[i] = (uint8_t*) vj_calloc(sizeof(uint8_t) * b->blob_dradius_ );
		if(!b->blob_[i]) {
            return 0;
	    }
    }

	b->blobs_ = (blob_t*) vj_calloc(sizeof(blob_t) * b->blob_num_ );
	if(!b->blobs_ ) {
        return 0;
    }
		
	for( i = -b->blob_radius_ ; i < b->blob_radius_ ; ++ i )
	{
		for( j = -b->blob_radius_ ; j < b->blob_radius_ ; ++ j ) 
		{
			int dist_sqrt = i * i + j * j;
			if( dist_sqrt < b->blob_sradius_ )
			{
				b->blob_[i + b->blob_radius_][j + b->blob_radius_] = 0xff;
			}
			else
			{
				b->blob_[i + b->blob_radius_][j + b->blob_radius_ ] = 0x0; // was 0
			}
		}
	}

	for( i = 0; i < b->blob_num_ ; i ++ )
	{
		blob_init_(b,  b->blobs_ + i , w , h );
	}


    return 1;
}

void *blob_malloc(int w, int h)
{
    blobs_t *b = (blobs_t*) vj_calloc(sizeof(blobs_t));
    if(!b) {
        return NULL;
    }

    if(!blob_reinit( b, 16, 5,w, h )) {
        blob_free(b);
        return NULL;
    }

    b->blob_type_ = 1;
	b->blob_image_ = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!b->blob_image_) {
        blob_free(b);
        return NULL;
    }
	veejay_memset( b->blob_image_ , 0 , w * h );
	b->blob_ready_  = 1;

	return (void*) b;
}


void blob_free(void *ptr)
{
    blobs_t *b = (blobs_t*) ptr;
    int i;
    if( b->blob_ ) {
        for( i = 0; i < b->blob_dradius_; i ++ ) {
            if( b->blob_[i] ) 
                free( b->blob_[i] );
        }
        free(b->blob_);
    }
    if( b->blobs_ ) {
        free(b->blobs_);
    }

	if( b->blob_image_)
		free(b->blob_image_);
    free(b);
}

typedef void (*blob_func)(blobs_t *b, int s, int width);

static void	blob_render_circle(blobs_t *b, int s, int width)
{
	int i,j;
	for( i = 0; i < b->blob_dradius_ ; ++ i )	
	{
		for( j = 0; j < b->blob_dradius_ ; ++ j)
		{
			if( b->blob_image_[ s + j ] + b->blob_[i][j] > 255 ) 
				    b->blob_image_[s + j] = 0xff;
			else
					b->blob_image_[s + j] += b->blob_[i][j];
		}
		s += width;
	}
}

static void	blob_render_rect(blobs_t *b, int s, int width)
{
	int i,j;
	for( i = 0; i < b->blob_dradius_ ; ++ i )	
	{
		for( j = 0; j < b->blob_dradius_ ; ++ j)
		{
			b->blob_image_[s + j] = 0xff;
		}
		s += width;
	}
}

static blob_func	blob_render(blobs_t *b)
{
	if( b->blob_type_ == BLOB_RECT)
		return &blob_render_rect;
	return &blob_render_circle;
}

void blob_apply(void *ptr, VJFrame *frame, int *args) {
    int radius = args[0];
    int num = args[1];
    int speed = args[2];
    int shape = args[3];

    blobs_t *b = (blobs_t*) ptr;

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *srcY = frame->data[0];
	uint8_t *srcCb= frame->data[1];
	uint8_t *srcCr= frame->data[2];
	int i,k;
	int s;
	double max = speed / 10.0;
	blob_func f = blob_render(b);

	b->blob_type_ = shape;

	if( radius != b->blob_radius_ || num != b->blob_num_ )
	{ // reinitialize
        if(!blob_reinit(b,radius, num, frame->width, frame->height) ) {
            return;
        }
	}

	// move blob
	for( i = 0; i < b->blob_num_; i ++)
	{
		b->blobs_[i].x += -2 + (int) ( max * (rand()/(RAND_MAX+1.0)));
		b->blobs_[i].y += -2 + (int) ( max * (rand()/(RAND_MAX+1.0)));
	}

	// fill blob
	for( k = 0; k < b->blob_num_ ; k ++ )
	{
		if( (b->blobs_[k].x > 0) &&
			(b->blobs_[k].x < (width - b->blob_dradius_)) &&
			(b->blobs_[k].y > 0) &&
			(b->blobs_[k].y < (height - b->blob_dradius_)) )
		{
			s = b->blobs_[k].x + b->blobs_[k].y * width;
			f(b,s,width);
		}
		else
		{
			blob_init_(b, b->blobs_ + k,width ,height );
		}
	}

	// project blob onto video frame
	for(i = 0; i < len ; i ++ )
	{
		if( b->blob_image_[i] == 0x0 )
		{
			srcY[i] = pixel_Y_lo_;
			srcCb[i] = 128;
			srcCr[i] = 128;
		}
		b->blob_image_[i] = 0x0;
	}
}
