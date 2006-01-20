/*
 * Linux VeeJay
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
#include <config.h>
#include "magicoverlays.h"
#include <libvje/internal.h>
#include <stdlib.h>


vj_effect *overlaymagic_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->description = "Overlay Magic";
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 32;
    ve->extra_frame = 1;
    ve->sub_format = 0;
	ve->has_user = 0;
	   return ve;
}

/* rename methods in lumamagick and chromamagick */


void _overlaymagic_adddistorted(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a + b;
	if (c > 240)
	    c = 240;
	if (c < 16)
	    c = 16;
	Y[i] = c;
    }

    for (i = 0; i < uv_len; i++) {
	a = Cb[i];
	b = Cb2[i];
	c = a + b;
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	Cb[i] = c;

	a = Cr[i];
	b = Cr2[i];
	c = a + b;
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	Cr[i] = c;
    }
}

void _overlaymagic_add_distorted(VJFrame *frame, VJFrame *frame2,
				 int width, int height)
{

    unsigned int i;
    uint8_t y1, y2, cb, cr;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	Y[i] = y1 + y2;
    }

    for (i = 0; i < uv_len; i++) {
	cb = Cb[i];
	cr = Cb2[i];
	Cb[i] = cb + cr;

	cb = Cr[i];
	cr = Cr2[i];

	Cr[i] = cb + cr;
    }

}

void _overlaymagic_subdistorted(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    uint8_t y1, y2, cb, cr;
    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	y1 -= y2;
	Y[i] = y1;
    }
    for (i = 0; i < uv_len; i++) {
	cb = Cb[i];
	cr = Cb2[i];
	cb -= cr;
	Cb[i] = cb;
	cb = Cr[i];
	cr = Cr2[i];
	cb -= cr;
	Cr[i] = cb;
    }
}

void _overlaymagic_sub_distorted(VJFrame *frame, VJFrame *frame2,
				 int width, int height)
{

    unsigned int i ;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    uint8_t y1, y2, cb, cr;
    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	Y[i] = y1 - y2;
    }
    for (i = 0; i < uv_len; i++) {
	cb = Cb[i];
	cr = Cb2[i];
	Cb[i] = cb - cr;

	cb = Cr[i];
	cr = Cr2[i];

	Cr[i] = cb - cr;

    }
}

void _overlaymagic_multiply(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    uint8_t y1, y2;
    for (i = 0; i < len; i++) {
	y1 = Y[i];
	y2 = Y2[i];
	y1 = (y1 * y2) >> 8;
	Y[i] = y1;
    }
}

void _overlaymagic_divide(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    int a, b, c;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
    for (i = 0; i < len; i++) {
	b = Y[i] * Y[i];
	c = 255 - Y2[i];
	if (c == 0)
	    c = 16;
	a = b / c;
	if (a > 235)
	    a = 235;
	if (a < 16)
	    a = 16;
	Y[i] = a;
    }
}

void _overlaymagic_additive(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a;
	while(len--) { 
		a = Y[len] + (2 * Y2[len]) - 255;
//		a = Y[len] + (2 * (Y2[len]-128)&255);
		if(a < 16) a  = 16; else if (a > 235) a = 235;
		Y[len] = a;
	}
}


void _overlaymagic_substractive(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{

    unsigned int i;
    int a;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] + (Y2[i] - 235);
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	Y[i] = a;
    }
}

void _overlaymagic_softburn(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];

	if (a + b < 240) {
	    if (a == 235)
		c = a;
	    else
		c = (b >> 7) / (255 - a);
	    if (c > 235)
		c = 235;
	} else {
	    if (b < 16)
		b = 16;
	    c = 255 - (((255 - a) >> 7) / b);
	    if (c < 16)
		c = 16;
	    if (c > 235)
		c = 2350;
	}
	Y[i] = c;
    }
}

void _overlaymagic_inverseburn(VJFrame *frame, VJFrame *frame2,
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a < 16)
	    c = 16;
	else
	    c = 255 - (((255 - b) >> 8) / a);
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}


void _overlaymagic_colordodge(VJFrame *frame, VJFrame *frame2,
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a >= 235)
	    c = 235;
	else
	    c = (b >> 8) / (256 - a);

	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}

void _overlaymagic_mulsub(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = 255 - Y2[i];
	if (b < 16)
	    b = 16;
	c = a / b;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}

void _overlaymagic_lighten(VJFrame *frame, VJFrame *frame2, int width,
			   int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a > b)
	    c = a;
	else
	    c = b;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}

void _overlaymagic_difference(VJFrame *frame, VJFrame *frame2,
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	Y[i] = abs(a - b);
    }
}

void _overlaymagic_diffnegate(VJFrame *frame, VJFrame *frame2,
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
		a = (255 - Y[i]);
		b = Y2[i];
		Y[i] = 255 - abs(a - b);
    }
}

void _overlaymagic_exclusive(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    int a, b, c;
    for (i = 0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		c = a + b - ((a * b) >> 8);
		if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
		Y[i] = c;	
    }

    for( i=0; i < uv_len; i++) {
	a = Cb[i]-128;
	b = Cb2[i]-128;
	c = a +b - (( a * b ) >> 8);
	c += 128;
	if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
	Cb[i] = c;
	

	a = Cr[i] - 128;
	b = Cr2[i] - 128;
	c = a + b - (( a * b) >> 8);
	c += 128;
	if ( c < 16) c = 16; else if ( c > 240 ) c = 240;
	Cr[i] = c;
	}
}

void _overlaymagic_basecolor(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    int a, b, c, d;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = 16;
	c = a * b >> 7;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 7) - c) >> 7);	//8
	Y[i] = d;
    }
}

void _overlaymagic_freeze(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];

	if (b < 16)
	    c = a; //16
	else
	    c = 255 - ((255 - a) * (255 - a)) / b;
	if (c < 16)
	    c = b ; //16
	if ( c > 235 ) c = 235;

	Y[i] = c;
    }
}

void _overlaymagic_unfreeze(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];

	if (a < 16)
	    c = 16;
	else
	    c = 255 - ((255 - b) * (255 - b)) / a;
	if (c < 16)
	    c = 16;

	Y[i] = c;
    }
}

void _overlaymagic_hardlight(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    for (i = 0; i < len; i++) {
		a = Y[i];
		b = Y2[i];

		if (b < 128)
		    c = (a * b) >> 7;
		else
		    c = 256 - ((256 - b) * (256 - a) >> 7);
		if (c < 16)
		    c = 16;

		Y[i] = c;
    }
}
void _overlaymagic_relativeaddlum(VJFrame *frame, VJFrame *frame2,
				  int width, int height)
{
    unsigned int i;
    int a, b, c, d;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i];
	c = a >> 1;
	b = Y2[i];
	d = b >> 1;
	if ((c + d) < a)
	    c = a;
	else
	    c += d;
	Y[i] = c;
    }
}

void _overlaymagic_relativesublum(VJFrame *frame, VJFrame *frame2,
				  int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
		a = Y[i];
		b = Y2[i];
		Y[i] = (a - b + 255) >> 1;
    }
}

void _overlaymagic_relativeadd(VJFrame *frame, VJFrame *frame2,
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    int a, b, c, d;
    for (i = 0; i < len; i++) {
	a = Y[i];
	c = a >> 1;
	b = Y2[i];
	d = b >> 1;
	Y[i] = c + d;
    }
    for (i = 0; i < uv_len; i++) {
	a = Cb[i];
	c = a >> 1;
	b = Cb2[i];
	d = b >> 1;
	Cb[i] = c + d;

	a = Cr[i];
	c = a >> 1;
	b = Cr2[i];
	d = b >> 1;
	Cr[i] = c + d;
    }
}

void _overlaymagic_relativesub(VJFrame *frame, VJFrame *frame2,
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	Y[i] = (a - b + 255) >> 1;
    }
    for (i = 0; i < uv_len; i++) {
	a = Cb[i];
	b = Cb2[i];
	Cb[i] = (a - b + 255) >> 1;
	a = Cr[i];
	b = Cr2[i];
	Cr[i] = (a - b + 255) >> 1;
    }

}
void _overlaymagic_minsubselect(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b < a)
	    Y[i] = (b - a + 255) >> 1;
	else
	    Y[i] = (a - b + 255) >> 1;
    }
}

void _overlaymagic_maxsubselect(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b > a)
	    Y[i] = (b - a + 255) >> 1;
	else
	    Y[i] = (a - b + 255) >> 1;
    }
}



void _overlaymagic_addsubselect(VJFrame *frame, VJFrame *frame2,
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
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
	}
    }
}


void _overlaymagic_maxselect(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b > a)
	    Y[i] = b;
    }
}

void _overlaymagic_minselect(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	if (b < a)
	    Y[i] = b;
    }
}

void _overlaymagic_addtest(VJFrame *frame, VJFrame *frame2, int width,
			   int height)
{
    unsigned int i;
    unsigned int len = width * height;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a + (2 * b) - 255;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}
void _overlaymagic_addtest2(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	c = a + (2 * b) - 255;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }

    for (i = 0; i < uv_len; i++) {
	a = Cb[i];
	b = Cb2[i];
	c = a + (2 * b) - 255;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	Cb[i] = c;

	a = Cr[i];
	b = Cr2[i];
	c = a + (2 * b) - 255;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	Cr[i] = c;

    }

}
void _overlaymagic_addtest4(VJFrame *frame, VJFrame *frame2,
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = Y2[i];
	b = b - 255;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = Y2[i];
	if (b < 16)
	    b = 16;
	c = (a * a) / b;

	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	Y[i] = c;
    }

}

void _overlaymagic_try
    (VJFrame *frame, VJFrame *frame2, int width, int height) {
    unsigned int i;
    unsigned int len = width * height;
    int a, b, p, q;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	/* calc p */
	a = Y[i];
	b = Y[i];

	if (b < 16)
	    p = 16;
	else
	    p = 255 - ((255 - a) * (255 - a)) / b;
	if (p < 16)
	    p = 16;

	/* calc q */
	a = Y2[i];
	b = Y2[i];
	if (b < 16)
	    q = 16;
	else
	    q = 255 - ((255 - a) * (255 - a)) / b;
	if (b < 16)
	    q = 16;

	/* calc pixel */
	if (q < 16)
	    q = 16;
	else
	    q = 255 - ((255 - p) * (255 - a)) / q;
	if (q < 16)
	    q = 16;


	Y[i] = q;


    }
}

void overlaymagic_apply(VJFrame *frame, VJFrame *frame2, int width,
			int height, int n)
{
    switch (n) {
    case VJ_EFFECT_BLEND_ADDITIVE:
	_overlaymagic_additive(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_SUBSTRACTIVE:
	_overlaymagic_substractive(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MULTIPLY:
	_overlaymagic_multiply(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIVIDE:
	_overlaymagic_divide(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_LIGHTEN:
	_overlaymagic_lighten(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIFFERENCE:
	_overlaymagic_difference(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIFFNEGATE:
	_overlaymagic_diffnegate(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_EXCLUSIVE:
	_overlaymagic_exclusive(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_BASECOLOR:
	_overlaymagic_basecolor(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_FREEZE:
	_overlaymagic_freeze(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_UNFREEZE:
	_overlaymagic_unfreeze(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELADD:
	_overlaymagic_relativeadd(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELSUB:
	_overlaymagic_relativesub(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELADDLUM:
	_overlaymagic_relativeaddlum(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELSUBLUM:
	_overlaymagic_relativesublum(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MAXSEL:
	_overlaymagic_maxselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MINSEL:
	_overlaymagic_minselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MINSUBSEL:
	_overlaymagic_minsubselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MAXSUBSEL:
	_overlaymagic_maxsubselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDSUBSEL:
	_overlaymagic_addsubselect(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDAVG:
	_overlaymagic_add_distorted(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST2:
	_overlaymagic_addtest(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST3:
	_overlaymagic_addtest2(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST4:
	_overlaymagic_addtest4(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_MULSUB:
	_overlaymagic_mulsub(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_SOFTBURN:
	_overlaymagic_softburn(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_INVERSEBURN:
	_overlaymagic_inverseburn(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_COLORDODGE:
	_overlaymagic_colordodge(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDDISTORT:
	_overlaymagic_adddistorted(frame, frame2, width, height);
	break;
    case VJ_EFFECT_BLEND_SUBDISTORT:
	_overlaymagic_subdistorted(frame, frame2, width, height);
	break;
    case 32:
	_overlaymagic_try(frame, frame2, width, height);
	break;

    }
}
void overlaymagic_free(){}
