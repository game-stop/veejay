/*
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "buffer.h"

#define MAX_FRAMES 1500
#define BUFFER_PARAMS 3

#define P_DELAY    0
#define P_OPACITY  1
#define P_FEEDBACK 2

typedef struct {
    VJFrame frame;
    uint8_t *data;
    size_t capacity;
    int valid;
} buffer_slot_t;

typedef struct {
    buffer_slot_t *slots;
    int write_pos;
    int filled;
    int n_threads;
} buffer_t;

static void buffer_release(buffer_t *b)
{
    if(!b || !b->slots)
        return;

    for(int i = 0; i < MAX_FRAMES; i++)
    {
        if(b->slots[i].data)
            free(b->slots[i].data);

        b->slots[i].data = NULL;
        b->slots[i].capacity = 0;
        b->slots[i].valid = 0;
    }

    b->write_pos = 0;
    b->filled = 0;
}

vj_effect *buffer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = BUFFER_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_DELAY] = 0;
    ve->limits[1][P_DELAY] = MAX_FRAMES;
    ve->defaults[P_DELAY] = 50;

    ve->limits[0][P_OPACITY] = 0;
    ve->limits[1][P_OPACITY] = 255;
    ve->defaults[P_OPACITY] = 255;

    ve->limits[0][P_FEEDBACK] = 0;
    ve->limits[1][P_FEEDBACK] = 255;
    ve->defaults[P_FEEDBACK] = 0;

    ve->description = "Frame Delay";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Memory Tap", "Opacity", "Feedback");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 240, 84, 100, 20, 420, 0, 4, 80, VJ_BEAT_COST_MODERATE, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 32, 255, 84, 100, 18, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LOG, 0, 224, 72, 98, 36, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void) w;
    (void) h;

    return ve;
}

void *buffer_malloc(int w, int h)
{
    buffer_t *b = (buffer_t*) vj_calloc(sizeof(buffer_t));

    if(!b)
        return NULL;

    b->slots = (buffer_slot_t*) vj_calloc(sizeof(buffer_slot_t) * MAX_FRAMES);

    if(!b->slots) {
        free(b);
        return NULL;
    }

    b->write_pos = 0;
    b->filled = 0;
    b->n_threads = vje_advise_num_threads(w * h);

    return b;
}

void buffer_free(void *ptr)
{
    buffer_t *b = (buffer_t*) ptr;

    if(!b)
        return;

    buffer_release(b);

    if(b->slots)
        free(b->slots);

    free(b);
}

static int buffer_store_slot(buffer_slot_t *slot, VJFrame *frame)
{
    const int has_alpha = frame->data[3] != NULL;
    const size_t y_len = (size_t)frame->len;
    const size_t uv_len = (size_t)frame->uv_len;
    const size_t a_len = has_alpha ? y_len : 0;
    const size_t total = y_len + uv_len + uv_len + a_len;

    if(total == 0)
        return 0;

    if(slot->capacity < total)
    {
        uint8_t *data = (uint8_t*) vj_malloc(total);

        if(!data)
            return 0;

        if(slot->data)
            free(slot->data);

        slot->data = data;
        slot->capacity = total;
    }

    veejay_memcpy(&slot->frame, frame, sizeof(VJFrame));

    slot->frame.data[0] = slot->data;
    slot->frame.data[1] = slot->frame.data[0] + y_len;
    slot->frame.data[2] = slot->frame.data[1] + uv_len;
    slot->frame.data[3] = has_alpha ? (slot->frame.data[2] + uv_len) : NULL;

    veejay_memcpy(slot->frame.data[0], frame->data[0], frame->len);
    veejay_memcpy(slot->frame.data[1], frame->data[1], frame->uv_len);
    veejay_memcpy(slot->frame.data[2], frame->data[2], frame->uv_len);

    if(has_alpha)
        veejay_memcpy(slot->frame.data[3], frame->data[3], frame->len);

    slot->frame.len = frame->len;
    slot->frame.uv_len = frame->uv_len;
    slot->frame.ssm = frame->ssm;
    slot->valid = 1;

    return 1;
}

static int buffer_put_frame(buffer_t *b, VJFrame *frame)
{
    buffer_slot_t *slot = &b->slots[b->write_pos];
    const int pos = b->write_pos;

    if(!buffer_store_slot(slot, frame))
        return -1;

    b->write_pos++;
    if(b->write_pos >= MAX_FRAMES)
        b->write_pos = 0;

    if(b->filled < MAX_FRAMES)
        b->filled++;

    return pos;
}

static buffer_slot_t *buffer_get_tap(buffer_t *b, int delay)
{
    if(delay <= 0 || delay > b->filled)
        return NULL;

    int pos = b->write_pos - delay;
    if(pos < 0)
        pos += MAX_FRAMES;

    buffer_slot_t *slot = &b->slots[pos];

    if(!slot->valid || !slot->data)
        return NULL;

    return slot;
}

static inline uint8_t buffer_blend_u8(uint8_t a, uint8_t b, int opacity)
{
    return (uint8_t)((((int)a * (255 - opacity)) + ((int)b * opacity) + 127) / 255);
}

static void buffer_copy_slot(buffer_slot_t *slot, VJFrame *dst)
{
    VJFrame *src = &slot->frame;

    veejay_memcpy(dst->data[0], src->data[0], src->len);
    veejay_memcpy(dst->data[1], src->data[1], src->uv_len);
    veejay_memcpy(dst->data[2], src->data[2], src->uv_len);

    if(dst->data[3] && src->data[3])
        veejay_memcpy(dst->data[3], src->data[3], src->len);

    dst->len = src->len;
    dst->uv_len = src->uv_len;
    dst->stride[3] = src->stride[3];
    dst->ssm = src->ssm;
}

static void buffer_mix_slot(buffer_slot_t *slot, VJFrame *dst, int opacity)
{
    VJFrame *src = &slot->frame;
    const int len = dst->len;
    const int uv_len = dst->uv_len;
    uint8_t *restrict dstY = dst->data[0];
    uint8_t *restrict dstU = dst->data[1];
    uint8_t *restrict dstV = dst->data[2];
    const uint8_t *restrict srcY = src->data[0];
    const uint8_t *restrict srcU = src->data[1];
    const uint8_t *restrict srcV = src->data[2];

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        dstY[i] = buffer_blend_u8(dstY[i], srcY[i], opacity);

#pragma omp for schedule(static)
    for(int i = 0; i < uv_len; i++) {
        dstU[i] = buffer_blend_u8(dstU[i], srcU[i], opacity);
        dstV[i] = buffer_blend_u8(dstV[i], srcV[i], opacity);
    }

    if(dst->data[3] && src->data[3]) {
        uint8_t *restrict dstA = dst->data[3];
        const uint8_t *restrict srcA = src->data[3];

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            dstA[i] = buffer_blend_u8(dstA[i], srcA[i], opacity);
    }
}

static void buffer_feedback_slot(buffer_slot_t *slot, VJFrame *frame, int feedback)
{
    VJFrame *dst = &slot->frame;
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    uint8_t *restrict dstY = dst->data[0];
    uint8_t *restrict dstU = dst->data[1];
    uint8_t *restrict dstV = dst->data[2];
    const uint8_t *restrict srcY = frame->data[0];
    const uint8_t *restrict srcU = frame->data[1];
    const uint8_t *restrict srcV = frame->data[2];

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        dstY[i] = buffer_blend_u8(dstY[i], srcY[i], feedback);

#pragma omp for schedule(static)
    for(int i = 0; i < uv_len; i++) {
        dstU[i] = buffer_blend_u8(dstU[i], srcU[i], feedback);
        dstV[i] = buffer_blend_u8(dstV[i], srcV[i], feedback);
    }

    if(dst->data[3] && frame->data[3]) {
        uint8_t *restrict dstA = dst->data[3];
        const uint8_t *restrict srcA = frame->data[3];

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            dstA[i] = buffer_blend_u8(dstA[i], srcA[i], feedback);
    }
}

static void buffer_black(VJFrame *frame)
{
    veejay_memset(frame->data[0], pixel_Y_lo_, frame->len);
    veejay_memset(frame->data[1], 128, frame->uv_len);
    veejay_memset(frame->data[2], 128, frame->uv_len);

    if(frame->data[3])
        veejay_memset(frame->data[3], 0, frame->len);
}

void buffer_apply(void *ptr, VJFrame *frame, int *args)
{
    buffer_t *b = (buffer_t*) ptr;
    int delay = args[P_DELAY];
    const int opacity = args[P_OPACITY];
    const int feedback = args[P_FEEDBACK];

    if(delay < 0)
        delay = 0;
    else if(delay > MAX_FRAMES)
        delay = MAX_FRAMES;

    const int write_slot = buffer_put_frame(b, frame);
    if(write_slot < 0)
        return;

    if(delay == 0)
        return;

    buffer_slot_t *tap = buffer_get_tap(b, delay);

    if(!tap) {
        if(opacity >= 255)
            buffer_black(frame);
        return;
    }

    if(opacity >= 255)
        buffer_copy_slot(tap, frame);

    const int do_mix = opacity > 0 && opacity < 255;
    const int do_feedback_mix = feedback > 0 && feedback < 255;

    if(do_mix || do_feedback_mix) {
#pragma omp parallel num_threads(b->n_threads)
        {
            if(do_mix)
                buffer_mix_slot(tap, frame, opacity);

            if(do_feedback_mix)
                buffer_feedback_slot(&b->slots[write_slot], frame, feedback);
        }
    }

    if(feedback >= 255)
        buffer_store_slot(&b->slots[write_slot], frame);
}

