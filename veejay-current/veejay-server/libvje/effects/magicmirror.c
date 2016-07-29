/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2016 Niels Elburg <nwelburg@gmail.com>
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
#include <stdio.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include "common.h"
#include "motionmap.h"
#include "magicmirror.h"


// if d or n changes, tables need to be calculated
static uint8_t *magicmirrorbuf[4] = { NULL,NULL,NULL,NULL };
static double *funhouse_x = NULL;
static double *funhouse_y = NULL;
static unsigned int *cache_x = NULL;
static unsigned int *cache_y = NULL;
static unsigned int last[4] = {0,0,20,20};
static int cx1 = 0;
static int cx2 = 0;
static int n__ = 0;
static int N__ = 0;

vj_effect *magicmirror_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 5;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */

	ve->defaults[0] = w/4;
	ve->defaults[1] = h/4;
	ve->defaults[2] = 20;
	ve->defaults[3] = 20;
	ve->defaults[4] = 0;

	ve->limits[0][0] = 0;
	ve->limits[1][0] = w/2;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = h/2;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 100;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 100;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 2;

	ve->motion = 1;
	ve->sub_format = 1;
	ve->description = "Magic Mirror Surface";
	ve->has_user =0;
	ve->extra_frame = 0;
	ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
	ve->param_description = vje_build_param_list(ve->num_params, "X", "Y", "X","Y", "Alpha" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][4], 4, "Normal", "Alpha Mirror Mask", "Alpha Mirror Mask Only" );

	return ve;
}

int magicmirror_malloc(int w, int h)
{
	magicmirrorbuf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*RUP8(w*h*4));
	if(!magicmirrorbuf[0])
		return 0;

	magicmirrorbuf[1] = magicmirrorbuf[0] + RUP8(w*h);
	magicmirrorbuf[2] = magicmirrorbuf[1] + RUP8(w*h);
	magicmirrorbuf[3] = magicmirrorbuf[2] + RUP8(w*h);
	
	funhouse_x = (double*)vj_calloc(sizeof(double) * w );
	if(!funhouse_x) return 0;

	cache_x = (unsigned int *)vj_calloc(sizeof(unsigned int)*w);
	if(!cache_x) return 0;

	funhouse_y = (double*)vj_calloc(sizeof(double) * h );
	if(!funhouse_y) return 0;

	cache_y = (unsigned int*)vj_calloc(sizeof(unsigned int)*h);
	if(!cache_y) return 0;

	n__ =0;
	N__ =0;

	return 1;
}

void magicmirror_free()
{
	if(magicmirrorbuf[0]) free(magicmirrorbuf[0]);
	if(funhouse_x) free(funhouse_x);
	if(funhouse_y) free(funhouse_y);
	if(cache_x) free(cache_x);
	if(cache_y) free(cache_y);
	magicmirrorbuf[0] = NULL;
	magicmirrorbuf[1] = NULL;
	magicmirrorbuf[2] = NULL;
	magicmirrorbuf[3] = NULL;
	cache_x = NULL;
	cache_y  = NULL;
	funhouse_x = NULL;
	funhouse_y = NULL;
}

void magicmirror_apply( VJFrame *frame, int vx, int vy, int d, int n, int alpha )
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const unsigned int len = frame->len;
	double c1 = (double)vx;
	double c2 = (double)vy;
	int motion = 0;
	int interpolate = 1;
	if( motionmap_active())
	{
		if( motionmap_is_locked() ) {
			d = cx1;
			n = cx2;
		} else {
			motionmap_scale_to( 100,100,0,0, &d, &n, &n__, &N__ );
			cx1 = d;
			cx2 = n;
		}
		motion = 1;
	}
	else
	{
		n__ = 0;
		N__ = 0;
	}

	if( N__ == n__ || n__ == 0 )
		interpolate = 0;

	double c3 = (double)d * 0.001;
	unsigned int dx,dy,x,y,p,q;
	double c4 = (double)n * 0.001;
	int changed = 0;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *A = frame->data[3];

	if( d != last[1] ) {
		changed = 1; last[1] =d;
	}
	if( n != last[0] ) {
		changed = 1; last[0] = n;
	}

	if( vx != last[2] ) {
		changed = 1; last[2] = vx;
	}
	if( vy != last[3] ) {
		changed = 1; last[3] = vy;
	} 

	if(changed==1)
	{	
		// degrees x or y changed, need new sin
		for(x=0; x < width ; x++)
		{
			double res;
			fast_sin(res,(double)(c3*x));
			funhouse_x[x] = res;
		}
		for(y=0; y < height; y++)
		{
			double res;
			fast_sin(res,(double)(c4*y));
			funhouse_y[y] = res;
		}
	}
	for(x=0; x < width; x++)
	{
		dx = x + funhouse_x[x] * c1;
		if(dx < 0) dx += width;
		if(dx < 0) dx = 0; else if (dx >= width) dx = width-1;
		cache_x[x] = dx;
	}
	for(y=0; y < height; y++)
	{
		dy = y + funhouse_y[y] * c2;
		if(dy < 0) dy += height;
		if(dy < 0) dy = 0; else if (dy >= height) dy = height-1;
		cache_y[y] = dy;
	}

	veejay_memcpy( magicmirrorbuf[0], frame->data[0], len );
	veejay_memcpy( magicmirrorbuf[1], frame->data[1], len );
	veejay_memcpy( magicmirrorbuf[2], frame->data[2], len );

	if( alpha ) {
		veejay_memcpy( magicmirrorbuf[3], frame->data[3], len );
		/* apply on alpha first */
		for(y=1; y < height-1; y++)
		{
			for(x=1; x < width-1; x++)
			{
				q = y * width + x;
				p = cache_y[y] * width + cache_x[x];
				A[q] = magicmirrorbuf[3][p];
			}
		}

		uint8_t *Am = magicmirrorbuf[3];
		
		switch(alpha) {
				case 1:
					for(y=1; y < height-1; y++)
					{
						for(x=1; x < width-1; x++)
						{
							q = y * width + x;
							p = cache_y[y] * width + cache_x[x];
							if( Am[p] || A[q] ) { 
								Y[q] = magicmirrorbuf[0][p];
								Cb[q] = magicmirrorbuf[1][p];
								Cr[q] = magicmirrorbuf[2][p];
							}
						}
					}
					break;
				case 2: 
					{
						//@ try get a bg from somwhere
						uint8_t *bgY = vj_effect_get_bg( 0, 0 );
						uint8_t *bgCb= vj_effect_get_bg( 0, 1 );
						uint8_t *bgCr= vj_effect_get_bg( 0, 2 );

						if( bgY == NULL || bgCb == NULL || bgCr == NULL ) {
							veejay_msg(0,"This mode requires 'Subtract background' FX");
							break;
						}
						for(y=1; y < height-1; y++)
						{
							for(x=1; x < width-1; x++)
							{
								q = y * width + x;
								p = cache_y[y] * width + cache_x[x];

								if( A[q] ) {
									Y[q] = magicmirrorbuf[0][p];
									Cb[q] = magicmirrorbuf[1][p];
									Cr[q] = magicmirrorbuf[2][p];
								} else if ( Am[q] ) {
									//@ put in pixels from static bg 
									Y[q] = bgY[q];
									Cb[q] = bgCb[q];
									Cr[q] = bgCr[q];
								}
							}
						}

					}
					break;
		}
	}
	else {
		for(y=1; y < height-1; y++)
		{
			for(x=1; x < width-1; x++)
			{
				q = y * width + x;
				p = cache_y[y] * width + cache_x[x];
	
				Y[q] = magicmirrorbuf[0][p];
				Cb[q] = magicmirrorbuf[1][p];
				Cr[q] = magicmirrorbuf[2][p];
			}
		}
	}


	if( interpolate )
	{
		motionmap_interpolate_frame( frame, N__, n__ );
	}

	if( motion )
	{
		motionmap_store_frame(frame);
	}

}
