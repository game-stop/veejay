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

/* 7 ,14, 24, 25, 26 */


#include "magicoverlays.h"
#include "sampleadm.h"
#include "../subsample.h"
#include "../vj-effect.h"
#include <stdlib.h>

/* 04/01/03: added transparency parameters for frame a and frame b in each function */

vj_effect *lumamagick_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    //ve->param_description = (char**)malloc(sizeof(char)* ve->num_params);
    ve->defaults[0] = 1;
    ve->defaults[1] = 100;
    ve->defaults[2] = 100;
    ve->description = "Luma Magick";
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 39;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 200;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 200;
   ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
    return ve;
}

/* 33 = illumination . it increases or decreases light intensity and associate color pixel*/


void _lumamagick_adddistorted(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const int opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	c = a + b;
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	Y[i] = c;
    }
    for (i = 0; i < uv_len; i++) {
	a = Cb[i];
	b = Cb2[i];
	c = a + b;
	if (c > 240)
	    c = 240;
	if (c < 16)
	    c = 16;
	Cb[i] = c;

	a = Cr[i];
	b = Cr2[i];
	c = a + b;
	if (c > 240)
	    c = 240;
	if (c < 16)
	    c = 16;
	Cr[i] = c;
    }
}

/*FIXME : overlay magic add distorted */
void _lumamagick_add_distorted(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int op_a, int op_b)
{

    unsigned int i;
    uint8_t y1, y2, y3, cb, cr, cs;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	y1 = Y[i] * opacity_a;
	y2 = Y2[i] * opacity_b;
	y3 = y1 + y2;
	y3 *= opacity_a;
	y3 += y2;
	Y[i] = y3;
    }
    for (i = 0; i < uv_len; i++) {
	cb = Cb[i] * opacity_a;
	cr = Cb2[i] * opacity_b;
	cs = cb + cr;
	cs += cr;
	Cb[i] = cs;

	cb = Cr[i];
	cr = Cr2[i];

	cs = cb + cr;
	cs += cr;
	Cr[i] = cs;
    }

}

void _lumamagick_subdistorted(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    uint8_t y1, y2, cb, cr;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	y1 = Y[i] * opacity_a;
	y2 = Y2[i] * opacity_b;
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

void _lumamagick_sub_distorted(VJFrame *frame, VJFrame *frame2,
			       int width, int height, int op_a, int op_b)
{

    unsigned int i;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    uint8_t y1, y2, cb, cr, y3, cs;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	y1 = Y[i] * opacity_a;
	y2 = Y2[i] * opacity_b;
	y3 = y1 - y2;
	y3 *= opacity_a;
	y3 -= y2;
	Y[i] = y3;
    }
    for (i = 0; i < uv_len; i++) {
	cb = Cb[i];
	cr = Cb2[i];
	cs = cb - cr;
	cs -= cr;
	Cb[i] = cs;

	cb = Cr[i];
	cr = Cr2[i];

	cs = cb - cr;
	cs -= cr;
	Cr[i] = cs;

    }
}

void _lumamagick_multiply(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    uint8_t y1, y2;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	y1 = Y[i] * opacity_a;
	y2 = Y2[i] * opacity_b;
	y1 = (y1 * y2) >> 8;
	Y[i] = y1;
    }
}

void _lumamagick_divide(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	b = (Y[i] * opacity_a) * (Y[i] * opacity_a);
	c = 255 - (Y2[i] * opacity_b);
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

void _lumamagick_additive(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    int a=0;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = (Y[i] * opacity_a) + (2 * (Y2[i] * opacity_b)) -
	    235;
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	Y[i] = a;
    }
}

void _lumamagick_substractive(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = (Y[i] * opacity_a) + ((Y2[i] - 235) * opacity_b);
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	Y[i] = a;
    }
}

void _lumamagick_softburn(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;

	if (a + b < 235) {
	    if (a == 235)
		c = a;
	    else
		c = (b >> 7) / (255 - a);
	    if (c > 235)
		c = 235;
	} else {
	    if (b < 16)
		b = 16;
	    c = 235 - (((255 - a) >> 7) / b);
	    if (c < 16)
		c = 16;
	}
	Y[i] = c;
    }
}

void _lumamagick_inverseburn(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
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


void _lumamagick_colordodge(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (a >= 235)
	    c = 235;
	else
	    c = (b >> 8) / (235 - a);

	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}

void _lumamagick_mulsub(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = (235 - Y2[i]) * opacity_b;
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

void _lumamagick_lighten(VJFrame *frame, VJFrame *frame2, int width,
			 int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
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

void _lumamagick_difference(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	Y[i] = abs(a - b);
    }
}

void _lumamagick_diffnegate(VJFrame *frame, VJFrame *frame2,
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = (255 - Y[i]) * opacity_a;
	b = Y2[i] * opacity_b;
	Y[i] = 255 - abs(a - b);
    }
}

void _lumamagick_exclusive(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	c = a + b - ((a * b) >> 8);
	//      Y[i] = Y[i] + Y2[i] -
	//      ((Y[i]*Y2[i])>>8);   //or try 7
	Y[i] = c;
    }
}

void _lumamagick_basecolor(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c, d;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = 16;
	c = a * b >> 7;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 7) - c) >> 7);	//8
	Y[i] = d;
    }
}

void _lumamagick_freeze(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;

	if (b < 16)
	    c = 16;
	else
	    c = 255 - ((255 - a) * (255 - a)) / b;
	if (c < 16)
	    c = 16;

	Y[i] = c;
    }
}

void _lumamagick_unfreeze(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;

	if (a < 16)
	    c = 16;
	else
	    c = 255 - ((255 - b) * (255 - b)) / a;
	if (c < 16)
	    c = 16;

	Y[i] = c;
    }
}

void _lumamagick_hardlight(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;

	if (b < 128)
	    c = (a * b) >> 7;
	else
	    c = 255 - ((255 - b) * (255 - a) >> 7);
	if (c < 16)
	    c = 16;

	Y[i] = c;
    }
}
void _lumamagick_relativeaddlum(VJFrame *frame, VJFrame *frame2,
				int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];
    int a, b, c, d;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	c = a >> 1;
	b = Y2[i] * opacity_b;
	d = b >> 1;
	Y[i] = c + d;
    }
}

void _lumamagick_relativesublum(VJFrame *frame, VJFrame *frame2,
				int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	Y[i] = (a - b + 255) >> 1;
    }
}

void _lumamagick_relativeadd(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b, c, d;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	c = a >> 1;
	b = Y2[i] * opacity_b;
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

void _lumamagick_relativesub(VJFrame *frame, VJFrame *frame2,
			     int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
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
void _lumamagick_minsubselect(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b < a)
	    Y[i] = (b - a + 255) >> 1;
	else
	    Y[i] = (a - b + 255) >> 1;
    }
}

void _lumamagick_maxsubselect(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b > a)
	    Y[i] = (b - a + 255) >> 1;
	else
	    Y[i] = (a - b + 255) >> 1;
    }
}



void _lumamagick_addsubselect(VJFrame *frame, VJFrame *frame2,
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
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


void _lumamagick_maxselect(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b > a)
	    Y[i] = b;
    }
}

void _lumamagick_minselect(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b < a)
	    Y[i] = b;
    }
}

void _lumamagick_addtest(VJFrame *frame, VJFrame *frame2, int width,
			 int height, int op_a, int op_b)
{
    unsigned int i;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	c = a + (2 * b) - 235;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
}
void _lumamagick_addtest2(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	c = a + (2 * b) - 235;
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
void _lumamagick_addtest4(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    int c, a, b;
    double opacity_a = op_a / 100.0;
    double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	b = b - 255;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = Y2[i] * opacity_b;
	if (b < 16)
	    b = 16;
	c = (a * a) / b;

	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }

}

void _lumamagick_selectmin(VJFrame *frame, VJFrame *frame2, int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[(i<<2)] * opacity_a;
	b = Y2[(i<<2)] * opacity_b;
	if (a > b) {
	    Cb[i] = Cb2[i];
	    Cr[i] = Cr2[i];
	}
    }


    for (i = 0; i < uv_len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b < a) {
	    Y[i] = b;
	}
    }
}
 
void _lumamagick_addtest3(VJFrame *frame, VJFrame *frame2, int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = (235 - Y2[i]) * opacity_b;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = Y2[i] * opacity_b;
	if (b < 16)
	    b = 16;
	c = (a * a) / b;

	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	Y[i] = c;
    }
    for (i = 0; i < uv_len; i++) {
	//a = Cb[i] * opacity_a;
	//b = (235 - Cb2[i]) * opacity_b;
	//if (b < 16) b = Cb2[i] * opacity_b;
	a = Cb[i];
	b = 255 - Cb2[i];
	if (b < 16)
	    b = Cb2[i];

	if (b < 16)
	    b = 16;
	if (a < 16)
	    a = 16;

	c = (a >> 1) + (b >> 1);
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;

	Cb[i] = c;

	//a = Cr[i] * opacity_a;
	//b = (235 - Cr2[i]) * opacity_b;
	//if (b < 16) b = Cr2[i] * opacity_b;

	a = Cr[i];
	b = 255 - Cr2[i];
	if (b < 16)
	    b = Cr2[i];

	if (b < 16)
	    b = 16;
	if (a < 16)
	    a = 16;

	c = (a >> 1) + (b >> 1);
	//c = (a * a) / ( b  - 255 ) ;
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;

	Cr[i] = c;

    }

}


void _lumamagick_addlum(VJFrame *frame, VJFrame *frame2, int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int len = frame->len;
  	uint8_t *Y = frame->data[0];
    uint8_t *Y2 = frame2->data[0];

    for (i = 0; i < len; i++) {
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b < 16)
	    b = 16;
	if (b > 235)
	    b = 235;
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	if ((255 - b) > 0) {
	    c = (a * a) / 255;
	} else {
	    c = (a * a) / (255 - b);
	}
	if (c > 240)
	    c = 240;
	Y[i] = c;

    }
}


void lumamagic_apply(VJFrame *frame, VJFrame *frame2, int width,
		     int height, int n, int op_a, int op_b)
{
    switch (n) {
    case VJ_EFFECT_BLEND_ADDDISTORT:
	_lumamagick_add_distorted(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_SUBDISTORT:
	_lumamagick_sub_distorted(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MULTIPLY:
	_lumamagick_multiply(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_DIVIDE:
	_lumamagick_divide(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDITIVE:
	_lumamagick_additive(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_SUBSTRACTIVE:
	_lumamagick_substractive(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_SOFTBURN:
	_lumamagick_softburn(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_INVERSEBURN:
	_lumamagick_inverseburn(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_COLORDODGE:
	_lumamagick_colordodge(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MULSUB:
	_lumamagick_mulsub(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_LIGHTEN:
	_lumamagick_lighten(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_DIFFERENCE:
	_lumamagick_difference(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_DIFFNEGATE:
	_lumamagick_diffnegate(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_EXCLUSIVE:
	_lumamagick_exclusive(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_BASECOLOR:
	_lumamagick_basecolor(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_HARDLIGHT:
	_lumamagick_hardlight(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELADD:
	_lumamagick_relativeadd(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELSUB:
	_lumamagick_relativesub(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MAXSEL:
	_lumamagick_maxselect(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MINSEL:
	_lumamagick_minselect(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELADDLUM:
	_lumamagick_relativeaddlum(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELSUBLUM:
	_lumamagick_relativesublum(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MINSUBSEL:
	_lumamagick_minsubselect(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MAXSUBSEL:
	_lumamagick_maxsubselect(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDSUBSEL:
	_lumamagick_addsubselect(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDAVG:
	_lumamagick_addtest(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDTEST2:
	_lumamagick_addtest2(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDTEST4:
	_lumamagick_addtest3(frame, frame2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDTEST3:
	_lumamagick_addtest4(frame, frame2, width, height, op_a, op_b);
	break;
    }
}
