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
#include "magicmirror.h"
#include <stdlib.h>
#include <math.h>
#include "common.h"

// if d or n changes, tables need to be calculated
static uint8_t *magicmirrorbuf[3];
static double *funhouse_x = NULL;
static double *funhouse_y = NULL;
static unsigned int *cache_x = NULL;
static unsigned int *cache_y = NULL;
static unsigned int last[2] = {0,0};

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

vj_effect *magicmirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */

    ve->defaults[0] = 76;
    ve->defaults[1] = 3;
    ve->defaults[2] = 8;
    ve->defaults[3] = 87;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 720;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 576;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 3600;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 3600;

    ve->sub_format = 1;
    ve->description = "Magic Mirror Surface";
    ve->has_internal_data = 1;
    ve->extra_frame = 0;

    return ve;
}

int magicmirror_malloc(int w, int h)
{
	
	magicmirrorbuf[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * w * h );
	if(!magicmirrorbuf[0]) return 0;
	magicmirrorbuf[1] = (uint8_t*)vj_malloc(sizeof(uint8_t) * w * h );
	if(!magicmirrorbuf[1]) return 0;
	magicmirrorbuf[2] = (uint8_t*)vj_malloc(sizeof(uint8_t) * w * h );
	if(!magicmirrorbuf[2]) return 0;
	funhouse_x = (double*)vj_malloc(sizeof(double) * w );
	if(!funhouse_x) return 0;
	cache_x = (unsigned int *)vj_malloc(sizeof(unsigned int)*w);
	if(!cache_x) return 0;
	funhouse_y = (double*)vj_malloc(sizeof(double) * h );
	if(!funhouse_y) return 0;
	cache_y = (unsigned int*)vj_malloc(sizeof(unsigned int)*h);
	if(!cache_y) return 0;
	memset(funhouse_x,0.0,w); 
	memset(cache_x,0,w);
	memset(funhouse_y,0.0,h); 
	memset(cache_y,0,h);


	memset(magicmirrorbuf[0],16,w*h);
	memset(magicmirrorbuf[1],128,w*h);
	memset(magicmirrorbuf[2],128,w*h);
	return 1;
}

void magicmirror_free()
{
	if(magicmirrorbuf[0]) free(magicmirrorbuf[0]);
	if(magicmirrorbuf[1]) free(magicmirrorbuf[1]);
	if(magicmirrorbuf[2]) free(magicmirrorbuf[2]);
	if(funhouse_x) free(funhouse_x);
	if(funhouse_y) free(funhouse_y);
	if(cache_x) free(cache_x);
	if(cache_y) free(cache_y);
}

void magicmirror_apply(uint8_t *yuv[3], int w, int h, int vx, int vy, int d, int n )
{
	double c1 = (double)vx;
	double c2 = (double)vy;
	double c3 = (double)d/1000.;
	unsigned int dx,dy,x,y,p,q,len=w*h;
	double c4 = (double)n/1000.;
	int changed = 0;
	unsigned int R = h/2;
	if( d != last[1] )
	{
		changed = 1; last[1] =d;
	}
	if( n != last[0] )
	{
		changed = 1; last[0] = n;
	}

	if(changed==1)
	{	// degrees x or y changed, need new sin
		for(x=0; x < w ; x++)
		{
			double res;
			fast_sin(res,(double)(c3*x));
			funhouse_x[x] = res;
			//funhouse_x[x] = sin(c3 * x);  
		}
		for(y=0; y < h; y++)
		{
			double res;
			fast_sin(res,(double)(c4*x));
			funhouse_y[y] = res;
			//funhouse_y[y] = sin(c4 * y);
		}
	}

	veejay_memcpy( magicmirrorbuf[0], yuv[0], len );
	veejay_memcpy( magicmirrorbuf[1], yuv[1], len );
	veejay_memcpy( magicmirrorbuf[2], yuv[2], len );


/*	for(y=0; y < h ; y++)
	{
		for(x=0; x < w; x++)
		{
			
			dx = x + sin( c3 * x) * c1;
			dy = y + sin( c4 * y ) * c2;

			//dy = y + cos( c4 * y ) * c3;
			if(dx < 0) dx += w;
			if(dy < 0) dy += h;
			if(dx < 0) dx = 0; else if (dx > w) dx = w;
			if(dy < 0) dy = 0; else if (dy >= h) dy = h-1;
			p = dy * w + dx;
			yuv[0][y*w+x] = magicmirrorbuf[p];
			
			//yuv[0][y*w+x] = (magicmirrorbuf[p] + yuv[0][y*w+x] )>> 1;

		}
	}
*/

	// cache row results (speed up)
	for(x=0; x < w; x++)
	{
		dx = x + funhouse_x[x] * c1;
		if(dx < 0) dx += w;
		if(dx < 0) dx = 0; else if (dx > w) dx = w-1;
		cache_x[x] = dx;
	}
	for(y=0; y < h; y++)
	{
		dy = y + funhouse_y[y] * c2;
		if(dy < 0) dy += h;
		if(dy < 0) dy = 0; else if (dy > h) dy = h-1;
		cache_y[y] = dy;
	}

	// do image 
	for(y=1; y < h-1; y++)
	{
		for(x=1; x < w-1; x++)
		{
			p = cache_y[y] * w + cache_x[x];
			q = y * w + x;
			yuv[0][q] = magicmirrorbuf[0][p];
			yuv[1][q] = magicmirrorbuf[1][p];
			yuv[2][q] = magicmirrorbuf[2][p];
		}
	}
}
