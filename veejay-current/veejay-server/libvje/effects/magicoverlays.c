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

#include "common.h"
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "magicoverlays.h"

vj_effect *overlaymagic_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 0;
	ve->defaults[1] = 0;
	ve->description = "Overlay Magic";
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 32;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1; // clear chroma or keep
	ve->extra_frame = 1;
	ve->sub_format = -1;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Keep or clear color" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
	                          "Additive", "Subtractive","Multiply","Divide",
	                          "Lighten","Hardlight", "Difference",
	                          "Difference Negate","Exclusive","Base","Freeze",
	                          "Unfreeze","Relative Add","Relative Subtract",
	                          "Max select", "Min select", "Relative Luma Add",
	                          "Relative Luma Subtract", "Min Subselect",
	                          "Max Subselect", "Add Subselect", "Add Average",
	                          "Experimental 1","Experimental 2", "Experimental 3",
	                          "Multisub", "Softburn", "Inverse Burn", "Dodge",
	                          "Distorted Add", "Distorted Subtract",
	                          "Experimental 4", "Negation Divide");

	return ve;
}

/* FIXME rename methods in lumamagick and chromamagick */
void overlaymagic_adddistorted(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;

	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = CLAMP_Y(Y[i] + Y2[i]);
	}
}

void overlaymagic_add_distorted(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = CLAMP_Y(Y[i] + Y2[i]);
	}
}

void overlaymagic_subdistorted(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = CLAMP_Y(Y[i] - Y2[i]);
	}
}

void overlaymagic_sub_distorted(VJFrame *frame, VJFrame *frame2 )
{
	unsigned int i ;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = Y2[i] - Y[i];
	}
}

void overlaymagic_multiply(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
		Y[i] = (Y[i] * Y2[i]) >> 8;
}

void overlaymagic_simpledivide(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		if(Y2[i] > pixel_Y_lo_ )
			Y[i] = Y[i] / Y2[i];
	}
}

void overlaymagic_divide(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	int a, b, c;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		b = Y[i] * Y[i];
		c = 255 - Y2[i];
		if (c == 0)
			c = 1;
		a = b / c;
		Y[i] = a;
	}
}

void overlaymagic_additive(VJFrame *frame, VJFrame *frame2 )
{
	int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	int i;
#pragma omp simd
	for( i = 0; i < len ; i ++ )
	{
		Y[len] = CLAMP_Y(Y[i] + (2 * Y2[i]) - 255);
	}
}


void overlaymagic_substractive(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
		Y[i] = CLAMP_Y( Y[i] - Y2[i] );
}

void overlaymagic_softburn(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int a, b, c;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];

		if ( (a + b) <= pixel_Y_hi_)
		{
			if (a == pixel_Y_hi_)
			c = a;
			else
			c = (b >> 7) / (256 - a);
		} else
		{
			if (b <= pixel_Y_lo_)
			{
				b = 255;
			}
			c = 255 - (((255 - a) >> 7) / b);
		}
		Y[i] = c;
	}
}

void overlaymagic_inverseburn(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	int a, b, c;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];
		if (a <= pixel_Y_lo_)
			c = pixel_Y_lo_;
		else
			c = 255 - (((255 - b) >> 8) / a);
		Y[i] = CLAMP_Y(c);
	}
}

void overlaymagic_colordodge(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int a, b, c;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];
		if (a >= pixel_Y_hi_)

			c = pixel_Y_hi_;
		else
			c = (b >> 8) / (256 - a);

		if (c >= pixel_Y_hi_)
			c = pixel_Y_hi_;
		Y[i] = c;
	}
}

void overlaymagic_mulsub(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	int a, b;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = 255 - Y2[i];
		if (b > pixel_Y_lo_)
			Y[i] = a / b;
	}
}

void overlaymagic_lighten(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = Y[i] > Y2[i] ? Y[i] : Y2[i];
	}
}

void overlaymagic_difference(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = abs(Y[i] - Y2[i]);
	}
}

void overlaymagic_diffnegate(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = 255 - abs((255 - Y[i]) - Y2[i]);
	}
}

void overlaymagic_exclusive(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int c;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		c = Y[i] + (2 * Y2[i]) - 255;
		Y[i] = CLAMP_Y(c - (( Y[i] * Y2[i] ) >> 8 ));
	}
}

void overlaymagic_basecolor(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	int a, b, c, d;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];
		c = a * b >> 8;
		d = c + a * ((255 - (((255 - a) * (255 - b)) >> 8) - c) >> 8);	//8
		Y[i] = CLAMP_Y(d);
	}
}

void overlaymagic_freeze(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int a, b;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];
		if ( b > pixel_Y_lo_ )
			Y[i] = CLAMP_Y(255 - ((( 255 - a) * ( 255 - a )) / b));
	}
}

void overlaymagic_unfreeze(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int a, b;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];
		if( a > pixel_Y_lo_ )
			Y[i] = CLAMP_Y( 255 - ((( 255 - b ) * ( 255 - b )) / a));
	}
}

void overlaymagic_hardlight(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	int a, b, c;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];

		if (b < 128)
			c = (a * b) >> 7;
		else
			c = 255 - ((255 - b) * (255 - a) >> 7);
		Y[i] = c;
	}
}

void overlaymagic_relativeaddlum(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = CLAMP_Y( (Y[i]>>1) + (Y2[i] >> 1));
	}
}

void overlaymagic_relativesublum(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = (Y[i] - Y2[i] + 255) >> 1;
	}
}

void overlaymagic_relativeadd(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = (Y[i]>>1) + (Y2[i]>>1);
	}
}

void overlaymagic_relativesub(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		Y[i] = (Y[i] - Y2[i] + 255) >> 1;
	}
}

void overlaymagic_minsubselect(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
        Y[i] = (Y2[i] < Y[i] ? (Y2[i] - Y[i] + 255) >> 1 : (Y[i] - Y2[i] + 255) >> 1);
	}
}

void overlaymagic_maxsubselect(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
        Y[i] = (Y2[i] > Y[i] ? ( Y2[i] - Y[i] + 255 ) >> 1 : (Y[i] - Y2[i] + 255 ) >> 1 );
	}
}

void overlaymagic_addsubselect(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
        Y[i] = (Y2[i] < Y[i] ? (Y[i] + Y2[i])>>1 : Y[i] );
	}
}

void overlaymagic_maxselect(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd	
    for (i = 0; i < len; i++)
	{
        Y[i] = (Y2[i] > Y[i] ? Y2[i] : Y[i] );
	}
}

void overlaymagic_minselect(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
        Y[i] = (Y2[i] < Y[i] ? Y2[i] : Y[i]);
	}
}

void overlaymagic_addtest(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
        Y[i] = CLAMP_Y( Y[i] + (((2 * Y2[i]) - 255 ) >> 1) );
	}
}

void overlaymagic_addtest2(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd	
    for (i = 0; i < len; i++)
	{
		Y[i] = CLAMP_Y( Y[i] + ( 2 * Y2[i] ) - 255 );
	}
}

void overlaymagic_addtest4(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
	int a, b;
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		a = Y[i];
		b = Y2[i];
		b = b - 255;
		if (b <= pixel_Y_lo_)
			Y[i] = a;
		else
			Y[i] = (a * a) / b;
	}
}

void overlaymagic_try (VJFrame *frame, VJFrame *frame2)
{
	int i;
	const int len = frame->len;
	int a, b, p, q;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
	{
		/* calc p */
		a = Y[i];
		b = Y[i];

		if (b <= pixel_Y_lo_)
			p = pixel_Y_lo_;
		else
			p = 255 - ((256 - a) * (256 - a)) / b;
		if (p <= pixel_Y_lo_)
			p = pixel_Y_lo_;

		/* calc q */
		a = Y2[i];
		b = Y2[i];
		if (b <= pixel_Y_lo_)
			q = pixel_Y_lo_;
		else
			q = 255 - ((256 - a) * (256 - a)) / b;
		if (b <= pixel_Y_lo_)
			q = pixel_Y_lo_;

		/* calc pixel */
		if (q <= pixel_Y_lo_)
			q = pixel_Y_lo_;
		else
			q = 255 - ((256 - p) * (256 - a)) / q;

		Y[i] = q;
	}
}

void overlaymagic_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int n = args[0];
    int clearchroma = args[1];

	switch (n)
	{
		case VJ_EFFECT_BLEND_ADDITIVE:
			overlaymagic_additive(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_SUBSTRACTIVE:
			overlaymagic_substractive(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MULTIPLY:
			overlaymagic_multiply(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_DIVIDE:
			overlaymagic_simpledivide(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_LIGHTEN:
			overlaymagic_lighten(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_DIFFERENCE:
			overlaymagic_difference(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_DIFFNEGATE:
			overlaymagic_diffnegate(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_EXCLUSIVE:
			overlaymagic_exclusive(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_BASECOLOR:
			overlaymagic_basecolor(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_FREEZE:
			overlaymagic_freeze(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_UNFREEZE:
			overlaymagic_unfreeze(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELADD:
			overlaymagic_relativeadd(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELSUB:
			overlaymagic_relativesub(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELADDLUM:
			overlaymagic_relativeaddlum(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELSUBLUM:
			overlaymagic_relativesublum(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MAXSEL:
			overlaymagic_maxselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MINSEL:
			overlaymagic_minselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MINSUBSEL:
			overlaymagic_minsubselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MAXSUBSEL:
			overlaymagic_maxsubselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDSUBSEL:
			overlaymagic_addsubselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDAVG:
			overlaymagic_add_distorted(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST2:
			overlaymagic_addtest(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST3:
			overlaymagic_addtest2(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST4:
			overlaymagic_addtest4(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MULSUB:
			overlaymagic_mulsub(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_SOFTBURN:
			overlaymagic_softburn(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_INVERSEBURN:
			overlaymagic_inverseburn(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_COLORDODGE:
			overlaymagic_colordodge(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDDISTORT:
			overlaymagic_adddistorted(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_SUBDISTORT:
			overlaymagic_subdistorted(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST5:
			overlaymagic_try(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_NEGDIV:
			overlaymagic_divide(frame,frame2 );
			break;
	}

	if(clearchroma)
	{
		veejay_memset( frame->data[1], 128, (frame->ssm ? frame->len : frame->uv_len) );
		veejay_memset( frame->data[2], 128, (frame->ssm ? frame->len : frame->uv_len) );
	}
}
