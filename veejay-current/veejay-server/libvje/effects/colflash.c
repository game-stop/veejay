/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include "colflash.h"

typedef struct {
    int color_flash_;
    int color_delay_;
    int delay_;
} colflash_t;


vj_effect *colflash_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 50;  ve->defaults[0] = 5;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 0;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 0;
    ve->limits[0][3] = 0; ve->limits[1][3] = 255; ve->defaults[3] = 0;
    ve->limits[0][4] = 1; ve->limits[1][4] = 10;  ve->defaults[4] = 3;

    ve->description = "Color Flash";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->rgb_conv = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Frametime", "Red", "Green", "Blue", "Delay");

    
{
    const vj_beat_param_hint_t beat_hints[] = {
        VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BPM, VJ_BEAT_OP_BEAT_TIME, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 2, 30, 100, 100, 0, 0, 0, 1, 180, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_KICK, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 92, 100, 0, 260, 0, 1, 80, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_SNARE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_MID_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 88, 100, 0, 300, 0, 1, 80, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_HAT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 255, 84, 100, 0, 220, 0, 1, 80, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 1, 6, 78, 100, 0, 300, 0, 1, 120, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0)
    };
    ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
}

    return ve;
}

void *colflash_malloc(int w, int h)
{
    colflash_t *c = (colflash_t*) vj_calloc(sizeof(colflash_t));

    (void) w;
    (void) h;

    return c;
}

void colflash_free(void *ptr)
{
    colflash_t *c = (colflash_t*) ptr;

    if(c)
        free(c);
}

void colflash_apply(void *ptr, VJFrame *frame, int *args)
{
    colflash_t *c = (colflash_t*) ptr;

    const int f = args[0];
    const int r = args[1];
    const int g = args[2];
    const int b = args[3];
    const int d = args[4];
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    if(d != c->delay_)
    {
        c->delay_ = d;
        c->color_delay_ = d;
        c->color_flash_ = 0;
    }

    if(c->color_delay_ > 0)
    {
        int y = 0;
        int u = 128;
        int v = 128;

        _rgb2yuv(r, g, b, y, u, v);

        veejay_memset(frame->data[0], y, len);
        veejay_memset(frame->data[1], u, uv_len);
        veejay_memset(frame->data[2], v, uv_len);

        c->color_delay_--;
        return;
    }

    c->color_flash_++;

    if(c->color_flash_ >= f)
    {
        c->color_delay_ = c->delay_;
        c->color_flash_ = 0;
    }
}
