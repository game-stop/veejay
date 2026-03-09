/*
 * VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
 */


/* Note that the 'opacity' parameter is sometimes used as a 
   threshold value or subtraction value depending on the mode
   of this effect */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "chromamagick.h"

vj_effect *chromamagick_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->defaults[1] = 150;
	ve->parallel = 1;
    ve->description = "Chroma Magic";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 40;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->parallel = 1;
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		"Add Subselect Luma", "Select Min", "Select Max", "Select Difference",
		"Select Difference Negate", "Add Luma", "Select Unfreeze", "Exclusive",
		"Difference Negate", "Additive", "Basecolor", "Freeze", "Unfreeze",
		"Hardlight", "Multiply", "Divide", "Subtract", "Add", "Screen",
		"Difference", "Softlight", "Dodge", "Reflect", "Difference Replace",
		"Darken", "Lighten", "Modulo Add", "Multiply LAB", "Quilt",
	   // process in LAB space:	
		"Dodge LAB", "Additive LAB" , "Divide LAB", "Freeze LAB", "Unfreeze LAB", 
		"Darken LAB", "Lighten LAB", "Softlight LAB", "Hardlight LAB", "Difference LAB",
		"Screen LAB", "Pixel Fuckery"
	);


	return ve;
}

static void chromamagic_selectmin(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (b < a) {
	    Y[i] = b;
	    Cb[i] = Cb2[i];
	    Cr[i] = Cr2[i];
	}
    }
}

static void chromamagic_addsubselectlum(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
    int c, a, b;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (b < a) {
	    c = (a + b) >> 1;
	    Y[i] = c;

	    a = Cb[i];
	    b = Cb2[i];
	    c = (a + b) >> 1;
	    Cb[i] = c;

	    a = Cr[i];
	    b = Cr2[i];
	    c = (a + b) >> 1;
	    Cr[i] = c;
	}
    }
}

static void chromamagic_selectmax(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (b > a) {
	    Y[i] = (3 * b + a)>>2;
	    Cb[i] = Cb2[i];
	    Cr[i] = Cr2[i];
	}
    }
}
static void chromamagic_selectdiff(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int a, b;
    int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (a > b) {
	    Y[i] = abs(Y[i] - Y2[i]);
	    Cb[i] = (Cb[i] + Cb2[i]) >> 1;
	    Cr[i] = (Cr[i] + Cr2[i]) >> 1;
	}
    }
}

static void chromamagic_diffreplace(VJFrame *frame, VJFrame *frame2, int threshold)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    unsigned long sum = 0;
#pragma omp simd reduction(+:sum)
    for(int i = 0; i < len; i++)
        sum += Y[i];

    const int op_b = sum & 0xff;
    const int op_a = 255 - op_b;

#pragma omp simd
    for(int i = 0; i < len; i++)
    {
        int diff = Y[i] - Y2[i];
        int mask = ((diff >= threshold) - (diff < -threshold)) & 0xFF;

        int y  = ((Y[i] * op_a + Y2[i] * op_b) >> 8);
        int cb = ((Cb[i] * op_a + Cb2[i] * op_b) >> 8);
        int cr = ((Cr[i] * op_a + Cr2[i] * op_b) >> 8);

        Y[i]  = (Y[i]  & ~mask) | (CLAMP_Y(y) & mask);
        Cb[i] = (Cb[i] & ~mask) | (CLAMP_UV(cb) & mask);
        Cr[i] = (Cr[i] & ~mask) | (CLAMP_UV(cr) & mask);
    }
}

static void chromamagic_selectdiffneg(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (a > b) {
	    Y[i] = 255 - abs(255 - a - b);
	    Cb[i] = ((Cb[i] * op_a) + (Cb2[i]*op_b) )>>8;
	    Cr[i] = ((Cr[i] * op_a) + (Cr2[i]*op_b) )>>8;
	}
    }
}

static void chromamagic_selectunfreeze(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha     = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int diffY = Y2[i] - Y[i];
        int brighten = (diffY > 0);
        int y_new = Y[i] + ((diffY * alpha * brighten) >> 8);

        if (y_new < pixel_Y_lo_) y_new = pixel_Y_lo_;
        if (y_new > 255) y_new = 255;

        Y[i] = (uint8_t)y_new;

        int diffCb = Cb2[i] - Cb[i];
        int cb_new = Cb[i] + ((diffCb * alpha * brighten) >> 8);
        if (cb_new < 0) cb_new = 0;
        if (cb_new > 255) cb_new = 255;
        Cb[i] = (uint8_t)cb_new;

        int diffCr = Cr2[i] - Cr[i];
        int cr_new = Cr[i] + ((diffCr * alpha * brighten) >> 8);
        if (cr_new < 0) cr_new = 0;
        if (cr_new > 255) cr_new = 255;
        Cr[i] = (uint8_t)cr_new;
    }
}

static void chromamagic_addlum(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	Y[i] = (a * a) / (256- b);
	Cb[i] = (Cb[i] + Cb2[i]) >> 1;
	Cr[i] = (Cr[i] + Cr2[i]) >> 1;
    }
}

static void chromamagic_exclusive(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int a=0, b=0, c=0;
#pragma omp simd
    for (i = 0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
 		c = a + (2 * b) - op_a;
		Y[i] = CLAMP_Y(c - (( a * b ) >> 8 ));
    }
#pragma omp simd    
    for( i = 0; i < len ; i ++ ) {
		a = Cb[i];
		b = Cb2[i];

		c = a + (2 * b) - 0xff;
		Cb[i] = CLAMP_UV(c);

		a = Cr[i];
		b = Cr2[i];
		c = a + (2 * b) - 0xff;
		Cr[i] = CLAMP_UV(c);
   	 }

}

static void chromamagic_diffnegate(VJFrame *frame, VJFrame *frame2, int op_a) {

	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,d;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - o1;
#define MAGIC_THRESHOLD 40
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		d = abs( a - b );
		if ( d > MAGIC_THRESHOLD )
		{
			a = Y[i] * o1;
			b = Cb2[i] * o2;
			Y[i] = 255 - ( (a + b) >>8 );

			a = (Cb[i] - 128) * o1;
			b = (Cb2[i] - 128) * o2;
			d = 128 + ((a + b) >> 8);
			Cb[i] = CLAMP_UV(d);

			a = (Cr[i] - 128) * o1;
			b = (Cr2[i] - 128) * o2;
			d = 128 + ((a + b) >> 8);
			Cr[i] = CLAMP_UV(d);
		}
	}
}

static void chromamagic_additive2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int L1, a1, b1, L2, a2, b2;
    int blended_L, blended_a, blended_b;
#pragma omp simd
    for(i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);
        
        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);
        
        blended_L = L1 + ((op_a * (L2 - L1)) >> 7);
        blended_a = a1 + ((op_a * (a2 - a1)) >> 7);
        blended_b = b1 + ((op_a * (b2 - b1)) >> 7);
        
        Y[i] = (uint8_t)CLAMP_Y((blended_L * 255) >> 8);
        Cb[i] = (uint8_t)CLAMP_UV(((blended_a * 255) >> 8) + 128);
        Cr[i] = (uint8_t)CLAMP_UV(((blended_b * 255) >> 8) + 128);
    }
}

static void chromamagic_additive(VJFrame *frame, VJFrame *frame2, int op_a) {
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++) {
        int y = Y[i] + ((Y2[i] * alpha) >> 8);
        if (y > 255) y = 255;
        Y[i] = (uint8_t)y;

        int cb = (Cb[i] - 128) + (((Cb2[i] - 128) * alpha) >> 8);
        if (cb < -128) cb = -128;
        if (cb > 127) cb = 127;
        Cb[i] = (uint8_t)(cb + 128);

        int cr = (Cr[i] - 128) + (((Cr2[i] - 128) * alpha) >> 8);
        if (cr < -128) cr = -128;
        if (cr > 127) cr = 127;
        Cr[i] = (uint8_t)(cr + 128);
    }
}

static void chromamagic_basecolor(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y_new = (Y[i] * inv_alpha + Y2[i] * alpha) >> 8;
        if (y_new < 0) y_new = 0;
        if (y_new > 255) y_new = 255;
        Y[i] = (uint8_t)y_new;

        int cb = ((Cb[i] - 128) * inv_alpha + (Cb2[i] - 128) * alpha) >> 8;
        if (cb < -128) cb = -128;
        if (cb > 127)  cb = 127;
        Cb[i] = (uint8_t)(cb + 128);

        int cr = ((Cr[i] - 128) * inv_alpha + (Cr2[i] - 128) * alpha) >> 8;
        if (cr < -128) cr = -128;
        if (cr > 127)  cr = 127;
        Cr[i] = (uint8_t)(cr + 128);
    }
}

static void chromamagic_freeze2(VJFrame *frame, VJFrame *frame2, int op_a) {
    const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    unsigned int i;
    int L1, a1, b1, L2, a2, b2;
    int c;

    if (op_a == 0) op_a = 255;

    #pragma omp simd
    for (i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);

        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);

		L2 = L2 | 1;
		a2 = a2 | 1;
		b2 = b2 | 1;

        c = L1 - (((op_a - L1) * (op_a - L1)) / L2);
        c = CLAMP_LAB(c);
        L1 = c;

        c = a1 - (((op_a - a1) * (op_a - a1)) / a2);
        c = CLAMP_LAB(c);
        a1 = c;

        c = b1 - (((op_a - b1) * (op_a - b1)) / b2);
        c = CLAMP_LAB(c);
        b1 = c;

        Y[i] = (uint8_t)((L1 * 255 + 128) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_freeze(VJFrame *frame, VJFrame *frame2, int op_a) {
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];
	unsigned int i;
	int a,b,c;

	if(op_a==0) op_a = 255;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if( b > 0 )
			c = 255 - ((op_a -a ) * (op_a - a)) / b;
		else
			c = 255 - a;

		Y[i] = CLAMP_Y(c);

		a = Cb[i];
		b = Cb2[i];

		if(b > 0)
			c = 255 - ((256-a) * (256 - a)) / b;
		else
			c = 255 - a;

		Cb[i] = CLAMP_UV(c);

		a = Cr[i];
		b = Cr2[i];

		if(b > 0)
			c = 255 - (( 256 - a ) * ( 256 - a )) / b;
		else
			c= 255 -a;
		Cr[i] = CLAMP_UV(c);
	}

}

static void chromamagic_quilt( VJFrame *frame, VJFrame *frame2, int op_a ) {

	unsigned int i;
	const int len = frame->len;
	const int width = frame->width;
	const int height = frame->height;
	int x,y;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];
	float blendFactorX, blendFactorY;
	float alpha = (float) op_a / 255.0f;

#pragma omp simd
	for ( i = 0; i < len ; i++ ) {
	    x = i % width;
        y = i / width;

        blendFactorX = (float)x / width * alpha;
        blendFactorY = (float)y / height * alpha;

        uint8_t y0 = Y[i] * (1 - blendFactorX) + Y2[i] * blendFactorX;
        uint8_t u0 = Cb[i] * (1 - blendFactorY) + Cb2[i] * blendFactorY;
        uint8_t v0 = Cr[i] * (1 - blendFactorX) + Cr2[i] * blendFactorX;

        Y[i] = y0;
        Cb[i] = u0;
        Cr[i] = v0;	
	}

}

static void chromamagic_pixelfuckery( VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int op_a_minus_b, a_minus_b;

#pragma omp simd
    for (i = 0; i < len; i++) {
        op_a_minus_b = op_a - Y2[i];
        a_minus_b = Y[i] - Y2[i];

        if (a_minus_b != 0) {
            Y[i] = 255 - ((op_a_minus_b * op_a_minus_b * 256) / a_minus_b);
        }

        op_a_minus_b = 256 - Cb2[i];
        a_minus_b = Cb[i] - Cb2[i];

        if (a_minus_b != 0) {
            Cb[i] = 255 - ((op_a_minus_b * op_a_minus_b * 256) / a_minus_b);
        }

        op_a_minus_b = 256 - Cr2[i];
        a_minus_b = Cr[i] - Cr2[i];

        if (a_minus_b != 0) {
            Cr[i] = 255 - ((op_a_minus_b * op_a_minus_b * 256) / a_minus_b);
        }
    }

}

static void chromamagic_unfreeze2(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const unsigned int alpha     = op_a;
    const unsigned int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int L1 = (Y[i] * 100) >> 8;
        int a1 = ((Cb[i] - 128) * 127) >> 8;
        int b1 = ((Cr[i] - 128) * 127) >> 8;

        int L2 = (Y2[i] * 100) >> 8;

        int diff_L = L1 - L2;

        diff_L = (diff_L * alpha) / 255;

        int safe_L2 = L2 ? L2 : 1;
        if (L1 > 0 && L2 >= pixel_Y_lo_) {
            L1 = 255 - ((diff_L * diff_L) / safe_L2);
        }
		if (L1 < 0) L1 = 0;
        if (L1 > 100) L1 = 100;

        Cb[i] = (uint8_t)((Cb[i] * inv_alpha + Cb2[i] * alpha) >> 8);
        Cr[i] = (uint8_t)((Cr[i] * inv_alpha + Cr2[i] * alpha) >> 8);
        Y[i]  = (uint8_t)((L1 * 255 + 50) / 100);
    }
}


static void chromamagic_unfreeze( VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b;

#pragma omp simd
	for(i=0; i < len; i++) {
        a = Y[i];
        b = Y2[i];
        if (a > pixel_Y_lo_ && a != 0) {
            int diff = op_a - b;
            Y[i] = CLAMP_Y(255 - ((diff * diff) / a));
        }

        a = Cb[i];
        b = Cb2[i];

        if (a > pixel_U_lo_ && a != 0) {
            int diff = 256 - b;
            Cb[i] = CLAMP_UV(255 - ((diff * diff) / a));
        }

        a = Cr[i];
        b = Cr2[i];

        if (a > pixel_U_lo_ && a != 0) {
            int diff = 256 - b;
            Cr[i] = CLAMP_UV(255 - ((diff * diff) / a));
        }
	} 
}

static void chromamagic_hardlight2(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int L1 = (Y[i] * 100) >> 8;
        int a1 = ((Cb[i] - 128) * 127) >> 8;
        int b1 = ((Cr[i] - 128) * 127) >> 8;

        int L2 = (Y2[i] * 100) >> 8;
        int a2 = ((Cb2[i] - 128) * 127) >> 8;
        int b2 = ((Cr2[i] - 128) * 127) >> 8;

        int HL_L = (L2 < 50) ? (2 * L1 * L2 / 100)
                             : (100 - 2 * (100 - L1) * (100 - L2) / 100);

        int a1n = a1 + 127;
        int a2n = a2 + 127;
        int HL_a = (a2n < 127) ? (a1n * a2n * 2 / 254)
                               : (254 - 2 * (254 - a1n) * (254 - a2n) / 254);
        HL_a -= 127;

        int b1n = b1 + 127;
        int b2n = b2 + 127;
        int HL_b = (b2n < 127) ? (b1n * b2n * 2 / 254)
                               : (254 - 2 * (254 - b1n) * (254 - b2n) / 254);
        HL_b -= 127;

        L1 = (HL_L * alpha + L1 * inv_alpha) >> 8;
        a1 = (HL_a * alpha + a1 * inv_alpha) >> 8;
        b1 = (HL_b * alpha + b1 * inv_alpha) >> 8;

        L1 = L1 < 0 ? 0 : (L1 > 100 ? 100 : L1);
        a1 = a1 < -127 ? -127 : (a1 > 127 ? 127 : a1);
        b1 = b1 < -127 ? -127 : (b1 > 127 ? 127 : b1);

        Y[i]  = (uint8_t)((L1 * 255 + 50) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_hardlight( VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,c;

#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if ( b < 128 ) {
			c = ( a * b ) >> 8;
		}
		else {
			c = 255 - (( op_a - b) * ( op_a - a ) >> 8);
		}
		Y[i] =CLAMP_Y( c);

		a = Cb[i]-128;
		b = Cb2[i]-128;
		if ( b < 128 ) c = ( a * b ) >> 8;
		else c = 255 - (( 256 - b) * ( 256 - a) >> 8);
		c += 128;
		Cb[i] = CLAMP_UV(c);

		a = Cr[i]-128;
		b = Cr2[i]-128;
		if ( b < 128) c = ( a * b ) >> 8;
		else c = 255 - (( 256 - b) * ( 256 - a) >> 8 );
		c += 128;
		Cr[i] = CLAMP_UV(c);

	}
}

static void chromamagic_multiply2(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha     = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int L1 = (Y[i] * 100) >> 8;
        int a1 = ((Cb[i] - 128) * 127) >> 8;
        int b1 = ((Cr[i] - 128) * 127) >> 8;

        int L2 = (Y2[i] * 100) >> 8;
        int a2 = ((Cb2[i] - 128) * 127) >> 8;
        int b2 = ((Cr2[i] - 128) * 127) >> 8;

        int HL_L = (L1 * L2) / 100;
        int a1n = a1 + 127;
        int a2n = a2 + 127;
        int b1n = b1 + 127;
        int b2n = b2 + 127;

        int HL_a = (a1n * a2n) / 127;
        int HL_b = (b1n * b2n) / 127;

        HL_a -= 127;
        HL_b -= 127;

        L1 = (HL_L * alpha + L1 * inv_alpha) >> 8;
        a1 = (HL_a * alpha + a1 * inv_alpha) >> 8;
        b1 = (HL_b * alpha + b1 * inv_alpha) >> 8;

        L1 = L1 < 0 ? 0 : (L1 > 100 ? 100 : L1);
        a1 = a1 < -127 ? -127 : (a1 > 127 ? 127 : a1);
        b1 = b1 < -127 ? -127 : (b1 > 127 ? 127 : b1);

        Y[i]  = (uint8_t)((L1 * 255 + 50) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_multiply( VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,c;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
#pragma omp simd
	for( i=0; i < len; i++) {
		a = (Y[i] * o1) >> 8;
		b = (Y2[i] * o2) >> 8;
		Y[i] = (a * b) >> 8;

		a = Cb[i]-128;
		b = Cb2[i]-128;
		c = ( a * b ) >> 8;
		c += 128;
		Cb[i] = CLAMP_UV(c);

		a = Cr[i] - 128;
		b = Cr2[i] - 128;
		c = ( a * b ) >> 8;
		c += 128;
		Cr[i] = CLAMP_UV(c);

	}
}

static void chromamagic_divide2(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const unsigned int alpha     = op_a;
    const unsigned int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int L1 = (Y[i] * 100) >> 8;
        int L2 = (Y2[i] * 100) >> 8;

        int safe_L2 = L2 ? L2 : 1;
        int div_L = (L1 * 256) / safe_L2;

        int blended_L = (div_L * alpha + L1 * inv_alpha) >> 8;
        if (blended_L < 0) blended_L = 0;
        if (blended_L > 100) blended_L = 100;

        Y[i] = (uint8_t)((blended_L * 255 + 50) / 100);
        Cb[i] = (uint8_t)((Cb[i] * inv_alpha + Cb2[i] * alpha) >> 8);
        Cr[i] = (uint8_t)((Cr[i] * inv_alpha + Cr2[i] * alpha) >> 8);
    }
}
static void chromamagic_divide(VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b;
	const unsigned int o1 = op_a;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i] * Y[i];
		b = o1 - Y2[i];
		if ( b > pixel_Y_lo_ )
			Y[i] = CLAMP_Y(a / b);
	
		a = Cb[i] * Cb2[i];
		b = 255 - Cb2[i];
		if( b > pixel_U_lo_ )
			Cb[i] = CLAMP_UV( a / b );

		a = Cr[i] * Cr[i];;
		b = 255 - Cr2[i];
		if( b > pixel_U_lo_ )
			Cr[i] = CLAMP_UV( a / b );
	}
}

static void chromamagic_subtract(VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a ,b;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
#pragma omp simd
	for (i = 0; i < len; i++) {
        a = Y[i] - ((Y2[i] * o1) >> 8);
        Y[i] = CLAMP_Y(a);

        a = Cb[i];
        b = Cb2[i];
        Cb[i] = CLAMP_UV(((a * o2 + b * o1) >> 8));

        a = Cr[i];
        b = Cr2[i];
        Cr[i] = CLAMP_UV(((a * o2 + b * o1) >> 8));
    }

}

static void chromamagic_add(VJFrame *frame, VJFrame *frame2, int op_a) {

	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,c;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		c = a + (( 2 * b ) - op_a);
		Y[i] = CLAMP_Y(c);

		a = Cb[i]-128;
		b = Cb2[i]-128;
		c = a + ( 2 * b );
		c += 128;
		Cb[i] = CLAMP_UV(c);

		a = Cr[i]-128;
		b = Cr2[i]-128;
		c = a + ( 2 * b );
		c += 128;	
		Cr[i] = CLAMP_UV(c);
	}
}

static void chromamagic_screen2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    for(i = 0; i < len; i++) {
        int inverted_Y = 255 - ((255 - Y[i]) * 255) / 255;
        int a1 = ((Cb[i] - 128) * 127) / 256;
        int b1 = ((Cr[i] - 128) * 127) / 256;
        
        int inverted_Y2 = 255 - ((255 - Y2[i]) * 255) / 255;
        int a2 = ((Cb2[i] - 128) * 127) / 256;
        int b2 = ((Cr2[i] - 128) * 127) / 256;
        
        int blended_Y = (inverted_Y * (256 - op_a) + inverted_Y2 * op_a) / 256;
        int blended_a = (a1 * (256 - op_a) + a2 * op_a) / 256;
        int blended_b = (b1 * (256 - op_a) + b2 * op_a) / 256;

        Y[i] = (uint8_t)((255 - blended_Y) + 0.5);
        Cb[i] = (uint8_t)(blended_a + 128);
        Cr[i] = (uint8_t)(blended_b + 128);
    }
}

static void chromamagic_screen(VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,c;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		Y[i] = 255 - ( (op_a-a) * (op_a-b) >> 8);
		a = Cb[i]-128;
		b = Cb2[i]-128;
		c = 255 - ( ( 256-a) * (256 - b) >> 8);
		c += 128;
		Cb[i] = CLAMP_UV(c);
		a = Cr[i]-128;
		b = Cr2[i]-128;
		c = 255 - ( ( 256 -a) * (256 - b)>>8);
		c += 128;
		Cr[i] = CLAMP_UV(c);
	}
}

static void chromamagic_difference2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const unsigned int alpha     = op_a;
    const unsigned int inv_alpha = 255 - op_a;

    int L1, a1, b1, L2, a2, b2;
    int L_diff, a_diff, b_diff;

#pragma omp simd
    for (i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = ((Cb[i] - 128) * 127) >> 8;
        b1 = ((Cr[i] - 128) * 127) >> 8;

        L2 = (Y2[i] * 100) >> 8;
        a2 = ((Cb2[i] - 128) * 127) >> 8;
        b2 = ((Cr2[i] - 128) * 127) >> 8;

        L_diff = (L1 > L2) ? (L1 - L2) : (L2 - L1);
        a_diff = (a1 > a2) ? (a1 - a2) : (a2 - a1);
        b_diff = (b1 > b2) ? (b1 - b2) : (b2 - b1);

        L1 = (L_diff * alpha) >> 8;
        a1 = ((a_diff * alpha) >> 8);
        b1 = ((b_diff * alpha) >> 8);

        Y[i]  = (uint8_t)((L1 * 255 + 50) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_difference(VJFrame *frame, VJFrame *frame2, int op_a) {
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const unsigned int alpha = op_a;
    const unsigned int inv_alpha = 255 - op_a;

#pragma omp simd
    for(int i = 0; i < len; i++) {
        int diffY = Y[i] - Y2[i];
        int maskY = diffY & ~(diffY >> 31);
        Y[i] = (maskY * alpha) >> 8;
        
        int diffCb = (Cb[i] - 128) - (Cb2[i] - 128);
        int maskCb = diffCb & ~(diffCb >> 31);
        int valCb = ((maskCb * alpha) >> 8) + 128;
        Cb[i] = CLAMP_UV(valCb);

        int diffCr = (Cr[i] - 128) - (Cr2[i] - 128);
        int maskCr = diffCr & ~(diffCr >> 31);
        int valCr = ((maskCr * alpha) >> 8) + 128;
        Cr[i] = CLAMP_UV(valCr);
    }
}

static void chromamagic_softlightmode2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

    int L1, a1, b1, L2, a2, b2;
    int blended_L, blended_a, blended_b;
    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    #pragma omp simd
    for (i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);

        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);

        if (L1 < 128) {
            blended_L = L1 * L2 / 128;
        } else {
            blended_L = 255 - ((255 - L1) * (255 - L2) / 128);
        }

        blended_a = a1 + ((a1 * (2 * a2 - 255) * (255 - L2)) >> 16);
        blended_b = b1 + ((b1 * (2 * b2 - 255) * (255 - L2)) >> 16);

        blended_L = CLAMP_LAB(blended_L);
        blended_a = CLAMP_LAB(blended_a);
        blended_b = CLAMP_LAB(blended_b);

        blended_L = (blended_L * o1 + L2 * o2) >> 8;
        blended_a = (blended_a * o1 + a2 * o2) >> 8;
        blended_b = (blended_b * o1 + b2 * o2) >> 8;

        Y[i] = (uint8_t)((blended_L * 255 + 128) / 100);
        Cb[i] = (uint8_t)(blended_a + 128);
        Cr[i] = (uint8_t)(blended_b + 128);
    }
}


/* not really softlight but still cool */
static void chromamagic_softlightmode(VJFrame *frame,VJFrame *frame2, int op_a) {

	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,c,d;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if ( a < op_a ) {
		c = (a * b) >> 8;
		Y[i] = (c + a * ( 255 - ( (255-a)*(255-b) >> 8) - c)) >> 8;
		
		a = abs(Cb[i]-128);
		b = abs(Cb2[i]-128);
		c = (a * b);
		d = (c + a * ( 255 - ( (a * b) >> 7) - c)) >> 7;
		d += 128;
		Cb[i] = CLAMP_UV(d);

		a = abs(Cr[i]-128);
		b = abs(Cr2[i]-128);
		c = (a * b) >> 7;
		d = (c + a * ( 255 - ( (a * b) >> 7) -c)) >> 7;
		d += 128;
		Cr[i] = CLAMP_UV(d);
		}
	}
}

static void chromamagic_dodge2(VJFrame *restrict frame, VJFrame *restrict frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    #pragma omp simd
    for (int i = 0; i < len; i++)
    {
        const int y1 = Y[i];
        const int mask = -(y1 < op_a);

        const int y2 = Y2[i];
        const int cb1 = Cb[i] - 128;
        const int cb2 = Cb2[i] - 128;
        const int cr1 = Cr[i] - 128;
        const int cr2 = Cr2[i] - 128;

        const uint8_t res_y  = (uint8_t)CLAMP_Y(y1 + y2);
        const uint8_t res_cb = (uint8_t)CLAMP_UV(128 + cb1 + cb2);
        const uint8_t res_cr = (uint8_t)CLAMP_UV(128 + cr1 + cr2);

        Y[i]  = (mask & res_y)  | (~mask & Y[i]);
        Cb[i] = (mask & res_cb) | (~mask & Cb[i]);
        Cr[i] = (mask & res_cr) | (~mask & Cr[i]);
    }
}

static void chromamagic_dodge(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int a = Y[i];
        int b = Y2[i];
        int denomY = 255 - b;
        denomY = denomY < 1 ? 1 : denomY;
        int y_dodge = a * 255 / denomY;
        int y_new = (a * (255 - alpha) + y_dodge * alpha) >> 8;

        Y[i] = (uint8_t)((y_new < 0) ? 0 : (y_new > 255 ? 255 : y_new));

        int cb = Cb[i] - 128;
        int cb2 = Cb2[i] - 128;
        int denomCb = 127 - cb2;
        denomCb = denomCb < 1 ? 1 : denomCb;

        int cb_dodge = cb * 127 / denomCb;
        int cb_new = (cb * (255 - alpha) + cb_dodge * alpha) >> 8;
        cb_new = (cb_new < -128) ? -128 : (cb_new > 127 ? 127 : cb_new);
        Cb[i] = (uint8_t)(cb_new + 128);

		int cr = Cr[i] - 128;
        int cr2 = Cr2[i] - 128;
        int denomCr = 127 - cr2;
        denomCr = denomCr < 1 ? 1 : denomCr;

        int cr_dodge = cr * 127 / denomCr;
        int cr_new = (cr * (255 - alpha) + cr_dodge * alpha) >> 8;
        cr_new = (cr_new < -128) ? -128 : (cr_new > 127 ? 127 : cr_new);
        Cr[i] = (uint8_t)(cr_new + 128);
    }
}

static void chromamagic_darken2(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha     = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int L1 = (Y[i] * 100) >> 8;
        int a1 = ((Cb[i] - 128) * 127) >> 8;
        int b1 = ((Cr[i] - 128) * 127) >> 8;

        int L2 = (Y2[i] * 100) >> 8;
        int a2 = ((Cb2[i] - 128) * 127) >> 8;
        int b2 = ((Cr2[i] - 128) * 127) >> 8;
		int dark_L = (L2 < L1) ? L2 : L1;

        L1 = (dark_L * alpha + L1 * inv_alpha) >> 8;
        a1 = (a2 * alpha + a1 * inv_alpha) >> 8;
        b1 = (b2 * alpha + b1 * inv_alpha) >> 8;

        L1 = L1 < 0 ? 0 : (L1 > 100 ? 100 : L1);
        a1 = a1 < -127 ? -127 : (a1 > 127 ? 127 : a1);
        b1 = b1 < -127 ? -127 : (b1 > 127 ? 127 : b1);

        Y[i]  = (uint8_t)((L1 * 255 + 50) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_darken(VJFrame *frame, VJFrame *frame2, int op_a)
{

	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
#pragma omp simd
	for(i=0; i < len; i++)
	{
		if(Y[i] > Y2[i])
		{
			Y[i] = ((Y[i] * o1) + (Y2[i] * o2)) >> 8; 
			Cb[i] = ((Cb[i] * o1) + (Cb2[i] * o2)) >> 8;
			Cr[i] = ((Cr[i] * o1) + (Cr2[i] * o2)) >> 8;
		}
	}
}

static void chromamagic_lighten2(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int L1 = (Y[i] * 100) >> 8;
        int a1 = ((Cb[i] - 128) * 127) >> 8;
        int b1 = ((Cr[i] - 128) * 127) >> 8;

        int L2 = (Y2[i] * 100) >> 8;
        int a2 = ((Cb2[i] - 128) * 127) >> 8;
        int b2 = ((Cr2[i] - 128) * 127) >> 8;

        int deltaL = L2 - L1;
        int brighten = (deltaL > 0) ? deltaL : 0;

        L1 = L1 + ((brighten * alpha) >> 8);

        a1 = (a1 * inv_alpha + a2 * alpha) >> 8;
        b1 = (b1 * inv_alpha + b2 * alpha) >> 8;

        L1 = L1 < 0 ? 0 : (L1 > 100 ? 100 : L1);
        a1 = a1 < -127 ? -127 : (a1 > 127 ? 127 : a1);
        b1 = b1 < -127 ? -127 : (b1 > 127 ? 127 : b1);

        Y[i]  = (uint8_t)((L1 * 255 + 50) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_lighten(VJFrame *frame, VJFrame *frame2, int op_a)
{

	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
#pragma omp simd
	for(i=0; i < len; i++)
	{
		if(Y[i] < Y2[i])
		{
			Y[i] = ((Y[i] * o1) + (Y2[i] * o2)) >> 8; 
			Cb[i] = ((Cb[i] * o1) + (Cb2[i] * o2)) >> 8;
			Cr[i] = ((Cr[i] * o1) + (Cr2[i] * o2)) >> 8;
		} 
	}
}

static void chromamagic_reflect(VJFrame *frame, VJFrame *frame2, int op_a) {

	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb = frame->data[1];
	uint8_t *restrict Cr = frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];

	int a,b,c;
#pragma omp simd
	for(i=0; i < len ; i++) {
		a = Y[i];
		b = Y2[i];

		if ( b > op_a ) c = b;
		else {
			Y[i] = CLAMP_Y((a * a) / (256 - b));

			a = Cb[i] - 128;
			b = Cb2[i] - 128;
			if (b == 128) b = 127;
			c = CLAMP_UV((a * a) / (128 - b) + 128);
			Cb[i] = c;

			a = Cr[i] - 128;
			b = Cr2[i] - 128;
			if (b == 128) b = 127;
			c = CLAMP_UV((a * a) / (128 - b) + 128);
			Cr[i] = c;
		}
	}
}

static void chromamagic_modadd(VJFrame *frame, VJFrame *frame2, int op_a)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int inv_alpha = 255 - op_a;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y = (Y[i] * alpha + Y2[i] * inv_alpha) >> 8;
        Y[i] = CLAMP_Y(y);

        int cb = ((Cb[i] - 128) * alpha + (Cb2[i] - 128) * inv_alpha) >> 8;
        Cb[i] = (uint8_t)(cb + 128);

        int cr = ((Cr[i] - 128) * alpha + (Cr2[i] - 128) * inv_alpha) >> 8;
        Cr[i] = (uint8_t)(cr + 128);
    }
}

void chromamagick_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int type = args[0];
    int op_a = args[1];

    switch (type) {
    case 0:
	chromamagic_addsubselectlum(frame, frame2, op_a);
	break;
    case 1:
	chromamagic_selectmin(frame, frame2, op_a);
	break;
    case 2:
	chromamagic_selectmax(frame, frame2, op_a);
	break;
    case 3:
	chromamagic_selectdiff(frame, frame2, op_a);
	break;
    case 4:
	chromamagic_selectdiffneg(frame, frame2, op_a);
	break;
    case 5:
	chromamagic_addlum(frame, frame2, op_a);
	break;
    case 6:
	chromamagic_selectunfreeze(frame, frame2, op_a);
	break;
    case 7:
	chromamagic_exclusive(frame,frame2, op_a);
	break;
   case 8:
	chromamagic_diffnegate(frame,frame2, op_a);
	break;
   case 9:
	chromamagic_additive( frame,frame2, op_a);
	break;
   case 10:
	chromamagic_basecolor(frame,frame2, op_a);
	break;
   case 11:
	chromamagic_freeze(frame,frame2, op_a);
	break;
   case 12:
	chromamagic_unfreeze(frame,frame2, op_a);
	break;
   case 13:
	chromamagic_hardlight(frame,frame2, op_a);
	break;
   case 14:
	chromamagic_multiply(frame,frame2, op_a);
	break;
  case 15:
	chromamagic_divide(frame,frame2, op_a);
	break;
  case 16:
	chromamagic_subtract(frame,frame2, op_a);
	break;
  case 17:
	chromamagic_add(frame,frame2, op_a);
	break;
  case 18:
	chromamagic_screen(frame,frame2, op_a);
	break;
  case 19:
	chromamagic_difference(frame,frame2, op_a);
	break;
  case 20:
	chromamagic_softlightmode(frame,frame2, op_a);
	break;
  case 21:
	chromamagic_dodge(frame,frame2, op_a);
	break;
  case 22:
	chromamagic_reflect(frame,frame2, op_a);
	break;
  case 23:
	chromamagic_diffreplace(frame,frame2, op_a);
	break;
  case 24:
	chromamagic_darken( frame,frame2, op_a);
	break;
  case 25:
	chromamagic_lighten( frame,frame2, op_a);
	break;
  case 26:
	chromamagic_modadd( frame,frame2, op_a);
	break;
  case 28:
	chromamagic_quilt(frame,frame2,op_a);
	break;
  case 29:
	chromamagic_dodge2(frame,frame2,op_a);
	break;
  case 30:
	chromamagic_additive2(frame,frame2,op_a);
	break;
  case 31:
	chromamagic_divide2(frame,frame2,op_a);
	break;
  case 32:
	chromamagic_freeze2(frame,frame2,op_a);
	break;
  case 33:
	chromamagic_unfreeze2(frame,frame2,op_a);
	break;
  case 34:
	chromamagic_darken2(frame,frame2,op_a);
	break;
  case 35:
	chromamagic_lighten2(frame,frame2,op_a);
	break;
  case 36:
	chromamagic_softlightmode2(frame,frame2,op_a);
	break;
  case 37:
	chromamagic_hardlight2(frame,frame2,op_a);
	break;
  case 38:
	chromamagic_difference2(frame,frame2,op_a);
	break;
  case 39:
    chromamagic_screen2(frame,frame2,op_a);
	break;
  case 27:
	chromamagic_multiply2(frame,frame2,op_a);
	break;
  case 40:
	chromamagic_pixelfuckery( frame, frame2, op_a );
	break;	
	}
}


