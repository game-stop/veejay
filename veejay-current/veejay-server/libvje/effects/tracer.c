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
#include "tracer.h"

#define MAX_OLD_FRAMES 128

#define TRACER_PARAMS 4
#define P_OPACITY     0
#define P_BUFFER_LEN  1
#define P_BEAT_PUSH   2
#define P_BEAT_SMOOTH 3

typedef struct {
    uint8_t *trace_buffer[3];
    float beat_env;
    int n_threads;
} tracer_t;

static inline int tracer_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int tracer_beat_shape(int beat_push)
{
    beat_push = tracer_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return tracer_clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

static inline uint8_t tracer_mix_y(uint8_t a, uint8_t b, int q8)
{
    q8 = tracer_clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t tracer_mix_uv(uint8_t a, uint8_t b, int q8)
{
    q8 = tracer_clampi(q8, 0, 256);

    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

vj_effect *tracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRACER_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_OPACITY] = 0;
    ve->limits[1][P_OPACITY] = 255;
    ve->defaults[P_OPACITY] = 150;

    ve->limits[0][P_BUFFER_LEN] = 1;
    ve->limits[1][P_BUFFER_LEN] = MAX_OLD_FRAMES;
    ve->defaults[P_BUFFER_LEN] = 8;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "Tracer (Frame Echo)";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Buffer length",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS,                       32,                 220,                8,  30, 1200, 3000, 0,    45,    /* Opacity */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,                  64,                 6,  22, 1800, 4200, 900,  30,    /* Buffer length */
        VJ_BEAT_KICK,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,   0,                  780,                18, 72, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY,                      280,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

    return ve;
}

void *tracer_malloc(int w, int h)
{
    tracer_t *t = (tracer_t*) vj_calloc(sizeof(tracer_t));
    if(!t)
        return NULL;

    const int len = w * h;
    if(len <= 0) {
        free(t);
        return NULL;
    }

    t->trace_buffer[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!t->trace_buffer[0]) {
        free(t);
        return NULL;
    }

    t->trace_buffer[1] = t->trace_buffer[0] + len;
    t->trace_buffer[2] = t->trace_buffer[1] + len;

    veejay_memset(t->trace_buffer[0], pixel_Y_lo_, len);
    veejay_memset(t->trace_buffer[1], 128,         len);
    veejay_memset(t->trace_buffer[2], 128,         len);

    t->beat_env = 0.0f;

    t->n_threads = vje_advise_num_threads(len);

    return (void*) t;
}

void tracer_free(void *ptr)
{
    tracer_t *t = (tracer_t*) ptr;

    if(!t)
        return;

    if(t->trace_buffer[0])
        free(t->trace_buffer[0]);

    free(t);
}

void tracer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    tracer_t *t = (tracer_t*) ptr;

    if(!t || !frame || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int len = frame->len;
    if(len <= 0)
        return;

    const int uv_len = frame->ssm ? len : frame->uv_len;
    if(uv_len <= 0)
        return;

    const int opacity = tracer_clampi(args[P_OPACITY], 0, 255);
    const int buffer_len = tracer_clampi(args[P_BUFFER_LEN], 1, MAX_OLD_FRAMES);
    const int beat_push = tracer_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = tracer_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int shaped = tracer_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)beat_smooth * 0.001f;
    const float attack = 0.20f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.035f + (1.0f - smooth_t) * 0.090f;

    if(target > t->beat_env)
        t->beat_env += (target - t->beat_env) * attack;
    else
        t->beat_env += (target - t->beat_env) * release;

    if(t->beat_env < 0.0001f)
        t->beat_env = 0.0f;
    else if(t->beat_env > 1.0f)
        t->beat_env = 1.0f;

    const int beat_q = tracer_clampi((int)(t->beat_env * 1000.0f + 0.5f), 0, 1000);

    int wet_q8 = (opacity * 256 + 127) / 255;
    wet_q8 = tracer_clampi(wet_q8 + ((beat_q * 58 + 500) / 1000), 0, 256);

    int feed = 256 / buffer_len;
    feed = tracer_clampi(feed + ((beat_q * 54 + 500) / 1000), 1, 160);

    const int decay = 256 - feed;

    const int chroma_wet_q8 = tracer_clampi(wet_q8 - ((beat_q * 20 + 500) / 1000), 0, 256);
    const int chroma_feed = tracer_clampi(feed - ((beat_q * 16 + 500) / 1000), 1, 144);
    const int chroma_decay = 256 - chroma_feed;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = (frame2 && frame2->data[0]) ? frame2->data[0] : frame->data[0];
    const uint8_t *restrict Cb2 = (frame2 && frame2->data[1]) ? frame2->data[1] : frame->data[1];
    const uint8_t *restrict Cr2 = (frame2 && frame2->data[2]) ? frame2->data[2] : frame->data[2];

    uint8_t *restrict tY = t->trace_buffer[0];
    uint8_t *restrict tU = t->trace_buffer[1];
    uint8_t *restrict tV = t->trace_buffer[2];

#pragma omp parallel num_threads(t->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            const int mixed = ((int)Y[i] + (int)Y2[i] + 1) >> 1;
            const int accum = (((int)tY[i] * decay) + (mixed * feed) + 128) >> 8;

            tY[i] = (uint8_t)CLAMP_Y(accum);
            Y[i] = tracer_mix_y(Y[i], tY[i], wet_q8);
        }

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            const int mixed_u = (((int)Cb[i] - 128) + ((int)Cb2[i] - 128)) >> 1;
            const int mixed_v = (((int)Cr[i] - 128) + ((int)Cr2[i] - 128)) >> 1;

            int acc_u = (int)tU[i] - 128;
            int acc_v = (int)tV[i] - 128;

            acc_u = ((acc_u * chroma_decay) + (mixed_u * chroma_feed) + 128) >> 8;
            acc_v = ((acc_v * chroma_decay) + (mixed_v * chroma_feed) + 128) >> 8;

            tU[i] = (uint8_t)CLAMP_UV(acc_u + 128);
            tV[i] = (uint8_t)CLAMP_UV(acc_v + 128);

            Cb[i] = tracer_mix_uv(Cb[i], tU[i], chroma_wet_q8);
            Cr[i] = tracer_mix_uv(Cr[i], tV[i], chroma_wet_q8);
        }
    }
}
