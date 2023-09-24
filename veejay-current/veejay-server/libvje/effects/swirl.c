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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "swirl.h"

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

typedef struct {
    double *polar_map;
    double *fish_angle;
    int *cached_coords;
    uint8_t *buf[4];
    int v;
} swirl_t;

void  *swirl_malloc(int w, int h)
{
	int x,y;
	int h2=h/2;
	int w2=w/2;
	int p = 0;


    swirl_t *s = (swirl_t*) vj_calloc( sizeof(swirl_t) );
    if(!s) {
        return NULL;
    }
    s->buf[0] = vj_malloc( sizeof(uint8_t) * RUP8(w*h*3));
    if(!s->buf[0]) {
        swirl_free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + (w*h);
    s->buf[2] = s->buf[1] + (w*h);

	s->polar_map = (double*) vj_calloc(sizeof(double) * RUP8(w * h) );
	if(!s->polar_map) {
        swirl_free(s);
        return NULL;
    }

	s->fish_angle = (double*) vj_calloc(sizeof(double) * RUP8(w * h) );
	if(!s->fish_angle) {
        swirl_free(s);
        return NULL;
    }

	s->cached_coords = (int*) vj_calloc(sizeof(int) * RUP8(w * h) );
	if(!s->cached_coords) {
        swirl_free(s);
        return NULL;
    }

    double *polar_map = s->polar_map;
    double *fish_angle = s->fish_angle;

	for(y=(-1 *h2); y < (h-h2); y++)
	{
		for(x=(-1 * w2); x < (w-w2); x++)
		{
			p = (h2+y) * w + (w2+x);
			polar_map[p] = sqrt( y*y + x*x );
			fish_angle[p] = atan2( (float) y, x);
		}
	}

	return (void*) s;
}

void	swirl_free(void *ptr)
{
    swirl_t *s = (swirl_t*) ptr;

    if(s) {
        if( s->buf[0] ) 
            free(s->buf[0] );
        if( s->polar_map )
            free(s->polar_map);
        if( s->fish_angle )
            free(s->fish_angle);
        if( s->cached_coords )
            free(s->cached_coords );
        free(s);
    }
}

void swirl_apply(void *ptr, VJFrame *frame, int *args)
{
	int i;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    int v = args[0];
    swirl_t *s = (swirl_t*) ptr;

    double *polar_map = s->polar_map;
    double *fish_angle = s->fish_angle;
    int *cached_coords = s->cached_coords;
    uint8_t **buf = s->buf;

	if( s->v != v )
	{
		const unsigned int R = width;
		const double coeef = v;
		
        /* pre calculate */
		int px,py;
		double r,a;
		double si,co;
		const int w2 = width/2;
		const int h2 = height/2;

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
				if(px > width) px = width;
				if(py < 0) py = 0;
				if(py >= (height-1)) py = height-1;

				cached_coords[i] = (py * width)+px;
			}
			else
			{
				cached_coords[i] = -1;
			}
		}
		s->v = v;
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
