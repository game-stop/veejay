/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include "chromamagickalpha.h"
#include <math.h>
#include "common.h"
// fixme: mode 8 and 9 corrupt (green/purple cbcr)

vj_effect *chromamagickalpha_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->defaults[1] = 150;
	ve->parallel = 1;
    ve->description = "Alpha: Chroma Magic composite (A or B)";
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 25;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->parallel = 1;
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value" );
    return ve;
}

void chromamagicalpha_selectmin(VJFrame *frame, VJFrame *frame2, int width,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
    int a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;
		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		if (b < a) {
			Y[i] = b;
			Cb[i] = Cb2[i];
			Cr[i] = Cr2[i];
		}
    }
}

void chromamagicalpha_addsubselectlum(VJFrame *frame, VJFrame *frame2,
				 int width, int height, int op_a)
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
  
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

void chromamagicalpha_selectmax(VJFrame *frame, VJFrame *frame2, int width,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
  
    int a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		if (b > a) {
		    Y[i] = (3 * b + a)>>2;
			Cb[i] = Cb2[i];
			Cr[i] = Cr2[i];
		}	
    }
}

void chromamagicalpha_selectdiff(VJFrame *frame, VJFrame *frame2,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

    int a, b;
    int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		if (a > b) {
		    Y[i] = abs(Y[i] - Y2[i]);
		    Cb[i] = (Cb[i] + Cb2[i]) >> 1;
		    Cr[i] = (Cr[i] + Cr2[i]) >> 1;
		}
    }
}

void chromamagicalpha_diffreplace(VJFrame *frame, VJFrame *frame2, int width, int height, int threshold)
{
	/* op_a = threshold */
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	unsigned int i ;
	int op_b ;
	int op_a ;
	int a,b;
	unsigned long sum = 0;
	for(i=0; i < len; i++)
	{
		if(aA[i] == 0 || aB[i] == 0)
			continue;
		sum += Y[i];
	}
	op_b = (sum & 0xff);
	op_a = 255 - op_b;
	for(i=0; i < len; i++)
	{
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

void chromamagicalpha_selectdiffneg(VJFrame *frame, VJFrame *frame2,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
    int a, b;
    const int op_b = 255 - op_a;
    
	for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		if (a > b) {
			Y[i] = 255 - abs(255 - a - b);
			Cb[i] = ((Cb[i] * op_a) + (Cb2[i]*op_b) )>>8;
			Cr[i] = ((Cr[i] * op_a) + (Cr2[i]*op_b) )>>8;
		}
    }
}

void chromamagicalpha_selectunfreeze(VJFrame *frame, VJFrame *frame2,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
    
    int a, b;
    const int op_b = 255 - op_a;
    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

void chromamagicalpha_addlum(VJFrame *frame, VJFrame *frame2, int width,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
 
    int a, b;
    const int op_b = 255 - op_a;

    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		Y[i] = (a * a) / (256- b);
		Cb[i] = (Cb[i] + Cb2[i]) >> 1;
		Cr[i] = (Cr[i] + Cr2[i]) >> 1;
    }
}

void chromamagicalpha_exclusive(VJFrame *frame, VJFrame *frame2, int width, int height, int op_a) {
    unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

    int a=0, b=0, c=0;

    for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];
 		c = a + (2 * b) - op_a;
		Y[i] = CLAMP_Y(c - (( a * b ) >> 8 ));   
		
		a = Cb[i];
		b = Cb2[i];

		c = a + (2 * b);
		Cb[i] = CLAMP_UV(c - 0xff);

		a = Cr[i];
		b = Cr2[i];
		c = a + (2 * b);
		Cr[i] = CLAMP_UV(c - 0xff);
   	 }

}

void chromamagicalpha_diffnegate(VJFrame *frame, VJFrame *frame2, int width, int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b,d;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - o1;

#define MAGIC_THRESHOLD 40
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];
		d = abs( a - b );
		if ( d > MAGIC_THRESHOLD )
		{
			a = Y[i] * o1;
			b = Cb2[i] * o2;
			Y[i] = 255 - ( (a + b) >>8 );

			a = (Cb[i]-128) * o1;
			b = (Cb2[i]-128) * o2;
			Cb[i] = 255 - ( 128 + (( a + b ) >> 8 ));


			a = (Cr[i]-128) * o1;
			b = (Cr2[i]-128) * o2;
			Cr[i] = 255 - ( 128 + (( a + b ) >> 8 ));
		
		}
	}
}

void chromamagicalpha_additive(VJFrame *frame, VJFrame *frame2, int width,
		int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b;	
	const unsigned int o1 = op_a;
 	const unsigned int o2 = 255 - op_a;
	
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;
		a = (Y[i]*o1) >> 7;
		b = (Y2[i]*o2) >> 7;
		Y[i] = a + (( 2 * b ) - 255);

		a = Cb[i];
		b = Cb2[i];

		Cb[i] = a + ( ( 2 * b ) - 255 );

		a = Cr[i] ;
		b = Cr2[i] ;

		Cr[i] = a + ( ( 2 * b ) - 255 );
	}

}

void chromamagicalpha_basecolor(VJFrame *frame, VJFrame *frame2,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
    int a, b, c, d;
    const unsigned int o1 = op_a;
    
	for (i = 0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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


void chromamagicalpha_freeze(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	unsigned int i;
	
	int a,b,c;

	if(op_a==0) op_a = 255;

	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

void chromamagicalpha_unfreeze( VJFrame *frame, VJFrame *frame2, int w, int h, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b;

	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];
		if( a > pixel_Y_lo_ )
		  Y[i] = 255 - (( op_a - b) * (op_a - b)) / a;
		
		a = Cb[i];
		b = Cb2[i];

		if( a > pixel_U_lo_ )
			Cb[i] = 255 - (( 256 - b) * ( 256 - b )) / a;
		
		a = Cr[i];
		b = Cr2[i];

		if( a > pixel_U_lo_ )
			Cr[i] = 255 - ((256 -b ) * (256 - b)) /a ;
	}
}


void chromamagicalpha_hardlight( VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b,c;

	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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


void chromamagicalpha_multiply( VJFrame *frame, VJFrame *frame2, int w, int h,int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	int a,b,c;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
	for( i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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


void chromamagicalpha_divide(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a ) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	int a,b;
	const unsigned int o1 = op_a;

	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i] * Y[i];
		b = o1 - Y2[i];
		if ( b > pixel_Y_lo_ ) 
			Y[i] = a / b;
	
		a = Cb[i] * Cb2[i];
		b = 255 - Cb2[i];
		if( b > pixel_U_lo_ )
			Cb[i] = a / b;

		a = Cr[i] * Cr[i];;
		b = 255 - Cr2[i];
		if( b > pixel_U_lo_ )
			Cr[i] = ( a / b );
	}
}

void chromamagicalpha_substract(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	int a ,b;
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for( i=0; i < len; i++ ) 
	{
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];

		Y[i] = a - ((b * o1) >> 8);

		a = Cb[i];
		b = Cb2[i];
		Cb[i] = (((a * o2) + (b * o1))>>8);

		a = Cr[i];
		b = Cr2[i];
		Cr[i] = (((a * o2) + (b * o1)) >> 8); 
	}

}


void chromamagicalpha_add(VJFrame *frame, VJFrame *frame2, int width,
		int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	int a,b,c;	
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];
		Y[i] = a + (( 2 * b ) - op_a);

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

void chromamagicalpha_screen(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

	int a,b,c;
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

void chromamagicalpha_difference(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;
	int a,b,c;
	
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

/* not really softlight but still cool */
void chromamagicalpha_softlightmode(VJFrame *frame,VJFrame *frame2,
			int width,int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b,c,d;
	
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

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

void chromamagicalpha_dodge(VJFrame *frame, VJFrame *frame2, int w, int h,
		int op_a) {
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b,c;
	
	for(i=0; i < len; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];
		if( a >= op_a) c = a;
		else {
			Y[i] = (a << 8) / ( 256 - b );

			a = Cb[i] - 128;
			b = Cb2[i] - 128;
			if ( b > 127 ) b = 127;
			c = ( a << 7 ) / ( 128 - b );
			c += 128;
			Cb[i] = CLAMP_UV(c);

			a = Cr[i] - 128;
			b = Cr2[i] - 128;
			if ( b > 127 ) b = 127;
			c = ( a << 7 ) / ( 128 - b);
			c += 128;
			Cr[i] = CLAMP_UV(c);
		}
	}	
}


void chromamagicalpha_darken(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a)
{
	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for(i=0; i < len; i++)
	{
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		if(Y[i] > Y2[i])
		{
			Y[i] = ((Y[i] * o1) + (Y2[i] * o2)) >> 8; 
			Cb[i] = ((Cb[i] * o1) + (Cb2[i] * o2)) >> 8;
			Cr[i] = ((Cr[i] * o1) + (Cr2[i] * o2)) >> 8;
		} 
	}
}

void chromamagicalpha_lighten(VJFrame *frame, VJFrame *frame2, int w, int h, int op_a)
{

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	const unsigned int o1 = op_a;
	const unsigned int o2 = 255 - op_a;

	for(i=0; i < len; i++)
	{
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		if(Y[i] < Y2[i])
		{
			Y[i] = ((Y[i] * o1) + (Y2[i] * o2)) >> 8; 
			Cb[i] = ((Cb[i] * o1) + (Cb2[i] * o2)) >> 8;
			Cr[i] = ((Cr[i] * o1) + (Cr2[i] * o2)) >> 8;
		} 
	}
}


void chromamagicalpha_reflect(VJFrame *frame, VJFrame *frame2,
		int width,int height, int op_a) {

	unsigned int i;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];
	int a,b,c;
	
	for(i=0; i < len ; i++) {
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = Y[i];
		b = Y2[i];

		if ( b > op_a ) c = b;
		else {
			Y[i] = (a * a) / ( 256 - b );

			a = Cb[i];
			b = Cb2[i];
			a -= 128;
			b -= 128;
			if ( b == 128 ) b = 127;
			c = ( a * a ) / ( 128 - b);
			c += 128;
			Cb[i] = CLAMP_UV(c);

			a = Cr[i];
			b = Cr2[i];
			a -= 128;
			b -= 128;
			if ( b == 128) b = 127;
			c = ( a * a ) / ( 128 - b);
			c += 128;
			Cr[i] = CLAMP_UV(c);

		}
	}
}


void chromamagicalpha_modadd(VJFrame *frame, VJFrame *frame2, int width,
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
	uint8_t *aB = frame2->data[3];
	uint8_t *aA = frame2->data[3];

    int a, b;
    const int op_b = 255 - op_a;

    for (i = 0; i < len; i++)
	{
		if(aA[i] == 0 || aB[i] == 0)
			continue;

		a = (Y[i] * op_a) >> 8;
		b = (Y2[i] * op_b) >> 8;
		Y[i] = (a + ( 2 * b - 128)) & 255;

		a = (Cb[i] * op_a) >> 8;
		b = (Cb2[i] * op_b ) >> 8;

		Cb[i] =  (a + ( 2 * b )) & 255;

		a = (Cr[i] * op_a ) >> 8;		
		b = (Cr2[i] * op_b ) >> 8;

		Cr[i] = (a + ( 2 * b ) ) & 255;

    }
}


void chromamagickalpha_apply(VJFrame *frame, VJFrame *frame2,
			int width, int height, int type, int op_a)
{

    switch (type) {
    case 0:
	chromamagicalpha_addsubselectlum(frame, frame2, width, height, op_a);
	break;
    case 1:
	chromamagicalpha_selectmin(frame, frame2, width, height, op_a);
	break;
    case 2:
	chromamagicalpha_selectmax(frame, frame2, width, height, op_a);
	break;
    case 3:
	chromamagicalpha_selectdiff(frame, frame2, width, height, op_a);
	break;
    case 4:
	chromamagicalpha_selectdiffneg(frame, frame2, width, height, op_a);
	break;
    case 5:
	chromamagicalpha_addlum(frame, frame2, width, height, op_a);
	break;
    case 6:
	chromamagicalpha_selectunfreeze(frame, frame2, width, height, op_a);
	break;
    case 7:
	chromamagicalpha_exclusive(frame,frame2,width,height,op_a);
	break;
   case 8:
	chromamagicalpha_diffnegate(frame,frame2,width,height,op_a);
	break;
   case 9:
	chromamagicalpha_additive( frame,frame2,width,height,op_a);	
	break;
   case 10:
	chromamagicalpha_basecolor(frame,frame2,width,height,op_a);
	break;
   case 11:
	chromamagicalpha_freeze(frame,frame2,width,height,op_a);
	break;
   case 12:
	chromamagicalpha_unfreeze(frame,frame2,width,height,op_a);
	break;
   case 13:
	chromamagicalpha_hardlight(frame,frame2,width,height,op_a);
	break;
   case 14:
	chromamagicalpha_multiply(frame,frame2,width,height,op_a);
	break;
  case 15:
	chromamagicalpha_divide(frame,frame2,width,height,op_a);
	break;
  case 16:
	chromamagicalpha_substract(frame,frame2,width,height,op_a);
	break;
  case 17:
	chromamagicalpha_add(frame,frame2,width,height,op_a);
	break;
  case 18:
	chromamagicalpha_screen(frame,frame2,width,height,op_a);
	break;
  case 19:
	chromamagicalpha_difference(frame,frame2,width,height,op_a);
	break;
  case 20:
	chromamagicalpha_softlightmode(frame,frame2,width,height,op_a);
	break;
  case 21:
	chromamagicalpha_dodge(frame,frame2,width,height,op_a);
	break;
  case 22:
	chromamagicalpha_reflect(frame,frame2,width,height,op_a);
	break;
  case 23:
	chromamagicalpha_diffreplace(frame,frame2,width,height,op_a);
	break;
  case 24:
	chromamagicalpha_darken( frame,frame2,width,height,op_a);
	break;
  case 25:
	chromamagicalpha_lighten( frame,frame2,width,height,op_a);
	break;
  case 26:
	chromamagicalpha_modadd( frame,frame2,width,height,op_a);
	break;
    }
}


