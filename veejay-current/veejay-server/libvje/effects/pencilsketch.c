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
#include "pencilsketch.h"
#include "common.h"

extern int vj_task_available();
 
vj_effect *pencilsketch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;/* type */
    ve->defaults[1] = pixel_Y_lo_;	/* min */
    ve->defaults[2] = pixel_Y_hi_;	/* max */
    ve->defaults[3] = 0; /* sharpen, 1=negate */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 0xff;
    ve->limits[1][2] = 0xff;
    ve->limits[0][2] = 0;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Blend Mode", "Min Threshold", "Max Treshold", "Mask" );
    ve->description = "Pencil Sketch (8)";   
    ve->extra_frame = 0;
    ve->sub_format = 0;
	ve->has_user = 0;
	ve->parallel = 1;    
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
			0xff - ( abs ( (0xff - abs((0xff-a)-a))  -    (0xff-abs((0xff-b)-b))) );
		p = (abs(abs(p-b) - b));
		return p;
	}

	static uint8_t _pcf_lghtn(uint8_t a, uint8_t b, int t_max)
	{
		return (a > b ? a : b );
	}

	static uint8_t _pcf_dneg2(uint8_t a,uint8_t b, int t_max)
	{
		uint8_t p = ( 0xff - abs ( (0xff-a)- b )  );
		return p;
	}

	static uint8_t _pcf_min(uint8_t a, uint8_t b, int t_max)
	{
		uint8_t p = ( (b < a) ? b : a);
		p = ( 0xff - abs( (0xff-p) - b ) );
		return p;
	}

	static uint8_t _pcf_max(uint8_t a,uint8_t b, int t_max)
	{
		int p =  ( (b > a) ? b : a);
		p = CLAMP_Y(p);
		if( p == 0 ) p = 1;
		p = ( 0xff - ((0xff - b) * (0xff - b)) / p);
		return (uint8_t)p;
	}

	static uint8_t _pcf_pq(uint8_t a,uint8_t b, int t_max)
	{
		a = CLAMP_Y(a);
		b = CLAMP_Y(b);
		if( a == 0 ) a = 1; else if ( b == 0 ) b = 1;
		int p = 0xff - ((0xff-a) * (0xff-a)) / a;
		int q = 0xff - ((0xff-b) * (0xff-b)) / b;
		p = ( 0xff - ((0xff-p) * (0xff - a)) / q);
		return (uint8_t)p;
	}

	static uint8_t _pcf_color(uint8_t a, uint8_t b, int t_max)
	{
		uint8_t p =  
			0xff - ( abs ( (0xff - abs((0xff-a)-a))  -    (0xff-abs((0xff-b)-b))) );
		p = (abs(abs(p-b) - b));
		p = p + b - (( p * b ) >> 8);
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
		if( a >= pixel_Y_lo_ || a <= t_max) a = pixel_Y_lo_ ; else a = pixel_Y_hi_;
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
		int threshold_max,
		int mode
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
	_pcf _pff =  (_pcf) _get_pf(type);
	_pcbcr _pcbcrff = &_pcbcr_color;

	if(mode == 1 ) {

		for(i=0; i < len; i++)
		{
			y = Y[i];
			yb = y;

			if(y >= threshold_min && y <= threshold_max)
				Y[i] = _pff(y,0xff-yb,threshold_max);
			else
				Y[i] = pixel_Y_hi_;
		}
	}
	else
	{
		len = len - width - 1;

		for(i=0; i < len; i++)
		{
			y = Y[i];
			yb = y;

			/* substract user defined mask from image */
			if(y >= threshold_min && y <= threshold_max)
			{
				/* sharpen the pixels */
				m = (Y[i] + Y[i+1] + Y[i+width] + Y[i+width-1]+2) >> 2;
				d = Y[i] - m;
				d *= 500;
				d /= 100;
				m = m + d;
				y = ((((y << 1) - (0xff - m))>>1) + Y[i])>>1;
				/* apply blend operation on masked pixel */
				Y[i] = _pff(y,yb,threshold_max);
			}
			else
			{
				Y[i] = pixel_Y_hi_;
			}
		}

		for(i = len; i < (len+width-1); i ++ ) {
			y = Y[i];
			yb = y;

			/* substract user defined mask from image */
			if(y >= threshold_min && y <= threshold_max)
			{
				/* sharpen the pixels */
				m = (Y[i] + Y[i+1] + Y[i] + Y[i+1]+2) >> 2;
				d = Y[i] - m;
				d *= 500;
				d /= 100;
				m = m + d;
				y = ((((y << 1) - (0xff - m))>>1) + Y[i])>>1;
				/* apply blend operation on masked pixel */
				Y[i] = _pff(y,yb,threshold_max);
			}
			else
			{
				Y[i] = pixel_Y_hi_;
			}
		
		}


	}
	/* data in I420 or YV12 */

	if(type != 7) /* all b/w sketches */
	{
		veejay_memset( Cb, 128, uv_len );
		veejay_memset( Cr, 128, uv_len );
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
