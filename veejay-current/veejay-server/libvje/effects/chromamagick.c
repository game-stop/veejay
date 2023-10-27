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
    ve->limits[1][0] = 38;
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
		"Darken", "Lighten", "Modulo Add", "Pixel Fuckery", "Quilt", 
		"Dodge LAB", "Additive LAB" , "Divide LAB", "Freeze LAB", "Unfreeze LAB", "Darken LAB", "Lighten LAB", "Softlight LAB", "Hardlight LAB", "Difference LAB"
	);


	return ve;
}

static void chromamagic_selectmin(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	unsigned int i ;
	int op_b ;
	int op_a ;
	int a,b;
	unsigned long sum = 0;
#pragma omp simd
	for(i=0; i < len; i++)
	{
		sum += Y[i];
	}
	op_b = (sum & 0xff);
	op_a = 255 - op_b;
#pragma omp simd
	for(i=0; i < len; i++)
	{
		if( abs(Y[i] - Y2[i]) >= threshold )
		{		
			a = ( Y[i] * op_a );
			b = ( Y2[i] * op_b );
			Y[i] = CLAMP_Y(a + b) >> 8;

			a = ( Cb[i] * op_a ) >> 8;
			b = ( Cb2[i] * op_b ) >> 8;
			Cb[i] = CLAMP_UV(a + b);
			
			a = ( Cr[i] * op_a ) >> 8;
			b = ( Cr2[i] * op_b ) >> 8;
			Cr[i] = CLAMP_UV(a + b);
		}
	}
}

static void chromamagic_selectdiffneg(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (a > b) {
	    if (a > pixel_Y_lo_)
		Y[i] = 255 - ((256 - b) * (256 - b)) / a;
	    Cb[i] = (Cb[i] + Cb2[i]) >> 1;
	    Cr[i] = (Cr[i] + Cr2[i]) >> 1;
	}
    }
}

static void chromamagic_addlum(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

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

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b;	
	const unsigned int o1 = op_a;
 	const unsigned int o2 = 255 - op_a;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = (Y[i]*o1) >> 7;
		b = (Y2[i]*o2) >> 7;
		Y[i] = a + (( 2 * b ) - 255) & 0xff;

		a = Cb[i] - 128;
		b = Cb2[i] - 128;

		Cb[i] = (( a + ( ( 2 * b ) - 255 )) + 128 ) & 0xff;

		a = Cr[i] - 128;
		b = Cr2[i] - 128;

		Cr[i] = ((a + ( ( 2 * b ) - 255 )) + 128 ) & 0xff;
	}

}

static void chromamagic_basecolor(VJFrame *frame, VJFrame *frame2, int op_a)
{
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int a, b, c, d;
    const unsigned int o1 = op_a;
#pragma omp simd
    for (i = 0; i < len; i++) {
	a = o1 - Y[i];
	b = o1 - Y2[i];
	c = a * b >> 8;
	Y[i] = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);	//8
	

	a = Cb[i]-128;
	b = Cb2[i]-128;
	c = a * b >> 8;
	d = c + a * ((255 - (((255-a) * (255-b)) >> 8) -c) >> 8);
	d += 128;
	Cb[i] = CLAMP_UV(d);

	a = Cr[i]-128;
	b = Cr2[i]-128;
	c = a * b >> 8;
	d = c + a * ((255 - (((255-a) * (255-b)) >> 8) -c ) >> 8);
	d += 128;
	Cr[i] = CLAMP_UV(d);

    }
}

static void chromamagic_freeze2(VJFrame *frame, VJFrame *frame2, int op_a) {
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    unsigned int i;
    int L1, a1, b1, L2, a2, b2;
    int c;

    if (op_a == 0) op_a = 255;

    #pragma omp simd
    for (i = 0; i < len; i++) {
        // Convert YUV to LAB for frame 1
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);

        // Convert YUV to LAB for frame 2
        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);

		L2 = L2 | 1;
		a2 = a2 | 1;
		b2 = b2 | 1;

        // Apply freeze blending operation on L channel
        c = L1 - (((op_a - L1) * (op_a - L1)) / L2);
        c = CLAMP_LAB(c);
        L1 = c;

        // Apply freeze blending operation on a channel
        c = a1 - (((op_a - a1) * (op_a - a1)) / a2);
        c = CLAMP_LAB(c);
        a1 = c;

        // Apply freeze blending operation on b channel
        c = b1 - (((op_a - b1) * (op_a - b1)) / b2);
        c = CLAMP_LAB(c);
        b1 = c;

        // Convert LAB back to YUV for frame 1
        Y[i] = (uint8_t)((L1 * 255 + 128) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}

static void chromamagic_freeze(VJFrame *frame, VJFrame *frame2, int op_a) {
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	float blendFactorX, blendFactorY;
	float alpha = (float) op_a / 255.0f;

#pragma omp simd
	for ( i = 0; i < len ; i++ ) {
	    x = i % width;
        y = i / width;

        blendFactorX = (float)x / width * alpha;
        blendFactorY = (float)y / height * alpha;

        uint8_t y = Y[i] * (1 - blendFactorX) + Y2[i] * blendFactorX;
        uint8_t u = Cb[i] * (1 - blendFactorY) + Cb2[i] * blendFactorY;
        uint8_t v = Cr[i] * (1 - blendFactorX) + Cr2[i] * blendFactorX;

        Y[i] = y;
        Cb[i] = u;
        Cr[i] = v;	
	}

}

static void chromamagic_pixelfuckery( VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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

static void chromamagic_unfreeze2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    int L1, a1, b1, L2, a2, b2;
    int diff_L, diff_a, diff_b;

    #pragma omp simd
    for(i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);

        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);

        diff_L = L1 - L2;
        diff_a = a1 - a2;
        diff_b = b1 - b2;

		L2 = L2 | 1;
		a2 = a2 | 1;
		b2 = b2 | 1;

        diff_L = (diff_L * op_a) / 255;
        diff_a = (diff_a * op_a) / 255;
        diff_b = (diff_b * op_a) / 255;

        if (L1 > 0 && L2 >= pixel_Y_lo_) {
            L1 = 255 - ((diff_L * diff_L) / L2);
        }

        if (a1 > 0 && a2 >= pixel_U_lo_) {
            a1 = 255 - ((diff_a * diff_a) / a2);
        }

        if (b1 > 0 && b2 >= pixel_U_lo_) {
            b1 = 255 - ((diff_b * diff_b) / b2);
        }

	    L1 = CLAMP_LAB(L1);
        a1 = CLAMP_LAB(a1);
        b1 = CLAMP_LAB(b1);

        Y[i] = (uint8_t)((L1 * 255 + 128) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
    }
}


static void chromamagic_unfreeze( VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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

static void chromamagic_hardlight2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

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

        blended_L = (L2 < 128) ? ((2 * L1 * L2) >> 7) : (255 - ((2 * (255 - L1) * (255 - L2)) >> 7));
        blended_a = (a2 < 128) ? ((2 * a1 * a2) >> 7) : (255 - ((2 * (255 - a1) * (255 - a2)) >> 7));
        blended_b = (b2 < 128) ? ((2 * b1 * b2) >> 7) : (255 - ((2 * (255 - b1) * (255 - b2)) >> 7));

        blended_L = (blended_L * o1 + L2 * o2) >> 8;
        blended_a = (blended_a * o1 + a2 * o2) >> 8;
        blended_b = (blended_b * o1 + b2 * o2) >> 8;

        blended_L = CLAMP_LAB(blended_L);
        blended_a = CLAMP_LAB(blended_a);
        blended_b = CLAMP_LAB(blended_b);

        Y[i] = (uint8_t)((blended_L * 255 + 128) / 100);
        Cb[i] = (uint8_t)(blended_a + 128);
        Cr[i] = (uint8_t)(blended_b + 128);
    }
}


static void chromamagic_hardlight( VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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



static void chromamagic_multiply( VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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


static void chromamagic_divide2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    int L1, a1, b1, L2, a2, b2;
    int blended_L, blended_a, blended_b;
	
	int alpha = (op_a * 255) / 100;

    #pragma omp simd
    for(i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);

        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);

		L2 = L2 | 1;
        a2 = a2 | 1;
        b2 = b2 | 1;

        blended_L = ((L1 * 255) / L2) * alpha / 255;
        blended_a = ((a1 * 255) / a2) * alpha / 255;
        blended_b = ((b1 * 255) / b2) * alpha / 255;

        Y[i] = (uint8_t)CLAMP_Y((blended_L * 255 + 128) >> 8);
        Cb[i] = (uint8_t)CLAMP_UV(((blended_a * 255 + 32768) >> 16) + 128);
        Cr[i] = (uint8_t)CLAMP_UV(((blended_b * 255 + 32768) >> 16) + 128);
    }
}


static void chromamagic_divide(VJFrame *frame, VJFrame *frame2, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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

static void chromamagic_screen(VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    int L1, a1, b1, L2, a2, b2;
    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - op_a;

    int L_diff, a_diff, b_diff;
    #pragma omp simd
    for (i = 0; i < len; i++) {
        L1 = (Y[i] * 100) >> 8;
        a1 = (((Cb[i] - 128) * 127) >> 8);
        b1 = (((Cr[i] - 128) * 127) >> 8);

        L2 = (Y2[i] * 100) >> 8;
        a2 = (((Cb2[i] - 128) * 127) >> 8);
        b2 = (((Cr2[i] - 128) * 127) >> 8);

        L_diff = abs(L1 - L2);
        a_diff = abs(a1 - a2);
        b_diff = abs(b1 - b2);

        L1 = ((L_diff * o1 + L2 * o2) >> 7);
        a1 = ((a_diff * o1 + 128) >> 7);
        b1 = ((b_diff * o1 + 128) >> 7);

        Y[i] = (uint8_t)((L1 * 255 + 50) / 100);
        Cb[i] = (uint8_t)(a1 + 128);
        Cr[i] = (uint8_t)(b1 + 128);
  
	}
	
}


static void chromamagic_difference(VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
	int a,b,c;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = (Y[i] * o1)>>7;
		b = (Y2[i] * o2)>>7;
		Y[i] = abs ( a - b );

		a = (Cb[i]-128);
		b = (Cb2[i]-128);
		c = abs ( a - b );
		c += 128;
		Cb[i] = CLAMP_UV(c);

		a = (Cr[i]-128);
		b = (Cr2[i]-128);
		c = abs( a - b );
		c += 128;
		Cr[i] = CLAMP_UV(c);
	}
}

static void chromamagic_softlightmode2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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

static void chromamagic_dodge2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *L = frame->data[0];
    uint8_t *a = frame->data[1];
    uint8_t *b = frame->data[2];
    uint8_t *L2 = frame2->data[0];
    uint8_t *a2 = frame2->data[1];
    uint8_t *b2 = frame2->data[2];

    int L1, a1, b1, L2_val, a2_val, b2_val, L_result, a_result, b_result;
#pragma omp simd
    for(i = 0; i < len; i++) {
        L1 = L[i];
        a1 = a[i];
        b1 = b[i];

        L2_val = L2[i];
        a2_val = a2[i];
        b2_val = b2[i];

        if(L1 < op_a) {
            L_result = (L1 * 100) >> 8;
            a_result = (((a1 - 128) * 127) >> 8);
            b_result = (((b1 - 128) * 127) >> 8);
            
            L_result += ((L2_val * 100) >> 8);
            a_result += (((a2_val - 128) * 127) >> 8);
            b_result += (((b2_val - 128) * 127) >> 8);
            
            L[i] = CLAMP_Y(L_result);
            a[i] = CLAMP_UV(128 + a_result);
            b[i] = CLAMP_UV(128 + b_result);
        }
    }
}

static void chromamagic_dodge(VJFrame *frame, VJFrame *frame2, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;
#pragma omp simd
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if( a >= op_a) c = a;
		else {
			Y[i] = (a << 8) / ( 256 - b );
            a = Cb[i] - 128;
            b = Cb2[i] - 128;
            c = (a << 7) / (128 - b) + 128;
            Cb[i] = CLAMP_UV(c);

            a = Cr[i] - 128;
            b = Cr2[i] - 128;
           	c = (a << 7) / (128 - b) + 128;
            Cr[i] = CLAMP_UV(c);
		}
	}
}


static void chromamagic_darken2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

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

        blended_L = (L1 * o1 + L2 * o2) >> 8;
        blended_a = (a1 * o1 + a2 * o2) >> 8;
        blended_b = (b1 * o1 + b2 * o2) >> 8;

        blended_L = (blended_L < L1) ? blended_L : L1;
        blended_a = (blended_a < a1) ? blended_a : a1;
        blended_b = (blended_b < b1) ? blended_b : b1;

        blended_L = CLAMP_LAB(blended_L);
        blended_a = CLAMP_LAB(blended_a);
        blended_b = CLAMP_LAB(blended_b);

        Y[i] = (uint8_t)((blended_L * 255 + 128) / 100);
        Cb[i] = (uint8_t)(blended_a + 128);
        Cr[i] = (uint8_t)(blended_b + 128);
    }
}


static void chromamagic_darken(VJFrame *frame, VJFrame *frame2, int op_a)
{

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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

static void chromamagic_lighten2(VJFrame *frame, VJFrame *frame2, int op_a) {
    unsigned int i;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

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

        blended_L = (L1 * o1 + L2 * o2) >> 8;
        blended_a = (a1 * o1 + a2 * o2) >> 8;
        blended_b = (b1 * o1 + b2 * o2) >> 8;

        blended_L = (blended_L > L1) ? blended_L : L1;
        blended_a = (blended_a > a1) ? blended_a : a1;
        blended_b = (blended_b > b1) ? blended_b : b1;

        blended_L = CLAMP_LAB(blended_L);
        blended_a = CLAMP_LAB(blended_a);
        blended_b = CLAMP_LAB(blended_b);

        Y[i] = (uint8_t)((blended_L * 255 + 128) / 100);
        Cb[i] = (uint8_t)(blended_a + 128);
        Cr[i] = (uint8_t)(blended_b + 128);
    }
}


static void chromamagic_lighten(VJFrame *frame, VJFrame *frame2, int op_a)
{

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

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
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
#pragma omp simd
    for (i = 0; i < len; i++)
	{
		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		Y[i] = CLAMP_Y(a + ( 2 * b - 128));

		a = (Cb[i] * op_a) >> 8;
		b = (Cb2[i] * op_b ) >> 8;

		Cb[i] = CLAMP_UV(a + ( 2 * b ));

		a = (Cr[i] * op_a ) >> 8;
		b = (Cr2[i] * op_b ) >> 8;

		Cr[i] = CLAMP_UV(a + ( 2 * b ) );

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
  case 27:
	chromamagic_pixelfuckery( frame, frame2, op_a );
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
	}
}


