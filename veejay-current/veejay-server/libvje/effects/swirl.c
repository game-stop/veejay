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
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <config.h>
#include "swirl.h"
#include <stdlib.h>
#include "common.h"
#include <math.h>

vj_effect *swirl_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->defaults[0] = 250;
    ve->description = "Swirl";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Degrees" );
    return ve;
}

static double *polar_map = NULL;
static double *fish_angle = NULL;
static int *cached_coords = NULL;
static uint8_t *buf[4] = { NULL,NULL,NULL,NULL };


int swirl_malloc(int w, int h)
{
	int x,y;
	int h2=h/2;
	int w2=w/2;
	int p = 0;
	
	int i;
	for( i = 0; i < 3;i ++ ) {
		buf[i] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(w*h));
		if(!buf[i])
			return 0;
	}
	
	polar_map = (double*) vj_calloc(sizeof(double) * w* h );
	if(!polar_map) return 0;
	fish_angle = (double*) vj_calloc(sizeof(double) * w* h );
	if(!fish_angle) return 0;

	cached_coords = (int*) vj_calloc(sizeof(int) * w * h );
	if(!cached_coords) return 0;

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
	int i;
	for( i = 0; i < 3; i ++ ) {
		if(buf[i])
			free(buf[i]);
		buf[i] = NULL;
	}
	
	if(polar_map) 
		free(polar_map);
	if(fish_angle)
		free(fish_angle);
	if(cached_coords)
		free(cached_coords);
	polar_map = NULL;
	fish_angle = NULL;
	cached_coords = NULL;
}


static int _v = 0;
void swirl_apply(VJFrame *frame, int w, int h, int v)
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

	if( v != _v )
	{
             //const double curve = (double) v; 
                const unsigned int R = w;
	        const double coeef = v;   
             //const double coeef = R / log(curve * R + 1);
                //const double coeef = R / log( curve * R + 2);
             /* pre calculate */
                unsigned int i;  
                int px,py;
                double r,a;
		double si,co;	
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

	int strides[4] = { len, len, len , 0 };
	vj_frame_copy( frame->data, buf, strides );

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

			Y[i] = Y[ cached_coords[i] ];
			Cb[i] = Cb[ cached_coords[i] ];
			Cr[i] = Cr[ cached_coords[i] ];
		}
	}
}
