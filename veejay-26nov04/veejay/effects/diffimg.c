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
#include <stdlib.h>
#include "../vj-effect.h"
#include <stdint.h>
#include "diffimg.h"
vj_effect *diffimg_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 6;/* type */
    ve->defaults[1] = 15;	/* min */
    ve->defaults[2] = 235;	/* max */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 7;
    /* 0,179,0253,0127 */
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;

    ve->limits[1][2] = 255;
    ve->limits[0][2] = 1;

    ve->description = "Enhanced Magic Blend";
    ve->has_internal_data = 0;
    ve->extra_frame = 0;
    ve->sub_format = 0;

    return ve;
}

typedef uint8_t (*_pf) (uint8_t a, uint8_t b);

static uint8_t _pf_dneg(uint8_t a, uint8_t b)
{
	uint8_t t =  
		255 - ( abs ( (255 - abs((255-a)-a))  -    (255-abs((255-b)-b))) );
	return ( abs( abs(t-b) - b ));
}

static uint8_t _pf_lghtn(uint8_t a, uint8_t b)
{
	if( a > b ) return a;
	return b;
}

static uint8_t _pf_dneg2(uint8_t a,uint8_t b)
{
	return ( 255 - abs ( (255-a)- b )  );
}
static uint8_t _pf_min(uint8_t a, uint8_t b)
{
	uint8_t p = ( (b < a) ? b : a);
	return ( 255 - abs( (255-p) - b ) );
}
static uint8_t _pf_max(uint8_t a,uint8_t b)
{
	uint8_t p = ( (b > a) ? b : a);
	return ( 255 - ((255 - b) * (255 - b)) / p);
		
}
static uint8_t _pf_pq(uint8_t a,uint8_t b)
{
	int p = 255 - ((255-a) * (255-a)) / a;
	int q = 255 - ((255-b) * (255-b)) / b;
	
	return ( 255 - ((255-p) * (255 - a)) / q);
}

static uint8_t _pf_none(uint8_t a, uint8_t b)
{
	return a;
}

_pf	_get_pf(int type)
{
	
	switch(type)
	{
	 
	 case 0: return &_pf_dneg;
	 case 3: return &_pf_lghtn;
	 case 1: return &_pf_min;
	 case 2: return &_pf_max;
	 case 5: return &_pf_pq;
	 case 6: return &_pf_dneg2;

	}
	return &_pf_none;
}


void diffimg_apply(
	    VJFrame *frame,
		int width,
		int height,
		int type,
		int threshold_min,
		int threshold_max
		)
{
	unsigned int i;
	const int len = frame->len - width - 2;
	int d,m;
	uint8_t y,yb;
 	uint8_t *Y = frame->data[0];

	_pf _pff = _get_pf(type);

	for(i=0; i < len; i++)
	{
		y = Y[i];
		if( y < 16 ) y = 16; else if (y > 235) y = 235;
		yb = y;
		if(y >= threshold_min && y <= threshold_max)
		{
			m = (Y[i+1] + Y[i+width] + Y[i+width+1]+2) >> 2;
			d = Y[i] - m;
			d *= 500;
			d /= 100;
			m = m + d;
			y = ((((y << 1) - (255 - m))>>1) + Y[i])>>1;
			if(y < 16) y = 16; else if (y>235) y = 235;
			Y[i] = _pff(y,yb);
		}
	}
}
void diffimg_free(){}
