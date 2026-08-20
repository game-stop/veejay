/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <stdlib.h>
#include <veejaycore/vjmem.h>
#include "whiteframe.h"

#define WHITEFRAME_PARAMS 4

#define P_THRESHOLD    0
#define P_SOFTNESS     1
#define P_EDGE_GLOW    2
#define P_CHROMA_EDGE  3

typedef struct {
    int n_threads;
    int env_ready;
    float threshold_env;
    float softness_env;
    float glow_env;
    float chroma_env;
} whiteframe_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline uint8_t whiteframe_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int whiteframe_abs_i(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t blend_u8(uint8_t a, uint8_t b, int t)
{
    return (uint8_t)((((int)a * (255 - t)) + ((int)b * t) + 127) / 255);
}

static inline float whiteframe_slew(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

vj_effect *whiteframe_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = WHITEFRAME_PARAMS;
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_THRESHOLD]   = 0;    ve->limits[1][P_THRESHOLD]   = 255; ve->defaults[P_THRESHOLD]   = 220;
    ve->limits[0][P_SOFTNESS]    = 1;    ve->limits[1][P_SOFTNESS]    = 128; ve->defaults[P_SOFTNESS]    = 24;
    ve->limits[0][P_EDGE_GLOW]   = 0;    ve->limits[1][P_EDGE_GLOW]   = 255; ve->defaults[P_EDGE_GLOW]   = 0;
    ve->limits[0][P_CHROMA_EDGE] = 0;    ve->limits[1][P_CHROMA_EDGE] = 255; ve->defaults[P_CHROMA_EDGE] = 0;

    ve->description = "Replace White";
    ve->sub_format  = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Softness",
        "Edge Glow",
        "Chroma Edge"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 120, 250, 94, 100, 8, 520, 0, 2, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 96, 86, 100, 6, 440, 24, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 94, 100, 4, 440, 24, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 255, 72, 98, 100, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *whiteframe_malloc(int w, int h)
{
    whiteframe_t *wf = (whiteframe_t*) vj_calloc(sizeof(whiteframe_t));

    if(!wf)
        return NULL;

    wf->n_threads = vje_advise_num_threads(w * h);

    wf->env_ready = 0;
    wf->threshold_env = 220.0f;
    wf->softness_env = 24.0f;
    wf->glow_env = 0.0f;
    wf->chroma_env = 0.0f;

    return (void*) wf;
}

void whiteframe_free(void *ptr)
{
    whiteframe_t *wf = (whiteframe_t*) ptr;

    free(wf);
}

void whiteframe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    whiteframe_t *wf = (whiteframe_t*) ptr;

    const int len = frame->len;

    const int threshold_arg = args[P_THRESHOLD];
    const int softness_arg = args[P_SOFTNESS];
    const int glow_arg = args[P_EDGE_GLOW];
    const int chroma_arg = args[P_CHROMA_EDGE];

    if(!wf->env_ready) {
        wf->threshold_env = (float)threshold_arg;
        wf->softness_env = (float)softness_arg;
        wf->glow_env = (float)glow_arg;
        wf->chroma_env = (float)chroma_arg;
        wf->env_ready = 1;
    }
    else {
        wf->threshold_env = whiteframe_slew(wf->threshold_env, (float)threshold_arg, 0.265f, 0.092f);
        wf->softness_env = whiteframe_slew(wf->softness_env, (float)softness_arg, 0.245f, 0.088f);
        wf->glow_env = whiteframe_slew(wf->glow_env, (float)glow_arg, 0.325f, 0.115f);
        wf->chroma_env = whiteframe_slew(wf->chroma_env, (float)chroma_arg, 0.285f, 0.105f);
    }

    const int threshold = (int)(wf->threshold_env + 0.5f);
    const int softness = (int)(wf->softness_env + 0.5f);
    const int edge_glow = (int)(wf->glow_env + 0.5f);
    const int chroma_edge = (int)(wf->chroma_env + 0.5f);
    const int n_threads = wf->n_threads;

    int full = threshold - softness;
    int edge = threshold + softness;

    if(full < 0)
        full = 0;
    if(edge > 255)
        edge = 255;
    if(edge <= full)
        edge = full + 1;

    const int denom = edge - full;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const int y  = Y[i];
        const int cb = Cb[i];
        const int cr = Cr[i];

        const int abs_cb = whiteframe_abs_i(cb - 128);
        const int abs_cr = whiteframe_abs_i(cr - 128);

        const int light = y - ((abs_cb + abs_cr) << 1);

        int t = ((light - full) * 255) / denom;
        if(t < 0)
            t = 0;
        else if(t > 255)
            t = 255;

        int out_y  = blend_u8((uint8_t)y,  Y2[i],  t);
        int out_cb = blend_u8((uint8_t)cb, Cb2[i], t);
        int out_cr = blend_u8((uint8_t)cr, Cr2[i], t);

        if(edge_glow > 0 || chroma_edge > 0) {
            const int rim = (t * (255 - t) + 127) / 255;

            if(edge_glow > 0)
                out_y += (rim * edge_glow + 127) / 255;

            if(chroma_edge > 0) {
                const int cq = (rim * chroma_edge + 127) / 255;
                out_cb = blend_u8((uint8_t)out_cb, Cb2[i], cq);
                out_cr = blend_u8((uint8_t)out_cr, Cr2[i], cq);
            }
        }

        Y[i]  = whiteframe_u8(out_y);
        Cb[i] = whiteframe_u8(out_cb);
        Cr[i] = whiteframe_u8(out_cr);
    }
}
