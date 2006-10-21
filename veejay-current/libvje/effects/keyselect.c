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

vj_effect *keyselect_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 300;	/* angle */
    ve->defaults[1] = 255;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 0;	/* b */
    ve->defaults[4] = 3;	/* blend type */
    ve->defaults[5] = 2400;	/* noise suppression */
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 900;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 256;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 7;

	ve->limits[0][5] = 0;
	ve->limits[1][5] = 3500;
	ve->has_user = 0;
    ve->description = "Blend by Color Key";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->rgb_conv = 1;
    return ve;
}

typedef uint8_t(*blend_func)(uint8_t y1, uint8_t y2);
blend_func get_blend_func(const int mode);

uint8_t blend_func1(uint8_t a, uint8_t b) {
	return (uint8_t)  255 - (abs(255-a-b));
}

uint8_t blend_func2(uint8_t a, uint8_t b) {
	uint8_t val;
	if(a < pixel_Y_lo_) a =  pixel_Y_lo_;
	if(b <  pixel_Y_lo_) b =  pixel_Y_lo_;
	val = 255 -  ((255-b) * (255-b))/a;
	return CLAMP_Y(val);
}

uint8_t blend_func3(uint8_t a , uint8_t b) {
	return (uint8_t) (a * b)>>8;
}


uint8_t blend_func4(uint8_t a, uint8_t b) {
	uint8_t val;
	if(b > pixel_Y_hi_) b = pixel_Y_hi_;
	val = (a * a) / ( 255 - b );
	return CLAMP_Y(val);
}

uint8_t blend_func5(uint8_t a, uint8_t b) {
	uint8_t val;
	int c = 255 - b;
	if( c  < pixel_Y_lo_ )
		c = pixel_Y_lo_;
	val = b / c;
	return CLAMP_Y(val);
}

uint8_t blend_func6(uint8_t a, uint8_t b) {
	uint8_t val = a + (2 * (b)) - 255;
	return CLAMP_Y(val);
}

uint8_t blend_func7(uint8_t a, uint8_t b) {
	return (uint8_t) ( a + ( b - 255) );
}

uint8_t blend_func8(uint8_t a, uint8_t b) {
	int c;
	if(b < 128) c = (a * b) >> 7;
	else c = 255 - ((255-b)*(255-a)>>7);
	return CLAMP_Y(c);
}



blend_func get_blend_func(const int mode) {
	switch(mode) {
		case 0: return &blend_func1;
		case 1: return &blend_func2;
		case 2: return &blend_func3;
		case 3: return &blend_func4;
		case 4: return &blend_func5;
		case 5: return &blend_func6;
		case 6: return &blend_func7;
		case 7: return &blend_func8;
	}
	return &blend_func1;	
}

/*
http://www.cs.utah.edu/~michael/chroma/
*/
void keyselect_apply( VJFrame *frame, VJFrame *frame2, int width,
		   int height, int i_angle, int r, int g,
		   int b, int mode, int i_noise)
{

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
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];
	int	iy=pixel_Y_lo_,iu=128,iv=128;
	_rgb2yuv( r,g,b, iy,iu,iv );
	_y = (float) iy;
	aa = (float) iu;
	bb = (float) iv;
    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 127 * (aa / tmp);
    cr = 127 * (bb / tmp);
    kg1 = tmp;

    blend_func blend_pixel = get_blend_func(mode);

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
	    val = fg_y[pos] - val;

	    Y[pos] = val;

	    // convert suppressed fg back to cbcr 
		// cb,cr are signed, go back to unsigned !
	    val = ((x1 * (cb-128)) - (y1 * (cr-128))) >> 7;
	    Cb[pos] = val;
	    val = ((x1 * (cr-128)) - (y1 * (cb-128))) >> 7;
	    Cr[pos] = val;
	    // deal with noise 

	    val = (yy * yy) + (kg * kg);
	    if (val < (noise_level * noise_level)) {
		kbg = 255;
	    }
	 	val = (Y[pos] + (kbg * bg_y[pos])) >> 8;
		Y[pos] = blend_pixel( val, fg_y[pos] );
		
	}
    }


}

void keyselect_free(){}
