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
#include "sampleadm.h"
#include "../subsample.h"

#include <stdlib.h>


vj_effect *overlaymagic_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 7;
    ve->description = "Overlay Magic";
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 32;
    ve->extra_frame = 1;
    ve->sub_format = 0;
    ve->has_internal_data = 0;
    return ve;
}

/* rename methods in lumamagick and chromamagick */


void _overlaymagic_adddistorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	c = a + b;
	if (c > 240)
	    c = 240;
	if (c < 16)
	    c = 16;
	yuv1[0][i] = c;
    }
    len >>= 2;			/* len = len / 4 */
    for (i = 0; i < len; i++) {
	a = yuv1[1][i];
	b = yuv2[1][i];
	c = a + b;
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	yuv1[1][i] = c;

	a = yuv1[2][i];
	b = yuv2[2][i];
	c = a + b;
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	yuv1[2][i] = c;
    }
}

void _overlaymagic_add_distorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
				 int width, int height)
{

    unsigned int i;
    unsigned int len = width * height;
    uint8_t y1, y2, cb, cr;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i];
	y2 = yuv2[0][i];
	yuv1[0][i] = y1 + y2;
    }
    len >>= 2;
    for (i = 0; i < len; i++) {
	cb = yuv1[1][i];
	cr = yuv2[1][i];
	yuv1[1][i] = cb + cr;

	cb = yuv1[2][i];
	cr = yuv2[2][i];

	yuv1[2][i] = cb + cr;
    }

}

void _overlaymagic_subdistorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t y1, y2, cb, cr;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i];
	y2 = yuv2[0][i];
	y1 -= y2;
	yuv1[0][i] = y1;
    }
    len = len/ 4;			/* len = len / 4 */
    for (i = 0; i < len; i++) {
	cb = yuv1[1][i];
	cr = yuv2[1][i];
	cb -= cr;
	yuv1[1][i] = cb;
	cb = yuv1[2][i];
	cr = yuv2[2][i];
	cb -= cr;
	yuv1[2][i] = cb;
    }
}

void _overlaymagic_sub_distorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
				 int width, int height)
{

    unsigned int i, len = width * height;
    uint8_t y1, y2, cb, cr;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i];
	y2 = yuv2[0][i];
	yuv1[0][i] = y1 - y2;
    }
    len >>= 2;
    for (i = 0; i < len; i++) {
	cb = yuv1[1][i];
	cr = yuv2[1][i];
	yuv1[1][i] = cb - cr;

	cb = yuv1[2][i];
	cr = yuv2[2][i];

	yuv1[2][i] = cb - cr;

    }
}

void _overlaymagic_multiply(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t y1, y2;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i];
	y2 = yuv2[0][i];
	y1 = (y1 * y2) >> 8;
	yuv1[0][i] = y1;
    }
}

void _overlaymagic_divide(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	b = yuv1[0][i] * yuv1[0][i];
	c = 255 - yuv2[0][i];
	if (c == 0)
	    c = 16;
	a = b / c;
	if (a > 235)
	    a = 235;
	if (a < 16)
	    a = 16;
	yuv1[0][i] = a;
    }
}

void _overlaymagic_additive(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height)
{
    
    unsigned int len = width * height;
    int i,a;
	while(len--) { 
		a = yuv1[0][len] + (2 * yuv2[0][len]) - 255;
		if(a < 16) a  = 16; else if (a > 240) a = 240;
		yuv1[0][len] = a;
	}
}


void _overlaymagic_substractive(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height)
{

    unsigned int i;
    unsigned int len = width * height;
    int a;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] + (yuv2[0][i] - 240);
	if (a < 16)
	    a = 16;
	if (a > 240)
	    a = 240;
	yuv1[0][i] = a;
    }
}

void _overlaymagic_softburn(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];

	if (a + b < 240) {
	    if (a == 240)
		c = a;
	    else
		c = (b >> 7) / (255 - a);
	    if (c > 240)
		c = 240;
	} else {
	    if (b < 16)
		b = 16;
	    c = 255 - (((255 - a) >> 7) / b);
	    if (c < 16)
		c = 16;
	    if (c > 240)
		c = 240;
	}
	yuv1[0][i] = c;
    }
}

void _overlaymagic_inverseburn(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (a < 16)
	    c = 16;
	else
	    c = 255 - (((255 - b) >> 8) / a);
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }
}


void _overlaymagic_colordodge(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (a >= 240)
	    c = 240;
	else
	    c = (b >> 8) / (256 - a);

	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }
}

void _overlaymagic_mulsub(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = 255 - yuv2[0][i];
	if (b < 16)
	    b = 16;
	c = a / b;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }
}

void _overlaymagic_lighten(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (a > b)
	    c = a;
	else
	    c = b;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }
}

void _overlaymagic_difference(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	yuv1[0][i] = abs(a - b);
    }
}

void _overlaymagic_diffnegate(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = (255 - yuv1[0][i]);
	b = yuv2[0][i];
	yuv1[0][i] = 255 - abs(a - b);
    }
}

void _overlaymagic_exclusive(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	c = a + b - ((a * b) >> 8);
	if ( c < 16 ) c = 16; else if ( c > 240 ) c = 240;
	yuv1[0][i] = c;	
    }
    len = len/2;
    for( i=0; i < len; i++) {
	a = yuv1[1][i]-128;
	b = yuv2[1][i]-128;
	c = a +b - (( a * b ) >> 8);
	c += 128;
	if ( c < 16 ) c = 16; else if ( c > 235 ) c = 235;
	yuv1[1][i] = c;
	

	a = yuv1[2][i] - 128;
	b = yuv2[2][i] - 128;
	c = a + b - (( a * b) >> 8);
	c += 128;
	if ( c < 16) c = 16; else if ( c > 235 ) c = 235;
	yuv1[2][i] = c;
	}
}

void _overlaymagic_basecolor(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c, d;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = 16;
	c = a * b >> 7;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 7) - c) >> 7);	//8
	yuv1[0][i] = d;
    }
}

void _overlaymagic_freeze(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];

	if (b < 16)
	    c = a; //16
	else
	    c = 255 - ((255 - a) * (255 - a)) / b;
	if (c < 16)
	    c = b ; //16
	if ( c > 235 ) c = 235;

	yuv1[0][i] = c;
    }
}

void _overlaymagic_unfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];

	if (a < 16)
	    c = 16;
	else
	    c = 255 - ((255 - b) * (255 - b)) / a;
	if (c < 16)
	    c = 16;

	yuv1[0][i] = c;
    }
}

void _overlaymagic_hardlight(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];

	if (b < 128)
	    c = (a * b) >> 7;
	else
	    c = 256 - ((256 - b) * (256 - a) >> 7);
	if (c < 16)
	    c = 16;

	yuv1[0][i] = c;
    }
}
void _overlaymagic_relativeaddlum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				  int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b, c, d;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	c = a >> 1;
	b = yuv2[0][i];
	d = b >> 1;
	if ((c + d) < a)
	    c = a;
	else
	    c += d;
	yuv1[0][i] = c;
    }
}

void _overlaymagic_relativesublum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				  int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	yuv1[0][i] = (a - b + 255) >> 1;
    }
}

void _overlaymagic_relativeadd(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height)
{
    unsigned int i;

    unsigned int len = width * height;
    int a, b, c, d;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	c = a >> 1;
	b = yuv2[0][i];
	d = b >> 1;
	yuv1[0][i] = c + d;
    }
    len >>= 2;
    for (i = 0; i < len; i++) {
	a = yuv1[1][i];
	c = a >> 1;
	b = yuv2[1][i];
	d = b >> 1;
	yuv1[1][i] = c + d;

	a = yuv1[2][i];
	c = a >> 1;
	b = yuv2[2][i];
	d = b >> 1;
	yuv1[2][i] = c + d;
    }
}

void _overlaymagic_relativesub(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	yuv1[0][i] = (a - b + 255) >> 1;
    }
    len >>= 2;
    for (i = 0; i < len; i++) {
	a = yuv1[1][i];
	b = yuv2[1][i];
	yuv1[1][i] = (a - b + 255) >> 1;
	a = yuv1[2][i];
	b = yuv2[2][i];
	yuv1[2][i] = (a - b + 255) >> 1;
    }

}
void _overlaymagic_minsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (b < a)
	    yuv1[0][i] = (b - a + 255) >> 1;
	else
	    yuv1[0][i] = (a - b + 255) >> 1;
    }
}

void _overlaymagic_maxsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (b > a)
	    yuv1[0][i] = (b - a + 255) >> 1;
	else
	    yuv1[0][i] = (a - b + 255) >> 1;
    }
}



void _overlaymagic_addsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (b < 16)
	    b = 16;
	if (b > 240)
	    b = 240;
	if (a < 16)
	    a = 16;
	if (a > 240)
	    a = 240;

	if (b < a) {
	    c = (a + b) >> 1;
	    yuv1[0][i] = c;
	}
    }
}


void _overlaymagic_maxselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (b > a)
	    yuv1[0][i] = b;
    }
}

void _overlaymagic_minselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	if (b < a)
	    yuv1[0][i] = b;
    }
}

void _overlaymagic_addtest(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	c = a + (2 * b) - 240;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }
}
void _overlaymagic_addtest2(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	c = a + (2 * b) - 240;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }
    len >>= 2;
    for (i = 0; i < len; i++) {
	a = yuv1[1][i];
	b = yuv2[1][i];
	c = a + (2 * b) - 255;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[1][i] = c;

	a = yuv1[2][i];
	b = yuv2[2][i];
	c = a + (2 * b) - 255;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[2][i] = c;

    }

}
void _overlaymagic_addtest4(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height)
{
    unsigned int i;
    unsigned int len = width * height;
    int c, a, b;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i];
	b = yuv2[0][i];
	b = b - 255;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = yuv2[0][i];
	if (b < 16)
	    b = 16;
	c = (a * a) / b;

	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;
	yuv1[0][i] = c;
    }

}

void _overlaymagic_try
    (uint8_t * yuv1[3], uint8_t * yuv2[3], int width, int height) {
    unsigned int i;
    unsigned int len = width * height;
    int a, b, p, q;
    for (i = 0; i < len; i++) {
	/* calc p */
	a = yuv1[0][i];
	b = yuv1[0][i];

	if (b < 16)
	    p = 16;
	else
	    p = 255 - ((255 - a) * (255 - a)) / b;
	if (p < 16)
	    p = 16;

	/* calc q */
	a = yuv2[0][i];
	b = yuv2[0][i];
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


	yuv1[0][i] = q;


    }
}

void overlaymagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int n)
{
    switch (n) {
    case VJ_EFFECT_BLEND_ADDITIVE:
	_overlaymagic_additive(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_SUBSTRACTIVE:
	_overlaymagic_substractive(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_MULTIPLY:
	_overlaymagic_multiply(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIVIDE:
	_overlaymagic_divide(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_LIGHTEN:
	_overlaymagic_lighten(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIFFERENCE:
	_overlaymagic_difference(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_DIFFNEGATE:
	_overlaymagic_diffnegate(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_EXCLUSIVE:
	_overlaymagic_exclusive(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_BASECOLOR:
	_overlaymagic_basecolor(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_FREEZE:
	_overlaymagic_freeze(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_UNFREEZE:
	_overlaymagic_unfreeze(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELADD:
	_overlaymagic_relativeadd(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELSUB:
	_overlaymagic_relativesub(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELADDLUM:
	_overlaymagic_relativeaddlum(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_RELSUBLUM:
	_overlaymagic_relativesublum(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_MAXSEL:
	_overlaymagic_maxselect(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_MINSEL:
	_overlaymagic_minselect(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_MINSUBSEL:
	_overlaymagic_minsubselect(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_MAXSUBSEL:
	_overlaymagic_maxsubselect(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDSUBSEL:
	_overlaymagic_addsubselect(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDAVG:
	_overlaymagic_add_distorted(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST2:
	_overlaymagic_addtest(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST3:
	_overlaymagic_addtest2(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDTEST4:
	_overlaymagic_addtest4(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_MULSUB:
	_overlaymagic_mulsub(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_SOFTBURN:
	_overlaymagic_softburn(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_INVERSEBURN:
	_overlaymagic_inverseburn(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_COLORDODGE:
	_overlaymagic_colordodge(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_ADDDISTORT:
	_overlaymagic_adddistorted(yuv1, yuv2, width, height);
	break;
    case VJ_EFFECT_BLEND_SUBDISTORT:
	_overlaymagic_subdistorted(yuv1, yuv2, width, height);
	break;
    case 32:
	_overlaymagic_try(yuv1, yuv2, width, height);
	break;

    }
}
void overlaymagic_free(){}
