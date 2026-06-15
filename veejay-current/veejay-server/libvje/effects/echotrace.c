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

#define ECHOTRACE_PARAMS 2

#define P_INTENSITY   0
#define P_DECAY       1

typedef struct {
    uint32_t *trace_y;
    int32_t *trace_u;
    int32_t *trace_v;
    int n_threads;
} echotrace_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t echotrace_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline int echotrace_div255(int x)
{
    return ((x + 1) + (x >> 8)) >> 8;
}

static inline int echotrace_mul255_signed(int x, int q)
{
    int v = x * q;
    return v >= 0 ? ((v + 127) / 255) : -(((-v) + 127) / 255);
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

    ve->limits[0][P_INTENSITY] = 0;   ve->limits[1][P_INTENSITY] = 255;        ve->defaults[P_INTENSITY] = 255;
    ve->limits[0][P_DECAY] = 1;       ve->limits[1][P_DECAY] = MAX_OLD_FRAMES; ve->defaults[P_DECAY] = 16;

    ve->description = "Frame Echo";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Intensity", "Decay");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 96, 255, 16, 62,  700, 2800, 0, 84,
        VJ_BEAT_MEMORY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 8,  128, 12, 46, 1100, 4200, 0, 66
    );

    return ve;
}

void *echotrace_malloc(int w, int h)
{
    echotrace_t *t = (echotrace_t*) vj_calloc(sizeof(echotrace_t));

    if(!t)
        return NULL;

    const int len = w * h;

    t->trace_y = (uint32_t*) vj_calloc(sizeof(uint32_t) * (size_t)len);
    t->trace_u = (int32_t*) vj_calloc(sizeof(int32_t) * (size_t)len * 2);

    if(!t->trace_y || !t->trace_u) {
        echotrace_free(t);
        return NULL;
    }

    t->trace_v = t->trace_u + len;
    t->n_threads = vje_advise_num_threads(len);

    return t;
}

void echotrace_free(void *ptr)
{
    echotrace_t *t = (echotrace_t*) ptr;

    if(!t)
        return;

    if(t->trace_y)
        free(t->trace_y);

    if(t->trace_u)
        free(t->trace_u);

    free(t);
}

void echotrace_apply(void *ptr, VJFrame *frame, int *args)
{
    echotrace_t *t = (echotrace_t*) ptr;

    const int intensity = clampi(args[P_INTENSITY], 0, 255);
    const int decay_val = clampi(args[P_DECAY], 1, MAX_OLD_FRAMES);

    if(intensity <= 0)
        return;

    const int len = frame->len;
    const int d_m = decay_val > 1 ? decay_val - 1 : 0;
    const int rounding = 1 << (FP_SHIFT - 1);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint32_t *restrict accY = t->trace_y;
    int32_t *restrict accU = t->trace_u;
    int32_t *restrict accV = t->trace_v;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int i = 0; i < len; i++) {
        const uint32_t fp_y = (uint32_t)echotrace_div255((int)Y[i] * intensity) << FP_SHIFT;
        const int32_t u_in = (int32_t)U[i] - 128;
        const int32_t v_in = (int32_t)V[i] - 128;
        const int32_t fp_u = echotrace_mul255_signed(u_in, intensity) << FP_SHIFT;
        const int32_t fp_v = echotrace_mul255_signed(v_in, intensity) << FP_SHIFT;

        accY[i] = ((accY[i] * (uint32_t)d_m) + fp_y) / (uint32_t)decay_val;
        accU[i] = ((accU[i] * d_m) + fp_u) / decay_val;
        accV[i] = ((accV[i] * d_m) + fp_v) / decay_val;

        Y[i] = (uint8_t)((accY[i] + (uint32_t)rounding) >> FP_SHIFT);
        U[i] = echotrace_u8(((accU[i] + rounding) >> FP_SHIFT) + 128);
        V[i] = echotrace_u8(((accV[i] + rounding) >> FP_SHIFT) + 128);
    }
}
