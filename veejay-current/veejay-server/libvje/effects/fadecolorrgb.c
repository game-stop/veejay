/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "fadecolorrgb.h"

vj_effect *fadecolorrgb_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 150;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 75;
    ve->sub_format = 0;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 1;

    ve->limits[0][5] = 1;
    ve->limits[1][5] = (25 * 120);
    ve->description = "Transition Fade to Color by RGB";
	ve->has_user = 0;
    ve->extra_frame = 0;
	ve->rgb_conv = 1;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Red","Green", "Blue", "Mode", "Frame length");
    return ve;
}

typedef struct {
    int value;
} fc_t;

void *fadecolorrgb_malloc(int w, int h) {
    return (void*) vj_calloc(sizeof(fc_t));
}

void fadecolorrgb_free(void *ptr) {
    free(ptr);
}

void fadecolorrgb_apply( void *ptr, VJFrame *frame, int *args) {
    int opacity = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];

    fc_t *state = (fc_t*) ptr;

    if (args[4] == 0) {
        if(state->value >= 255 ) {
           state->value = opacity;
        }
        state->value += (opacity / args[5]);
    } else {
        if ( state->value <= 0 ) {
            state->value = opacity;
        }
        state->value = (opacity / args[5]);
    }

    unsigned int i, op0, op1;
    const int len = frame->len;
    unsigned int colorCb = 128, colorCr = 128;
    unsigned int colorY;
    const int uv_len = frame->uv_len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    opacity = state->value;

    colorY = ((0.257 * r) + (0.504 * g) + (0.098 * b) + 16);
    colorCb = ((0.439 * r) - (0.368 * g) - (0.071 * b) + 128);
    colorCr = (-(0.148 * r) - (0.291 * g) + (0.439 * b) + 128);

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
