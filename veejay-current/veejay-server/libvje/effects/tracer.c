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

#define TRACER_PARAMS 5
#define P_OPACITY      0
#define P_BUFFER_LEN   1
#define P_MIX_DRIVE    2
#define P_FEED_DRIVE   3
#define P_CHROMA_TRAIL 4

typedef struct {
    uint8_t *trace_buffer[3];

    float opacity_s;
    float buffer_s;
    float mix_drive_s;
    float feed_drive_s;
    float chroma_trail_s;
    int state_ready;

    int n_threads;
} tracer_t;

static inline int tracer_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float tracer_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
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

static inline int tracer_to_q8(int opacity)
{
    opacity = tracer_clampi(opacity, 0, 255);
    return (opacity * 256 + 127) / 255;
}

static inline float tracer_smoothf(float oldv, float target, float coeff)
{
    return oldv + (target - oldv) * coeff;
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
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_OPACITY] = 0;
    ve->limits[1][P_OPACITY] = 255;
    ve->defaults[P_OPACITY] = 150;

    ve->limits[0][P_BUFFER_LEN] = 1;
    ve->limits[1][P_BUFFER_LEN] = MAX_OLD_FRAMES;
    ve->defaults[P_BUFFER_LEN] = 8;

    ve->limits[0][P_MIX_DRIVE] = 0;
    ve->limits[1][P_MIX_DRIVE] = 1000;
    ve->defaults[P_MIX_DRIVE] = 0;

    ve->limits[0][P_FEED_DRIVE] = 0;
    ve->limits[1][P_FEED_DRIVE] = 1000;
    ve->defaults[P_FEED_DRIVE] = 0;

    ve->limits[0][P_CHROMA_TRAIL] = 0;
    ve->limits[1][P_CHROMA_TRAIL] = 1000;
    ve->defaults[P_CHROMA_TRAIL] = 1000;

    ve->description = "Tracer (Frame Echo)";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Buffer length",
        "Mix Drive",
        "Feed Drive",
        "Chroma Trail"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 64, 255, 88, 100, 8, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TRAIL_LENGTH, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 64, 84, 100, 10, 480, 0, 1, 100, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 90, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 120, 1000, 72, 96, 240, 1700, 0, 5, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 300, 1000, 70, 96, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *tracer_malloc(int w, int h)
{
    tracer_t *t = (tracer_t*) vj_calloc(sizeof(tracer_t));
    if(!t)
        return NULL;

    const int len = w * h;

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

    t->opacity_s = 150.0f;
    t->buffer_s = 8.0f;
    t->mix_drive_s = 0.0f;
    t->feed_drive_s = 0.0f;
    t->chroma_trail_s = 1000.0f;
    t->state_ready = 0;

    t->n_threads = vje_advise_num_threads(len);

    return (void*) t;
}

void tracer_free(void *ptr)
{
    tracer_t *t = (tracer_t*) ptr;

    free(t->trace_buffer[0]);
    free(t);
}



void tracer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    tracer_t *t = (tracer_t*) ptr;

    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    const int opacity_arg = args[P_OPACITY];
    const int buffer_arg = args[P_BUFFER_LEN];
    const int mix_drive_arg = args[P_MIX_DRIVE];
    const int feed_drive_arg = args[P_FEED_DRIVE];
    const int chroma_trail_arg = args[P_CHROMA_TRAIL];

    const float param_coeff = 0.185f;

    if(!t->state_ready) {
        t->opacity_s = (float)opacity_arg;
        t->buffer_s = (float)buffer_arg;
        t->mix_drive_s = (float)mix_drive_arg;
        t->feed_drive_s = (float)feed_drive_arg;
        t->chroma_trail_s = (float)chroma_trail_arg;
        t->state_ready = 1;
    } else {
        t->opacity_s = tracer_smoothf(t->opacity_s, (float)opacity_arg, param_coeff);
        t->buffer_s = tracer_smoothf(t->buffer_s, (float)buffer_arg, param_coeff * 0.80f);
        t->mix_drive_s = tracer_smoothf(t->mix_drive_s, (float)mix_drive_arg, param_coeff);
        t->feed_drive_s = tracer_smoothf(t->feed_drive_s, (float)feed_drive_arg, param_coeff * 0.90f);
        t->chroma_trail_s = tracer_smoothf(t->chroma_trail_s, (float)chroma_trail_arg, param_coeff * 0.76f);
    }

    const float mix_t = tracer_clampf(t->mix_drive_s * 0.001f, 0.0f, 1.0f);
    const float feed_t = tracer_clampf(t->feed_drive_s * 0.001f, 0.0f, 1.0f);
    const float chroma_t = tracer_clampf(t->chroma_trail_s * 0.001f, 0.0f, 1.0f);

    const int opacity_i = tracer_clampi((int)(t->opacity_s + 0.5f), 0, 255);
    const int buffer_i = tracer_clampi((int)(t->buffer_s + 0.5f), 1, MAX_OLD_FRAMES);

    int wet_q8 = tracer_to_q8(opacity_i);
    {
        const int headroom = 256 - wet_q8;
        const float lift = mix_t * 0.68f;
        wet_q8 += (int)((float)headroom * lift + 0.5f);
        wet_q8 = tracer_clampi(wet_q8, 0, 256);
    }

    int feed = 256 / buffer_i;
    feed += (int)(feed_t * 132.0f + 0.5f);
    feed = tracer_clampi(feed, 1, 224);

    const int decay = 256 - feed;

    int chroma_wet_q8 = (int)((float)wet_q8 * (0.56f + chroma_t * 0.44f) + chroma_t * 18.0f + 0.5f);
    chroma_wet_q8 = tracer_clampi(chroma_wet_q8, 0, 256);

    int chroma_feed = (int)((float)feed * (0.50f + chroma_t * 0.50f) + chroma_t * 18.0f + 0.5f);
    chroma_feed = tracer_clampi(chroma_feed, 1, 224);
    const int chroma_decay = 256 - chroma_feed;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

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
