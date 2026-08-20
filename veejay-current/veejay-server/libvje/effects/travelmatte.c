/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "travelmatte.h"

#define TRAVELMATTE_PARAMS 5

#define P_MATTE_SOURCE 0
#define P_MATTE_GAIN   1
#define P_MATTE_BIAS   2
#define P_MATTE_SOFTEN 3
#define P_MIX_DRIVE    4

typedef struct {
    float gain_state;
    float bias_state;
    float soften_state;
    float mix_drive_state;

    int state_ready;
    int n_threads;
} travelmatte_t;

static inline int travelmatte_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}





static inline int travelmatte_alpha_xform(int a,
                                          int gain,
                                          int bias,
                                          int soften,
                                          int mix_drive)
{
    a = travelmatte_clampi(a, 0, 255);
    gain = travelmatte_clampi(gain, 0, 2000);
    bias = travelmatte_clampi(bias, -255, 255);
    soften = travelmatte_clampi(soften, 0, 255);
    mix_drive = travelmatte_clampi(mix_drive, 0, 1000);

    int out = ((a * gain) + 500) / 1000;
    out += bias;
    out = travelmatte_clampi(out, 0, 255);

    if(soften > 0)
        out = ((out * (255 - soften)) + (128 * soften) + 127) / 255;

    if(mix_drive > 0)
        out += ((255 - out) * mix_drive + 500) / 1000;

    return travelmatte_clampi(out, 0, 255);
}

static inline uint8_t travelmatte_blend_u8(uint8_t a, uint8_t b, int alpha)
{
    alpha = travelmatte_clampi(alpha, 0, 255);

    if(alpha <= 0)
        return a;

    if(alpha >= 255)
        return b;

    return ALPHA_BLEND(alpha, a, b);
}



vj_effect *travelmatte_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRAVELMATTE_PARAMS;
    ve->description = "Alpha: Travel Matte";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_MATTE_SOURCE] = 0;    ve->limits[1][P_MATTE_SOURCE] = 1;    ve->defaults[P_MATTE_SOURCE] = 1;
    ve->limits[0][P_MATTE_GAIN]   = 0;    ve->limits[1][P_MATTE_GAIN]   = 2000; ve->defaults[P_MATTE_GAIN]   = 1000;
    ve->limits[0][P_MATTE_BIAS]   = -255; ve->limits[1][P_MATTE_BIAS]   = 255;  ve->defaults[P_MATTE_BIAS]   = 0;
    ve->limits[0][P_MATTE_SOFTEN] = 0;    ve->limits[1][P_MATTE_SOFTEN] = 255;  ve->defaults[P_MATTE_SOFTEN] = 0;
    ve->limits[0][P_MIX_DRIVE]    = 0;    ve->limits[1][P_MIX_DRIVE]    = 1000; ve->defaults[P_MIX_DRIVE]    = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Matte Source",
        "Matte Gain",
        "Matte Bias",
        "Matte Softness",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MATTE_SOURCE],
        P_MATTE_SOURCE,
        "Use Alpha from A",
        "Use Alpha from B"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 600, 1600, 92, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -180, 180, 84, 100, 0, 360, 0, 2, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 160, 68, 94, 180, 1200, 0, 2, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 94, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *travelmatte_malloc(int w, int h)
{
    travelmatte_t *tm = (travelmatte_t*) vj_calloc(sizeof(travelmatte_t));
    if(!tm)
        return NULL;

    tm->gain_state = 1000.0f;
    tm->bias_state = 0.0f;
    tm->soften_state = 0.0f;
    tm->mix_drive_state = 0.0f;
    tm->state_ready = 0;

    tm->n_threads = vje_advise_num_threads(w * h);

    return tm;
}

void travelmatte_free(void *ptr)
{
    free(ptr);
}

void travelmatte_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    travelmatte_t *tm = (travelmatte_t*) ptr;

    const int len = frame->len;

    uint8_t *a0 = frame->data[0];
    uint8_t *a1 = frame->data[1];
    uint8_t *a2 = frame->data[2];

    uint8_t *b0 = frame2->data[0];
    uint8_t *b1 = frame2->data[1];
    uint8_t *b2 = frame2->data[2];

    uint8_t *aA = frame->data[3];
    uint8_t *aB = frame2->data[3];

    const int source = args[P_MATTE_SOURCE] ? 1 : 0;
    uint8_t *matte = source == 0 ? aA : aB;

    int gain = args[P_MATTE_GAIN];
    int bias = args[P_MATTE_BIAS];
    int soften = args[P_MATTE_SOFTEN];
    int mix_drive = args[P_MIX_DRIVE];

    const float fast = 0.24f;
    const float slow = 0.085f;

    if(!tm->state_ready) {
        tm->gain_state = (float)gain;
        tm->bias_state = (float)bias;
        tm->soften_state = (float)soften;
        tm->mix_drive_state = (float)mix_drive;
        tm->state_ready = 1;
    } else {
        tm->gain_state += ((float)gain - tm->gain_state) * fast;
        tm->bias_state += ((float)bias - tm->bias_state) * (fast * 0.92f);
        tm->soften_state += ((float)soften - tm->soften_state) * slow;
        tm->mix_drive_state += ((float)mix_drive - tm->mix_drive_state) * (fast * 1.18f);
    }

    gain = travelmatte_clampi((int)(tm->gain_state + 0.5f), 0, 2000);
    bias = travelmatte_clampi((int)(tm->bias_state + (tm->bias_state >= 0.0f ? 0.5f : -0.5f)), -255, 255);
    soften = travelmatte_clampi((int)(tm->soften_state + 0.5f), 0, 255);
    mix_drive = travelmatte_clampi((int)(tm->mix_drive_state + 0.5f), 0, 1000);

#pragma omp parallel for schedule(static) num_threads(tm->n_threads)
    for(int i = 0; i < len; i++) {
        const int a = travelmatte_alpha_xform((int)matte[i], gain, bias, soften, mix_drive);

        if(a <= 0)
            continue;

        if(a >= 255) {
            a0[i] = b0[i];
            a1[i] = b1[i];
            a2[i] = b2[i];
        } else {
            a0[i] = travelmatte_blend_u8(a0[i], b0[i], a);
            a1[i] = travelmatte_blend_u8(a1[i], b1[i], a);
            a2[i] = travelmatte_blend_u8(a2[i], b2[i], a);
        }
    }
}
