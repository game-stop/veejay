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
#include <veejaycore/vjmem.h>
#include "stretch.h"

#define STRETCH_PARAMS 5

#define P_UPPER_BOUND   0
#define P_LOWER_BOUND   1
#define P_GAIN_FACTOR   2
#define P_SAT_AMP       3
#define P_CHROMA_DRIVE  4

typedef struct {
    float eff_upper;
    float eff_lower;
    float eff_gain;
    float eff_sat;
    float eff_chroma_drive;
    int initialized;
    int n_threads;
} stretch_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t stretch_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
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



static inline int stretch_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}



vj_effect *stretch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = STRETCH_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

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

    ve->limits[0][P_CHROMA_DRIVE] = 0;
    ve->limits[1][P_CHROMA_DRIVE] = 1000;
    ve->defaults[P_CHROMA_DRIVE] = 0;

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
        "Chroma Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS,                           16,  255,  10, 38, 1000, 3600, 0, 52,
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS,                           0,   220,  10, 38, 1000, 3600, 0, 52,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 80,  1000, 12, 46, 900,  3000, 0, 76,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                           0,   1000, 12, 46, 900,  3000, 0, 70,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120, 1000, 16, 62, 700,  2800, 0, 92
    );

    return ve;
}

void *stretch_malloc(int w, int h)
{
    stretch_t *s = (stretch_t*) vj_calloc(sizeof(stretch_t));

    if(!s)
        return NULL;

    s->eff_upper = 255.0f;
    s->eff_lower = 0.0f;
    s->eff_gain = 40.0f;
    s->eff_sat = 0.0f;
    s->eff_chroma_drive = 0.0f;
    s->initialized = 0;

    s->n_threads = vje_advise_num_threads(w * h);

    return (void*) s;
}

void stretch_free(void *ptr)
{
    free(ptr);
}

void stretch_apply(void *ptr, VJFrame *frame, int *args)
{
    stretch_t *s = (stretch_t*) ptr;

    const int len = frame->len;

    int upper_target = args[P_UPPER_BOUND];
    int lower_target = args[P_LOWER_BOUND];
    const int gain_target = args[P_GAIN_FACTOR];
    const int sat_target = args[P_SAT_AMP];
    const int chroma_drive_target = args[P_CHROMA_DRIVE];

    if(lower_target > upper_target) {
        const int t = lower_target;
        lower_target = upper_target;
        upper_target = t;
    }

    if(!s->initialized) {
        s->eff_upper = (float)upper_target;
        s->eff_lower = (float)lower_target;
        s->eff_gain = (float)gain_target;
        s->eff_sat = (float)sat_target;
        s->eff_chroma_drive = (float)chroma_drive_target;
        s->initialized = 1;
    }

    const float fast = 0.165f;
    const float slow = 0.072f;

    int upper = stretch_smooth_i(&s->eff_upper, upper_target, fast, slow);
    int lower = stretch_smooth_i(&s->eff_lower, lower_target, fast, slow);
    int gain = stretch_smooth_i(&s->eff_gain, gain_target, fast, slow);
    int gain_saturation = stretch_smooth_i(&s->eff_sat, sat_target, fast, slow);
    int chroma_drive = stretch_smooth_i(&s->eff_chroma_drive, chroma_drive_target, fast, slow);

    upper = clampi(upper, 0, 255);
    lower = clampi(lower, 0, 255);
    gain = clampi(gain, 0, 1000);
    gain_saturation = clampi(gain_saturation, 0, 1000);
    chroma_drive = clampi(chroma_drive, 0, 1000);

    if(lower > upper) {
        const int t = lower;
        lower = upper;
        upper = t;
    }

    if(chroma_drive > 0) {
        const int spread = ((chroma_drive * (12 + ((chroma_drive * 20) / 1000))) + 500) / 1000;
        lower = clampi(lower - spread, 0, 255);
        upper = clampi(upper + spread, 0, 255);
    }

    if(lower >= upper) {
        if(upper < 255)
            upper++;
        else if(lower > 0)
            lower--;
    }

    int fixed_gain = gain_saturation > 0
        ? (gain * gain_saturation * 256) / 10000
        : (gain * 256) / 100;

    fixed_gain = clampi(fixed_gain, 0, 32768);

    const int direct_extra_gain = (chroma_drive * 896 + 500) / 1000;
    const int effective_gain = clampi(fixed_gain + direct_extra_gain, 0, 32768);

    const int beat_twist_q8 = clampi((chroma_drive * 62 + 500) / 1000, 0, 192);

    const int use_soft_guard = (chroma_drive > 0 || gain_saturation > 0);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
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
