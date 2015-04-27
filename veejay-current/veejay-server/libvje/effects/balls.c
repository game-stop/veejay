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

#include <config.h>
#include <stdint.h>
#include <math.h>
#include <libvjmem/vjmem.h>
#include "common.h"
#include "blob.h"


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

static blob_t 		*blobs_;
static uint8_t 		**blob_;
static uint8_t 		*blob_image_;

static int		blob_ready_	 = 0;
static int		blob_radius_ 	 = 16;
static int		blob_dradius_ 	 = 0;
static int		blob_sradius_ 	 = 0;
static int		blob_num_	 = 100;
static int		blob_type_	 = 1;
static int		blob_home_radius_= 203;

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
	ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Blobs", "Shape", "Cohesion","Seperation","Alignment","Speed", "Home Radius");
	return ve;
}




static void	blob_home_position( int blob_id, int w, int h , double v[2] )
{
	double theta = 360.0 / ( (double) blob_num_ ) * blob_id;
	double rad = (theta / 180.0 ) * M_PI;
	double ratio = (w/h);
	double cx = ( double )( w/ 2);
	double cy = ( double )( h/ 2) * ratio;
	v[0] = cx + cos(rad) * blob_home_radius_;
	v[1] = cy + sin(rad) * blob_home_radius_;
}

static void	blob_init_( blob_t *b , int blob_id, int w , int h)
{
	double v[2];

	blob_home_position( blob_id,w,h,v );

	b->x = v[0];
	b->y = v[1];
	b->vx = 0.01;
	b->vy = 0.01;
}

int	boids_malloc(int w, int h)
{
	int j,i;
	double frac;
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

	blob_image_ = (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h );
	if(!blob_image_) return 0;

		
	for( i = -blob_radius_ ; i < blob_radius_ ; ++ i )
	{
		for( j = -blob_radius_ ; j < blob_radius_ ; ++ j ) 
		{
			dist_sqrt = i * i + j * j;
			if( dist_sqrt < blob_sradius_ )
			{
				frac = (double) (sqrt(dist_sqrt)) / (double) blob_sradius_;
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
		blob_init_( blobs_ + i ,i, w , h );
	}

	veejay_memset( blob_image_ , 0 , w * h );

	blob_ready_  = 1;

	return 1;
}


void boids_free()
{
	int i;
	for (i = 0; i < blob_dradius_ ; i ++ )
		if( blob_[i] ) free( blob_[i] );
	if(blobs_)
		free(blobs_);
	if(blob_image_)
		free(blob_image_);
}

typedef void (*blob_func)(int s, int width);

static	int	blob_collision( blob_t  b, blob_t this )
{
	int dx = this.x - b.x;
	int dy = this.y - b.y;
	double dvx = this.vx - b.vx;
	double dvy = this.vy - b.vy;
	double D   = dx * dx + dy * dy;
	
	if( abs( dx ) > this.d || abs(dy) > this.d )
		return 0;	
	if( D > this.d2 )
		return 0;


	double mag = dvx * dx + dvy * dy;

	if( mag > 0 )	
		return 0;

	mag /= D;

	double ovx = dx * mag;
	double ovy = dy * mag;


	this.vx -= ovx;
	this.vy -= ovy;

	b.vx += ovx;
	b.vy += ovy;

	return 1;
}

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
		return blob_render_rect;
	return blob_render_circle;
}


// calculate center of mass
static	void	boid_rule1_( int boid_id, double v1[2] )
{
	int i;
	double v[2] = { 0.0, 0.0 };
	for( i = 0; i < blob_num_ ; i ++ )
	{
		if( i != boid_id )
		{
			v[0] += (double) blobs_[i].x;
			v[1] += (double) blobs_[i].y;
		}
	}
	v[0] = v[0] / ( (double) blob_num_ - 1 );
	v[1] = v[1] / ( (double) blob_num_ - 1 );
	v1[0] = (v[0] - ((double)blobs_[boid_id].x)) / 100.0;  	
	v1[1] = (v[1] - ((double)blobs_[boid_id].y)) / 100.0;
}


// try to keep a small distance away from other blobs
static void	boid_rule2_( int boid_id, double v1[2] )
{
	double v[2] = {0.0 , 0.0};
	int i;
	for( i = 0; i < blob_num_; i ++ )
	{
		if( i != boid_id)
		{
			// find nearby blob		
			double d = ( blobs_[boid_id].x - blobs_[i].x ) * ( blobs_[boid_id].x - blobs_[i].x ) +
					    ( blobs_[boid_id].y - blobs_[i].y ) * ( blobs_[boid_id].y - blobs_[i].y );
			
			if( d < blob_sradius_ )
			{
				v[0] = v[0] - ((double) ( blobs_[boid_id].x - blobs_[i].x ));
				v[1] = v[1] - ((double) ( blobs_[boid_id].y - blobs_[i].y ));
			}
		}
	}
	v1[0] = v[0];
	v1[1] = v[1];
}
	
// try to match velocity with near blobs
static void	boid_rule3_( int boid_id, double v1[2] )
{
	double v[2] = { 0.0, 0.0 };
	int i;
	for( i = 0; i < blob_num_; i ++ )
	{
		if( boid_id != i )
		{
			v[0] = v[0] + blobs_[i].vx;
			v[1] = v[1] + blobs_[i].vy;
		}
	}
	v1[0] = v[0] /( (double)( blob_num_ -1 ));
	v1[0] = ( v[0] - blobs_[boid_id].vx ) / 8;
	v1[1] = v[1] /( (double)( blob_num_ -1 ));
	v1[1] = ( v[1] - blobs_[boid_id].vy ) / 8;
}

static void	boid_rule4_( int boid_id, int vlim )
{
	// speed limiter
	if( blobs_[boid_id].vx > vlim )
		blobs_[boid_id].vx = ( blobs_[boid_id].vx / fabs( blobs_[boid_id].vx) ) * vlim;
	if( blobs_[boid_id].vy > vlim )
		blobs_[boid_id].vy = ( blobs_[boid_id].vy / fabs( blobs_[boid_id].vy) ) * vlim;
}

void boids_apply(VJFrame *frame,
			   int width, int height, int radius, int num, int shape, int m1, int m2, int m3, int speed, int home_radius )
{
	const int len = frame->len;
	uint8_t *srcY = frame->data[0];
	uint8_t *srcCb= frame->data[1];
	uint8_t *srcCr= frame->data[2];
	int s,i,j,k;
	const double M1 = ( (m1==0? 0.0 : m1/100.0) );
	const double M2 = ( (m2==0? 0.0 : m2/100.0) );
	const double M3 = ( (m3==0? 0.0 : m3/100.0) );
	
	blob_func f = blob_render();

	blob_type_ = shape;

	if( radius != blob_radius_ || num != blob_num_ )
	{ // reinitialize
		blob_radius_ = radius;
		blob_num_ 	 = num;
		boids_free();
		boids_malloc(width,height);
	}

	if( home_radius != blob_home_radius_ )
	{
		blob_home_radius_ = home_radius;
		for( i = 0; i < blob_num_ ; i ++ )
			blob_init_(blobs_ + i , i, width, height);
	}

	// move boid to new positions
	for( i = 0; i < blob_num_; i ++)
	{
		double v1[2],v2[2],v3[2];

		boid_rule1_( i, v1 );
		boid_rule2_( i, v2 );
		boid_rule3_( i, v3 );

		v1[0] *= M1;
		v1[1] *= M1;
		v2[0] *= M2;
		v2[1] *= M2;
		v3[0] *= M3;
		v3[1] *= M3;

		blobs_[i].vx = blobs_[i].vx + v1[0] + v2[0] + v3[0];
		blobs_[i].vy = blobs_[i].vy + v1[1] + v2[1] + v3[1];

		boid_rule4_( i, speed * speed );

		blobs_[i].x = blobs_[i].x + (short) blobs_[i].vx;
		blobs_[i].y = blobs_[i].y + (short) blobs_[i].vy;

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
			blob_init_( blobs_ + k,k,width ,height );
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
