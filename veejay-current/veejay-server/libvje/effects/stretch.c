/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "stretch.h"

#define STRETCH_PARAMS 7

#define P_UPPER_BOUND   0
#define P_LOWER_BOUND   1
#define P_GAIN_FACTOR   2
#define P_SAT_AMP       3
#define P_BEAT_CHROMA   4
#define P_BEAT_PUSH     5
#define P_BEAT_SMOOTH   6

typedef struct {
    float beat_env;
    float beat_kick;
} stretch_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t stretch_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int stretch_beat_shape(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return clampi((beat_push * 38 + sq * 62 + 50) / 100, 0, 1000);
}

static inline uint8_t stretch_soft_chroma_u8(int v)
{
    int c = v - 128;

    if(c > 118)
        c = 118 + ((c - 118) >> 2);
    else if(c < -118)
        c = -118 + ((c + 118) >> 2);

    if(c > 127)
        c = 127;
    else if(c < -128)
        c = -128;

    return stretch_u8(128 + c);
}

vj_effect *stretch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = STRETCH_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_UPPER_BOUND] = 0;
    ve->limits[1][P_UPPER_BOUND] = 255;
    ve->defaults[P_UPPER_BOUND] = 255;

    ve->limits[0][P_LOWER_BOUND] = 0;
    ve->limits[1][P_LOWER_BOUND] = 255;
    ve->defaults[P_LOWER_BOUND] = 0;

    ve->limits[0][P_GAIN_FACTOR] = 0;
    ve->limits[1][P_GAIN_FACTOR] = 1000;
    ve->defaults[P_GAIN_FACTOR] = 40;

    ve->limits[0][P_SAT_AMP] = 0;
    ve->limits[1][P_SAT_AMP] = 1000;
    ve->defaults[P_SAT_AMP] = 0;

    ve->limits[0][P_BEAT_CHROMA] = 0;
    ve->limits[1][P_BEAT_CHROMA] = 1000;
    ve->defaults[P_BEAT_CHROMA] = 420;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Chroma Stretch";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Upper bound",
        "Lower bound",
        "Gain factor",
        "Saturation Amplifier",
        "Beat Chroma",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 96,                 255,                6,  22, 1800, 4200, 900,  24,    /* Upper bound */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 0,                  160,                6,  22, 1800, 4200, 900,  24,    /* Lower bound */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                       0,                  420,                8,  30, 1200, 3000, 0,    45,    /* Gain factor */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                       0,                  620,                8,  30, 1200, 3000, 0,    42,    /* Saturation Amplifier */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Chroma */
        VJ_BEAT_KICK,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,   0,                  760,                18, 72, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                      260,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *stretch_malloc(int w, int h)
{
    stretch_t *s = (stretch_t*) vj_calloc(sizeof(stretch_t));

    if(!s)
        return NULL;

    s->beat_env = 0.0f;
    s->beat_kick = 0.0f;

    (void) w;
    (void) h;

    return (void*) s;
}

void stretch_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

void stretch_apply(void *ptr, VJFrame *frame, int *args)
{
    stretch_t *s = (stretch_t*) ptr;

    if(!frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int len = frame->len;
    if(len <= 0)
        return;

    int upper = clampi(args[P_UPPER_BOUND], 0, 255);
    int lower = clampi(args[P_LOWER_BOUND], 0, 255);
    int gain = clampi(args[P_GAIN_FACTOR], 0, 1000);
    int gain_saturation = clampi(args[P_SAT_AMP], 0, 1000);
    const int beat_chroma = clampi(args[P_BEAT_CHROMA], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    if(lower > upper) {
        const int t = lower;
        lower = upper;
        upper = t;
    }

    if(lower == upper)
        return;

    int fixed_gain = gain_saturation > 0
        ? (gain * gain_saturation * 256) / 10000
        : (gain * 256) / 100;

    fixed_gain = clampi(fixed_gain, 0, 32768);

    int beat_q = 0;
    int kick_q = 0;

    if(s) {
        const int shaped = stretch_beat_shape(beat_push);
        const float target = (float)shaped * 0.001f;
        const float smooth = (float)beat_smooth * 0.001f;
        const float attack = 0.18f + (1.0f - smooth) * 0.38f;
        const float release = 0.026f + (1.0f - smooth) * 0.105f;
        const float prev_env = s->beat_env;

        if(target > s->beat_env)
            s->beat_env += (target - s->beat_env) * attack;
        else
            s->beat_env += (target - s->beat_env) * release;

        if(s->beat_env < 0.0001f)
            s->beat_env = 0.0f;
        else if(s->beat_env > 1.0f)
            s->beat_env = 1.0f;

        const float rise = s->beat_env - prev_env;
        if(rise > s->beat_kick)
            s->beat_kick += (rise - s->beat_kick) * 0.78f;
        else
            s->beat_kick *= 0.56f;

        if(s->beat_kick < 0.0001f)
            s->beat_kick = 0.0f;
        else if(s->beat_kick > 1.0f)
            s->beat_kick = 1.0f;

        beat_q = clampi((int)(s->beat_env * 1000.0f + 0.5f), 0, 1000);
        kick_q = clampi((int)(s->beat_kick * 1000.0f + 0.5f), 0, 1000);
    } else if(beat_push > 0) {
        beat_q = stretch_beat_shape(beat_push);
        kick_q = beat_q >> 1;
    }

    const int beat_drive_q = clampi(((beat_q * 680) + (kick_q * 420) + 500) / 1000, 0, 1000);
    const int beat_extra_gain = (beat_chroma * beat_drive_q * 1536 + 500000) / 1000000;
    const int beat_twist_q8 = (beat_chroma * beat_drive_q * 96 + 500000) / 1000000;
    const int effective_gain = clampi(fixed_gain + beat_extra_gain, 0, 32768);
    const int use_soft_guard = (beat_drive_q > 0);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int n_threads = vje_advise_num_threads(len);
    if(n_threads < 1)
        n_threads = 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int y = Y[i];

        if(y > lower && y < upper) {
            const int cb = (int)Cb[i] - 128;
            const int cr = (int)Cr[i] - 128;

            int out_cb = 128 + cb + ((cb * effective_gain) >> 8);
            int out_cr = 128 + cr - ((cr * effective_gain) >> 8);

            if(beat_twist_q8 > 0) {
                out_cb += (cr * beat_twist_q8) >> 8;
                out_cr -= (cb * beat_twist_q8) >> 8;
            }

            Cb[i] = use_soft_guard ? stretch_soft_chroma_u8(out_cb) : stretch_u8(out_cb);
            Cr[i] = use_soft_guard ? stretch_soft_chroma_u8(out_cr) : stretch_u8(out_cr);
        }
    }
}
