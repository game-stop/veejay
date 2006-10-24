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

#include "complexsaturate.h"
#include <stdlib.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "common.h"

vj_effect *complexsaturation_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 300;	/* angle */
    ve->defaults[1] = 255;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 0;	/* b */
    ve->defaults[4] = 50;	/* v_adjust */
    ve->defaults[5] = 50;	/* degrees */
    ve->defaults[6] = 2400;	/* noise suppression */	
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 900;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 360;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 256;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 3500;
	ve->has_user = 0;
    ve->description = "Complex Saturation";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    return ve;
}

void complexsaturation_apply(VJFrame *frame, int width,
		   int height, int i_angle, int r, int g,
		   int b, int adjust_v, int adjust_degrees, int i_noise)
{
	double dsaturation,dcolor;
//	double degrees = adjust_degrees * 0.01;
//	double dsat = adjust_v * 0.01;


	float	hue	= (adjust_degrees/180.0)*M_PI;
	float	sat	= (adjust_v * 0.01);

	uint8_t *fg_y, *fg_cb, *fg_cr;
	uint8_t *bg_y, *bg_cb, *bg_cr;
  	int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    int cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 128, bb = 128, _y = 0;
    float angle = (float) i_angle * 0.1f;
    float noise_level = (i_noise * 0.01f);
    unsigned int pos;
    uint8_t val, tmp1;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame->data[0];
 	uint8_t *Cb2= frame->data[1];
	uint8_t *Cr2= frame->data[2];
	int	iy=pixel_Y_lo_,iu=128,iv=128;
	_rgb2yuv( r,g,b, iy,iu,iv );
	_y = (float) iy;
	aa = (float) iu;
	bb = (float) iv;
    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 127 * (aa / tmp);
    cr = 127 * (bb / tmp);
    kg1 = tmp;

    /* obtain coordinate system for cb / cr */
    accept_angle_tg = 0xf * tan(M_PI * angle / 180.0);
    accept_angle_ctg = 0xf / tan(M_PI * angle / 180.0);

    tmp = 1 / kg1;
    one_over_kc = 0xff * 2 * tmp - 0xff;
    kfgy_scale = 0xf * (float) (_y) / kg1;
    kg = kg1;

    /* intialize pointers */
    fg_y = frame->data[0];
    fg_cb = frame->data[1];
    fg_cr = frame->data[2];

    bg_y = frame->data[0];
    bg_cb = frame->data[1];
    bg_cr = frame->data[2];
	const int s = (int) rint( sin(hue) * (1<<16) * sat );
	const int c = (int) rint( cos(hue) * (1<<16) * sat );
	for (pos = 0; pos < frame->len; pos++)
	{
		short xx, yy;

		xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;

		if (xx < -128) 
		    xx = -128;
	
		if (xx > 127) 
		    xx = 127;
	

		yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;

		if (yy < -128)
		    yy = -128;
	
		if (yy > 127) 
		    yy = 127;	
	


	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

		val = (xx * accept_angle_tg) >> 4;
		if (val > 127)
		    val = 127;

	if (abs(yy) > val) { /* pixel is within selected color range,  saturate */	
		val = (yy * accept_angle_ctg) >> 4;

		    x1 = abs(val);
		    y1 = yy;
		    tmp1 = xx - x1;

		    kbg = (tmp1 * one_over_kc) >> 1;
		    if (kbg < 0)
				kbg = 0;
			    if (kbg > 255)
			kbg = 255;

		    val = (tmp1 * kfgy_scale) >> 4;
		    val = fg_y[pos] - val;
	   		Y[pos] = val;
 			val = ((x1 * (cb-128)) - (y1 * (cr-128))) >> 7;
	    	Cb[pos] = val;
	    	val = ((x1 * (cr-128)) - (y1 * (cb-128))) >> 7;
	    	Cr[pos] = val;

	    	val = (yy * yy) + (kg * kg);
	    	if (val < (noise_level * noise_level)) {
			kbg = 255;
	    	}

	   	val = (Y[pos] + (kbg * bg_y[pos])) >> 8;
		Y[pos] = CLAMP_Y(val);

	    	val = (Cb[pos] + (kbg * bg_cb[pos])) >> 8;
		Cb[pos] = CLAMP_UV(val);

	   	val = (Cr[pos] + (kbg * bg_cr[pos])) >> 8;
		Cr[pos] = CLAMP_UV(val);

		int _cb = Cb[pos] - 128;
		int _cr = Cr[pos] - 128;
		if( _cb != 0 && _cr != 0)
		{
		/*	double co=0.0,si=0.0;
			//fast_sqrt( dsaturation, (double) (_cb * cr + _cr * _cr) );
			dsaturation = ccolor_sqrt( (double) _cb, (double) _cr);
			dcolor = ccolor_sine( _cb, dsaturation);
			if( _cr < 0) {
				dcolor = M_PI - dcolor;
			}
			dcolor += (degrees * M_PI) / 180.0;
			dsaturation *= dsat;
			sin_cos( co, si , dcolor );
			Cb[pos] = si * dsaturation + 128;
				const int u = Cb[i] - 128;
		const int v = Cr[i] - 128;
		int new_u = (c * u - s * v + (1<<15) + (128<<16)) >> 16;
		int new_v = (s * u + c * v + (1<<15) + (128<<16)) >> 16;
		if( new_u & 768 ) new_u = (-new_u) >> 31;
		if( new_v & 768 ) new_v = (-new_v) >> 31;
Cr[pos] = co * dsaturation + 128;*/
			const int u = Cb[pos] - 128;
			const int v = Cr[pos] - 128;
			int new_u = (c * u - s * v + (1<<15) + (128<<16)) >> 16;
			int new_v = (s * u + c * v + (1<<15) + (128<<16)) >> 16;
			if( new_u & 768 ) new_u = (-new_u) >> 31;
			if( new_v & 768 ) new_v = (-new_v) >> 31;
			Cb[pos] = new_u;
			Cr[pos] = new_v;
		}
    	}

	}
}

void complexsaturate_free(){}
