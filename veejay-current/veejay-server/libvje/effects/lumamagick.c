/* 
 * Linux VeeJay
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

/* 7 ,14, 24, 25, 26 */
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvje/internal.h>
#include <libvjmem/vjmem.h>
#include "lumamagick.h"
#include "common.h"
/* 04/01/03: added transparency parameters for frame a and frame b in each function */

vj_effect *lumamagick_init(int width, int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 3;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	//ve->param_description = (char**)vj_calloc(sizeof(char)* ve->num_params);
	ve->defaults[0] = 0;
	ve->defaults[1] = 100;
	ve->defaults[2] = 100;
	ve->parallel = 1;
	ve->description = "Luma Magick";
	ve->limits[0][0] = 0;
	ve->limits[1][0] = VJ_NUM_BLEND_EFFECTS;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 200;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 200;
	ve->sub_format = -1;
	ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Opacity A", "Opacity B" );
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
	                          "Additive", "Subtractive","Multiply",
	                          "Divide","Lighten","Hardlight",
	                          "Difference","Difference Negate",
	                          "Exclusive","Base","Freeze",
	                          "Unfreeze","Relative Add","Relative Subtract",
	                          "Max select", "Min select", "Relative Luma Add",
	                          "Relative Luma Subtract", "Min Subselect",
	                          "Max Subselect", "Add Subselect", "Add Average",
	                          "Experimental 2","Experimental 3",
	                          "Experimental 4", "Multisub", "Softburn",
	                          "Inverse Burn", "Dodge", "Distorted Add",
	                          "Distorted Subtract", "Experimental 5",
	                          "Negate Difference", "Add Luma",
	                          "Experimental 6", "Experimental 7");

	return ve;
}

/* 33 = illumination . it increases or decreases light intensity and associate color pixel*/

static void lumamagick_adddistorted(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const int opacity_b = op_b * 0.01;
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a + b;
		Y[i] = CLAMP_Y(c);
	}
	for (i = 0; i < uv_len; i++)
	{
		a = Cb[i];
		b = Cb2[i];
		c = a + b;
		Cb[i] = CLAMP_UV(c);

		a = Cr[i];
		b = Cr2[i];
		c = a + b;
		Cr[i] = CLAMP_UV(c);
	}
}

/*FIXME : overlay magic add distorted */
static void lumamagick_add_distorted(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	uint8_t y1, y2, y3, cb, cr, cs;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y3 = y1 + y2;
		y3 *= opacity_a;
		y3 += y2;
		Y[i] = y3;
	}
	for (i = 0; i < uv_len; i++)
	{
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

static void lumamagick_subdistorted(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	uint8_t y1, y2, cb, cr;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y1 -= y2;
		Y[i] = y1;
	}
	for (i = 0; i < uv_len; i++)
	{
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

static void lumamagick_sub_distorted(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	uint8_t y1, y2, cb, cr, y3, cs;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y3 = y1 - y2;
		y3 *= opacity_a;
		y3 -= y2;
		Y[i] = y3;
	}
	for (i = 0; i < uv_len; i++)
	{
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

static void lumamagick_multiply(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	uint8_t y1, y2;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y1 = (y1 * y2) >> 8;
		Y[i] = y1;
	}
}

static void lumamagick_divide(VJFrame *frame, VJFrame *frame2 , int op_a, int op_b)
{
	unsigned int i;
	const unsigned int len = frame->width * frame->height;
	int b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		b = (Y[i] * opacity_a) * (Y[i] * opacity_a);
		c = 0xff - (Y2[i] * opacity_b);
		if (c > pixel_Y_lo_)
			Y[i] = b/c;
	}
}

static void lumamagick_negdiv(VJFrame *frame, VJFrame *frame2, int op_a ,int op_b )
{
	unsigned int i;
	unsigned int len = frame->width * frame->height;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	int b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;

	for (i = 0; i < len; i++) 
	{
		b = (Y[i] * opacity_a) * (Y[i] * opacity_a);
		c = 0xff - (Y2[i] * opacity_b);
		if (c > pixel_Y_lo_)
			Y[i] = b/c;
		else
			Y[i] = c;
	}
}

static void lumamagick_additive(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a=0;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++) 
	{
		a = (Y[i] * opacity_a) + ((2 * (Y2[i] * opacity_b)) - 0xff);
		Y[i] = CLAMP_Y(a);
	}
}

static void lumamagick_substractive(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++) 
	{
		a = (Y[i] * opacity_a) + ((Y2[i] - 0xff) * opacity_b);
		Y[i] = CLAMP_Y(a);
	}
}

static void lumamagick_softburn(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;

		if (a + b < 0xff) 
		{
			if (a > pixel_Y_hi_)
				c = a;
			else
				c = (b >> 7) / (0xff - a);
		} else 
		{
			if (b <= pixel_Y_lo_)
				b = 0xff;
			c = 0xff - (((0xff - a) >> 7) / b);
		}
		Y[i] = c;
	}
}

static void lumamagick_inverseburn(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a  * 0.01;
	const double opacity_b = op_b *  0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (a <= pixel_Y_lo_)
			c = pixel_Y_lo_;
		else
			c = 0xff - (((0xff - b) >> 8) / a);
		Y[i] = c;
	}
}

static void lumamagick_colordodge(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c,d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (a >= pixel_Y_hi_)
			c = pixel_Y_hi_;
		else
		{
			d = pixel_Y_hi_ - a;
			if( d <= pixel_Y_lo_ )
				d = 1;
			c = (b >> 8) / d;
		}
		Y[i] = c;
	}
}

static void lumamagick_mulsub(VJFrame *frame, VJFrame *frame2 , int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = (pixel_Y_hi_ - Y2[i]) * opacity_b;
		if (b <= pixel_Y_lo_)
			b = 1;
		c = a / b;
		Y[i] = c;
	}
}

static void lumamagick_lighten(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (a > b)
			c = a;
		else
			c = b;
		Y[i] = c;
	}
}

static void lumamagick_difference(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		Y[i] = abs(a - b);
	}
}

static void lumamagick_diffnegate(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = (0xff - Y[i]) * opacity_a;
		b = Y2[i] * opacity_b;
		Y[i] = 0xff - abs(a - b);
	}
}

static void lumamagick_exclusive(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a + b - ((a * b) >> 8);
		//      Y[i] = Y[i] + Y2[i] -
		//      ((Y[i]*Y2[i])>>8);   //or try 7
		Y[i] = c;
	}
}

static void lumamagick_basecolor(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c, d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a * b >> 7;
		d = c + a * ((0xff - (((0xff - a) * (0xff - b)) >> 8) - c) >> 8);	//8
		Y[i] = d;
	}
}

static void lumamagick_freeze(VJFrame *frame, VJFrame *frame2 , int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;

		if (b <= pixel_Y_lo_)
			c = pixel_Y_lo_;
		else
			c = 0xff - ((0xff - a) * (0xff - a)) / b;

		Y[i] = c;
	}
}

static void lumamagick_unfreeze(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;

		if (a <= pixel_Y_lo_)
			c = pixel_Y_lo_;
		else
			c = 0xff - ((0xff - b) * (0xff - b)) / a;

		Y[i] = c;
	}
}

static void lumamagick_hardlight(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;

		if (b < 128)
			c = (a * b) >> 7;
		else
			c = 0xff - ((0xff - b) * (0xff - a) >> 7);

		Y[i] = c;
	}
}

static void lumamagick_relativeaddlum(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	int a, b, c, d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		c = a >> 1;
		b = Y2[i] * opacity_b;
		d = b >> 1;
		Y[i] = c + d;
	}
}

static void lumamagick_relativesublum(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		Y[i] = (a - b + 0xff) >> 1;
	}
}

static void lumamagick_relativeadd(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c, d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);

	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		c = a >> 1;
		b = Y2[i] * opacity_b;
		d = b >> 1;
		Y[i] = c + d;
	}
	for (i = 0; i < uv_len; i++)
	{
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

static void lumamagick_relativesub(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		Y[i] = (a - b + 0xff) >> 1;
	}
	for (i = 0; i < uv_len; i++)
	{
		a = Cb[i];
		b = Cb2[i];
		Cb[i] = (a - b + 0xff) >> 1;
		a = Cr[i];
		b = Cr2[i];
		Cr[i] = (a - b + 0xff) >> 1;
	}
}

static void lumamagick_minsubselect(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (b < a)
			Y[i] = (b - a + 0xff) >> 1;
		else
			Y[i] = (a - b + 0xff) >> 1;
	}
}

static void lumamagick_maxsubselect(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
	a = Y[i] * opacity_a;
	b = Y2[i] * opacity_b;
	if (b > a)
		Y[i] = (b - a + 0xff) >> 1;
	else
		Y[i] = (a - b + 0xff) >> 1;
	}
}

static void lumamagick_addsubselect(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int c, a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;

		if (b < a)
		{
			c = (a + b) >> 1;
			Y[i] = c;
		}
	}
}

static void lumamagick_maxselect(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (b > a)
			Y[i] = b;
	}
}

static void lumamagick_minselect(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (b < a)
			Y[i] = b;
	}
}

static void lumamagick_addtest(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int c, a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a + ((2 * b) - 0xff);
		Y[i] = CLAMP_Y(c);
	}
}

static void lumamagick_addtest2(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int c, a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a + ((2 * b) - 0xff);
		Y[i] = CLAMP_Y(c);
	}
	for (i = 0; i < uv_len; i++)
	{
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

static void lumamagick_addtest4(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int c, a, b;
	double opacity_a = op_a * 0.01;
	double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		b = b - 0xff;
		if (b <= pixel_Y_lo_)
			b = 0xff;
		c = (a * a) / b;

		Y[i] = CLAMP_Y(c);
	}
}

/*
void lumamagick_selectmin(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		a = Y[(i<<2)] * opacity_a;
		b = Y2[(i<<2)] * opacity_b;
		if (a > b)
		{
			Cb[i] = Cb2[i];
			Cr[i] = Cr2[i];
		}
	}

	for (i = 0; i < uv_len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (b < a)
		{
			Y[i] = b;
		}
	}
}
*/
 
static void lumamagick_addtest3(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int c, a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	const unsigned int uv_len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = (0xff - Y2[i]) * opacity_b;
		if (b <= pixel_Y_lo_)
			b = 1;
		c = (a * a) / b;

		Y[i] = c;
	}
	for (i = 0; i < uv_len; i++)
	{
		//a = Cb[i] * opacity_a;
		//b = (pixel_Y_hi_ - Cb2[i]) * opacity_b;
		//if (b < pixel_Y_lo_) b = Cb2[i] * opacity_b;
		a = Cb[i];
		b = 0xff - Cb2[i];
		if (b < pixel_U_lo_)
			b = Cb2[i];

		c = (a >> 1) + (b >> 1);

		Cb[i] = c;

		//a = Cr[i] * opacity_a;
		//b = (pixel_Y_hi_ - Cr2[i]) * opacity_b;
		//if (b < pixel_Y_lo_) b = Cr2[i] * opacity_b;

		a = Cr[i];
		b = 0xff - Cr2[i];
		if (b < pixel_U_lo_)
			b = Cr2[i];


		c = (a >> 1) + (b >> 1);

		Cr[i] = c;
	}
}

static void lumamagick_addlum(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int c, a, b;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const unsigned int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		if (b > pixel_Y_hi_)
			b = pixel_Y_hi_;

		if((0xff - b) > 0){
			c = (a * a) / (0xff - b);
		} else {
			c = (a * a) / 0xff;
		}
		Y[i] = c;
	}
}

void lumamagic_apply(VJFrame *frame, VJFrame *frame2, int n, int op_a, int op_b)
{
	switch (n)
	{
		case VJ_EFFECT_BLEND_ADDDISTORT:
			lumamagick_add_distorted(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_SUBDISTORT:
			lumamagick_sub_distorted(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_MULTIPLY:
			lumamagick_multiply(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_DIVIDE:
			lumamagick_divide(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDITIVE:
			lumamagick_additive(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_SUBSTRACTIVE:
			lumamagick_substractive(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_SOFTBURN:
			lumamagick_softburn(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_INVERSEBURN:
			lumamagick_inverseburn(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_COLORDODGE:
			lumamagick_colordodge(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_MULSUB:
			lumamagick_mulsub(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_LIGHTEN:
			lumamagick_lighten(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_DIFFERENCE:
			lumamagick_difference(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_DIFFNEGATE:
			lumamagick_diffnegate(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_EXCLUSIVE:
			lumamagick_exclusive(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_BASECOLOR:
			lumamagick_basecolor(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_HARDLIGHT:
			lumamagick_hardlight(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_RELADD:
			lumamagick_relativeadd(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_RELSUB:
			lumamagick_relativesub(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_MAXSEL:
			lumamagick_maxselect(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_MINSEL:
			lumamagick_minselect(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_RELADDLUM:
			lumamagick_relativeaddlum(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_RELSUBLUM:
			lumamagick_relativesublum(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_MINSUBSEL:
			lumamagick_minsubselect(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_MAXSUBSEL:
			lumamagick_maxsubselect(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDSUBSEL:
			lumamagick_addsubselect(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDAVG:
			lumamagick_addtest(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDTEST2:
			lumamagick_addtest2(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDTEST4:
			lumamagick_addtest3(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDTEST3:
			lumamagick_addtest4(frame, frame2, op_a, op_b);
			break;
		case VJ_EFFECT_BLEND_ADDTEST6:
			lumamagick_adddistorted(frame,frame2, op_a,op_b);
			break;
		case VJ_EFFECT_BLEND_ADDTEST7:
			lumamagick_subdistorted(frame,frame2, op_a,op_b);
			break;
		case VJ_EFFECT_BLEND_FREEZE:
			lumamagick_freeze(frame,frame2, op_a,op_b);
			break;
		case VJ_EFFECT_BLEND_UNFREEZE:
			lumamagick_unfreeze(frame,frame2, op_a,op_b);
			break;
		case VJ_EFFECT_BLEND_ADDLUM:
			lumamagick_addlum(frame,frame2, op_a,op_b);
			break;
		case VJ_EFFECT_BLEND_NEGDIV:
			lumamagick_negdiv(frame,frame2, op_a,op_b);
			break;
	}
}
