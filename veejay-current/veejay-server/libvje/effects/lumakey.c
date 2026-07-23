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
#include "lumakey.h"

#define LUMAKEY_PARAMS 5

#define P_OPACITY  0
#define P_LUMA_MIN 1
#define P_LUMA_MAX 2
#define P_SOFTNESS 3
#define P_INVERT   4

typedef struct {
    int n_threads;
} lumakey_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t lumakey_mix_q8(uint8_t a, uint8_t b, int aq)
{
    return (uint8_t)(((int)a * aq + (int)b * (256 - aq) + 128) >> 8);
}

vj_effect *lumakey_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = LUMAKEY_PARAMS;
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

    ve->limits[0][P_OPACITY] = 0;  ve->limits[1][P_OPACITY] = 255;  ve->defaults[P_OPACITY] = 255;
    ve->limits[0][P_LUMA_MIN] = 0; ve->limits[1][P_LUMA_MIN] = 255; ve->defaults[P_LUMA_MIN] = 0;
    ve->limits[0][P_LUMA_MAX] = 0; ve->limits[1][P_LUMA_MAX] = 255; ve->defaults[P_LUMA_MAX] = 50;
    ve->limits[0][P_SOFTNESS] = 0; ve->limits[1][P_SOFTNESS] = 255; ve->defaults[P_SOFTNESS] = 20;
    ve->limits[0][P_INVERT] = 0;   ve->limits[1][P_INVERT] = 1;     ve->defaults[P_INVERT] = 0;

    ve->description = "Luma Key Mixer";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Luma Min", "Luma Max", "Softness", "Invert");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_INVERT], P_INVERT, "Normal", "Invert");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 80, 255, 84, 100, 10, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 80, 72, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 1, 0, VJ_BEAT_GROUP_ASCENDING, 8),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 72, 220, 76, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 1, 1, VJ_BEAT_GROUP_ASCENDING, 8),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 4, 96, 68, 96, 30, 760, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *lumakey_malloc(int w, int h)
{
    lumakey_t *lk = (lumakey_t*) vj_malloc(sizeof(lumakey_t));

    if(!lk)
        return NULL;

    lk->n_threads = vje_advise_num_threads(w * h);

    return (void*) lk;
}

void lumakey_free(void *ptr)
{
    free(ptr);
}

static void lumakey_build_lut(uint16_t *restrict alpha_lut,
                              int opacity,
                              int luma_min,
                              int luma_max,
                              int softness,
                              int invert)
{
    const int t1 = luma_min < luma_max ? luma_min : luma_max;
    const int t2 = luma_min < luma_max ? luma_max : luma_min;
    const int lo = t1 - softness;
    const int hi = t2 + softness;

    for(int i = 0; i < 256; i++) {
        int a;

        if(i >= t1 && i <= t2) {
            a = 0;
        }
        else if(softness > 0 && i >= lo && i < t1) {
            a = 255 - (((i - lo) * 255 + (softness >> 1)) / softness);
        }
        else if(softness > 0 && i > t2 && i <= hi) {
            a = ((i - t2) * 255 + (softness >> 1)) / softness;
        }
        else {
            a = 255;
        }

        a = clampi(a, 0, 255);

        if(invert)
            a = 255 - a;

        alpha_lut[i] = (uint16_t)(((uint32_t)a * (uint32_t)opacity * 256u + 32512u) / 65025u);
    }
}

void lumakey_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    lumakey_t *lk = (lumakey_t*) ptr;

    const int opacity = args[P_OPACITY];
    const int luma_min = args[P_LUMA_MIN];
    const int luma_max = args[P_LUMA_MAX];
    const int softness = args[P_SOFTNESS];
    const int invert = args[P_INVERT];
    const int len = frame->len;

    uint16_t alpha_lut[256];

    lumakey_build_lut(alpha_lut, opacity, luma_min, luma_max, softness, invert);

    uint8_t *restrict Y1 = frame->data[0];
    uint8_t *restrict Cb1 = frame->data[1];
    uint8_t *restrict Cr1 = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for schedule(static) num_threads(lk->n_threads)
    for(int pos = 0; pos < len; pos++) {
        const int aq = alpha_lut[Y1[pos]];

        Y1[pos]  = lumakey_mix_q8(Y1[pos],  Y2[pos],  aq);
        Cb1[pos] = lumakey_mix_q8(Cb1[pos], Cb2[pos], aq);
        Cr1[pos] = lumakey_mix_q8(Cr1[pos], Cr2[pos], aq);
    }
}
