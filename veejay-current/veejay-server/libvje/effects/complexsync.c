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
#include "complexsync.h"

typedef struct {
    uint8_t *c_outofsync_buffer[4];
    int complex_not_completed;
    int position;
} complexsync_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *complexsync_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = height - 1;  ve->defaults[0] = 36;
    ve->limits[0][1] = 0; ve->limits[1][1] = 1;           ve->defaults[1] = 1;
    ve->limits[0][2] = 1; ve->limits[1][2] = 25 * 10;      ve->defaults[2] = 1;

    ve->description = "Out of Sync -Replace selection-";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Vertical size", "Mode", "Framespeed");
    ve->is_transition_ready_func = complexsync_ready;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, (height > 8 ? height / 2 : height - 1), 82, 100, 0, 480, 0, 1, 80, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 160, 84, 100, 0, 420, 0, 1, 80, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

int complexsync_ready(void *ptr, int width, int height)
{
    complexsync_t *c = (complexsync_t*) ptr;

    (void) width;
    (void) height;

    return c ? !c->complex_not_completed : 1;
}

void *complexsync_malloc(int width, int height)
{
    complexsync_t *c = (complexsync_t*) vj_calloc(sizeof(complexsync_t));

    if(!c)
        return NULL;

    const int len = width * height;

    c->c_outofsync_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 3);

    if(!c->c_outofsync_buffer[0]) {
        free(c);
        return NULL;
    }

    c->c_outofsync_buffer[1] = c->c_outofsync_buffer[0] + len;
    c->c_outofsync_buffer[2] = c->c_outofsync_buffer[1] + len;
    c->c_outofsync_buffer[3] = NULL;

    vj_frame_clear1(c->c_outofsync_buffer[0], pixel_Y_lo_, len);
    vj_frame_clear1(c->c_outofsync_buffer[1], 128, len * 2);

    return c;
}

void complexsync_free(void *ptr)
{
    complexsync_t *c = (complexsync_t*) ptr;

    if(!c)
        return;

    if(c->c_outofsync_buffer[0])
        free(c->c_outofsync_buffer[0]);

    free(c);
}

void complexsync_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    complexsync_t *c = (complexsync_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int val = clampi(args[0], 1, height - 1);
    const int auto_inc = clampi(args[1], 0, 1);
    int duration = clampi(args[2], 1, 25 * 10);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int planes[4] = { len, len, len, 0 };

    if(auto_inc == 1)
    {
        c->position += (val / duration) + 1;

        if(c->position > height - 2)
            c->position = 1;
    }
    else
    {
        c->position = val;
    }

    c->position = clampi(c->position, 1, height - 1);

    const int region = width * c->position;

    vj_frame_copy(frame->data, c->c_outofsync_buffer, planes);
    vj_frame_copy(frame2->data, frame->data, planes);

    c->complex_not_completed = (len - region) > 0;

    if(c->complex_not_completed)
    {
        uint8_t *dest[4] = { Y + region, Cb + region, Cr + region, NULL };
        int dst_strides[4] = { len - region, len - region, len - region, 0 };

        vj_frame_copy(c->c_outofsync_buffer, dest, dst_strides);
    }
}
