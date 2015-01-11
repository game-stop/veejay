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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "rgbkey.h"
#include <math.h>
#include "common.h"
#include "complexinvert.h"


vj_effect *complexinvert_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 3000;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 3000; /* noise suppression*/
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;
    ve->parallel = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

	ve->limits[0][4] = 0;
	ve->limits[1][4] = 5500;
	ve->has_user = 0;
    ve->description = "Complex Invert (RGB)";
    ve->extra_frame = 0;
    ve->sub_format = 1;
	ve->rgb_conv = 1;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list(
					ve->num_params, "Angle", "Red", "Green", "Blue", "Noise suppression" );
    return ve;
}

void complexinvert_apply(VJFrame *frame, int width,
			int height, int i_angle, int r, int g, int b, int i_noise)
{
	uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    uint8_t cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 255.0f, bb = 255.0f, _y = 0;
    float angle = (float) i_angle /100.0f;
    float noise_level = (i_noise / 100.0);
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
    cb = 0xff * (aa / tmp);
    cr = 0xff* (bb / tmp);
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

    for (pos = 0; pos < frame->len; pos++)
	{
		short xx, yy;

		xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
		yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;


	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

		val = (xx * accept_angle_tg) >> 4;
		if (abs(yy) < val )
		{
  			val = (yy * accept_angle_ctg) >> 4;

		    x1 = abs(val);
		    y1 = yy;
		    tmp1 = xx - x1;

		    kbg = (tmp1 * one_over_kc) >> 1;
			if (kbg > 255)
				kbg = 255;

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

			Y[pos] = 0xff - ((Y[pos] + (kbg * bg_y[pos])) >> 8);
	    	Cb[pos] = 0xff - ((Cb[pos] + (kbg * bg_cb[pos])) >> 8);
   			Cr[pos] = 0xff - ( (Cr[pos] + (kbg * bg_cr[pos])) >> 8);
	    }
	}
}
void complexinvert_free(){}
