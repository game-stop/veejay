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
    ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 10;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 1;	/* blend type */
    ve->limits[0][0] = 5;
    ve->limits[1][0] = 900;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 256;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 7;	/* total noise suppression off */

    ve->has_internal_data = 0;
    ve->description = "Blend by Color Key";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    return ve;
}

typedef uint8_t(*blend_func)(uint8_t y1, uint8_t y2);
blend_func get_blend_func(const int mode);

uint8_t blend_func1(uint8_t a, uint8_t b) {
	return (uint8_t)  255 - (abs(255-a-b));
}

uint8_t blend_func2(uint8_t a, uint8_t b) {
	uint8_t val;
	if(a < 16) a = 16;
	if(b < 16) b = 16;
	val = 255 -  ((255-b) * (255-b))/a;
	if(val < 16)val=16;
	if(val > 235) val=235;
	return val;
}

uint8_t blend_func3(uint8_t a , uint8_t b) {
	return (uint8_t) (a * b)>>8;
}


uint8_t blend_func4(uint8_t a, uint8_t b) {
	uint8_t val;
	if(b > 235) b = 235;
	val = (a * a) / ( 255 - b );
	if(val < 16) val = a;
	if(val > 235) val = a;
	return val;
}

uint8_t blend_func5(uint8_t a, uint8_t b) {
	uint8_t val;
	int c = 255 - b;
	if(c<16)c=16;
	val = b / c;
	if(val > 235) val = 235;
	if(val < 16) val = 16;
	return val;
}

uint8_t blend_func6(uint8_t a, uint8_t b) {
	uint8_t val = a + (2 * (b)) - 235;
	if(val < 16) val = 16;
	if(val > 235) val = 235;
	return val;
}

uint8_t blend_func7(uint8_t a, uint8_t b) {
	return (uint8_t) ( a + ( b - 255) );
}

uint8_t blend_func8(uint8_t a, uint8_t b) {
	int c;
	if(b < 128) c = (a * b) >> 7;
	else c = 255 - ((255-b)*(255-a)>>7);
	if(c<16)c=16;
	return (uint8_t) c;
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
void keyselect_apply(uint8_t * src1[3], uint8_t * src2[3], int width,
		   int height, int i_angle, int r, int g,
		   int b, int mode)
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
    blend_func blend_pixel;
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
    blend_pixel = get_blend_func((const int)mode);
    
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
	if (abs(yy) < val) {
		src1[0][pos] = blend_pixel(src1[0][pos],src2[0][pos]);
	}
    }
}

void keyselect_free(){}
