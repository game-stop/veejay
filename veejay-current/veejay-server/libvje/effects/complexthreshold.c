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
#include "complexthreshold.h"


vj_effect *complexthreshold_init(int w, int h)
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
    ve->defaults[4] = 1;	/* smoothen level */
    ve->defaults[5] = 255;	/* threshold */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 4;

	ve->parallel = 1;
    ve->description = "Complex Threshold (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->has_user = 0;
	ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list( ve->num_params,"Angle", "Red", "Green", "Blue", "Smoothen", "Threshold" );
    return ve;
}

/* this method decides whether or not a pixel from the fg will be accepted for keying */
int accept_tpixel(uint8_t fg_cb, uint8_t fg_cr, int cb, int cr,
		 int accept_angle_tg)
{
    short xx, yy;
    /* convert foreground to xz coordinates where x direction is
       defined by key color */
    uint8_t val;

    xx = ((fg_cb * cb) + (fg_cr * cr)) >> 7;
    yy = ((fg_cr * cb) - (fg_cb * cr)) >> 7;

    /* accept angle should not be > 90 degrees 
       reasonable results between 10 and 80 degrees.
     */

    val = (xx * accept_angle_tg) >> 4;
    if (abs(yy) < val) {
	return 1;
    }
    return 0;
}

void complexthreshold_apply(VJFrame *frame, VJFrame *frame2, int width,
			int height, int i_angle, int r, int g, int b,
			int level, int threshold)
{

    uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;

    int cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 255.0f, bb = 255.0f, _y = 0;
    float angle = (float) i_angle / 100.0f;
    //float noise_level = 350.0;
    unsigned int pos;
    int matrix[5];
    int val, tmp1;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	int iy,iv,iu;
	_rgb2yuv(r,g,b,iy,iu,iv);
	_y = (float)iy;
	aa = (float)iu;
	bb = (float)iv;

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

    /* intialize pointers */
    fg_y = Y2;
    fg_cb = Cb2;
    fg_cr = Cr2;

    bg_y = Y;
    bg_cb = Cb;
    bg_cr = Cr;

    for (pos = width + 1; pos < (len) - width - 1; pos++) {
	int i = 0;
	int smooth = 0;
	/* setup matrix 
	   [ - 0 - ] = do not accept. [ - 1 - ] = level 5 , accept only when all n = 1
	   [ 0 0 0 ]                  [ 1 1 1 ]
	   [ - 0 - ]                  [ - 1 - ]

	   [ - 0 - ] sum of all n is acceptance value for level
	   [ 1 0 1 ]                    
	   [ 0 1 0 ]
	 */
	matrix[0] = accept_tpixel(fg_cb[pos], fg_cr[pos], cb, cr, accept_angle_tg);	/* center pixel */
	matrix[1] = accept_tpixel(fg_cb[pos - 1], fg_cr[pos - 1], cb, cr, accept_angle_tg);	/* left pixel */
	matrix[2] = accept_tpixel(fg_cb[pos + 1], fg_cr[pos + 1], cb, cr, accept_angle_tg);	/* right pixel */
	matrix[3] = accept_tpixel(fg_cb[pos + width], fg_cr[pos + width], cb, cr, accept_angle_tg);	/* top pixel */
	matrix[4] = accept_tpixel(fg_cb[pos - width], fg_cr[pos - width], cb, cr, accept_angle_tg);	/* bottom pixel */
	for (i = 0; i < 5; i++) {
	    if (matrix[i] == 1)
		smooth++;
	}
	if (smooth >= level) {
	    short xx, yy;
	    /* get bg/fg pixels */
	    uint8_t p1 = (matrix[0] == 0 ? fg_y[pos] : bg_y[pos]);
	    uint8_t p2 = (matrix[1] == 0 ? fg_y[pos - 1] : bg_y[pos - 1]);
	    uint8_t p3 = (matrix[2] == 0 ? fg_y[pos + 1] : bg_y[pos + 1]);
	    uint8_t p4 =
		(matrix[3] == 0 ? fg_y[pos + width] : bg_y[pos + width]);
	    uint8_t p5 =
		(matrix[4] == 0 ? fg_y[pos - width] : bg_y[pos - width]);
	    /* and blur the pixel */
	    fg_y[pos] = (p1 + p2 + p3 + p4 + p5 + p1) / 6;

	    /* convert foreground to xz coordinates where x direction is
	       defined by key color */
	    xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
	    yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;

	    val = (xx * accept_angle_tg) >> 4;
	    /* see if pixel is within range of color and threshold */
	    if (abs(yy) < val && fg_y[pos] > threshold) {

		val = (yy * accept_angle_ctg) >> 4;

		x1 = abs(val);
		y1 = yy;
		tmp1 = xx - x1;

		kbg = (tmp1 * one_over_kc) >> 1;
		val = (tmp1 * kfgy_scale) >> 4;

		Y[pos] = fg_y[pos] - val;
		/* convert suppressed fg back to cbcr */
		Cb[pos] = ((x1 * cb) - (y1 * cr)) >> 7;
		Cr[pos] = ((x1 * cr) - (y1 * cb)) >> 7;

		Y[pos] = (Y[pos] + (kbg * bg_y[pos])) >> 8;
		Cb[pos] = (Cb[pos] + (kbg * bg_cb[pos])) >> 8;
		Cr[pos] = (Cr[pos] + (kbg * bg_cr[pos])) >> 8;
	    }
	}
    }
}
void complexthreshold_free(){}
