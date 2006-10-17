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
#include "pencilsketch.h"
#include "common.h"
 
vj_effect *pencilsketch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;/* type */
    ve->defaults[1] = 16;	/* min */
    ve->defaults[2] = 235;	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[1][2] = 255;
    ve->limits[0][2] = 0;

    ve->description = "Pencil Sketch (8)";   
    ve->extra_frame = 0;
    ve->sub_format = 0;
	ve->has_user = 0;
    return ve;
}

/* PenCil sketch pixel Function 
   applies some artithematic on pixel a and b,
   if the resulting pixel is within some range, 
   it will be made black otherwise white
*/
typedef uint8_t (*_pcf) (uint8_t a, uint8_t b, int t_max);
typedef uint8_t (*_pcbcr) (uint8_t a, uint8_t b);

	static uint8_t _pcf_dneg(uint8_t a, uint8_t b, int t_max)
	{
		uint8_t p =  
			255 - ( abs ( (255 - abs((255-a)-a))  -    (255-abs((255-b)-b))) );
		p = (abs(abs(p-b) - b));
	//	if( p >= 16 || p <= t_max) p = 16 ; else p = 240;
		return p;
	}

	static uint8_t _pcf_lghtn(uint8_t a, uint8_t b, int t_max)
	{
		return (a > b ? a : b );
	}

	static uint8_t _pcf_dneg2(uint8_t a,uint8_t b, int t_max)
	{
		uint8_t p = ( 255 - abs ( (255-a)- b )  );
		if( p >= 16 || p <= t_max) p = 16 ; else p = 240;
		return p;
	}

	static uint8_t _pcf_min(uint8_t a, uint8_t b, int t_max)
	{
		uint8_t p = ( (b < a) ? b : a);
		p = ( 255 - abs( (255-p) - b ) );
	//	if( p >= 16 || p <= t_max) p = 16 ; else p = 240;
		return p;
	}

	static uint8_t _pcf_max(uint8_t a,uint8_t b, int t_max)
	{
		int p = ( (b > a) ? b : a);
		if( p <= 0 )
			return 0;
		p = ( 255 - ((255 - b) * (255 - b)) / p);
	//	if( p >= 16 || p <= t_max) p = 16 ; else p = 240;
		return (uint8_t)p;
	}

	static uint8_t _pcf_pq(uint8_t a,uint8_t b, int t_max)
	{
		if(a <= 0 ) a=16;
		if(b <= 0 ) b=16;
		int p = 255 - ((255-a) * (255-a)) / a;
		int q = 255 - ((255-b) * (255-b)) / b;
		if(q <= 0 ) q=16;
		p = ( 255 - ((255-p) * (255 - a)) / q);
	//	if( p >= 16 || p <= t_max) p = 16 ; else p = 240;
		return (uint8_t)p;
	}

	static uint8_t _pcf_color(uint8_t a, uint8_t b, int t_max)
	{
		uint8_t p =  
			255 - ( abs ( (255 - abs((255-a)-a))  -    (255-abs((255-b)-b))) );
		p = (abs(abs(p-b) - b));
		p = p + b - (( p * b ) >> 8);
	//	if( p >= 16 || p <= t_max) p = 16 ; else p = 240;
	
		return p;
	}
	static uint8_t _pcbcr_color(uint8_t a,uint8_t b)
	{
		int p = a - 128;
		int q = b - 128;
		return ( p + q - (( p * q ) >> 8) ) + 128 ;
	}

	static uint8_t _pcf_none(uint8_t a, uint8_t b, int t_max)
	{
		if( a >= 16 || a <= t_max) a = 16 ; else a = 240;
		return a;
	}

	/* get a pointer to a pixel function */
	_pcf	_get_pcf(int type)
	{
	
		switch(type)
		{
	 
		 case 0: return &_pcf_dneg;
	 	 case 3: return &_pcf_lghtn;
	 	 case 1: return &_pcf_min;
	 	 case 2: return &_pcf_max;
	 	 case 5: return &_pcf_pq;
	 	 case 6: return &_pcf_dneg2;
		 case 7: return &_pcf_color;
		}
	
		return &_pcf_none;
	}


void pencilsketch_apply(
	 	VJFrame *frame,
		int width,	
		int height,
		int type,
	   	int threshold_min,
		int threshold_max
		)
{
	unsigned int i;
	int len = frame->len;
	int uv_len = frame->uv_len;
	int m,d;
	uint8_t y,yb;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];


 	/* get a pointer to a pixel blend function */
	_pcf _pff = _get_pf(type);
	_pcbcr _pcbcrff = &_pcbcr_color;

	len = len - width - 1 ;

	for(i=0; i < len; i++)
	{
		y = Y[i];
	//	if( y < 16 ) y = 16; else if (y > 235) y = 235;
		yb = y;

		/* substract user defined mask from image */
		if(y >= threshold_min && y <= threshold_max)
		{
			/* sharpen the pixels */
			m = (Y[i+1] + Y[i+width] + Y[i+width+1]+2) >> 2;
			d = Y[i] - m;
			d *= 500;
			d /= 100;
			m = m + d;
			/* a magical forumula to combine the pixel with the original*/
			y = ((((y << 1) - (255 - m))>>1) + Y[i])>>1;
		//	if(y < 16) y = 16; else if (y>240) y = 240;
			/* apply blend operation on masked pixel */
			Y[i] = _pff(y,yb,threshold_max);
		}
		else
		{
			Y[i] = 240;
		}
	}

	/* data in I420 or YV12 */

	if(type != 7) /* all b/w sketches */
	{
		for(i=0; i < uv_len; i++)
		{	
			Cb[i] = 128;
			Cr[i] = 128;
		}
	}
	else /* all colour sketches */
	{
		for(i=0; i < uv_len; i++)	
		{
			Cb[i] = _pcbcrff(128, Cb[i]);
			Cr[i] = _pcbcrff(128, Cr[i]);
		}
	}
}
void pencilsketch_free(){}
