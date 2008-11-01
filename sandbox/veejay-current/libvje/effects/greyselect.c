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

#include "rgbkey.h"
#include <stdlib.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "common.h"

vj_effect *greyselect_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 300;	/* angle */
    ve->defaults[1] = 255;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 0;	/* b */
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 900;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
	ve->has_user = 0;
    ve->description = "Grayscale by Color Key";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    return ve;
}

void greyselect_apply( VJFrame *frame, int width,
		   int height, int i_angle, int r, int g,
		   int b)
{

    uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    int cb, cr;
    float kg1, tmp, aa = 128, bb = 128, _y = 0;
    float angle = (float) i_angle / 10.0;
    unsigned int pos;
    uint8_t val;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	int iy,iu,iv;
	_rgb2yuv(r,g,b,iy,iu,iv);
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
    fg_y = Y;
    fg_cb = Cb;
    fg_cr = Cr;

    bg_y = Y;
    bg_cb = Cb;
    bg_cr = Cr;

    for (pos = (width * height); pos != 0; pos--) {
	short xx, yy;
	xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
	if (xx < -128) {
	    xx = -128;
	}
	if (xx > 127) {
	    xx = 127;
	}
	yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;
	if (yy < -128) {
	    yy = -128;
	}
	if (yy > 127) {
	    yy = 127;
	}
	val = (xx * accept_angle_tg) >> 4;
	if (val > 127)
	    val = 127;

	if (abs(yy) > val) {
		Cb[pos]=128;
		Cr[pos]=128;
	}
    }
}

void greyselect_free(){}
