/*
 * VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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


/* Note that the 'opacity' parameter is sometimes used as a 
   threshold value or substraction value depending on the mode
   of this effect */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "chromamagick.h"
#include <math.h>
// fixme: mode 8 and 9 corrupt (green/purple cbcr)

vj_effect *chromamagick_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->defaults[1] = 150;
    ve->description = "Chroma Magic";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 25;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_internal_data = 0;

    return ve;
}

void chromamagic_selectmin(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int a, b;
    const int op_b = 255 - op_a;
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

void chromamagic_addsubselectlum(VJFrame *frame, VJFrame *frame2,
				 int width, int height, int op_a)
{
    unsigned int i;
    int c, a, b;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (b < 16)
	    b = 16;
	if (b > 235)
	    b = 235;
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;

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


void chromamagic_selectmax(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a)
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
void chromamagic_selectdiff(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a)
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

void chromamagic_diffreplace(VJFrame *frame, VJFrame *frame2, int width, int height, int threshold)
{
	/* op_a = threshold */
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
	for(i=0; i < len; i++)
	{
		sum += Y[i];
	}
	op_b = (sum & 0xff);
	op_a = 255 - op_b;
	for(i=0; i < len; i++)
	{
		if( abs(Y[i] - Y2[i]) >= threshold )
		{		
			a = ( Y[i] * op_a );
			b = ( Y2[i] * op_b );
			Y[i] = (a + b) >> 8;
			a = ( Cb[i] * op_a );
			b = ( Cb2[i] * op_b );

			Cb[i] = (a + b) >> 8;
			a = ( Cr[i] * op_a );
			b = ( Cr2[i] * op_b );

			Cr[i] = (a + b) >> 8;
		}
	}
}

void chromamagic_selectdiffneg(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int a, b,c;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (a > b) {
	    c = 255 - abs(255 - a - b);
	    if( c < 16 ) c = 16; else if ( c > 240) c = 240;
	    Y[i] = c;
	    c = ((Cb[i] * op_a) + (Cb2[i]*op_b) )>>8;
	    if( c < 16 ) c = 16; else if ( c > 235) c = 235;
	    Cb[i] = c;
	    c = ((Cr[i] * op_a) + (Cr2[i]*op_b) )>>8;
	    if( c < 16) c = 16; else if ( c > 235) c = 235;
	    Cr[i] = c;
	}
    }
}

void chromamagic_selectunfreeze(VJFrame *frame, VJFrame *frame2,
				int width, int height, int op_a)
{
    unsigned int i;
	const int len = frame->len;
	const int uv_len = frame->uv_len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int c, a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (a > b) {
	    if (a < 16)
		c = 16;
	    else
		c = 255 - ((255 - b) * (255 - b)) / a;
	    if (c < 16)
		c = 16;
	    Y[i] = c;
	    Cb[i] = (Cb[i] + Cb2[i]) >> 1;
	    Cr[i] = (Cr[i] + Cr2[i]) >> 1;
	}
    }
}

void chromamagic_addlum(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a)
{
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int c, a, b;
    const int op_b = 255 - op_a;

    for (i = 0; i < len; i++) {
	a = (Y[i] * op_a) >> 8;
	b = (Y2[i] * op_b) >> 8;
	if (b < 16)
	    b = 16;
	if (b > 235)
	    b = 235;
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	c = (a * a) / (255 - b);
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	Y[i] = c;
	Cb[i] = (Cb[i] + Cb2[i]) >> 1;
	Cr[i] = (Cr[i] + Cr2[i]) >> 1;
    }
}

void chromamagic_exclusive(VJFrame *frame, VJFrame *frame2, int width, int height, int op_a) {
    unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

    int a=0, b=0, c=0;
    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - a;

    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];


	if(a < 16) a = 16; else if(a > 235) a = 235;
	if(b < 16) b = 16; else if(b > 235) b = 235;

	a *= o1;
	b *= o2;
	a = a >> 8;
	b = b >> 8;	

	c = (a+b) - ((a * b) >> 8);
	if( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
	Y[i] = c;

	a = Cb[i]-128;
	b = Cb2[i]-128;
	c = (a + b) - (( a * b) >> 8);
	c += 128;
	if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
	Cb[i] = c;


	a = Cr[i]-128;
	b = Cr2[i]-128;
	c = (a + b) - ((a*b) >> 8);
	c += 128;
	if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
	Cr[i] = c;
    }
}

void chromamagic_diffnegate(VJFrame *frame, VJFrame *frame2, int width, int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c,d;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - o1;
#define MAGIC_THRESHOLD 40
	for(i=0; i < len; i++) {
		//a = (( 255 - Y[i]) * o1) >> 7; /* negation */
	//	b = (Y2[i] * o2) >> 7;
	//	d = abs( a - b );
		a = Y[i];
		b = Y2[i];
		d = abs( a - b );
		if ( d > MAGIC_THRESHOLD )
		{
			a = Y[i] * o1;
			b = Cb2[i] * o2;
			c = 255 - ( (a + b) >>8 );
			if( c < 16 ) c = 16; else if ( c > 240) c = 240;	
			Y[i] = c;

	//		a = (( 255 - Cb[i]) * o1) >> 7;	
	//		b = (Cb2[i] * o2) >> 7;
	//		c = 255 - abs(a - b);
	//		if ( c < 16) c = 16; else if ( c > 235 ) c = 235;
	//		Cb[i] = c;


			a = (Cb[i]-128) * o1;
			b = (Cb2[i]-128) * o2;
			c = 255 - ( 128 + (( a + b ) >> 8 ));
			Cb[i] = c;


			a = (Cr[i]-128) * o1;
			b = (Cr2[i]-128) * o2;
			c = 255 - ( 128 + (( a + b ) >> 8 ));

	//	a = (( 255 - Cr[i]) * o1) >> 7;
	//	b = (Cr2[i] * o2) >> 7;
	//	c = 255 - abs( a- b);
			if ( c < 16) c = 16; else if ( c > 235) c = 235;
			Cr[i] = c;
		
		}
	}
}

void chromamagic_additive(VJFrame *frame, VJFrame *frame2, int width,
		int height, int op_a) {

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
	for(i=0; i < len; i++) {
		a = (Y[i]*o1) >> 7;
		b = (Y2[i]*o2) >> 7;
		c = a + (( 2 * b ) - 255);
		if( c < 16) c = 16; else if ( c > 235 ) c = 235;
		
		Y[i] = c;

		a = Cb[i];
		b = Cb2[i];

		c = a + ( ( 2 * b ) - 255 );
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		Cb[i] = c ;

		a = Cr[i] ;
		b = Cr2[i] ;

		c = a + ( ( 2 * b ) - 255 );
		if ( c < 16 ) c = 16; else if ( c > 240 ) c  = 240;

		Cr[i] = c ;
	
	}

}

void chromamagic_basecolor(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a)
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
    for (i = 0; i < len; i++) {
	a = o1 - Y[i];
	b = o1 - Y2[i];
	c = a * b >> 8;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);	//8
	if ( d < 16 ) d = 16; else if ( d > 240 ) d = 240;
	Y[i] = d;

	a = Cb[i]-128;
	b = Cb2[i]-128;
	c = a * b >> 8;
	d = c + a * ((255 - (((255-a) * (255-b)) >> 8) -c) >> 8);
	d += 128;
	if ( d < 16 ) d = 16; else if ( d > 235 ) d = 235;
	Cb[i] = d;	

	a = Cr[i]-128;
	b = Cr2[i]-128;
	c = a * b >> 8;
	d = c + a * ((255 - (((255-a) * (255-b)) >> 8) -c ) >> 8);
	d += 128;
	if ( d < 16 ) d = 16; else if ( d > 235 ) d = 235;
	Cr[i] = d;
	

    }
}


void chromamagic_freeze(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
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

	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - ((op_a -a ) * (op_a - a)) / b;
		if ( c < 16) c = 16; else if ( c > 240 ) c = 240;

		Y[i] = c;

		a = Cb[i];
		b = Cb2[i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - ((255-a) * (255 - a)) / b;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;

		a = Cr[i];
		b = Cr2[i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - (( 255 - a ) * ( 255 - a )) / b;
		if ( c < 16 ) c = 16; else if ( c> 235 ) c = 235;
		Cr[i] = c;
	}

}

void chromamagic_unfreeze( VJFrame *frame, VJFrame *frame2, int w, int h, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;

	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - (( op_a - b) * (op_a - b)) / a;
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		Y[i] = c;
		
		a = Cb[i];
		b = Cb2[i];
		if ( a < 16) a = 16;
		if ( b < 16) b = 16;
		c = 255 - (( 255 - b) * ( 255 - b )) / a;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;
		
		a = Cr[i];
		b = Cr2[i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - ((255 -b ) * (255 - b)) /a ;
		if ( c < 16 ) c = 16; else if ( c > 235) c = 235;
		Cr[i] = c;

	}
}


void chromamagic_hardlight( VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;

	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if ( b < 128 ) {
			c = ( a * b ) >> 8;
		}
		else {
			c = 255 - (( op_a - b) * ( op_a - a ) >> 8);
		}
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		Y[i] = c;

		a = Cb[i]-128;
		b = Cb2[i]-128;
		if ( b < 128 ) c = ( a * b ) >> 8;
		else c = 255 - (( 255 - b) * ( 255 - a) >> 8);
		c += 128;
		if ( c < 16) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;

		a = Cr[i]-128;
		b = Cr[i]-128;
		if ( b < 128) c = ( a * b ) >> 8;
		else c = 255 - (( 255 - b) * ( 255 - a) >> 8 );
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cr[i] = c;		

	}
}


void chromamagic_multiply( VJFrame *frame, VJFrame *frame2, int w, int h,int op_a ) {
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
	for( i=0; i < len; i++) {
		a = (Y[i] * o1) >> 8;
		b = (Y2[i] * o2) >> 8;
		c = (a * b) >> 8;
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		Y[i] = c;

		a = Cb[i]-128;
		b = Cb2[i]-128;
		c = ( a * b ) >> 8;
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;

		a = Cr[i] - 128;
		b = Cr2[i] - 128;
		c = ( a * b ) >> 8;
		c += 128;
		if ( c < 16 ) c = 16; else if ( c> 235) c = 235;
		Cr[i] = c;

	}
}


void chromamagic_divide(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a ) {
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

	for(i=0; i < len; i++) {
		a = Y[i] * Y[i];
		b = o1 - Y2[i];
		if ( b < 16 ) b = 16;
		c = a / b;
		if ( c < 16 ) c = 16; else if (c > 240) c = 240;
		Y[i] = c;

	
		a = Cb[i] * Cb2[i];
		b = 255 - Cb2[i];
		if ( b < 16 ) b = 16;
		c = a / b;
		if ( c < 16) c= 16; else if ( c > 235) c = 235;
		Cb[i] = c;

		a = Cr[i] * Cr[i];;
		b = 255 - Cr2[i];
		if ( b < 16 ) b = 16;
		c = ( a / b );
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cr[i] = c;
	}
}

void chromamagic_substract(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a ,b, c;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for( i=0; i < len; i++ ) 
	{
		a = Y[i];
		b = Y2[i];

		c = a - ((b * o1) >> 8);
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Y[i] = c;

		a = Cb[i];
		b = Cb2[i];
		c = (((a * o2) + (b * o1))>>8);

		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;

		Cb[i] = c;

		a = Cr[i];
		b = Cr2[i];
		c = (((a * o2) + (b * o1)) >> 8); 

		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;

		Cr[i] = c;
	}

}


void chromamagic_add(VJFrame *frame, VJFrame *frame2, int width,
		int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;	
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		c = a + (( 2 * b ) - op_a);
		if( c < 16) c = 16; else if ( c > 240 ) c = 240;
		
		Y[i] = c;

		a = Cb[i]-128;
		b = Cb2[i]-128;
		c = a + ( 2 * b );
		c += 128;
		if( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;

		a = Cr[i]-128;
		b = Cr2[i]-128;
		c = a + ( 2 * b );
		c += 128;	
		if( c < 16 ) c = 16; else if ( c > 235) c = 235;
		Cr[i] = c;
	}
}

void chromamagic_screen(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		c = 255 - ( (op_a-a) * (op_a-b) >> 8);
		if( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		Y[i] = c;
		a = Cb[i]-128;
		b = Cb2[i]-128;
		c = 255 - ( ( 255-a) * (255 - b) >> 8);
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;
		a = Cr[i]-128;
		b = Cr2[i]-128;
		c = 255 - ( ( 255 -a) * (255 - b)>>8);
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cr[i] = c;
	}
}

void chromamagic_difference(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
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
	for(i=0; i < len; i++) {
		a = (Y[i] * o1)>>7;
		b = (Y2[i] * o2)>>7;
		c = abs ( a - b );
		if( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		Y[i] = c;

		a = (Cb[i]-128);
		b = (Cb2[i]-128);
		c = abs ( a - b );
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cb[i] = c;		

		a = (Cr[i]-128);
		b = (Cr2[i]-128);
		c = abs( a - b );
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Cr[i] = c;
	}
}

/* not really softlight but still cool */
void chromamagic_softlightmode(VJFrame *frame,VJFrame *frame2,
			int width,int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c,d;
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if ( a < op_a ) {
		c = (a * b) >> 8;
		d = (c + a * ( 255 - ( (255-a)*(255-b) >> 8) - c)) >> 8;
		if ( d < 16 ) d = 16; else if ( d > 240) d = 240;
	
		/* deal with chroma red/blue in a magical way
		   range gets narrowed. 
		*/
		a = abs(Cb[i]-128);
		b = abs(Cb2[i]-128);
		c = (a * b);
		d = (c + a * ( 255 - ( (a * b) >> 7) - c)) >> 7;
		d += 128;
		if ( d < 16) d = 16; else if ( d > 235 ) d = 235;
		Cb[i] = d;

		a = abs(Cr[i]-128);
		b = abs(Cr2[i]-128);
		c = (a * b) >> 7;
		d = (c + a * ( 255 - ( (a * b) >> 7) -c)) >> 7;
		d += 128;
		if( d < 16) d= 16; else if ( d > 235 ) d= 235;
		Cr[i] = d;
		}
	}
}

void chromamagic_dodge(VJFrame *frame, VJFrame *frame2, int w, int h,
		int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;
	for(i=0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		if( a >= op_a) c = a;
		else {
			if( b > 240 ) b = 240;
			if( a < 16) a = 16;
			c = (a << 8) / ( 255 - b );
			if ( c < 16 ) c = 16; else if (c > 240 ) c = 240;
			Y[i] = c;

			a = Cb[i] - 128;
			b = Cb2[i] - 128;
			if ( b > 127 ) b = 127;
			c = ( a << 7 ) / ( 128 - b );
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235) c = 235;
			Cb[i] = c;

			a = Cr[i] - 128;
			b = Cr2[i] - 128;
			if ( b > 127 ) b = 127;
			c = ( a << 7 ) / ( 128 - b);
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
			Cr[i] = c;
		}
	}	
}


void chromamagic_darken(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a)
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

void chromamagic_lighten(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a)
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
	int a,b,c;

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


void chromamagic_reflect(VJFrame *frame, VJFrame *frame2,
		int width,int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];

	int a,b,c;
	
	for(i=0; i < len ; i++) {
		a = Y[i];
		b = Y2[i];

		if ( b > op_a ) c = b;
		else {
			if ( b > 240 ) b = 240;
			if ( a < 16 ) a = 16;
			c = (a * a) / ( 255 - b );
			if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
			Y[i] = c;

			a = Cb[i];
			b = Cb2[i];
			if ( a < 16 ) a = 16;
			if ( b < 16 ) b = 16;
			a -= 128;
			b -= 128;
			if ( b == 128 ) b = 127;
			c = ( a * a ) / ( 128 - b);
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
			Cb[i] = c;

			a = Cr[i];
			b = Cr2[i];
			if ( a < 16 ) a = 16; 
			if ( b < 16 ) b = 16;
			a -= 128;
			b -= 128;
			if ( b == 128) b = 127;
			c = ( a * a ) / ( 128 - b);
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
			Cr[i] = c;

		}
	}
}

void chromamagick_apply(VJFrame *frame, VJFrame *frame2,
			int width, int height, int type, int op_a)
{

    switch (type) {
    case 0:
	chromamagic_addsubselectlum(frame, frame2, width, height, op_a);
	break;
    case 1:
	chromamagic_selectmin(frame, frame2, width, height, op_a);
	break;
    case 2:
	chromamagic_selectmax(frame, frame2, width, height, op_a);
	break;
    case 3:
	chromamagic_selectdiff(frame, frame2, width, height, op_a);
	break;
    case 4:
	chromamagic_selectdiffneg(frame, frame2, width, height, op_a);
	break;
    case 5:
	chromamagic_addlum(frame, frame2, width, height, op_a);
	break;
    case 6:
	chromamagic_selectunfreeze(frame, frame2, width, height, op_a);
	break;
    case 7:
	chromamagic_exclusive(frame,frame2,width,height,op_a);
	break;
   case 8:
	chromamagic_diffnegate(frame,frame2,width,height,op_a);
	break;
   case 9:
	chromamagic_additive( frame,frame2,width,height,op_a);	
	break;
   case 10:
	chromamagic_basecolor(frame,frame2,width,height,op_a);
	break;
   case 11:
	chromamagic_freeze(frame,frame2,width,height,op_a);
	break;
   case 12:
	chromamagic_unfreeze(frame,frame2,width,height,op_a);
	break;
   case 13:
	chromamagic_hardlight(frame,frame2,width,height,op_a);
	break;
   case 14:
	chromamagic_multiply(frame,frame2,width,height,op_a);
	break;
  case 15:
	chromamagic_divide(frame,frame2,width,height,op_a);
	break;
  case 16:
	chromamagic_substract(frame,frame2,width,height,op_a);
	break;
  case 17:
	chromamagic_add(frame,frame2,width,height,op_a);
	break;
  case 18:
	chromamagic_screen(frame,frame2,width,height,op_a);
	break;
  case 19:
	chromamagic_difference(frame,frame2,width,height,op_a);
	break;
  case 20:
	chromamagic_softlightmode(frame,frame2,width,height,op_a);
	break;
  case 21:
	chromamagic_dodge(frame,frame2,width,height,op_a);
	break;
  case 22:
	chromamagic_reflect(frame,frame2,width,height,op_a);
	break;
  case 23:
	chromamagic_diffreplace(frame,frame2,width,height,op_a);
	break;
  case 24:
	chromamagic_darken( frame,frame2,width,height,op_a);
	break;
  case 25:
	chromamagic_lighten( frame,frame2,width,height,op_a);
	break;
    }
}
void chromamagick_free(){}
