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


void _lumamagick_adddistorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int uv_len = len / 4;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const int opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	c = a + b;
	if (c > 235)
	    c = 235;
	if (c < 16)
	    c = 16;
	yuv1[0][i] = c;
    }
    for (i = 0; i < uv_len; i++) {
	a = yuv1[1][i];
	b = yuv2[1][i];
	c = a + b;
	if (c > 240)
	    c = 240;
	if (c < 16)
	    c = 16;
	yuv1[1][i] = c;

	a = yuv1[2][i];
	b = yuv2[2][i];
	c = a + b;
	if (c > 240)
	    c = 240;
	if (c < 16)
	    c = 16;
	yuv1[2][i] = c;
    }
}

/*FIXME : overlay magic add distorted */
void _lumamagick_add_distorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a, int op_b)
{

    unsigned int i;
    const unsigned int len = width * height;
    uint8_t y1, y2, y3, cb, cr, cs;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = len / 4;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i] * opacity_a;
	y2 = yuv2[0][i] * opacity_b;
	y3 = y1 + y2;
	y3 *= opacity_a;
	y3 += y2;
	yuv1[0][i] = y3;
    }
    for (i = 0; i < uv_len; i++) {
	cb = yuv1[1][i] * opacity_a;
	cr = yuv2[1][i] * opacity_b;
	cs = cb + cr;
	cs += cr;
	yuv1[1][i] = cs;

	cb = yuv1[2][i];
	cr = yuv2[2][i];

	cs = cb + cr;
	cs += cr;
	yuv1[2][i] = cs;
    }

}

void _lumamagick_subdistorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    uint8_t y1, y2, cb, cr;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = len / 4;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i] * opacity_a;
	y2 = yuv2[0][i] * opacity_b;
	y1 -= y2;
	yuv1[0][i] = y1;
    }
    for (i = 0; i < uv_len; i++) {
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

void _lumamagick_sub_distorted(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a, int op_b)
{

    unsigned int i;
    const unsigned int len = width * height;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    uint8_t y1, y2, cb, cr, y3, cs;
    const unsigned int uv_len = len / 4;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i] * opacity_a;
	y2 = yuv2[0][i] * opacity_b;
	y3 = y1 - y2;
	y3 *= opacity_a;
	y3 -= y2;
	yuv1[0][i] = y3;
    }
    for (i = 0; i < uv_len; i++) {
	cb = yuv1[1][i];
	cr = yuv2[1][i];
	cs = cb - cr;
	cs -= cr;
	yuv1[1][i] = cs;

	cb = yuv1[2][i];
	cr = yuv2[2][i];

	cs = cb - cr;
	cs -= cr;
	yuv1[2][i] = cs;

    }
}

void _lumamagick_multiply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    unsigned int len = width * height;
    uint8_t y1, y2;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	y1 = yuv1[0][i] * opacity_a;
	y2 = yuv2[0][i] * opacity_b;
	y1 = (y1 * y2) >> 8;
	yuv1[0][i] = y1;
    }
}

void _lumamagick_divide(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	b = (yuv1[0][i] * opacity_a) * (yuv1[0][i] * opacity_a);
	c = 255 - (yuv2[0][i] * opacity_b);
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

void _lumamagick_additive(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a=0;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * opacity_a) + (2 * (yuv2[0][i] * opacity_b)) -
	    235;
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	yuv1[0][i] = a;
    }
}

void _lumamagick_substractive(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = (yuv1[0][i] * opacity_a) + ((yuv2[0][i] - 235) * opacity_b);
	if (a < 16)
	    a = 16;
	if (a > 235)
	    a = 235;
	yuv1[0][i] = a;
    }
}

void _lumamagick_softburn(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;

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
	yuv1[0][i] = c;
    }
}

void _lumamagick_inverseburn(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a < 16)
	    c = 16;
	else
	    c = 255 - (((255 - b) >> 8) / a);
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
}


void _lumamagick_colordodge(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a >= 235)
	    c = 235;
	else
	    c = (b >> 8) / (235 - a);

	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
}

void _lumamagick_mulsub(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = (235 - yuv2[0][i]) * opacity_b;
	if (b < 16)
	    b = 16;
	c = a / b;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
}

void _lumamagick_lighten(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			 int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a > b)
	    c = a;
	else
	    c = b;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
}

void _lumamagick_difference(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	yuv1[0][i] = abs(a - b);
    }
}

void _lumamagick_diffnegate(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = (255 - yuv1[0][i]) * opacity_a;
	b = yuv2[0][i] * opacity_b;
	yuv1[0][i] = 255 - abs(a - b);
    }
}

void _lumamagick_exclusive(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	c = a + b - ((a * b) >> 8);
	//      yuv1[0][i] = yuv1[0][i] + yuv2[0][i] -
	//      ((yuv1[0][i]*yuv2[0][i])>>8);   //or try 7
	yuv1[0][i] = c;
    }
}

void _lumamagick_basecolor(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c, d;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = 16;
	c = a * b >> 7;
	d = c + a * ((255 - (((255 - a) * (255 - b)) >> 7) - c) >> 7);	//8
	yuv1[0][i] = d;
    }
}

void _lumamagick_freeze(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;

	if (b < 16)
	    c = 16;
	else
	    c = 255 - ((255 - a) * (255 - a)) / b;
	if (c < 16)
	    c = 16;

	yuv1[0][i] = c;
    }
}

void _lumamagick_unfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;

	if (a < 16)
	    c = 16;
	else
	    c = 255 - ((255 - b) * (255 - b)) / a;
	if (c < 16)
	    c = 16;

	yuv1[0][i] = c;
    }
}

void _lumamagick_hardlight(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;

	if (b < 128)
	    c = (a * b) >> 7;
	else
	    c = 255 - ((255 - b) * (255 - a) >> 7);
	if (c < 16)
	    c = 16;

	yuv1[0][i] = c;
    }
}
void _lumamagick_relativeaddlum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c, d;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	c = a >> 1;
	b = yuv2[0][i] * opacity_b;
	d = b >> 1;
	yuv1[0][i] = c + d;
    }
}

void _lumamagick_relativesublum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	yuv1[0][i] = (a - b + 255) >> 1;
    }
}

void _lumamagick_relativeadd(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b, c, d;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = len / 4;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	c = a >> 1;
	b = yuv2[0][i] * opacity_b;
	d = b >> 1;
	yuv1[0][i] = c + d;
    }
    for (i = 0; i < uv_len; i++) {
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

void _lumamagick_relativesub(uint8_t * yuv1[3], uint8_t * yuv2[3],
			     int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = len / 4;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	yuv1[0][i] = (a - b + 255) >> 1;
    }
    for (i = 0; i < uv_len; i++) {
	a = yuv1[1][i];
	b = yuv2[1][i];
	yuv1[1][i] = (a - b + 255) >> 1;
	a = yuv1[2][i];
	b = yuv2[2][i];
	yuv1[2][i] = (a - b + 255) >> 1;
    }

}
void _lumamagick_minsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (b < a)
	    yuv1[0][i] = (b - a + 255) >> 1;
	else
	    yuv1[0][i] = (a - b + 255) >> 1;
    }
}

void _lumamagick_maxsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (b > a)
	    yuv1[0][i] = (b - a + 255) >> 1;
	else
	    yuv1[0][i] = (a - b + 255) >> 1;
    }
}



void _lumamagick_addsubselect(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
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
	}
    }
}


void _lumamagick_maxselect(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (b > a)
	    yuv1[0][i] = b;
    }
}

void _lumamagick_minselect(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (b < a)
	    yuv1[0][i] = b;
    }
}

void _lumamagick_addtest(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			 int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	c = a + (2 * b) - 235;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
}
void _lumamagick_addtest2(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = len / 4;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	c = a + (2 * b) - 235;
	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
    for (i = 0; i < uv_len; i++) {
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
void _lumamagick_addtest4(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    unsigned int len = width * height;
    int c, a, b;
    double opacity_a = op_a / 100.0;
    double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	b = b - 255;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = yuv2[0][i] * opacity_b;
	if (b < 16)
	    b = 16;
	c = (a * a) / b;

	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }

}

void _lumamagick_selectmin(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (a > b) {
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }


    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (b < a) {
	    yuv1[0][i] = b;
	}
    }
}

void _lumamagick_addsubselectlum(uint8_t * yuv1[3], uint8_t * yuv2[3],
				 int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;

    for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (b < a) {
	    a = yuv1[1][i];
	    b = yuv2[1][i];
	    c = (a + b) >> 1;
	    yuv1[1][i] = c;

	    a = yuv1[2][i];
	    b = yuv2[2][i];
	    c = (a + b) >> 1;
	    yuv2[2][i] = c;

	}
    }


    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
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


	}
    }
}


void _lumamagick_selectmax(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			   int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
  for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (b > a) {
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }



    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (b > a) {
	    yuv1[0][i] = b;
	}
    }
}
void _lumamagick_selectdiff(uint8_t * yuv1[3], uint8_t * yuv2[3],
			    int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;

    for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (a > b) {
	    /* FIXME? */
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }

    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a > b) {
	    /* FIXME? */
	    yuv1[0][i] = abs(yuv1[0][i] - yuv2[0][i]);
	}
    }

 



}
void _lumamagick_selectdiffneg(uint8_t * yuv1[3], uint8_t * yuv2[3],
			       int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;

    for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (a > b) {
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }


    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a > b) {
	    yuv1[0][i] = 235 - abs(235 - a - b);
	}
    }
}

void _lumamagick_selectfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3],
			      int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;

    for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (a > b) {
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
    }


    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a > b) {
	    if (b < 16)
		c = 16;
	    else
		c = 255 - ((255 - a) * (255 - a)) / b;
	    if (c < 16)
		c = 16;
	    yuv1[0][i] = c;
	}
    }
}
void _lumamagick_selectunfreeze(uint8_t * yuv1[3], uint8_t * yuv2[3],
				int width, int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    const unsigned int ulen = len / 4;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;

    for (i = 0; i < ulen; i++) {
	a = yuv1[0][(i<<2)] * opacity_a;
	b = yuv2[0][(i<<2)] * opacity_b;
	if (a > b) {
	    yuv1[1][i] = yuv2[1][i];
	    yuv1[2][i] = yuv2[2][i];
	}
     }


    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
	if (a > b) {
	    if (a < 16)
		c = 16;
	    else
		c = 255 - ((255 - b) * (255 - b)) / a;
	    if (c < 16)
		c = 16;

	    yuv1[0][i] = c;
	}
    }
}


void _lumamagick_addtest3(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			  int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    const unsigned int uv_len = len / 4;
    /* FIXME: see old code ? */
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = (235 - yuv2[0][i]) * opacity_b;
	if (a < 16)
	    a = 16;
	if (b < 16)
	    b = yuv2[0][i] * opacity_b;
	if (b < 16)
	    b = 16;
	c = (a * a) / b;

	if (c < 16)
	    c = 16;
	if (c > 235)
	    c = 235;
	yuv1[0][i] = c;
    }
    for (i = 0; i < uv_len; i++) {
	//a = yuv1[1][i] * opacity_a;
	//b = (235 - yuv2[1][i]) * opacity_b;
	//if (b < 16) b = yuv2[1][i] * opacity_b;
	a = yuv1[1][i];
	b = 255 - yuv2[1][i];
	if (b < 16)
	    b = yuv2[1][i];

	if (b < 16)
	    b = 16;
	if (a < 16)
	    a = 16;

	c = (a >> 1) + (b >> 1);
	if (c < 16)
	    c = 16;
	if (c > 240)
	    c = 240;

	yuv1[1][i] = c;

	//a = yuv1[2][i] * opacity_a;
	//b = (235 - yuv2[2][i]) * opacity_b;
	//if (b < 16) b = yuv2[2][i] * opacity_b;

	a = yuv1[2][i];
	b = 255 - yuv2[2][i];
	if (b < 16)
	    b = yuv2[2][i];

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

	yuv1[2][i] = c;

    }

}


void _lumamagick_addlum(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int op_a, int op_b)
{
    unsigned int i;
    const unsigned int len = width * height;
    int c, a, b;
    const double opacity_a = op_a / 100.0;
    const double opacity_b = op_b / 100.0;
    for (i = 0; i < len; i++) {
	a = yuv1[0][i] * opacity_a;
	b = yuv2[0][i] * opacity_b;
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
	yuv1[0][i] = c;

    }
}


void lumamagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		     int height, int n, int op_a, int op_b)
{
    switch (n) {
    case VJ_EFFECT_BLEND_ADDDISTORT:
	_lumamagick_add_distorted(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_SUBDISTORT:
	_lumamagick_sub_distorted(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MULTIPLY:
	_lumamagick_multiply(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_DIVIDE:
	_lumamagick_divide(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDITIVE:
	_lumamagick_additive(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_SUBSTRACTIVE:
	_lumamagick_substractive(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_SOFTBURN:
	_lumamagick_softburn(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_INVERSEBURN:
	_lumamagick_inverseburn(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_COLORDODGE:
	_lumamagick_colordodge(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MULSUB:
	_lumamagick_mulsub(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_LIGHTEN:
	_lumamagick_lighten(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_DIFFERENCE:
	_lumamagick_difference(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_DIFFNEGATE:
	_lumamagick_diffnegate(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_EXCLUSIVE:
	_lumamagick_exclusive(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_BASECOLOR:
	_lumamagick_basecolor(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_FREEZE:
	_lumamagick_freeze(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_UNFREEZE:
	_lumamagick_unfreeze(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_HARDLIGHT:
	_lumamagick_hardlight(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELADD:
	_lumamagick_relativeadd(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELSUB:
	_lumamagick_relativesub(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MAXSEL:
	_lumamagick_maxselect(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MINSEL:
	_lumamagick_minselect(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELADDLUM:
	_lumamagick_relativeaddlum(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_RELSUBLUM:
	_lumamagick_relativesublum(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MINSUBSEL:
	_lumamagick_minsubselect(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_MAXSUBSEL:
	_lumamagick_maxsubselect(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDSUBSEL:
	_lumamagick_addsubselect(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDAVG:
	_lumamagick_addtest(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDTEST2:
	_lumamagick_addtest2(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDTEST4:
	_lumamagick_addtest3(yuv1, yuv2, width, height, op_a, op_b);
	break;
    case VJ_EFFECT_BLEND_ADDTEST3:
	_lumamagick_addtest4(yuv1, yuv2, width, height, op_a, op_b);
	break;
    }
}
