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

/* 

	This effect is based on this small project:

	http://www.cs.utah.edu/~michael/chroma/

	The algorithm decides which pixels belong to resp. foreground 
        or background. Other effects that make use of this same algorithm are

	Complex Invert, Complex Negation, Complex Saturation, Smooth RGB Key 
	Isolate by Color ,  Complex Threshold and Blend by Color Key, 
*/


vj_effect *rgbkey_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 210;	/* angle */
    ve->defaults[1] = 3500;	/* noise */
    ve->defaults[2] = 255;	/* r */
    ve->defaults[3] = 0;	/* g */
    ve->defaults[4] = 0;	/* b */
    ve->defaults[5] = 1;	/* type */
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 900;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 6300;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 1;	/* total noise suppression off */

    ve->has_internal_data =0;
    ve->description = "Chroma Key (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    return ve;
}
void rgbkey_scan_fg(uint8_t * src2[3], int *r, int *g, int *b)
{
    *r = (int) (+(1.0 * src2[0][0]) + (0 * src2[1][0]) +
		(1.402 * src2[2][0]));
    *g = (int) (+(1.0 * src2[0][0]) - (0.344136 * src2[1][0]) +
		(-0.714136 * src2[2][0]));
    *b = (int) (+(1.0 * src2[0][0]) + (1.772 * src2[1][0]) +
		(0 * src2[2][0]));
}

void rgbkey_apply1(uint8_t * src1[3], uint8_t * src2[3], int width,
		   int height, int i_angle, int i_noise, int r, int g,
		   int b)
{

    uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    int cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 128, bb = 128, _y = 0;
    float angle = (float) i_angle / 10.0;
    float noise_level = (i_noise / 100.0);
    unsigned int pos;
    uint8_t val, tmp1;

    _y = ((Y_Redco * r) + (Y_Greenco * g) + (Y_Blueco * b) + 16);
    aa = ((U_Redco * r) - (U_Greenco * g) - (U_Blueco * b) + 128);
    bb = (-(V_Redco * r) - (V_Greenco * g) + (V_Blueco * b) + 128);
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
    fg_y = src1[0];
    fg_cb = src1[1];
    fg_cr = src1[2];

    bg_y = src2[0];
    bg_cb = src2[1];
    bg_cr = src2[2];

    for (pos = (width * height); pos != 0; pos--) {
	short xx, yy;
	/* convert foreground to xz coordinates where x direction is
	   defined by key color */

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


	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

	val = (xx * accept_angle_tg) >> 4;
	if (val > 127)
	    val = 127;
	//      if (abs(yy) > val) {
	if (abs(yy) < val) {
	    /* compute fg, suppress fg in xz according to kfg 
	*/
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
	    if (val > 0xff)
		val = 0xff;
	    val = fg_y[pos] - val;

	    src1[0][pos] = val;

	    // convert suppressed fg back to cbcr 

	    val = ((x1 * cb) - (y1 * cr)) >> 7;

	    src1[1][pos] = val;

	    val = ((x1 * cr) - (y1 * cb)) >> 7;
	    src1[2][pos] = val;

	    // deal with noise 

	    val = (yy * yy) + (kg * kg);
	    if (val > 0xff)
		val = 0xff;
	    if (val < (noise_level * noise_level)) {
		kbg = 255;
	    }

	    val = (src1[0][pos] + (kbg * bg_y[pos])) >> 8;
	    if (val < 16)
		val = 16;
	    else if (val > 235)
		val = 235;
	    src1[0][pos] = val;

	    val = (src1[1][pos] + (kbg * bg_cb[pos])) >> 8;
	    if (val < 16)
		val = 16;
	    else if (val > 240)
		val = 240;
	    src1[1][pos] = val;

	    val = (src1[2][pos] + (kbg * bg_cr[pos])) >> 8;
	    if (val < 16)
		val = 16;
	    else if (val > 240)
		val = 240;
	    src1[2][pos] = val;
		
	}
    }
}

void rgbkey_apply2(uint8_t * src1[3], uint8_t * src2[3], int width,
		   int height, int i_angle, int i_noise, int r, int g,
		   int b)
{

    uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    int cb, cr;
    int kbg, x1, y1;
    float kg1, tmp, aa = 128, bb = 128, _y = 0;
    float angle = (float) i_angle;
    float noise_level = (i_noise / 100.0);
    unsigned int pos;
    uint8_t val, tmp1;

    _y = ((Y_Redco * r) + (Y_Greenco * g) + (Y_Blueco * b) + 16);
    aa = ((U_Redco * r) - (U_Greenco * g) - (U_Blueco * b) + 128);
    bb = (-(V_Redco * r) - (V_Greenco * g) + (V_Blueco * b) + 128);
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
    fg_y = src1[0];
    fg_cb = src1[1];
    fg_cr = src1[2];

    bg_y = src2[0];
    bg_cb = src2[1];
    bg_cr = src2[2];

    for (pos = (width * height); pos != 0; pos--) {
	short xx, yy;
	/* convert foreground to xz coordinates where x direction is
	   defined by key color */

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


	/* accept angle should not be > 90 degrees 
	   reasonable results between 10 and 80 degrees.
	 */

	val = (xx * accept_angle_tg) >> 4;
	if (val > 127)
	    val = 127;
	//      if (abs(yy) > val) {
	if (abs(yy) < val) {
	    /* compute fg, suppress fg in xz according to kfg */

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
	    if (val > 0xff)
		val = 0xff;
	    val = fg_y[pos] - val;

	    src1[0][pos] = val;

	    /* convert suppressed fg back to cbcr */

	    val = ((x1 * cb) - (y1 * cr)) >> 7;

	    src1[1][pos] = val;

	    val = ((x1 * cr) - (y1 * cb)) >> 7;
	    src1[2][pos] = val;

	    /* deal with noise */

	    val = (yy * yy) + (kg * kg);
	    if (val > 0xff)
		val = 0xff;
	    if (val < (noise_level * noise_level)) {
		src1[0][pos] = src1[1][pos] = src1[2][pos] = 0;
		kbg = 255;
	    }

	    val = (src1[0][pos] + (kbg * bg_y[pos])) >> 8;
	    if (val < 16)
		val = 16;
	    else if (val > 235)
		val = 235;
	    src1[0][pos] = val;

	    val = (src1[1][pos] + (kbg * bg_cb[pos])) >> 8;
	    if (val < 16)
		val = 16;
	    else if (val > 240)
		val = 240;
	    src1[1][pos] = val;

	    val = (src1[2][pos] + (kbg * bg_cr[pos])) >> 8;
	    if (val < 16)
		val = 16;
	    else if (val > 240)
		val = 240;
	    src1[2][pos] = val;
	}
    }
}

 /* this is the same as rgbkey_apply1, but here we have total noise suppression 
  */

void rgbkey_apply(uint8_t * src1[3], uint8_t * src2[3], int width,
		  int height, int i_angle, int i_noise, int red, int green,
		  int blue, int type)
{

    switch (type) {
    case 0:
	rgbkey_apply1(src1, src2, width, height, i_angle, i_noise, red,
		      green, blue);
	break;
    case 1:
	rgbkey_apply2(src1, src2, width, height, i_angle, i_noise, red,
		      green, blue);
	break;
    }

}
void rgbkey_free(){}
