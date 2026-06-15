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
#include "buffer.h"

#define MAX_FRAMES 1500

typedef struct {
    VJFrame frame;
    uint8_t *data;
    size_t capacity;
    int valid;
} buffer_slot_t;

typedef struct {
    buffer_slot_t *slots;
    int write_pos;
    int read_pos;
    int ready;
    int length;
    int filled;
} buffer_t;

static void buffer_reset(buffer_t *b)
{
    if(!b || !b->slots)
        return;

    for(int i = 0; i < MAX_FRAMES; i++)
        b->slots[i].valid = 0;

    b->write_pos = 0;
    b->read_pos = 0;
    b->ready = 0;
    b->length = 0;
    b->filled = 0;
}

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
}

vj_effect *buffer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = MAX_FRAMES;
    ve->defaults[0] = 50;

    ve->description = "Frame Delay";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Frame Delay");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MEMORY, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );

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

    (void) w;
    (void) h;

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

static int put_frame(buffer_t *b, VJFrame *frame)
{
    if(!b || !frame || !b->slots || b->length <= 0)
        return 0;

    buffer_slot_t *slot = &b->slots[b->write_pos];

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

    if(b->filled < b->length)
        b->filled++;

    if(b->filled >= b->length)
        b->ready = 1;

    b->write_pos = (b->write_pos + 1) % b->length;

    return 1;
}

static int get_frame(buffer_t *b, VJFrame *dst)
{
    if(!b || !dst || !b->slots || b->length <= 0)
        return 0;

    buffer_slot_t *slot = &b->slots[b->read_pos];

    if(!slot->valid || !slot->data)
        return 0;

    VJFrame *src = &slot->frame;

    if(dst->data[0] && src->data[0])
        veejay_memcpy(dst->data[0], src->data[0], src->len);

    if(dst->data[1] && src->data[1])
        veejay_memcpy(dst->data[1], src->data[1], src->uv_len);

    if(dst->data[2] && src->data[2])
        veejay_memcpy(dst->data[2], src->data[2], src->uv_len);

    if(dst->data[3] && src->data[3])
        veejay_memcpy(dst->data[3], src->data[3], src->len);

    dst->len = src->len;
    dst->uv_len = src->uv_len;
    dst->stride[3] = src->stride[3];
    dst->ssm = src->ssm;

    b->read_pos = (b->read_pos + 1) % b->length;

    return 1;
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
    int delay = args[0];

    if(delay < 0)
        delay = 0;
    else if(delay > MAX_FRAMES)
        delay = MAX_FRAMES;

    if(delay == 0) {
        if(b->length != 0)
            buffer_reset(b);
        return;
    }

    if(b->length != delay) {
        buffer_reset(b);
        b->length = delay;
    }

    if(!put_frame(b, frame))
        return;

    if(b->ready) {
        if(!get_frame(b, frame))
            buffer_black(frame);
    }
    else {
        buffer_black(frame);
    }
}
