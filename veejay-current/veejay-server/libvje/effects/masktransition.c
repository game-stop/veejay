/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "masktransition.h"

#define MASKTRANSITION_PARAMS 2

#define P_TIME_INDEX 0
#define P_SMOOTH     1

#define SMOOTH_DEFAULT 256
#define USE_FROM_A     0
#define USE_FROM_B     1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t masktransition_div255(int v)
{
    return (uint8_t)(((v + 1) + (v >> 8)) >> 8);
}

static inline uint8_t masktransition_blend(uint8_t a, uint8_t b, uint8_t alpha)
{
    return masktransition_div255((int)a * (int)alpha + (int)b * (255 - (int)alpha));
}

vj_effect *masktransition_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MASKTRANSITION_PARAMS;
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

    ve->limits[0][P_TIME_INDEX] = 0; ve->limits[1][P_TIME_INDEX] = 1255; ve->defaults[P_TIME_INDEX] = 0;
    ve->limits[0][P_SMOOTH] = 0;     ve->limits[1][P_SMOOTH] = 1000;     ve->defaults[P_SMOOTH] = 50;

    ve->description = "Alpha: Transition Map";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_SRC_A;
    ve->param_description = vje_build_param_list(ve->num_params, "Time Index", "Smooth");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, 0, 1255, 92, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 24, 650, 70, 96, 20, 720, 0, 5, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void masktransition_build_lookup(uint8_t *restrict lookup, int time_index, int duration)
{
    for(int i = 0; i < 256; i++) {
        if(time_index < i) {
            lookup[i] = 0;
        }
        else if(time_index >= i + duration) {
            lookup[i] = 255;
        }
        else {
            lookup[i] = (uint8_t)(((time_index - i) * 255 + (duration >> 1)) / duration);
        }
    }
}

static void alpha_blend_transition(uint8_t *restrict Y,
                                   uint8_t *restrict Cb,
                                   uint8_t *restrict Cr,
                                   const uint8_t *restrict a0,
                                   const uint8_t *restrict Y2,
                                   const uint8_t *restrict Cb2,
                                   const uint8_t *restrict Cr2,
                                   const uint8_t *restrict a1,
                                   int len,
                                   int time_index,
                                   int duration,
                                   int alpha_select)
{
    uint8_t lookup[256];
    const uint8_t *restrict alpha_map = alpha_select == USE_FROM_A ? a0 : a1;
    const int n_threads = vje_advise_num_threads(len);

    masktransition_build_lookup(lookup, time_index, duration);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const uint8_t alpha = lookup[alpha_map[i]];

        Y[i] = masktransition_blend(Y[i], Y2[i], alpha);
        Cb[i] = masktransition_blend(Cb[i], Cb2[i], alpha);
        Cr[i] = masktransition_blend(Cr[i], Cr2[i], alpha);
    }
}

void alpha_transition_apply(VJFrame *frame, uint8_t *B[4], int time_index)
{
    alpha_blend_transition(
        frame->data[0], frame->data[1], frame->data[2], frame->data[3],
        B[0], B[1], B[2], B[3],
        frame->len,
        clampi(time_index, 0, 1255),
        SMOOTH_DEFAULT,
        USE_FROM_A
    );
}

void masktransition_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int time_index = args[P_TIME_INDEX];
    const int duration = args[P_SMOOTH] + 1;

    alpha_blend_transition(
        frame->data[0], frame->data[1], frame->data[2], frame->data[3],
        frame2->data[0], frame2->data[1], frame2->data[2], frame2->data[3],
        frame->len,
        time_index,
        duration,
        USE_FROM_A
    );
}
