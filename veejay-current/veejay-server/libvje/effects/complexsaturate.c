/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "complexsaturate.h"

vj_effect *complexsaturation_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4500;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 50;	/* v_adjust */
    ve->defaults[5] = 50;	/* degrees */
    ve->defaults[6] = 3500;	/* noise suppression */	
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 1;
    ve->limits[1][4] = 36000;

    ve->limits[0][5] = 1;
    ve->limits[1][5] = 3600;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 5500;

	ve->has_user = 0;
    ve->description = "Complex Saturation (RGB)";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1; 
    ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Angle", "Red", "Green", "Blue", "Intensity", "Degrees", "Noise suppression" );
    return ve;
}

void complexsaturation_apply(void *ptr, VJFrame *frame,int *args ) {
    int i_angle = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];
    int adjust_v = args[4];
    int adjust_degrees = args[5];
    int i_noise = args[6];

	const int len = frame->len;

	float	hue	= (adjust_degrees/180.0)*M_PI;
	float	sat	= (adjust_v / 100.0f);

	uint8_t *fg_y, *fg_cb, *fg_cr;
	uint8_t *bg_y, *bg_cb, *bg_cr;
  	int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    uint8_t cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 255.0f, bb = 255.0f, _y = 0;
    float angle = (float) i_angle / 100.0f;
    float noise_level = (i_noise  / 100.0f);
    unsigned int pos;
    uint8_t val, tmp1;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	int	iy=pixel_Y_lo_,iu=128,iv=128;
	_rgb2yuv( r,g,b, iy,iu,iv );
	_y = (float) iy;
	aa = (float) iu;
	bb = (float) iv;
    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 255 * (aa / tmp);
    cr = 255 * (bb / tmp);
    kg1 = tmp;

    /* obtain coordinate system for cb / cr */
    accept_angle_tg = (int)( 15.0f * tanf(M_PI * angle / 180.0f));
    accept_angle_ctg= (int)( 15.0f / tanf(M_PI * angle / 180.0f));

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
	const int s = (int) rint( a_sin(hue) * (1<<16) * sat );
	const int c = (int) rint( a_cos(hue) * (1<<16) * sat );
	for (pos = 0; pos < len; pos++)
	{
		short xx, yy;

		xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
		yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;

	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

		val = (xx * accept_angle_tg) >> 4;
		if (abs(yy) > val) { /* pixel is within selected color range,  saturate */	
			val = (yy * accept_angle_ctg) >> 4;

		    x1 = abs(val);
		    y1 = yy;
		    tmp1 = xx - x1;

		    kbg = (tmp1 * one_over_kc) >> 1;

		  	val = (tmp1 * kfgy_scale) >> 4;
			val = fg_y[pos] - val;
	   		Y[pos] = val;
 			val = ((x1 * cb) - (y1 * cr)) >> 7;
		    	Cb[pos] = val;
	    		val = ((x1 * cr) - (y1 * cb)) >> 7;
	    		Cr[pos] = val;

	    		val = (yy * yy) + (kg * kg);
	    		if (val < (noise_level * noise_level)) {
				kbg = 255;
	    		}

	   		Y[pos] = (Y[pos] + (kbg * bg_y[pos])) >> 8;
	    		Cb[pos] = (Cb[pos] + (kbg * bg_cb[pos])) >> 8;
	   		Cr[pos] = (Cr[pos] + (kbg * bg_cr[pos])) >> 8;

			int _cb = Cb[pos] - 128;
			int _cr = Cr[pos] - 128;
			if( _cb != 0 && _cr != 0)
			{
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
