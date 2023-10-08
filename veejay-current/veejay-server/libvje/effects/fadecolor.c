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

#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include "fadecolor.h"

vj_effect *fadecolor_init(int w,int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 255;
	ve->defaults[1] = VJ_EFFECT_COLOR_BLACK;
	ve->defaults[2] = 15;
	ve->defaults[3] = 1;
	ve->sub_format = 0;
	ve->limits[0][0] = 1;
	ve->limits[1][0] = 255;

	ve->limits[0][1] = 0;
	ve->limits[1][1] = 7;

	ve->limits[0][2] = 1;
	ve->limits[1][2] = (120 * 25);
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 1;
	ve->has_user = 0;
	ve->description = "Transition Fade to Color";
	ve->extra_frame = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Color", "Frame length", "Mode" );
	return ve;
}

typedef struct {
    int value;
} fc_t;

void *fadecolor_malloc(int w, int h) {
    return vj_calloc(sizeof(fc_t));
}

void fadecolor_free(void *ptr) {
    free(ptr);
}


void fadecolor_apply(void *ptr, VJFrame *frame, int *args) {
    int opacity = args[0];
    int color = args[1];

    fc_t *state = (fc_t*) ptr;

    if (args[3] == 0)  {
        if (state->value >= 255)
            state->value = opacity;
        state->value += (opacity / args[2]);
    } else {
        if (state->value <= 0)
            state->value = opacity;
        state->value -= (opacity / args[2]);
    }

	unsigned int i, op0, op1;
	const int len = frame->len;
	uint8_t colorCb = 128, colorCr = 128;
	uint8_t colorY=0;
	const int uv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

    opacity = state->value;

	switch (color) {
		case VJ_EFFECT_COLOR_RED:
			colorCb = VJ_EFFECT_CB_RED;
			colorCr = VJ_EFFECT_CR_RED;
			colorY = VJ_EFFECT_LUM_RED;
		break;
		case VJ_EFFECT_COLOR_BLUE:
			colorCb = VJ_EFFECT_CB_BLUE;
			colorCr = VJ_EFFECT_CR_BLUE;
			colorY = VJ_EFFECT_LUM_BLUE;
		break;
		case VJ_EFFECT_COLOR_GREEN:
			colorCb = VJ_EFFECT_CB_GREEN;
			colorCr = VJ_EFFECT_CR_GREEN;
			colorY = VJ_EFFECT_LUM_GREEN;
		break;
		case VJ_EFFECT_COLOR_CYAN:
			colorCb = VJ_EFFECT_CB_CYAN;
			colorCr = VJ_EFFECT_CR_CYAN;
			colorY = VJ_EFFECT_LUM_CYAN;
		break;
		case VJ_EFFECT_COLOR_MAGNETA:
			colorCb = VJ_EFFECT_CB_MAGNETA;
			colorCr = VJ_EFFECT_CR_MAGNETA;
			colorY = VJ_EFFECT_LUM_MAGNETA;
		break;
		case VJ_EFFECT_COLOR_YELLOW:
			colorCb = VJ_EFFECT_CB_YELLOW;
			colorCr = VJ_EFFECT_CR_YELLOW;
			colorY = VJ_EFFECT_LUM_YELLOW;
		break;
		case VJ_EFFECT_COLOR_BLACK:
			colorCb = VJ_EFFECT_CB_BLACK;
			colorCr = VJ_EFFECT_CR_BLACK;
			colorY = VJ_EFFECT_LUM_BLACK;
		break;
		case VJ_EFFECT_COLOR_WHITE:
			colorCb = VJ_EFFECT_CB_WHITE;
			colorCr = VJ_EFFECT_CR_WHITE;
		break;
	}

	op1 = (opacity > 255) ? 255 : opacity;
	op0 = 255 - op1;
#pragma omp simd
	for (i = 0; i < len; i++)
		Y[i] = (op0 * Y[i] + op1 * colorY) >> 8;
#pragma omp simd
	for (i = 0; i < uv_len; i++) {
		Cb[i] = (op0 * Cb[i] + op1 * colorCb) >> 8;
		Cr[i] = (op0 * Cr[i] + op1 * colorCr) >> 8;
	}
}
