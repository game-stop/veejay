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
#include "keyselect.h"

vj_effect *keyselect_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4500;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 3;	/* blend type */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 7;

	ve->has_user = 0;
    ve->parallel = 1;
	ve->description = "Blend by Color Key (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Angle","Red","Green","Blue","Blend mode" );
    return ve;
}

typedef uint8_t(*blend_func)(uint8_t y1, uint8_t y2);
static blend_func get_blend_func(const int mode);

static uint8_t blend_func1(uint8_t a, uint8_t b) {
	int diff = abs( 0xff - a - b );
	return (uint8_t)  (0xff - diff);
}

static uint8_t blend_func2(uint8_t a, uint8_t b) {
	uint8_t val;
	if( a == 0 )
	  a = 0xff;
	val = 255 -  ((255-b) * (255-b))/a;
	return CLAMP_Y(val);
}

static uint8_t blend_func3(uint8_t a , uint8_t b) {
	return (uint8_t) ( (uint16_t) a * b)>>8;
}


static uint8_t blend_func4(uint8_t a, uint8_t b) {
	uint8_t c = 0xff - b;
	if( c == 0 )
		return CLAMP_Y(c);

	return CLAMP_Y( (a*a)/c  );
}

static uint8_t blend_func5(uint8_t a, uint8_t b) {
	uint8_t val;
	uint8_t c = 0xff - b;
	if( c == 0 )
		return CLAMP_Y(b);
	val = (uint8_t)(b * 0xff / c);
	return CLAMP_Y(val);
}

static uint8_t blend_func6(uint8_t a, uint8_t b) {
	int val = a + (b - 0xff);
	return CLAMP_Y(val);
}

static uint8_t blend_func7(uint8_t a, uint8_t b) {
    int val = a + (2 * b) - 255;
    return CLAMP_Y(val);
}

static uint8_t blend_func8(uint8_t a, uint8_t b) {
    int c;

    if (b < 128) {
        c = (a * b) >> 7;
    } else {
        c = 255 - (((255 - b) * (255 - a)) >> 7);
    }

    return CLAMP_Y(c);
}

static blend_func get_blend_func(const int mode) {
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
Originally from:
http://www.cs.utah.edu/~michael/chroma/
*/
void keyselect_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int i_angle = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];
    int mode = args[4];

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const unsigned int len = width * height;

    float aa = 255.0f, bb = 255.0f;
    float angle = (float)i_angle / 100.0f;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    uint8_t *Y2 = frame2->data[0];
    uint8_t *U2 = frame2->data[1];
    uint8_t *V2 = frame2->data[2];

    int iy = pixel_Y_lo_, iu = 128, iv = 128;
    _rgb2yuv(r, g, b, iy, iu, iv);
    aa = (float)iu;
    bb = (float)iv;

    float tmp = sqrtf((aa * aa) + (bb * bb));
    const int cb = 255 * (aa / tmp);
    const int cr = 255 * (bb / tmp);

    blend_func blend_pixel = get_blend_func(mode);

    const int accept_angle_tg = (int)(15.0f * tanf(M_PI * angle / 180.0f));

#pragma omp simd
    for (unsigned int pos = 0; pos < len; pos++) {
        short xx = ((Cb[pos] * cb) + (Cr[pos] * cr)) >> 7;
        short yy = ((Cr[pos] * cb) - (Cb[pos] * cr)) >> 7;

        uint8_t val = (xx * accept_angle_tg) >> 4;

        int abs_yy = (yy ^ (yy >> 15)) - (yy >> 15);

        int mask = -((abs_yy - val) >> 31 ^ 1);

        uint8_t blended_Y = blend_pixel(Y[pos], Y2[pos]);
        Y[pos] = (Y[pos] & ~mask) | (blended_Y & mask);

        uint8_t new_Cb = ((Y2[pos] * (Cb[pos] - U2[pos])) >> 8) + Cb[pos];
        uint8_t new_Cr = ((Y2[pos] * (Cr[pos] - V2[pos])) >> 8) + Cr[pos];
        Cb[pos] = (Cb[pos] & ~mask) | (new_Cb & mask);
        Cr[pos] = (Cr[pos] & ~mask) | (new_Cr & mask);
    }
}

