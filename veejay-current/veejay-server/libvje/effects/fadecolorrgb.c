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
    ve->sub_format = -1;
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
    ve->sub_format = -1;
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

void fadecolorrgb_apply(void *ptr, VJFrame *frame, int *args) {
    int opacity = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];

    fc_t *state = (fc_t*) ptr;

    if (args[4] == 0) {
        if(state->value >= 255 ) state->value = opacity;
        state->value += (opacity / args[5]);
    } else {
        if (state->value <= 0) state->value = opacity;
        state->value = (opacity / args[5]);
    }

    const int len = frame->len;
    const int uv_len = frame->uv_len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    const int current_opacity = state->value;
    const int op1 = (current_opacity > 255) ? 255 : current_opacity;
    const int op0 = 255 - op1;

    const uint8_t colorY  = (uint8_t)((0.257f * r) + (0.504f * g) + (0.098f * b) + 16.0f);
    const uint8_t colorCb = (uint8_t)((0.439f * r) - (0.368f * g) - (0.071f * b) + 128.0f);
    const uint8_t colorCr = (uint8_t)(-(0.148f * r) - (0.291f * g) + (0.439f * b) + 128.0f);

    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for (int i = 0; i < len; i++) {
            Y[i] = (uint8_t)((op0 * Y[i] + op1 * colorY) >> 8);
        }

#pragma omp for schedule(static)
        for (int i = 0; i < uv_len; i++) {
            Cb[i] = (uint8_t)((op0 * Cb[i] + op1 * colorCb) >> 8);
            Cr[i] = (uint8_t)((op0 * Cr[i] + op1 * colorCr) >> 8);
        }
    }
}