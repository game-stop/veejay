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
#include "mtracer.h"
#include "magicoverlays.h"
#include "internal.h"

typedef struct {
    uint8_t *mtrace_buffer[4];
    int mtrace_counter;
	int started;
	int prev_n;
	int mode_transition;      // current transition progress
	int mode_transition_len;  // total frames to blend
	uint8_t *mode_buffer;     // temp buffer for old mode
	int prev_mode;
	int n_threads;
} m_tracer_t;

#define DIV255(x) (((x) + 128 + (((x) + 128) >> 8)) >> 8)
#define BLEND_SM(a, b) (uint8_t)((a * b) / 255)
#define BLEND_SCR(a, b) (uint8_t)(255 - (((255 - a) * (255 - b)) / 255))


static inline void overlaymagic1_decay(uint8_t *restrict buffer, int len, int decay_val) {
    if (decay_val >= 255) return;
    if (decay_val <= 0) {
        veejay_memset(buffer, 0, len);
        return;
    }
#pragma omp simd
    for (int i = 0; i < len; i++) {
        buffer[i] = (uint8_t)((buffer[i] * decay_val) >> 8); 
    }
}

static inline void overlaymagic1_motion_mask(
    uint8_t *cur,
    uint8_t *prev,
    uint8_t *out,
    int len
) {
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int diff = (int)cur[i] - (int)prev[i];
        int abs_diff = (diff < 0) ? -diff : diff;
        out[i] = (abs_diff > 255) ? 255 : (uint8_t)abs_diff;
    }
}
// copied back from old version, buggy overlay modes that clip/saturate pixels
void overlaymagic1_adddistorted(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_add_distorted(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        Y[i] = (Y[i] + Y2[i]) >> 1;
    }
}

void overlaymagic1_subdistorted(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_sub_distorted(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_multiply(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        Y[i] = (Y[i] * Y2[i]) / 255;
    }
}

void overlaymagic1_simpledivide(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_divide(VJFrame *frame, VJFrame *frame2 )
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        Y[i] = BLEND_SCR(Y[i], Y2[i]);
    }
}



void overlaymagic1_additive(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for(int i = 0; i < len; i++) {
        int res = Y[i] + Y2[i];
        Y[i] = (res > 255) ? 255 : res;
    }
}

void overlaymagic1_substractive(VJFrame *frame, VJFrame *frame2 )
{
	int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];
#pragma omp simd
	for (i = 0; i < len; i++)
		Y[i] = CLAMP_Y( Y[i] - Y2[i] );
}

void overlaymagic1_softburn(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int base = Y[i];
        int blend = Y2[i];
        if (blend == 0) Y[i] = 0;
        else {
            int res = 255 - (((255 - base) << 8) / blend);
            Y[i] = (res < 0) ? 0 : res;
        }
    }
}
void overlaymagic1_inverseburn(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_colordodge(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int base = Y[i];
        int blend = Y2[i];
        if (blend == 255) Y[i] = 255;
        else {
            int res = (base << 8) / (255 - blend);
            Y[i] = (res > 255) ? 255 : res;
        }
    }
}

void overlaymagic1_mulsub(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_lighten(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_difference(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int res = Y[i] - Y2[i];
        Y[i] = (res < 0) ? -res : res;
    }
}

void overlaymagic1_diffnegate(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_exclusive(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int a = Y[i];
        int b = Y2[i];
        Y[i] = a + b - (2 * a * b / 255);
    }
}

void overlaymagic1_basecolor(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_freeze(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_unfreeze(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_hardlight(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int a = Y[i];
        int b = Y2[i];
        if (b < 128) Y[i] = (2 * a * b) / 255;
        else Y[i] = 255 - (2 * (255 - a) * (255 - b) / 255);
    }
}

void overlaymagic1_relativeaddlum(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_relativesublum(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_relativeadd(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_relativesub(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_minsubselect(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_maxsubselect(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_addsubselect(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_maxselect(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_minselect(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_addtest(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_addtest2(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_addtest4(VJFrame *frame, VJFrame *frame2 )
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

void overlaymagic1_screen(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        Y[i] = 255 - (((255 - Y[i]) * (255 - Y2[i])) / 255);
    }
}

void overlaymagic1_overlay(VJFrame *frame, VJFrame *frame2)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Y2 = frame2->data[0];
#pragma omp simd
    for (int i = 0; i < len; i++) {
        int a = Y[i];
        int b = Y2[i];
        Y[i] = (a < 128) ? (2 * a * b / 255) : (255 - 2 * (255 - a) * (255 - b) / 255);
    }
}

void overlaymagic1_try(VJFrame *frame, VJFrame *frame2)
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

void overlaymagic1_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int n ) {

	switch (n)
	{
		case VJ_EFFECT_BLEND_ADDITIVE:
			overlaymagic1_additive(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_SUBSTRACTIVE:
			overlaymagic1_substractive(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MULTIPLY:
			overlaymagic1_multiply(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_DIVIDE:
			overlaymagic1_simpledivide(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_LIGHTEN:
			overlaymagic1_lighten(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_DIFFERENCE:
			overlaymagic1_difference(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_DIFFNEGATE:
			overlaymagic1_diffnegate(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_EXCLUSIVE:
			overlaymagic1_exclusive(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_BASECOLOR:
			overlaymagic1_basecolor(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_FREEZE:
			overlaymagic1_freeze(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_UNFREEZE:
			overlaymagic1_unfreeze(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELADD:
			overlaymagic1_relativeadd(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELSUB:
			overlaymagic1_relativesub(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELADDLUM:
			overlaymagic1_relativeaddlum(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_RELSUBLUM:
			overlaymagic1_relativesublum(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MAXSEL:
			overlaymagic1_maxselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MINSEL:
			overlaymagic1_minselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MINSUBSEL:
			overlaymagic1_minsubselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MAXSUBSEL:
			overlaymagic1_maxsubselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDSUBSEL:
			overlaymagic1_addsubselect(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDAVG:
			overlaymagic1_add_distorted(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST2:
			overlaymagic1_addtest(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST3:
			overlaymagic1_addtest2(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST4:
			overlaymagic1_addtest4(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_MULSUB:
			overlaymagic1_mulsub(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_SOFTBURN:
			overlaymagic1_softburn(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_INVERSEBURN:
			overlaymagic1_inverseburn(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_COLORDODGE:
			overlaymagic1_colordodge(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDDISTORT:
			overlaymagic1_adddistorted(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_SUBDISTORT:
			overlaymagic1_subdistorted(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_ADDTEST5:
			overlaymagic1_try(frame, frame2 );
			break;
		case VJ_EFFECT_BLEND_NEGDIV:
			overlaymagic1_divide(frame,frame2);
			break;
		case VJ_EFFECT_BLEND_SCREEN:
			overlaymagic1_screen(frame,frame2 );
			break;
		default:
            overlaymagic1_overlay(frame, frame2);
            break;
	}


}

vj_effect *mtracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params); // min
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params); // max


    ve->limits[0][0] = 0;   ve->limits[1][0] = 34;   // Mode
    ve->limits[0][1] = 1;   ve->limits[1][1] = 255;  // Strength (Opacity)
    ve->limits[0][2] = 0;   ve->limits[1][2] = 1;    // Use Classic Blend
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255;  // Softness (Curve)
    ve->limits[0][4] = 1;   ve->limits[1][4] = 255;  // Decay Strength
    ve->limits[0][5] = 0;   ve->limits[1][5] = 1;    // Motion Only
	ve->limits[0][6] = 0;   ve->limits[1][6] = 255;  // Frame2 Opacity

    ve->defaults[0] = 0;    // Mode
    ve->defaults[1] = 200;  // Strength
    ve->defaults[2] = 0;    // Classic
    ve->defaults[3] = 128;  // Character
    ve->defaults[4] = 11;  // Decay Strength
    ve->defaults[5] = 0;    // Motion Only
	ve->defaults[6] = 128;  // Frame2 Opacity (50%)

    ve->description = "Magic Tracer";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Strength",
        "Use Classic Blend",
        "Character",
        "Decay Strength",
        "Motion Only",
		"Frame2 Opacity"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0,
        "Additive","Subtractive","Multiply","Divide","Lighten","Hardlight",
        "Difference","Difference Negate","Exclusive","Base","Freeze",
        "Unfreeze","Relative Add","Relative Subtract","Max select","Min select",
        "Relative Luma Add","Relative Luma Subtract","Min Subselect","Max Subselect",
        "Add Subselect","Add Average","Experimental 1","Experimental 2","Experimental 3",
        "Multisub","Softburn","Inverse Burn","Dodge","Distorted Add","Distorted Subtract",
        "Experimental 4","Negation Divide","Screen","Overlay"
    );

    return ve;
}

void *mtracer_malloc(int w, int h)
{
    const size_t buflen = (size_t) w * h;
    const size_t total_buffers = 5;
    const size_t total_size = buflen * total_buffers;

    m_tracer_t *m = (m_tracer_t*) vj_calloc(sizeof(m_tracer_t));
    if (!m)
        return NULL;

    uint8_t *block = (uint8_t*) vj_malloc(total_size);
    if (!block) {
        free(m);
        return NULL;
    }

    for (int i = 0; i < 4; i++) {
        m->mtrace_buffer[i] = block + (i * buflen);

        if (i == 0 || i == 3)
            veejay_memset(m->mtrace_buffer[i], pixel_Y_lo_, buflen);
    }

    m->mode_buffer = block + (4 * buflen);
	m->mode_transition = 0;
	m->mode_transition_len = 12;
	m->n_threads = vje_advise_num_threads(w*h);
    return (void*) m;
}

void mtracer_free(void *ptr)
{
    if (!ptr)
        return;

    m_tracer_t *m = (m_tracer_t*) ptr;
    if (m->mtrace_buffer[0])
        free(m->mtrace_buffer[0]);

    free(m);
}

void mtracer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    m_tracer_t *m = (m_tracer_t*) ptr;
    int mode = args[0], length = args[1], classic = args[2], character = args[3];
    int decay_val = args[4], motion_only = args[5], frame2_opacity = args[6];

    const int len = frame->len;
    uint8_t *feedback_buf   = m->mtrace_buffer[0];
    uint8_t *blended_result = m->mtrace_buffer[1];
    uint8_t *prev_frame     = m->mtrace_buffer[2];

	const int n_threads = m->n_threads;

    VJFrame tmp_frame;
    veejay_memcpy(&tmp_frame, frame, sizeof(VJFrame));
    tmp_frame.data[0] = blended_result;

    if (!m->started)
    {
        veejay_memcpy(feedback_buf, frame->data[0], len);
        veejay_memcpy(prev_frame, frame->data[0], len);
        m->prev_mode = mode;
        m->mode_transition = 0;
        m->started = 1;
    }

    if (mode != m->prev_mode)
    {
        veejay_memcpy(m->mode_buffer, feedback_buf, len);
        m->mode_transition = m->mode_transition_len;
        m->prev_mode = mode;
    }

    veejay_memcpy(blended_result, frame->data[0], len);
    overlaymagic1_apply(NULL, &tmp_frame, frame2, mode);

	#pragma omp parallel num_threads(n_threads)
	{
    if (frame2_opacity < 255)
    {
        uint8_t *f1 = frame->data[0];
		#pragma omp for simd schedule(static)
        for (int i = 0; i < len; i++)
        {
            int b = blended_result[i];
            blended_result[i] = (uint8_t)(((f1[i] * (255 - frame2_opacity)) + (b * frame2_opacity)) >> 8);
        }
    }

    if (m->mode_transition > 0)
    {
        int t = m->mode_transition_len - m->mode_transition;
        int x = (t << 8) / m->mode_transition_len;
        int alpha = (x * x * (768 - (x << 1))) >> 16;
        uint8_t *mode_buf = m->mode_buffer;
        #pragma omp for simd schedule(static)
        for (int i = 0; i < len; i++)
        {
            int b = blended_result[i];
            blended_result[i] = (uint8_t)((mode_buf[i] * (255 - alpha) + b * alpha) >> 8);
        }
        m->mode_transition--;
    }

    if (motion_only)
        overlaymagic1_motion_mask(blended_result, prev_frame, blended_result, len);

    int combined_scale = (length * character) / 255;
    combined_scale = combined_scale < 1 ? 1 : (combined_scale > 255 ? 255 : combined_scale);

    int decay = 256 - (256 / (decay_val ? decay_val : 1));
    int blend = 256 - decay;

    #pragma omp for simd schedule(static)
    for (int i = 0; i < len; i++)
    {
        int f = feedback_buf[i];
        int b = blended_result[i];
        int accum = ((f * decay) + ((b * combined_scale * blend) >> 8)) >> 8;
        feedback_buf[i] = (uint8_t)(accum < 0 ? 0 : (accum > 255 ? 255 : accum));
    }
	}

    veejay_memcpy(prev_frame, frame->data[0], len);

    if (classic)
    {
        tmp_frame.data[0] = feedback_buf;
        overlaymagic1_apply(NULL, frame, &tmp_frame, mode);
    }
    else
    {
        veejay_memcpy(frame->data[0], feedback_buf, len);
    }

    veejay_memset(frame->data[1], 128, frame->uv_len);
    veejay_memset(frame->data[2], 128, frame->uv_len);
}

