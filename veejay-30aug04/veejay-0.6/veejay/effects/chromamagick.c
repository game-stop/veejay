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

void chromamagic_selectmin(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
	if (b < a) {
	    yuv1[0][i] = b;
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }
}

void chromamagic_addsubselectlum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				 int width, int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
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
	    yuv1[0][i] = c;

	    a = yuv1[1][i];
	    b = yuv2[1][i];
	    c = (a + b) >> 1;
	    yuv1[1][i] = c;

	    a = yuv1[2][i];
	    b = yuv2[2][i];
	    c = (a + b) >> 1;
	    yuv1[2][i] = c;
	}
    }
}


void chromamagic_selectmax(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
	if (b > a) {
	    yuv1[0][i] = (3 * b + a)>>2;
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }
}
void chromamagic_selectdiff(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
	if (a > b) {
	    yuv1[0][i] = abs(yuv1[0][i] - yuv2[0][i]);
	    yuv1[1][i] = (yuv1[1][i] + yuv2[1][i]) >> 1;
	    yuv1[2][i] = (yuv1[2][i] + yuv2[2][i]) >> 1;
	}
    }
}

void chromamagic_diffreplace(uint8_t *yuv1[3], uint8_t *yuv2[3], int width, int height, int threshold)
{
	/* op_a = threshold */
	const unsigned int len = width * height;
	unsigned int i ;
	int op_b ;
	int op_a ;
	int a,b;
	unsigned long sum = 0;
	for(i=0; i < len; i++)
	{
		sum += yuv1[0][i];
	}
	op_b = (sum & 0xff);
	op_a = 255 - op_b;
	for(i=0; i < len; i++)
	{
		if( abs(yuv1[0][i] - yuv2[0][i]) >= threshold )
		{		
			a = ( yuv1[0][i] * op_a );
			b = ( yuv2[0][i] * op_b );
			yuv1[0][i] = (a + b) >> 8;
			a = ( yuv1[1][i] * op_a );
			b = ( yuv2[1][i] * op_b );

			yuv1[1][i] = (a + b) >> 8;
			a = ( yuv1[2][i] * op_a );
			b = ( yuv2[2][i] * op_b );

			yuv1[2][i] = (a + b) >> 8;
		}
	}
}

void chromamagic_selectdiffneg(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b,c;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
	if (a > b) {
	    c = 255 - abs(255 - a - b);
	    if( c < 16 ) c = 16; else if ( c > 240) c = 240;
	    yuv1[0][i] = c;
	    c = ((yuv1[1][i] * op_a) + (yuv2[1][i]*op_b) )>>8;
	    if( c < 16 ) c = 16; else if ( c > 235) c = 235;
	    yuv1[1][i] = c;
	    c = ((yuv1[2][i] * op_a) + (yuv2[2][i]*op_b) )>>8;
	    if( c < 16) c = 16; else if ( c > 235) c = 235;
	    yuv1[2][i] = c;
	}
    }
}

void chromamagic_selectunfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
	if (a > b) {
	    if (a < 16)
		c = 16;
	    else
		c = 255 - ((255 - b) * (255 - b)) / a;
	    if (c < 16)
		c = 16;
	    yuv1[0][i] = c;
	    yuv1[1][i] = (yuv1[1][i] + yuv2[1][i]) >> 1;
	    yuv1[2][i] = (yuv1[2][i] + yuv2[2][i]) >> 1;
	}
    }
}

void chromamagic_addlum(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * op_a) >> 8;
	b = (yuv2[0][i] * op_b) >> 8;
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
	yuv1[0][i] = c;
	yuv1[1][i] = (yuv1[1][i] + yuv2[1][i]) >> 1;
	yuv1[2][i] = (yuv1[2][i] + yuv2[2][i]) >> 1;
    }
}

void chromamagic_exclusive(uint8_t *yuv1[3], uint8_t *yuv2[3], int width, int height, int op_a) {
    unsigned int i;
    const unsigned int len = width * height;
    int a=0, b=0, c=0;
    const unsigned int o1 = op_a;
    const unsigned int o2 = 255 - a;

    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];


	if(a < 16) a = 16; else if(a > 235) a = 235;
	if(b < 16) b = 16; else if(b > 235) b = 235;

	a *= o1;
	b *= o2;
	a = a >> 8;
	b = b >> 8;	

	c = (a+b) - ((a * b) >> 8);
	if( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
	yuv1[0][i] = c;

	a = yuv1[1][i]-128;
	b = yuv2[1][i]-128;
	c = (a + b) - (( a * b) >> 8);
	c += 128;
	if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
	yuv1[1][i] = c;


	a = yuv1[2][i]-128;
	b = yuv2[2][i]-128;
	c = (a + b) - ((a*b) >> 8);
	c += 128;
	if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
	yuv1[2][i] = c;
    }
}

void chromamagic_diffnegate(uint8_t *yuv1[3], uint8_t *yuv2[3], int width, int height, int op_a) {

	unsigned int i;
	const unsigned int len = (width*height);
	int a,b,c,d;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - o1;
#define MAGIC_THRESHOLD 40
	for(i=0; i < len; i++) {
		//a = (( 255 - yuv1[0][i]) * o1) >> 7; /* negation */
	//	b = (yuv2[0][i] * o2) >> 7;
	//	d = abs( a - b );
		a = yuv1[0][i];
		b = yuv2[0][i];
		d = abs( a - b );
		if ( d > MAGIC_THRESHOLD )
		{
			a = yuv1[0][i] * o1;
			b = yuv2[1][i] * o2;
			c = 255 - ( (a + b) >>8 );
			if( c < 16 ) c = 16; else if ( c > 240) c = 240;	
			yuv1[0][i] = c;

	//		a = (( 255 - yuv1[1][i]) * o1) >> 7;	
	//		b = (yuv2[1][i] * o2) >> 7;
	//		c = 255 - abs(a - b);
	//		if ( c < 16) c = 16; else if ( c > 235 ) c = 235;
	//		yuv1[1][i] = c;


			a = (yuv1[1][i]-128) * o1;
			b = (yuv2[1][i]-128) * o2;
			c = 255 - ( 128 + (( a + b ) >> 8 ));
			yuv1[1][i] = c;


			a = (yuv1[2][i]-128) * o1;
			b = (yuv2[2][i]-128) * o2;
			c = 255 - ( 128 + (( a + b ) >> 8 ));

	//	a = (( 255 - yuv1[2][i]) * o1) >> 7;
	//	b = (yuv2[2][i] * o2) >> 7;
	//	c = 255 - abs( a- b);
			if ( c < 16) c = 16; else if ( c > 235) c = 235;
			yuv1[2][i] = c;
		
		}
	}
}

void chromamagic_additive(uint8_t *yuv1[3], uint8_t *yuv2[3], int width,
		int height, int op_a) {

	unsigned int i;
	const unsigned int len = (width*height);
	int a,b,c;	
	const unsigned int o1 = op_a;
 	const unsigned int o2 = 255 - op_a;
	for(i=0; i < len; i++) {
		a = (yuv1[0][i]*o1) >> 7;
		b = (yuv2[0][i]*o2) >> 7;
		c = a + (( 2 * b ) - 255);
		if( c < 16) c = 16; else if ( c > 235 ) c = 235;
		
		yuv1[0][i] = c;

		a = yuv1[1][i];
		b = yuv2[1][i];

		c = a + ( ( 2 * b ) - 255 );
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		yuv1[1][i] = c ;

		a = yuv1[2][i] ;
		b = yuv2[2][i] ;

		c = a + ( ( 2 * b ) - 255 );
		if ( c < 16 ) c = 16; else if ( c > 240 ) c  = 240;

		yuv1[2][i] = c ;
	
	}

}

void chromamagic_basecolor(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c, d;
    const unsigned int o1 = op_a;
    for (i = 0; i < len; i++) {
	a = o1 - yuv1[0][i];
	b = o1 - yuv2[0][i];
	c = a * b >> 8;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);	//8
	if ( d < 16 ) d = 16; else if ( d > 240 ) d = 240;
	yuv1[0][i] = d;

	a = yuv1[1][i]-128;
	b = yuv2[1][i]-128;
	c = a * b >> 8;
	d = c + a * ((255 - (((255-a) * (255-b)) >> 8) -c) >> 8);
	d += 128;
	if ( d < 16 ) d = 16; else if ( d > 235 ) d = 235;
	yuv1[1][i] = d;	

	a = yuv1[2][i]-128;
	b = yuv2[2][i]-128;
	c = a * b >> 8;
	d = c + a * ((255 - (((255-a) * (255-b)) >> 8) -c ) >> 8);
	d += 128;
	if ( d < 16 ) d = 16; else if ( d > 235 ) d = 235;
	yuv1[2][i] = d;
	

    }
}


void chromamagic_freeze(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a) {
	const unsigned int len = (w*h);
	unsigned int i;
	
	int a,b,c;

	if(op_a==0) op_a = 255;

	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - ((op_a -a ) * (op_a - a)) / b;
		if ( c < 16) c = 16; else if ( c > 240 ) c = 240;

		yuv1[0][i] = c;

		a = yuv1[1][i];
		b = yuv2[1][i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - ((255-a) * (255 - a)) / b;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;

		a = yuv1[2][i];
		b = yuv2[2][i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - (( 255 - a ) * ( 255 - a )) / b;
		if ( c < 16 ) c = 16; else if ( c> 235 ) c = 235;
		yuv1[2][i] = c;
	}

}

void chromamagic_unfreeze( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a ) {
	unsigned int i;
	const unsigned int len = w * h;
	int a,b,c;

	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - (( op_a - b) * (op_a - b)) / a;
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		yuv1[0][i] = c;
		
		a = yuv1[1][i];
		b = yuv2[1][i];
		if ( a < 16) a = 16;
		if ( b < 16) b = 16;
		c = 255 - (( 255 - b) * ( 255 - b )) / a;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;
		
		a = yuv1[2][i];
		b = yuv2[2][i];
		if ( a < 16 ) a = 16;
		if ( b < 16 ) b = 16;
		c = 255 - ((255 -b ) * (255 - b)) /a ;
		if ( c < 16 ) c = 16; else if ( c > 235) c = 235;
		yuv1[2][i] = c;

	}
}


void chromamagic_hardlight( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a) {
	unsigned int i;
	const unsigned int len = w * h;
	int a,b,c;

	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		if ( b < 128 ) {
			c = ( a * b ) >> 8;
		}
		else {
			c = 255 - (( op_a - b) * ( op_a - a ) >> 8);
		}
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		yuv1[0][i] = c;

		a = yuv1[1][i]-128;
		b = yuv2[1][i]-128;
		if ( b < 128 ) c = ( a * b ) >> 8;
		else c = 255 - (( 255 - b) * ( 255 - a) >> 8);
		c += 128;
		if ( c < 16) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;

		a = yuv1[2][i]-128;
		b = yuv1[2][i]-128;
		if ( b < 128) c = ( a * b ) >> 8;
		else c = 255 - (( 255 - b) * ( 255 - a) >> 8 );
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[2][i] = c;		

	}
}


void chromamagic_multiply( uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h,int op_a ) {
	unsigned int i;
	const unsigned int len = w * h;
	int a,b,c;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
	for( i=0; i < len; i++) {
		a = (yuv1[0][i] * o1) >> 8;
		b = (yuv2[0][i] * o2) >> 8;
		c = (a * b) >> 8;
		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		yuv1[0][i] = c;

		a = yuv1[1][i]-128;
		b = yuv2[1][i]-128;
		c = ( a * b ) >> 8;
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;

		a = yuv1[2][i] - 128;
		b = yuv2[2][i] - 128;
		c = ( a * b ) >> 8;
		c += 128;
		if ( c < 16 ) c = 16; else if ( c> 235) c = 235;
		yuv1[2][i] = c;

	}
}


void chromamagic_divide(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a ) {
	unsigned int i;
	const unsigned int len = w * h;
	int a,b,c;
	const unsigned int o1 = op_a;

	for(i=0; i < len; i++) {
		a = yuv1[0][i] * yuv1[0][i];
		b = o1 - yuv2[0][i];
		if ( b < 16 ) b = 16;
		c = a / b;
		if ( c < 16 ) c = 16; else if (c > 240) c = 240;
		yuv1[0][i] = c;

	
		a = yuv1[1][i] * yuv2[1][i];
		b = 255 - yuv2[1][i];
		if ( b < 16 ) b = 16;
		c = a / b;
		if ( c < 16) c= 16; else if ( c > 235) c = 235;
		yuv1[1][i] = c;

		a = yuv1[2][i] * yuv1[2][i];;
		b = 255 - yuv2[2][i];
		if ( b < 16 ) b = 16;
		c = ( a / b );
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[2][i] = c;
	}
}

void chromamagic_substract(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a) {
	unsigned int i;
	const unsigned int len = (w * h);
	int a ,b, c;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for( i=0; i < len; i++ ) 
	{
		a = yuv1[0][i];
		b = yuv2[0][i];

		c = a - ((b * o1) >> 8);
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[0][i] = c;

		a = yuv1[1][i];
		b = yuv2[1][i];
		c = (((a * o2) + (b * o1))>>8);

		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;

		yuv1[1][i] = c;

		a = yuv1[2][i];
		b = yuv2[2][i];
		c = (((a * o2) + (b * o1)) >> 8); 

		if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;

		yuv1[2][i] = c;
	}

}


void chromamagic_add(uint8_t *yuv1[3], uint8_t *yuv2[3], int width,
		int height, int op_a) {

	unsigned int i;
	const unsigned int len = (width * height);
	int a,b,c;	
	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		c = a + (( 2 * b ) - op_a);
		if( c < 16) c = 16; else if ( c > 240 ) c = 240;
		
		yuv1[0][i] = c;

		a = yuv1[1][i]-128;
		b = yuv2[1][i]-128;
		c = a + ( 2 * b );
		c += 128;
		if( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;

		a = yuv1[2][i]-128;
		b = yuv2[2][i]-128;
		c = a + ( 2 * b );
		c += 128;	
		if( c < 16 ) c = 16; else if ( c > 235) c = 235;
		yuv1[2][i] = c;
	}
}

void chromamagic_screen(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a) {
	unsigned int i;
	const unsigned int len = w * h;
	int a,b,c;
	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		c = 255 - ( (op_a-a) * (op_a-b) >> 8);
		if( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		yuv1[0][i] = c;
		a = yuv1[1][i]-128;
		b = yuv2[1][i]-128;
		c = 255 - ( ( 255-a) * (255 - b) >> 8);
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;
		a = yuv1[2][i]-128;
		b = yuv2[2][i]-128;
		c = 255 - ( ( 255 -a) * (255 - b)>>8);
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[2][i] = c;
	}
}

void chromamagic_difference(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h, int op_a) {
	unsigned int i;
	const unsigned int len = w * h;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
	int a,b,c;
	for(i=0; i < len; i++) {
		a = (yuv1[0][i] * o1)>>7;
		b = (yuv2[0][i] * o2)>>7;
		c = abs ( a - b );
		if( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
		yuv1[0][i] = c;

		a = (yuv1[1][i]-128);
		b = (yuv2[1][i]-128);
		c = abs ( a - b );
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[1][i] = c;		

		a = (yuv1[2][i]-128);
		b = (yuv2[2][i]-128);
		c = abs( a - b );
		c += 128;
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		yuv1[2][i] = c;
	}
}

/* not really softlight but still cool */
void chromamagic_softlightmode(uint8_t *yuv1[3],uint8_t *yuv2[3],
			int width,int height, int op_a) {

	unsigned int i;
	const unsigned int len = (width*height);
	int a,b,c,d;
	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		if ( a < op_a ) {
		c = (a * b) >> 8;
		d = (c + a * ( 255 - ( (255-a)*(255-b) >> 8) - c)) >> 8;
		if ( d < 16 ) d = 16; else if ( d > 240) d = 240;
	
		/* deal with chroma red/blue in a magical way
		   range gets narrowed. 
		*/
		a = abs(yuv1[1][i]-128);
		b = abs(yuv2[1][i]-128);
		c = (a * b);
		d = (c + a * ( 255 - ( (a * b) >> 7) - c)) >> 7;
		d += 128;
		if ( d < 16) d = 16; else if ( d > 235 ) d = 235;
		yuv1[1][i] = d;

		a = abs(yuv1[2][i]-128);
		b = abs(yuv2[2][i]-128);
		c = (a * b) >> 7;
		d = (c + a * ( 255 - ( (a * b) >> 7) -c)) >> 7;
		d += 128;
		if( d < 16) d= 16; else if ( d > 235 ) d= 235;
		yuv1[2][i] = d;
		}
	}
}

void chromamagic_dodge(uint8_t *yuv1[3], uint8_t *yuv2[3], int w, int h,
		int op_a) {
	unsigned int i;
	const unsigned int len = (w * h);
	int a,b,c;
	for(i=0; i < len; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];
		if( a >= op_a) c = a;
		else {
			if( b > 240 ) b = 240;
			if( a < 16) a = 16;
			c = (a << 8) / ( 255 - b );
			if ( c < 16 ) c = 16; else if (c > 240 ) c = 240;
			yuv1[0][i] = c;

			a = yuv1[1][i] - 128;
			b = yuv2[1][i] - 128;
			if ( b > 127 ) b = 127;
			c = ( a << 7 ) / ( 128 - b );
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235) c = 235;
			yuv1[1][i] = c;

			a = yuv1[2][i] - 128;
			b = yuv2[2][i] - 128;
			if ( b > 127 ) b = 127;
			c = ( a << 7 ) / ( 128 - b);
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
			yuv1[2][i] = c;
		}
	}	
}


void chromamagic_darken(uint8_t *src1[3], uint8_t *src2[3], int w, int h, int op_a)
{

	unsigned int i;
	const unsigned int len = (w * h);
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for(i=0; i < len; i++)
	{
		if(src1[0][i] > src2[0][i])
		{
			src1[0][i] = ((src1[0][i] * o1) + (src2[0][i] * o2)) >> 8; 
			src1[1][i] = ((src1[1][i] * o1) + (src2[1][i] * o2)) >> 8;
			src1[2][i] = ((src1[2][i] * o1) + (src2[2][i] * o2)) >> 8;
		} 
	}
}

void chromamagic_lighten(uint8_t *src1[3], uint8_t *src2[3], int w, int h, int op_a)
{

	unsigned int i;
	const unsigned int len = (w* h);
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
	int a,b,c;

	for(i=0; i < len; i++)
	{
		if(src1[0][i] < src2[0][i])
		{
			src1[0][i] = ((src1[0][i] * o1) + (src2[0][i] * o2)) >> 8; 
			src1[1][i] = ((src1[1][i] * o1) + (src2[1][i] * o2)) >> 8;
			src1[2][i] = ((src1[2][i] * o1) + (src2[2][i] * o2)) >> 8;
		} 
	}
}


void chromamagic_reflect(uint8_t *yuv1[3], uint8_t *yuv2[3],
		int width,int height, int op_a) {

	unsigned int i;
	const unsigned int len = (width * height);
	int a,b,c;
	
	for(i=0; i < len ; i++) {
		a = yuv1[0][i];
		b = yuv2[0][i];

		if ( b > op_a ) c = b;
		else {
			if ( b > 240 ) b = 240;
			if ( a < 16 ) a = 16;
			c = (a * a) / ( 255 - b );
			if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
			yuv1[0][i] = c;

			a = yuv1[1][i];
			b = yuv2[1][i];
			if ( a < 16 ) a = 16;
			if ( b < 16 ) b = 16;
			a -= 128;
			b -= 128;
			if ( b == 128 ) b = 127;
			c = ( a * a ) / ( 128 - b);
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
			yuv1[1][i] = c;

			a = yuv1[2][i];
			b = yuv2[2][i];
			if ( a < 16 ) a = 16; 
			if ( b < 16 ) b = 16;
			a -= 128;
			b -= 128;
			if ( b == 128) b = 127;
			c = ( a * a ) / ( 128 - b);
			c += 128;
			if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
			yuv1[2][i] = c;

		}
	}
}

void chromamagick_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			int width, int height, int type, int op_a)
{

    switch (type) {
    case 0:
	chromamagic_addsubselectlum(yuv1, yuv2, width, height, op_a);
	break;
    case 1:
	chromamagic_selectmin(yuv1, yuv2, width, height, op_a);
	break;
    case 2:
	chromamagic_selectmax(yuv1, yuv2, width, height, op_a);
	break;
    case 3:
	chromamagic_selectdiff(yuv1, yuv2, width, height, op_a);
	break;
    case 4:
	chromamagic_selectdiffneg(yuv1, yuv2, width, height, op_a);
	break;
    case 5:
	chromamagic_addlum(yuv1, yuv2, width, height, op_a);
	break;
    case 6:
	chromamagic_selectunfreeze(yuv1, yuv2, width, height, op_a);
	break;
    case 7:
	chromamagic_exclusive(yuv1,yuv2,width,height,op_a);
	break;
   case 8:
	chromamagic_diffnegate(yuv1,yuv2,width,height,op_a);
	break;
   case 9:
	chromamagic_additive( yuv1,yuv2,width,height,op_a);	
	break;
   case 10:
	chromamagic_basecolor(yuv1,yuv2,width,height,op_a);
	break;
   case 11:
	chromamagic_freeze(yuv1,yuv2,width,height,op_a);
	break;
   case 12:
	chromamagic_unfreeze(yuv1,yuv2,width,height,op_a);
	break;
   case 13:
	chromamagic_hardlight(yuv1,yuv2,width,height,op_a);
	break;
   case 14:
	chromamagic_multiply(yuv1,yuv2,width,height,op_a);
	break;
  case 15:
	chromamagic_divide(yuv1,yuv2,width,height,op_a);
	break;
  case 16:
	chromamagic_substract(yuv1,yuv2,width,height,op_a);
	break;
  case 17:
	chromamagic_add(yuv1,yuv2,width,height,op_a);
	break;
  case 18:
	chromamagic_screen(yuv1,yuv2,width,height,op_a);
	break;
  case 19:
	chromamagic_difference(yuv1,yuv2,width,height,op_a);
	break;
  case 20:
	chromamagic_softlightmode(yuv1,yuv2,width,height,op_a);
	break;
  case 21:
	chromamagic_dodge(yuv1,yuv2,width,height,op_a);
	break;
  case 22:
	chromamagic_reflect(yuv1,yuv2,width,height,op_a);
	break;
  case 23:
	chromamagic_diffreplace(yuv1,yuv2,width,height,op_a);
	break;
  case 24:
	chromamagic_darken( yuv1,yuv2,width,height,op_a);
	break;
  case 25:
	chromamagic_lighten( yuv1,yuv2,width,height,op_a);
	break;
    }
}
void chromamagick_free(){}
