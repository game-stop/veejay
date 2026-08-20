/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "internal.h"
#include "fadecolor.h"

typedef struct {
    int value_q8;
    int last_mode;
    int n_threads;
} fc_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *fadecolor_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 4;
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

    ve->defaults[0] = 255;
    ve->defaults[1] = VJ_EFFECT_COLOR_BLACK;
    ve->defaults[2] = 15;
    ve->defaults[3] = 1;

    ve->limits[0][0] = 1;       ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;       ve->limits[1][1] = 7;
    ve->limits[0][2] = 1;       ve->limits[1][2] = 120 * 25;
    ve->limits[0][3] = 0;       ve->limits[1][3] = 1;

    ve->has_user = 0;
    ve->sub_format = -1;
    ve->description = "Transition Fade to Color";
    ve->extra_frame = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Color", "Frame length", "Mode");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 16, 255, 80, 100, 30, 720, 0, 1, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BPM, VJ_BEAT_OP_BEAT_TIME, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 2, 300, 100, 100, 0, 0, 0, 1, 120, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *fadecolor_malloc(int w, int h)
{
    fc_t *state = (fc_t*) vj_calloc(sizeof(fc_t));

    if(!state)
        return NULL;

    state->value_q8 = 0;
    state->last_mode = -1;
    state->n_threads = vje_advise_num_threads(w * h);
    return state;
}

void fadecolor_free(void *ptr)
{
    free(ptr);
}

static inline void fadecolor_get_yuv(int color, uint8_t *y, uint8_t *u, uint8_t *v)
{
    switch(color) {
        case VJ_EFFECT_COLOR_RED:
            *y = VJ_EFFECT_LUM_RED;
            *u = VJ_EFFECT_CB_RED;
            *v = VJ_EFFECT_CR_RED;
            break;
        case VJ_EFFECT_COLOR_BLUE:
            *y = VJ_EFFECT_LUM_BLUE;
            *u = VJ_EFFECT_CB_BLUE;
            *v = VJ_EFFECT_CR_BLUE;
            break;
        case VJ_EFFECT_COLOR_GREEN:
            *y = VJ_EFFECT_LUM_GREEN;
            *u = VJ_EFFECT_CB_GREEN;
            *v = VJ_EFFECT_CR_GREEN;
            break;
        case VJ_EFFECT_COLOR_CYAN:
            *y = VJ_EFFECT_LUM_CYAN;
            *u = VJ_EFFECT_CB_CYAN;
            *v = VJ_EFFECT_CR_CYAN;
            break;
        case VJ_EFFECT_COLOR_MAGNETA:
            *y = VJ_EFFECT_LUM_MAGNETA;
            *u = VJ_EFFECT_CB_MAGNETA;
            *v = VJ_EFFECT_CR_MAGNETA;
            break;
        case VJ_EFFECT_COLOR_YELLOW:
            *y = VJ_EFFECT_LUM_YELLOW;
            *u = VJ_EFFECT_CB_YELLOW;
            *v = VJ_EFFECT_CR_YELLOW;
            break;
        case VJ_EFFECT_COLOR_WHITE:
            *y = pixel_Y_hi_;
            *u = VJ_EFFECT_CB_WHITE;
            *v = VJ_EFFECT_CR_WHITE;
            break;
        case VJ_EFFECT_COLOR_BLACK:
        default:
            *y = VJ_EFFECT_LUM_BLACK;
            *u = VJ_EFFECT_CB_BLACK;
            *v = VJ_EFFECT_CR_BLACK;
            break;
    }
}

void fadecolor_apply(void *ptr, VJFrame *frame, int *args)
{
    fc_t *state = (fc_t*) ptr;

    const int len = frame->len;

    const int opacity_arg = args[0];
    const int color_arg = args[1];
    const int frame_length_arg = args[2];
    const int mode_arg = args[3];

    const int opacity = clampi(opacity_arg, 1, 255);
    const int color = clampi(color_arg, 0, 7);
    const int frame_length = clampi(frame_length_arg, 1, 120 * 25);
    const int mode = clampi(mode_arg, 0, 1);
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

    uint8_t colorY = VJ_EFFECT_LUM_BLACK;
    uint8_t colorCb = 128;
    uint8_t colorCr = 128;

    fadecolor_get_yuv(color, &colorY, &colorCb, &colorCr);

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
