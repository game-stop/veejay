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

#include "common.h"
#include "fadecolorrgb.h"

typedef struct {
    int value_q8;
    int last_mode;
    int n_threads;
} fc_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *fadecolorrgb_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[0] = 150;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 75;

    ve->limits[0][0] = 1;       ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;       ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;       ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;       ve->limits[1][3] = 255;
    ve->limits[0][4] = 0;       ve->limits[1][4] = 1;
    ve->limits[0][5] = 1;       ve->limits[1][5] = 25 * 120;

    ve->description = "Transition Fade to Color by RGB";
    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->rgb_conv = 1;
    ve->sub_format = -1;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Red", "Green", "Blue", "Mode", "Frame length");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                      24,                 245,                14, 54,  800, 3000, 0,    82,
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                      32,                 255,                12, 46,  900, 3400, 0,    62,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED,                                           0,                  190,                10, 38, 1100, 3800, 0,    44,
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                      36,                 255,                12, 46,  900, 3400, 0,    64,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SPEED,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8, 360, 4, 14, 3000, 8200, 2200, 20
    );

    return ve;
}

void *fadecolorrgb_malloc(int w, int h)
{
    fc_t *state = (fc_t*) vj_calloc(sizeof(fc_t));

    if(!state)
        return NULL;

    state->value_q8 = 0;
    state->last_mode = -1;
    state->n_threads = vje_advise_num_threads(w * h);

    return state;
}

void fadecolorrgb_free(void *ptr)
{
    free(ptr);
}

void fadecolorrgb_apply(void *ptr, VJFrame *frame, int *args)
{
    fc_t *state = (fc_t*) ptr;

    const int len = frame->len;

    const int opacity_arg = args[0];
    const int r_arg = args[1];
    const int g_arg = args[2];
    const int b_arg = args[3];
    const int mode_arg = args[4];
    const int frame_length_arg = args[5];

    const int opacity = clampi(opacity_arg, 1, 255);
    const int r = clampi(r_arg, 0, 255);
    const int g = clampi(g_arg, 0, 255);
    const int b = clampi(b_arg, 0, 255);
    const int mode = clampi(mode_arg, 0, 1);
    const int frame_length = clampi(frame_length_arg, 1, 25 * 120);
    const int target_q8 = opacity << 8;
    int step_q8 = target_q8 / frame_length;

    if(step_q8 < 1)
        step_q8 = 1;

    if(mode != state->last_mode) {
        state->value_q8 = mode ? target_q8 : 0;
        state->last_mode = mode;
    }

    if(mode == 0) {
        state->value_q8 += step_q8;
        if(state->value_q8 > target_q8)
            state->value_q8 = target_q8;
    }
    else {
        state->value_q8 -= step_q8;
        if(state->value_q8 < 0)
            state->value_q8 = 0;
    }

    const int op1 = clampi((state->value_q8 + 128) >> 8, 0, 255);
    const int op0 = 255 - op1;

    if(op1 <= 0)
        return;

    int colorY = 0;
    int colorCb = 128;
    int colorCr = 128;

    _rgb2yuv(r, g, b, colorY, colorCb, colorCr);

    colorY = clampi(colorY, 0, 255);
    colorCb = clampi(colorCb, 0, 255);
    colorCr = clampi(colorCr, 0, 255);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int uv_len = frame->ssm ? len : frame->uv_len;

#pragma omp parallel num_threads(state->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = (uint8_t)((op0 * Y[i] + op1 * colorY + 127) / 255);

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb[i] = (uint8_t)((op0 * Cb[i] + op1 * colorCb + 127) / 255);
            Cr[i] = (uint8_t)((op0 * Cr[i] + op1 * colorCr + 127) / 255);
        }
    }
}
