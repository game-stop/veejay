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
#include "3bar.h"

static inline int bar_max_i(int a, int b)
{
    return a > b ? a : b;
}

static inline unsigned int bar_sanitize_divider(int divider, unsigned int height)
{
    if(divider < 1)
        divider = 1;
    if(height > 0 && (unsigned int)divider > height)
        divider = (int)height;
    return (unsigned int)divider;
}

static inline unsigned int bar_wrap_step(int step, unsigned int span)
{
    if(span == 0 || step <= 0)
        return 0;
    return ((unsigned int)step) % span;
}

static inline void bar_copy_wrap_row(uint8_t *restrict dst, const uint8_t *restrict src, unsigned int width, unsigned int shift)
{
    if(width == 0)
        return;

    shift %= width;

    if(shift == 0) {
        veejay_memcpy(dst, src, width);
        return;
    }

    const unsigned int right = width - shift;

    veejay_memcpy(dst,         src + shift, right);
    veejay_memcpy(dst + right, src,         shift);
}

vj_effect *bar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 4;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 1;
    ve->defaults[4] = 2;

    ve->limits[0][0] = 1; ve->limits[1][0] = height;
    ve->limits[0][1] = 0; ve->limits[1][1] = height;
    ve->limits[0][2] = 0; ve->limits[1][2] = height;
    ve->limits[0][3] = 0; ve->limits[1][3] = width;
    ve->limits[0][4] = 0; ve->limits[1][4] = width;

    ve->sub_format = 1;
    ve->description = "Horizontal Sliding Bars";
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Divider", "Top Y", "Bot Y", "Top X", "Bot X");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, bar_max_i(10, height / 48 + 1), 72, 100, 0, 220, 0, 1, 0, VJ_BEAT_COST_CHEAP, 74, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, bar_max_i(12, height / 42 + 1), 74, 100, 0, 240, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_FLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, bar_max_i(22, width / 24 + 1), 88, 100, 0, 180, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_FLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, bar_max_i(28, width / 20 + 1), 90, 100, 0, 200, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

typedef struct {
    unsigned int bar_top_auto_x;
    unsigned int bar_bot_auto_x;
    unsigned int bar_top_auto_y;
    unsigned int bar_bot_auto_y;
} bar_t;

void *bar_malloc(int w, int h)
{
    return vj_calloc(sizeof(bar_t));
}

void bar_free(void *ptr)
{
    free(ptr);
}

void bar_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    bar_t *bar = (bar_t *) ptr;

    const unsigned int width = (unsigned int) frame->width;
    const unsigned int height = (unsigned int) frame->height;

    const unsigned int divider = bar_sanitize_divider(args[0], height);
    const unsigned int top_height = height / divider;
    const unsigned int bottom_height = height - top_height;

    const unsigned int top_y_step = bar_wrap_step(args[1], top_height);
    const unsigned int bot_y_step = bar_wrap_step(args[2], bottom_height);
    const unsigned int top_x_step = bar_wrap_step(args[3], width);
    const unsigned int bot_x_step = bar_wrap_step(args[4], width);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    bar->bar_top_auto_y = (bar->bar_top_auto_y + top_y_step) % top_height;
    bar->bar_top_auto_x = (bar->bar_top_auto_x + top_x_step) % width;

    for(unsigned int y = 0; y < top_height; y++) {
        const unsigned int src_y = (y + bar->bar_top_auto_y) % top_height;

        uint8_t *restrict dY = Y + y * width;
        uint8_t *restrict dCb = Cb + y * width;
        uint8_t *restrict dCr = Cr + y * width;

        const uint8_t *restrict sY = Y2 + src_y * width;
        const uint8_t *restrict sCb = Cb2 + src_y * width;
        const uint8_t *restrict sCr = Cr2 + src_y * width;

        bar_copy_wrap_row(dY, sY, width, bar->bar_top_auto_x);
        bar_copy_wrap_row(dCb, sCb, width, bar->bar_top_auto_x);
        bar_copy_wrap_row(dCr, sCr, width, bar->bar_top_auto_x);
    }

    if(bottom_height > 0) {
        const unsigned int bottom_start = top_height * width;

        bar->bar_bot_auto_y = (bar->bar_bot_auto_y + bot_y_step) % bottom_height;
        bar->bar_bot_auto_x = (bar->bar_bot_auto_x + bot_x_step) % width;

        for(unsigned int y = 0; y < bottom_height; y++) {
            const unsigned int src_y = (y + bar->bar_bot_auto_y) % bottom_height;

            uint8_t *restrict dY = Y + bottom_start + y * width;
            uint8_t *restrict dCb = Cb + bottom_start + y * width;
            uint8_t *restrict dCr = Cr + bottom_start + y * width;

            const uint8_t *restrict sY = Y2 + bottom_start + src_y * width;
            const uint8_t *restrict sCb = Cb2 + bottom_start + src_y * width;
            const uint8_t *restrict sCr = Cr2 + bottom_start + src_y * width;

            bar_copy_wrap_row(dY, sY, width, bar->bar_bot_auto_x);
            bar_copy_wrap_row(dCb, sCb, width, bar->bar_bot_auto_x);
            bar_copy_wrap_row(dCr, sCr, width, bar->bar_bot_auto_x);
        }
    }
}
