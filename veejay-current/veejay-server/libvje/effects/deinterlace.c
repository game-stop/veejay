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
#include "deinterlace.h"

vj_effect *deinterlace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 64;
    ve->defaults[0] = 8;

    ve->description = "Deinterlace";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Motion threshold");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 2, 40, 48, 82, 60, 900, 0, 1, 80, VJ_BEAT_COST_CHEAP, 62, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void deinterlace_plane(uint8_t *restrict src, int w, int h, int threshold)
{
    for(int y = 1; y < h - 1; y++)
    {
        uint8_t *restrict prev = src + (y - 1) * w;
        uint8_t *restrict curr = src + y * w;
        uint8_t *restrict next = src + (y + 1) * w;

        for(int x = 0; x < w; x++)
        {
            const unsigned p = prev[x];
            const unsigned n = next[x];
            const unsigned diff = (p > n) ? (p - n) : (n - p);

            if(diff >= (unsigned)threshold)
                curr[x] = (uint8_t)((p + n) >> 1);
        }
    }
}

void deinterlace_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int threshold = args[0];

    deinterlace_plane(frame->data[0], frame->width, frame->height, threshold);
    deinterlace_plane(frame->data[1], frame->uv_width, frame->uv_height, threshold);
    deinterlace_plane(frame->data[2], frame->uv_width, frame->uv_height, threshold);
}
