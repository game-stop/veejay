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
#include <config.h>
#include "swirl.h"
#include <stdlib.h>
#include "common.h"
#include <math.h>
#include "vj-common.h"

vj_effect *swirl_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->defaults[0] = 250;
    ve->description = "Swirl";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_internal_data= 1;

    return ve;
}

static double *polar_map;
static double *fish_angle;
static int *cached_coords;
static uint8_t *buf[3];


swirl_malloc(int w, int h)
{
	int x,y;
	int h2=h/2;
	int w2=w/2;
	int p = 0;

	buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!buf[0]) return -1;
	buf[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!buf[1]) return -1;

	buf[2] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!buf[2]) return -1;

	polar_map = (double*) vj_malloc(sizeof(double) * w* h );
	if(!polar_map) return -1;
	fish_angle = (double*) vj_malloc(sizeof(double) * w* h );
	if(!fish_angle) return -1;

	cached_coords = (int*) vj_malloc(sizeof(int) * w * h );
	if(!cached_coords) return -1;

	for(y=(-1 *h2); y < (h-h2); y++)
	{
		for(x=(-1 * w2); x < (w-w2); x++)
		{
			p = (h2+y) * w + (w2+x);
			polar_map[p] = sqrt( y*y + x*x );
			fish_angle[p] = atan2( (float) y, x);
		}
	}

	return 1;
}

void	swirl_free()
{
	if(buf[0])	free(buf[0]);
	if(buf[1])	free(buf[1]);
	if(buf[2])	free(buf[2]);

	if(polar_map) 	free(polar_map);
	if(fish_angle)	free(fish_angle);


}


extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

static double __fisheye(double r,double v, double e)
{
        return (exp( r / v )-1) / e;
}

static int _v = 0;
void swirl_apply(uint8_t *yuv[3], int w, int h, int v)
{
	int i;
	const int len = w * h;
	unsigned int n1=0,n2=0;
	if( v != _v )
	{
             //const double curve = (double) v; 
                const unsigned int R = w;
	        const double coeef = v;   
             //const double coeef = R / log(curve * R + 1);
                //const double coeef = R / log( curve * R + 2);
             /* pre calculate */
                unsigned int i;  
                int px,py,x,y;
                double r,a;
		double si,co;	
		double p_r,p_y;
                const int w2 = w/2;
                const int h2 = h/2;
                
                for(i=0; i < len; i++)
                {
                        r = polar_map[i];
                        a = fish_angle[i];
                        if(r <= R)
                        {
				//uncomment for simple fisheye
				//p_y = a;
				//p_r = (r*r)/R;
				//sin_cos( co, si, p_y );
			sin_cos( co,si, (a+r/coeef));	
			px = (int) ( r * co );
			py = (int) ( r * si );
				//sin_cos( co, si, (double)v );
				//px = (int) (r * co);
				//py = (int) (r * si);
				//px = (int) ( p_r * co);
				//py = (int) ( p_r * si);
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
	veejay_memcpy(buf[0], yuv[0],(w*h));
	veejay_memcpy(buf[1], yuv[1],(w*h));
	veejay_memcpy(buf[2], yuv[2],(w*h));

	for(i=0; i < len; i++)
	{
		if(cached_coords[i] == -1)
		{
			yuv[0][i] = 16;		
			yuv[1][i] = 128;
			yuv[2][i] = 128;
		}
		else
		{

			yuv[0][i] = buf[0][ cached_coords[i] ];
			yuv[1][i] = buf[1][ cached_coords[i] ];
			yuv[2][i] = buf[2][ cached_coords[i] ];
		//	yuv[0][i] = 240;
		//	yuv[1][i] = 128;
		//	yuv[2][i] = 128;
		}
	}
}
