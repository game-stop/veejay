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
#include "common.h"
#include <libvje/internal.h>
#include <veejaycore/vjmem.h>
#include "lumamagick.h"
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
	ve->limits[1][0] = VJ_EFFECT_BLEND_COUNT;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 200;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 200;
	ve->sub_format = -1;
	ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Opacity A", "Opacity B" );
	ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, VJ_EFFECT_BLEND_STRINGS);

	return ve;
}

static void lumamagick_adddistorted(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * alpha) >> 8;
        int y2 = (Y2[i] * beta)  >> 8;
        int y_out = y1 + y2;
        y_out = y_out < 0 ? 0 : (y_out > 255 ? 255 : y_out);
        Y[i] = (uint8_t)y_out;
    }

#pragma omp simd
    for (int i = 0; i < uv_len; i++)
    {
        int cb1 = ((Cb[i] - 128) * alpha) >> 8;
        int cb2 = ((Cb2[i] - 128) * beta)  >> 8;
        int cb_out = cb1 + cb2;
        cb_out = cb_out < -128 ? -128 : (cb_out > 127 ? 127 : cb_out);
        Cb[i] = (uint8_t)(cb_out + 128);

        int cr1 = ((Cr[i] - 128) * alpha) >> 8;
        int cr2 = ((Cr2[i] - 128) * beta)  >> 8;
        int cr_out = cr1 + cr2;
        cr_out = cr_out < -128 ? -128 : (cr_out > 127 ? 127 : cr_out);
        Cr[i] = (uint8_t)(cr_out + 128);
    }
}

static void lumamagick_add_distorted(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	uint8_t y1, y2, y3, cb, cr, cs;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y3 = y1 + y2;
		y3 *= opacity_a;
		y3 += y2;
		Y[i] = y3;
	}
#pragma omp simd
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
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y1 -= y2;
		Y[i] = y1;
	}

#pragma omp simd
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
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		y1 = Y[i] * opacity_a;
		y2 = Y2[i] * opacity_b;
		y3 = y1 - y2;
		y3 *= opacity_a;
		y3 -= y2;
		Y[i] = y3;
	}
#pragma omp simd
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
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * alpha) >> 8;
        int y2 = (Y2[i] * beta) >> 8;
        int mult = (y1 * y2) >> 8;
        Y[i] = (uint8_t)mult;
    }

#pragma omp simd
    for (int i = 0; i < uv_len; i++)
    {
        int cb1 = ((Cb[i] - 128) * alpha) >> 8;
        int cb2 = ((Cb2[i] - 128) * beta) >> 8;
        int cb_mult = (cb1 * cb2) >> 7;
        Cb[i] = (uint8_t)(128 + (cb_mult > 127 ? 127 : (cb_mult < -128 ? -128 : cb_mult)));

        int cr1 = ((Cr[i] - 128) * alpha) >> 8;
        int cr2 = ((Cr2[i] - 128) * beta) >> 8;
        int cr_mult = (cr1 * cr2) >> 7;
        Cr[i] = (uint8_t)(128 + (cr_mult > 127 ? 127 : (cr_mult < -128 ? -128 : cr_mult)));
    }
}

static void lumamagick_divide(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * alpha) >> 8;
        int y2 = (Y2[i] * beta) >> 8;
        int denom = 255 - y2;

        int result = (y1 * y1) / (denom + (denom <= pixel_Y_lo_));
        int mask = -(denom > pixel_Y_lo_);

        Y[i] = (uint8_t)((result & mask) | (Y[i] & ~mask));
    }
}

static void lumamagick_negdiv(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * alpha) >> 8;
        int y2 = (Y2[i] * beta)  >> 8;

        int diff = y1 - y2;
        int mask = diff >> 31;
        int abs_diff = (diff + mask) ^ mask;

        int negdiff = 255 - abs_diff;
        Y[i] = (uint8_t)negdiff;
    }

#pragma omp simd
    for (int i = 0; i < uv_len; i++)
    {
        int cb1 = ((Cb[i] - 128) * alpha) >> 8;
        int cb2 = ((Cb2[i] - 128) * beta)  >> 8;
        Cb[i] = (uint8_t)(128 + ((cb1 + cb2) - ((cb1 * cb2) >> 7)));

        int cr1 = ((Cr[i] - 128) * alpha) >> 8;
        int cr2 = ((Cr2[i] - 128) * beta)  >> 8;
        Cr[i] = (uint8_t)(128 + ((cr1 + cr2) - ((cr1 * cr2) >> 7)));
    }
}

static void lumamagick_screen(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);
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

        int screen = 255 - (((255 - a) * (255 - b)) >> 8);
        Y[i] = (uint8_t)((screen * alpha + a * (255 - alpha)) >> 8);
    }

#pragma omp simd
    for (int i = 0; i < uv_len; i++)
    {
        int cb = Cb[i];
        int cb2 = Cb2[i];
        Cb[i] = (uint8_t)((cb * (255 - alpha) + cb2 * alpha) >> 8);

        int cr = Cr[i];
        int cr2 = Cr2[i];
        Cr[i] = (uint8_t)((cr * (255 - alpha) + cr2 * alpha) >> 8);
    }
}

static void lumamagick_additive(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * op_a) >> 8;
        int y2 = (Y2[i] * op_b) >> 8;
        int y_out = y1 + y2;
        y_out = y_out < 0 ? 0 : (y_out > 255 ? 255 : y_out);
        Y[i] = (uint8_t)y_out;

        int cb1 = ((Cb[i] - 128) * op_a) >> 8;
        int cb2 = ((Cb2[i] - 128) * op_b) >> 8;
        int cb_out = cb1 + cb2;
        cb_out = cb_out < -128 ? -128 : (cb_out > 127 ? 127 : cb_out);
        Cb[i] = (uint8_t)(cb_out + 128);

        int cr1 = ((Cr[i] - 128) * op_a) >> 8;
        int cr2 = ((Cr2[i] - 128) * op_b) >> 8;
        int cr_out = cr1 + cr2;
        cr_out = cr_out < -128 ? -128 : (cr_out > 127 ? 127 : cr_out);
        Cr[i] = (uint8_t)(cr_out + 128);
    }
}

static void lumamagick_substractive(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * alpha) >> 8;
        int y2 = (Y2[i] * beta)  >> 8;

        int denom = 255 - y2;
        denom |= (denom == 0);

        int dodge = y1 + ((y1 * y2) / denom);
        if (dodge > 255) dodge = 255;
        Y[i] = (uint8_t)dodge;
    }

#pragma omp simd
	for (int i = 0; i < uv_len; i++)
	{
		int cb1 = ((Cb[i] - 128) * alpha) >> 8;
		int cb2 = ((Cb2[i] - 128) * beta)  >> 8;

		int denom_cb = 127 - cb2;
		denom_cb = denom_cb ? denom_cb : 1;
		int cb_dodge = cb1 + ((cb1 * cb2) / denom_cb);
		Cb[i] = (uint8_t)(128 + (cb_dodge > 127 ? 127 : (cb_dodge < -128 ? -128 : cb_dodge)));

		int cr1 = ((Cr[i] - 128) * alpha) >> 8;
		int cr2 = ((Cr2[i] - 128) * beta)  >> 8;

		int denom_cr = 127 - cr2;
		denom_cr = denom_cr ? denom_cr : 1;
		int cr_dodge = cr1 + ((cr1 * cr2) / denom_cr);
		Cr[i] = (uint8_t)(128 + (cr_dodge > 127 ? 127 : (cr_dodge < -128 ? -128 : cr_dodge)));
	}
}

static void lumamagick_mulsub(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int a = Y[i] * alpha;
        int b = pixel_Y_hi_ - Y2[i];
        b = b | 1;

        int y_out = a / b;
        y_out = y_out > 255 ? 255 : y_out;
        Y[i] = (uint8_t)y_out;

        int cb = ((Cb[i] - 128) * alpha + (Cb2[i] - 128) * beta) >> 8;
        int cr = ((Cr[i] - 128) * alpha + (Cr2[i] - 128) * beta) >> 8;

        Cb[i] = (uint8_t)(cb + 128);
        Cr[i] = (uint8_t)(cr + 128);
    }
}

static void lumamagick_lighten(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		Y[i] = abs(a - b);
	}
}

static void lumamagick_diffnegate(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = ((255 - Y[i]) * alpha) >> 8;
        int y2 = (Y2[i] * beta) >> 8;
        int diff = y1 - y2;
        int mask = diff >> 31;
        int abs_diff = (diff + mask) ^ mask;
        int out = 255 - abs_diff;
        out = out < 0 ? 0 : (out > 255 ? 255 : out);
        Y[i] = (uint8_t)out;
    }

#pragma omp simd
    for (int i = 0; i < uv_len; i++)
    {
        int cb1 = (((Cb[i] - 128) * alpha) >> 8);
        int cb2 = (((Cb2[i] - 128) * beta) >> 8);
        int cb_diff = cb1 - cb2;
        int cb_mask = cb_diff >> 31;
        int cb_abs = (cb_diff + cb_mask) ^ cb_mask;
        int cb_out = 0 - cb_abs;
        cb_out = cb_out < -128 ? -128 : (cb_out > 127 ? 127 : cb_out);
        Cb[i] = (uint8_t)(cb_out + 128);

        int cr1 = (((Cr[i] - 128) * alpha) >> 8);
        int cr2 = (((Cr2[i] - 128) * beta) >> 8);
        int cr_diff = cr1 - cr2;
        int cr_mask = cr_diff >> 31;
        int cr_abs = (cr_diff + cr_mask) ^ cr_mask;
        int cr_out = 0 - cr_abs;
        cr_out = cr_out < -128 ? -128 : (cr_out > 127 ? 127 : cr_out);
        Cr[i] = (uint8_t)(cr_out + 128);
    }
}

static void lumamagick_exclusive(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1  = (Y[i]  * alpha) >> 8;
        int y2  = (Y2[i] * beta)  >> 8;
        int cb1 = ((Cb[i]  - 128) * alpha) >> 8;
        int cb2 = ((Cb2[i] - 128) * beta)  >> 8;
        int cr1 = ((Cr[i]  - 128) * alpha) >> 8;
        int cr2 = ((Cr2[i] - 128) * beta)  >> 8;

        int y_ex  = y1  + y2  - ((y1  * y2)  >> 7);
        int cb_ex = cb1 + cb2 - ((cb1 * cb2) >> 7);
        int cr_ex = cr1 + cr2 - ((cr1 * cr2) >> 7);

        Y[i]  = y_ex  > 255 ? 255 : (y_ex  < 0 ? 0 : y_ex);
        Cb[i] = (cb_ex < -128 ? -128 : (cb_ex > 127 ? 127 : cb_ex)) + 128;
        Cr[i] = (cr_ex < -128 ? -128 : (cr_ex > 127 ? 127 : cr_ex)) + 128;
    }
}

static void lumamagick_basecolor(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c, d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a * b >> 7;
		d = c + a * ((0xff - (((0xff - a) * (0xff - b)) >> 8) - c) >> 8);	//8
		Y[i] = d;
	}
}

static void lumamagick_freeze(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = Y[i];
        int y2 = Y2[i];

        int freeze = y2 + ((255 - y2) * y1 >> 8);
        int mask_lo = (freeze - pixel_Y_lo_) >> 31;
        Y[i] = (uint8_t)((freeze & ~mask_lo) | (pixel_Y_lo_ & mask_lo));

        int cb = ((Cb[i] - 128) * alpha + (Cb2[i] - 128) * beta) >> 8;
        int cr = ((Cr[i] - 128) * alpha + (Cr2[i] - 128) * beta) >> 8;

        Cb[i] = (uint8_t)(cb + 128);
        Cr[i] = (uint8_t)(cr + 128);
    }
}

static void lumamagick_unfreeze(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = Y[i] | 1;
        int y2 = Y2[i];

        int unfreeze = 255 - ((255 - y2) * (255 - y2) / y1);
        int mask_lo = (unfreeze - pixel_Y_lo_) >> 31;
        Y[i] = (uint8_t)((unfreeze & ~mask_lo) | (pixel_Y_lo_ & mask_lo));

        int cb = ((Cb[i] - 128) * alpha + (Cb2[i] - 128) * beta) >> 8;
        int cr = ((Cr[i] - 128) * alpha + (Cr2[i] - 128) * beta) >> 8;

        Cb[i] = (uint8_t)(cb + 128);
        Cr[i] = (uint8_t)(cr + 128);
    }
}

static void lumamagick_hardlight(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];

	int a, b, c;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
	int a, b, c, d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
#pragma omp simd
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
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * op_a) >> 8;
        int y2 = (Y2[i] * op_b) >> 8;
        int y_out = y1 - y2 + 128;
        y_out = y_out < 0 ? 0 : (y_out > 255 ? 255 : y_out);
        Y[i] = (uint8_t)y_out;

        int cb1 = ((Cb[i] - 128) * op_a) >> 8;
        int cb2 = ((Cb2[i] - 128) * op_b) >> 8;
        int cb_out = cb1 - cb2;
        cb_out = cb_out < -128 ? -128 : (cb_out > 127 ? 127 : cb_out);
        Cb[i] = (uint8_t)(cb_out + 128);

        int cr1 = ((Cr[i] - 128) * op_a) >> 8;
        int cr2 = ((Cr2[i] - 128) * op_b) >> 8;
        int cr_out = cr1 - cr2;
        cr_out = cr_out < -128 ? -128 : (cr_out > 127 ? 127 : cr_out);
        Cr[i] = (uint8_t)(cr_out + 128);
    }
}

static void lumamagick_relativeadd(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
	unsigned int i;
	int a, b, c, d;
	const double opacity_a = op_a * 0.01;
	const double opacity_b = op_b * 0.01;
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);

	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		c = a >> 1;
		b = Y2[i] * opacity_b;
		d = b >> 1;
		Y[i] = c + d;
	}
#pragma omp simd
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
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		Y[i] = (a - b + 0xff) >> 1;
	}
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];

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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];

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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];

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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
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
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = Y2[i] * opacity_b;
		c = a + ((2 * b) - 0xff);
		Y[i] = CLAMP_Y(c);
	}
#pragma omp simd
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
	const int len = frame->len;
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Y2 = frame2->data[0];

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
	const int len = frame->len;
	const int uv_len = frame->uv_len;
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
	const int len = frame->len;
	const int uv_len = (frame->ssm ? len : frame->uv_len);
	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2= frame2->data[1];
	uint8_t *restrict Cr2= frame2->data[2];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i] * opacity_a;
		b = (0xff - Y2[i]) * opacity_b;
		if (b <= pixel_Y_lo_)
			b = 1;
		c = (a * a) / b;

		Y[i] = c;
	}
#pragma omp simd
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
    const int len = frame->len;
    const int uv_len = (frame->ssm ? len : frame->uv_len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int alpha = op_a;
    const int beta  = op_b;

#pragma omp simd
    for (int i = 0; i < len; i++)
    {
        int y1 = (Y[i] * alpha) >> 8;
        int y2 = (Y2[i] * beta)  >> 8;

        int denom = 0xff - y2;
        denom = denom ? denom : 1;

        int out = (y1 * y1) / denom;
        Y[i] = (uint8_t)(out > 255 ? 255 : out);
    }

#pragma omp simd
    for (int i = 0; i < uv_len; i++)
    {
        int cb = (((Cb[i] - 128) * alpha) + ((Cb2[i] - 128) * beta)) >> 8;
        int cr = (((Cr[i] - 128) * alpha) + ((Cr2[i] - 128) * beta)) >> 8;

        Cb[i] = (uint8_t)(128 + (cb > 127 ? 127 : (cb < -128 ? -128 : cb)));
        Cr[i] = (uint8_t)(128 + (cr > 127 ? 127 : (cr < -128 ? -128 : cr)));
    }
}
void lumamagick_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int n = args[0];
    int op_a = args[1];
    int op_b = args[2];

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
		case VJ_EFFECT_BLEND_SCREEN:
			lumamagick_screen(frame,frame2,op_a,op_b);
			break;
	}
}
