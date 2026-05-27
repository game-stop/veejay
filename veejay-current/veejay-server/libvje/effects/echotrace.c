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
#include "echotrace.h"

#include <stdint.h>

#define MAX_OLD_FRAMES 256
#define FP_SHIFT 8

#define ECHOTRACE_PARAMS 4

#define P_INTENSITY   0
#define P_DECAY       1
#define P_BEAT_PUSH   2
#define P_BEAT_SMOOTH 3

typedef struct {
    uint32_t *trace_buffer[3];
    float beat_env;
    int n_threads;
} echotrace_t;

static inline int CLAMP(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int echotrace_beat_shape(int beat_push)
{
    int lin;
    int sq;

    beat_push = CLAMP(beat_push, 0, 1000);

    lin = beat_push;
    sq = (beat_push * beat_push + 500) / 1000;

    return CLAMP((lin * 40 + sq * 60 + 50) / 100, 0, 1000);
}

vj_effect *echotrace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = ECHOTRACE_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    ve->limits[0][P_INTENSITY]   = 0;
    ve->limits[1][P_INTENSITY]   = 255;
    ve->limits[0][P_DECAY]       = 1;
    ve->limits[1][P_DECAY]       = MAX_OLD_FRAMES;
    ve->limits[0][P_BEAT_PUSH]   = 0;
    ve->limits[1][P_BEAT_PUSH]   = 1000;
    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;

    ve->defaults[P_INTENSITY]   = 255;
    ve->defaults[P_DECAY]       = 16;
    ve->defaults[P_BEAT_PUSH]   = 0;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Frame Echo";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Intensity",
        "Decay",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_PHRASE_ONLY,                   80,                 255,                6,  22, 1800, 4200, 900,  28,    /* Intensity */
        VJ_BEAT_MEMORY,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 4,                96,                 6,  24, 1800, 4200, 900,  34,    /* Decay */
        VJ_BEAT_INTENSITY,  VJ_BEAT_F_CONTINUOUS,                    0,                  760,                16, 68, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,     VJ_BEAT_F_PHRASE_ONLY,                   260,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *echotrace_malloc(int w, int h)
{
    echotrace_t *t = (echotrace_t*) vj_calloc(sizeof(echotrace_t));
    if(!t)
        return NULL;

    const int pixels = w * h;
    const int len = pixels * 3;

    t->trace_buffer[0] = (uint32_t *) vj_calloc(sizeof(uint32_t) * len);
    if(!t->trace_buffer[0]) {
        free(t);
        return NULL;
    }

    t->trace_buffer[1] = t->trace_buffer[0] + pixels;
    t->trace_buffer[2] = t->trace_buffer[1] + pixels;

    t->beat_env = 0.0f;
    t->n_threads = vje_advise_num_threads(pixels);
    if(t->n_threads <= 0)
        t->n_threads = 1;

    return (void*) t;
}

void echotrace_free(void *ptr)
{
    echotrace_t *t = (echotrace_t*) ptr;

    if(t) {
        if(t->trace_buffer[0])
            free(t->trace_buffer[0]);
        free(t);
    }
}

void echotrace_apply(void *ptr, VJFrame *frame, int *args)
{
    echotrace_t *t = (echotrace_t*) ptr;

    if(!t || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int base_intensity = CLAMP(args[P_INTENSITY], 0, 255);
    const int base_decay = CLAMP(args[P_DECAY], 1, MAX_OLD_FRAMES);
    const int beat_push = CLAMP(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = CLAMP(args[P_BEAT_SMOOTH], 0, 1000);

    if(base_intensity <= 0 && beat_push <= 0)
        return;

    const int shaped = echotrace_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)beat_smooth * 0.001f;
    const float attack = 0.20f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.028f + (1.0f - smooth_t) * 0.105f;

    if(target > t->beat_env)
        t->beat_env += (target - t->beat_env) * attack;
    else
        t->beat_env += (target - t->beat_env) * release;

    if(t->beat_env < 0.0001f)
        t->beat_env = 0.0f;
    else if(t->beat_env > 1.0f)
        t->beat_env = 1.0f;

    const int beat_q = CLAMP((int)(t->beat_env * 1000.0f + 0.5f), 0, 1000);

    int intensity = base_intensity;

    if(beat_q > 0 && intensity < 255) {
        const int lift = ((255 - intensity) * beat_q * 760 + 500000) / 1000000;
        intensity = CLAMP(intensity + lift, 0, 255);
    }

    int decay_val = base_decay;

    if(beat_q > 0 && decay_val > 1) {
        const int pull_q = (beat_q * 620 + 500) / 1000;
        const int pull = ((decay_val - 1) * pull_q + 500) / 1000;
        decay_val = CLAMP(decay_val - pull, 1, MAX_OLD_FRAMES);
    }

    const int len = frame->len;
    const int d_m = (decay_val > 1) ? (decay_val - 1) : 0;
    const int rounding = (1 << (FP_SHIFT - 1));

#pragma omp parallel num_threads(t->n_threads)
    {
        uint8_t *restrict Y = frame->data[0];
        uint32_t *restrict accY = t->trace_buffer[0];

#pragma omp for schedule(static)
        for(int x = 0; x < len; x++) {
            uint32_t fp_new = ((Y[x] * intensity) >> 8) << FP_SHIFT;
            accY[x] = ((accY[x] * d_m) + fp_new) / decay_val;
            Y[x] = (uint8_t)((accY[x] + rounding) >> FP_SHIFT);
        }

        for(int c = 1; c < 3; c++) {
            uint8_t *restrict C = frame->data[c];
            int32_t *restrict accC = (int32_t*) t->trace_buffer[c];

#pragma omp for schedule(static)
            for(int x = 0; x < len; x++) {
                int32_t signed_in = (int32_t) C[x] - 128;
                int32_t fp_new = ((signed_in * intensity) >> 8) << FP_SHIFT;

                accC[x] = ((accC[x] * d_m) + fp_new) / decay_val;

                int32_t res = (accC[x] + rounding) >> FP_SHIFT;
                C[x] = (uint8_t) CLAMP(res + 128, 0, 255);
            }
        }
    }
}
