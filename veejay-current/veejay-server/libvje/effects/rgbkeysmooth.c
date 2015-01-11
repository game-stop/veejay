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
#include <stdlib.h>
#include <math.h>
#include "common.h"
#include "rgbkey.h"


vj_effect *rgbkeysmooth_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4500;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 150;	/* opacity */
    ve->defaults[5] = 1500;	/* noise level */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 5500;

    ve->has_user = 0;
    ve->description = "Transparent Chroma Key (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params,"Angle","Red","Green","Blue","Opacity","Noise level");
    return ve;
}


void rgbkeysmooth_apply(VJFrame *frame, VJFrame *frame2, int width,
			int height, int i_angle, int r, int g, int b,
			int opacity, int i_noise)
{

  uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    int cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 255.0f, bb = 255.0f, _y = 0;
    float angle = (float) i_angle /100.0f;
    float noise_level = (i_noise / 100.0f);
    unsigned int pos;
    uint8_t val, tmp1;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];
	int	iy=16,iu=128,iv=128;
	unsigned int op0 = opacity > 255 ? 255 : opacity;
	unsigned int op1 = 255 - op0;

	_rgb2yuv( r,g,b, iy,iu,iv );
	_y = (float) iy;
	aa = (float) iu;
	bb = (float) iv;
    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 255 * (aa / tmp);
    cr = 255 * (bb / tmp);
    kg1 = tmp;

    /* obtain coordinate system for cb / cr */
    accept_angle_tg = 0xf * tan(M_PI * angle / 180.0);
    accept_angle_ctg = 0xf / tan(M_PI * angle / 180.0);

    tmp = 1 / kg1;
    one_over_kc = 0xff * 2 * tmp - 0xff;
    kfgy_scale = 0xf * (float) (_y) / kg1;
    kg = kg1;

    /* intialize pointers */
    fg_y = Y;
    fg_cb = Cb;
    fg_cr = Cr;
	/* 2005: swap these !! */
    bg_y = Y2;
    bg_cb = Cb2;
    bg_cr = Cr2;

    for (pos = (width * height); pos != 0; pos--) {
	short xx, yy;
	/* convert foreground to xz coordinates where x direction is
	   defined by key color */

	xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
	yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;

	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

	val = (xx * accept_angle_tg) >> 4;
	if (abs(yy) < val) {
	    /* compute fg, suppress fg in xz according to kfg 
	*/
	    val = (yy * accept_angle_ctg) >> 4;

	    x1 = abs(val);
	    y1 = yy;
	    tmp1 = xx - x1;

	    kbg = (tmp1 * one_over_kc) >> 1;

	    val = (tmp1 * kfgy_scale) >> 4;
	    val = fg_y[pos] - val;

	    Y[pos] = val;

	    // convert suppressed fg back to cbcr 
		// cb,cr are signed, go back to unsigned !
	    Cb[pos] = ((x1 * cb) - (y1 * cr)) >> 7;
	    Cr[pos] = ((x1 * cr) - (y1 * cb)) >> 7;

	    // deal with noise 
	    val = (yy * yy) + (kg * kg);
	    if (val < (noise_level * noise_level)) {
		kbg = 255;
	    }

	    Y[pos] = ( Y[pos] + (kbg * bg_y[pos])) >> 8;
	    Y[pos] = ( Y[pos] * op0 + bg_y[pos]  * op1 ) >> 8;
	    Cb[pos] = (Cb[pos] + (kbg * bg_cb[pos])) >> 8;
	    Cb[pos] = (Cb[pos] * op0 + bg_cb[pos]  * op1) >> 8;
	    Cr[pos] = (Cr[pos] + (kbg * bg_cr[pos])) >> 8;
	    Cr[pos] = (Cr[pos] * op0 + bg_cr[pos] * op1 ) >> 8;
		
	}
    }

}
