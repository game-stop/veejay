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
#include <stdint.h>
#include <stdio.h>
#include "fisheye.h"
#include <stdlib.h>
#include "common.h"
#include <math.h>
#define    RUP8(num)(((num)+8)&~8)

vj_effect *fisheye_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = -1000;
    ve->limits[1][0] = 1000;
    ve->defaults[0] = 1;
    ve->description = "Fish Eye";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Curve" );
    return ve;
}

static int _v = 0;
static double *polar_map = NULL;
static double *fish_angle = NULL;
static int *cached_coords = NULL; 
static uint8_t *buf[3] = { NULL,NULL,NULL };

int	fisheye_malloc(int w, int h)
{
	int x,y;
	int h2=h/2;
	int w2=w/2;
	int p =0;

	buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h  *  3 ) );
	if(!buf[0]) return 0;
	buf[1] = buf[0] + (w*h);
	buf[2] = buf[1] + (w*h);

	polar_map = (double*) vj_calloc(sizeof(double) * RUP8(w* h) );
	if(!polar_map) return 0;
	fish_angle = (double*) vj_calloc(sizeof(double) * RUP8(w* h) );
	if(!fish_angle) return 0;

	cached_coords = (int*) vj_calloc(sizeof(int) * RUP8( w * h));
	if(!cached_coords) return 0;

	for(y=(-1 *h2); y < (h-h2); y++)
	{
		for(x= (-1 * w2); x < (w-w2); x++)
		{
			double res;
			fast_sqrt( res,(double) (y*y+x*x));
			p = (h2+y)*w+(w2+x);
			polar_map[p] = res;
			//polar_map[p] = sqrt( y*y + x*x );
			fish_angle[p] = atan2( (float) y, x);
		}
	}
	_v = 0;
	return 1;
}

void	fisheye_free()
{
	if(buf[0])
		free(buf[0]);
	buf[0] =  NULL;
	buf[1] =  NULL;
	buf[2] =  NULL;
	
	if(polar_map) 	free(polar_map);
	if(fish_angle)	free(fish_angle);
	if(cached_coords) free(cached_coords);

}

static double __fisheye(double r,double v, double e)
{
	return (exp( r / v )-1) / e;
}
			
static double __fisheye_i(double r, double v, double e)
{
	return v * log(1 + e * r);
}	



void fisheye_apply(VJFrame *frame, int w, int h, int v )
{
	int i;
	double (*pf)(double a, double b, double c);
 	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	if( v==0) v =1;

	if( v < 0 ) {
		pf = &__fisheye_i;
		v = v * -1;
	}
	else  {
		pf = &__fisheye;
	}

	if( v != _v )
	{
		const double curve = 0.001 * v;
		const unsigned int R = h/2;
		const double coeef = R / log(curve * R + 1);
		/* pre calculate */
		unsigned int i;  
		int px,py;
		double r,a,co,si;
		const int w2 = w/2;
		const int h2 = h/2;
		
		for(i=0; i < len; i++)
		{
			r = polar_map[i];
			a = fish_angle[i];
			if(r <= R)
			{
				r = pf( r, coeef, curve );
				sin_cos( si,co, a);
				px =(int) ( r * co);
				py =(int) ( r * si);
				
				px += w2;
				py += h2;

				if(px < 0) px =0;
				if(px > w) px = w;
				if(py < 0) py = 0;
				if(py >= (h-1)) py = h-1;

				cached_coords[i] = (py * w)+px;
			}
			else
			{
				cached_coords[i] = -1;

			}
		}
		_v = v;
	}

	veejay_memcpy(buf[0], Y,(w*h));
	veejay_memcpy(buf[1], Cb,(w*h));
	veejay_memcpy(buf[2], Cr,(w*h));

	for(i=0; i < len; i++)
	{
		if(cached_coords[i] == -1)
		{
			Y[i] = pixel_Y_lo_;		
			Cb[i] = 128;
			Cr[i] = 128;
		}
		else
		{

			Y[i] = buf[0][ cached_coords[i] ];
			Cb[i] = buf[1][ cached_coords[i] ];
			Cr[i] = buf[2][ cached_coords[i] ];
		}
	}
}
