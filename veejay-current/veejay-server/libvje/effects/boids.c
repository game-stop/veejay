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
	extended to video boids. the boids are an implementation of
 	Craig Reynolds's BOIDS behavioral algorithm 
     (http://www.vergenet.net/~conrad/boids/pseudocode.html)

	p0: radius
	p1: number of blobs
	p2: shape (rect,circle)
	p3: influence boids trying to fly towards centre of mass of neighbouring boids
	p4: influence boids trying to keep a small distance away from other boids
	p5: influence boids trying to match velocity with near boids
	p6: speed limiter
	p7: home position distance to center point

	added optional flock rules:
	+ limiting speed

*/

#include "common.h"
#include <veejaycore/vjmem.h>
#include "boids.h"
typedef struct 
{
	short x;	// x
	short y;	// y
	double vx;	// velocity x
	double vy;  	// velocity y
} blob_t;

#define DEFAULT_RADIUS 16
#define DEFAULT_NUM 100

#define	BLOB_RECT 0
#define BLOB_CIRCLE 1

typedef struct {
    blob_t 	*blobs_;
    uint8_t **blob_;
    uint8_t *blob_image_;
    int	blob_ready_;
    int	blob_radius_; // 16
    int	blob_dradius_;
    int	blob_sradius_;
    int	blob_num_; // 100
    int	blob_type_; // 1
    int	blob_home_radius_; // 203
} boids_t;

static void boid_rule1_( boids_t *b, int boid_id, double v1[2] );
static void	boid_rule2_( boids_t *b, int boid_id, double v1[2] );
static void	boid_rule3_( boids_t *b, int boid_id, double v1[2] );
static void	boid_rule4_( boids_t *b, int boid_id, int velocity_limit );

vj_effect *boids_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 8;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 1;
	ve->limits[1][0] = w/2;  // radius
	ve->limits[0][1] = 2; 
	ve->limits[1][1] = 256;  // num blobs
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1;	// shape
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 100;  // m1
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 100;  // m2 
	ve->limits[0][5] = 0;
	ve->limits[1][5] = 100;  // m3
	ve->limits[0][6] = 1;
	ve->limits[1][6] = 100;
	ve->limits[0][7] = 1;
	ve->limits[1][7] = 360;
	ve->defaults[0] = DEFAULT_RADIUS;
	ve->defaults[1] = DEFAULT_NUM;
	ve->defaults[2] = 1;
	ve->defaults[3] = 1;
	ve->defaults[4] = 0;
	ve->defaults[5] = 0;
	ve->defaults[6] = 199;
	ve->defaults[7] = w/4;

	ve->description = "Video Boids";
	ve->sub_format = 1;
	ve->extra_frame = 0;
	ve->has_user =0;
	ve->param_description = vje_build_param_list( ve->num_params, "Radius","Blobs","Shape","Cohesion","Seperation","Alignment","Speed","Home Radius");
	return ve;
}




static void	blob_home_position(boids_t *b, int blob_id, int w, int h , double v[2] )
{
	double theta = 360.0 / ( (double) b->blob_num_ ) * blob_id;
	double rad = (theta / 180.0 ) * M_PI;
	double ratio = (w/h);
	double cx = ( double )( w/ 2);
	double cy = ( double )( h/ 2) * ratio;
	v[0] = cx + a_cos(rad) * b->blob_home_radius_;
	v[1] = cy + a_sin(rad) * b->blob_home_radius_;
}

static void	blob_init_( boids_t *g, blob_t *b , int blob_id, int w , int h)
{
	double v[2];

	blob_home_position(g, blob_id,w,h,v );

	b->x = v[0];
	b->y = v[1];
	b->vx = 0.01;
	b->vy = 0.01;
}

static int boids_reinit(boids_t *b, int radius, int num, int w, int h)
{
    int i,j;
    if( b->blob_ ) {
        for( i = 0; i < b->blob_dradius_; i ++ ) {
            if(b->blob_[i]) free(b->blob_[i]);
        }
        free(b->blob_);
    }

    b->blob_radius_ = radius;
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


    if(b->blobs_) {
        free(b->blobs_);
    }

    b->blob_num_ = num;
    b->blobs_ = (blob_t*) vj_calloc(sizeof(blob_t) * b->blob_num_ );
	if(!b->blobs_) {
        return 0;
    }
    
    int blob_radius_ = b->blob_radius_;
	for( i = -blob_radius_ ; i < blob_radius_ ; ++ i )
	{
		for( j = -blob_radius_ ; j < blob_radius_ ; ++ j ) 
		{
			int dist_sqrt = i * i + j * j;
			if( dist_sqrt < b->blob_sradius_ )
			{
				b->blob_[i + blob_radius_][j + blob_radius_] = 0xff;
			}
			else
			{
				b->blob_[i + blob_radius_][j + blob_radius_ ] = 0x0; // was 0
			}
		}
	}

    int blob_num_ = b->blob_num_;
        
	for( i = 0; i < blob_num_ ; i ++ )
	{
		blob_init_( b,b->blobs_ + i ,i, w , h );
	}


    return 1;
}

void *boids_malloc(int w, int h)
{
    boids_t *b = (boids_t*) vj_calloc(sizeof(boids_t));
    if(!b) {
        return NULL;
    }

    if(!boids_reinit( b, 16, 100,w,h )) {
        boids_free(b);
        return NULL;
    }

    b->blob_type_ = 1;
    b->blob_home_radius_ = 203;

	b->blob_image_ = (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h );
	if(!b->blob_image_) {
        boids_free(b);
        return NULL;
    }

  	veejay_memset( b->blob_image_ , 0 , w * h );

	b->blob_ready_  = 1;

	return (void*) b;
}


void boids_free(void *ptr)
{
    boids_t *b = (boids_t*) ptr;

	int i;
    if( b->blob_ ) {
	    for (i = 0; i < b->blob_dradius_ ; i ++ ) {
		    if( b->blob_[i] ) 
                free( b->blob_[i] );
        }
        free(b->blob_);
    }
	if(b->blobs_)
		free(b->blobs_);
	if(b->blob_image_)
		free(b->blob_image_);
    free(b);
}

typedef void (*blob_func)(boids_t *b, int s, int width);

static void	blob_render_circle(boids_t *b, int s, int width)
{
	int i,j;
    int blob_dradius_ = b->blob_dradius_;

	for( i = 0; i < blob_dradius_ ; ++ i )	
	{
		for( j = 0; j < blob_dradius_ ; ++ j)
		{
			if( b->blob_image_[ s + j ] + b->blob_[i][j] > 255 ) 
				    b->blob_image_[s + j] = 0xff;
			else
					b->blob_image_[s + j] += b->blob_[i][j];
		}
		s += width;
	}
}

static void	blob_render_rect(boids_t *b, int s, int width)
{
	int i,j;
    int blob_dradius_ = b->blob_dradius_;
	for( i = 0; i < blob_dradius_ ; ++ i )	
	{
		for( j = 0; j < blob_dradius_ ; ++ j)
		{
			b->blob_image_[s + j] = 0xff;
		}
		s += width;
	}
}

static blob_func	blob_render(boids_t *b)
{
	if( b->blob_type_ == BLOB_RECT)
		return blob_render_rect;
	return blob_render_circle;
}


// calculate center of mass
static	void	boid_rule1_( boids_t *b, int boid_id, double v1[2] )
{
	int i;
	double v[2] = { 0.0, 0.0 };
	for( i = 0; i < b->blob_num_ ; i ++ )
	{
		if( i != boid_id )
		{
			v[0] += (double) b->blobs_[i].x;
			v[1] += (double) b->blobs_[i].y;
		}
	}
	v[0] = v[0] / ( (double) b->blob_num_ - 1 );
	v[1] = v[1] / ( (double) b->blob_num_ - 1 );
	v1[0] = (v[0] - ((double)b->blobs_[boid_id].x)) / 100.0;  	
	v1[1] = (v[1] - ((double)b->blobs_[boid_id].y)) / 100.0;
}


// try to keep a small distance away from other blobs
static void	boid_rule2_( boids_t *b, int boid_id, double v1[2] )
{
	double v[2] = {0.0 , 0.0};
	int i;
	for( i = 0; i < b->blob_num_; i ++ )
	{
		if( i != boid_id)
		{
			// find nearby blob		
			double d = ( b->blobs_[boid_id].x - b->blobs_[i].x ) * ( b->blobs_[boid_id].x - b->blobs_[i].x ) +
					    ( b->blobs_[boid_id].y - b->blobs_[i].y ) * ( b->blobs_[boid_id].y - b->blobs_[i].y );
			
			if( d < b->blob_sradius_ )
			{
				v[0] = v[0] - ((double) ( b->blobs_[boid_id].x - b->blobs_[i].x ));
				v[1] = v[1] - ((double) ( b->blobs_[boid_id].y - b->blobs_[i].y ));
			}
		}
	}
	v1[0] = v[0];
	v1[1] = v[1];
}
	
// try to match velocity with near blobs
static void	boid_rule3_( boids_t *b, int boid_id, double v1[2] )
{
	double v[2] = { 0.0, 0.0 };
	int i;
	for( i = 0; i < b->blob_num_; i ++ )
	{
		if( boid_id != i )
		{
			v[0] = v[0] + b->blobs_[i].vx;
			v[1] = v[1] + b->blobs_[i].vy;
		}
	}
	v1[0] = v[0] /( (double)( b->blob_num_ -1 ));
	v1[0] = ( v[0] - b->blobs_[boid_id].vx ) / 8;
	v1[1] = v[1] /( (double)( b->blob_num_ -1 ));
	v1[1] = ( v[1] - b->blobs_[boid_id].vy ) / 8;
}

static void	boid_rule4_( boids_t *b, int boid_id, int vlim )
{
	// speed limiter
	if( b->blobs_[boid_id].vx > vlim )
		b->blobs_[boid_id].vx = ( b->blobs_[boid_id].vx / fabs( b->blobs_[boid_id].vx) ) * vlim;
	if( b->blobs_[boid_id].vy > vlim )
		b->blobs_[boid_id].vy = ( b->blobs_[boid_id].vy / fabs( b->blobs_[boid_id].vy) ) * vlim;
}

void boids_apply(void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0];
    int num = args[1];
    int shape = args[2];
    int m1 = args[3];
    int m2 = args[4];
    int m3 = args[5];
    int speed = args[6];
    int home_radius = args[7];

    boids_t *b = (boids_t*) ptr;

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *srcY = frame->data[0];
	uint8_t *srcCb= frame->data[1];
	uint8_t *srcCr= frame->data[2];
	int s,i,k;
	const double M1 = ( (m1==0? 0.0 : m1/100.0) );
	const double M2 = ( (m2==0? 0.0 : m2/100.0) );
	const double M3 = ( (m3==0? 0.0 : m3/1000.0) );
	blob_func f = blob_render(b);

	b->blob_type_ = shape;

	if( radius != b->blob_radius_ || num != b->blob_num_ )
	{ // reinitialize
        if(!boids_reinit(b, radius, num, frame->width, frame->height ))
            return;
	}

	if( home_radius != b->blob_home_radius_ )
	{
		b->blob_home_radius_ = home_radius;
		for( i = 0; i < b->blob_num_ ; i ++ )
			blob_init_(b,b->blobs_ + i , i, width, height);
	}

	// move boid to new positions
	for( i = 0; i < b->blob_num_; i ++)
	{
		double v1[2],v2[2],v3[2];

		boid_rule1_(b, i, v1 );
		boid_rule2_(b, i, v2 );
		boid_rule3_(b, i, v3 );

		v1[0] *= M1;
		v1[1] *= M1;
		v2[0] *= M2;
		v2[1] *= M2;
		v3[0] *= M3;
		v3[1] *= M3;

		b->blobs_[i].vx = b->blobs_[i].vx + v1[0] + v2[0] + v3[0];
		b->blobs_[i].vy = b->blobs_[i].vy + v1[1] + v2[1] + v3[1];

		boid_rule4_( b,i, speed * speed );

		b->blobs_[i].x = b->blobs_[i].x + (short) b->blobs_[i].vx;
		b->blobs_[i].y = b->blobs_[i].y + (short) b->blobs_[i].vy;

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
			blob_init_(b,b->blobs_ + k,k,width ,height );
		}
	}

	// project blob onto video frame
	for(i = 0; i < len ; i ++ )
	{
		if( b->blob_image_[i]  == 0x0 )
		{
			srcY[i] = pixel_Y_lo_;
			srcCb[i] = 128;
			srcCr[i] = 128;
		}
		b->blob_image_[i] = 0x0;
	}
}
