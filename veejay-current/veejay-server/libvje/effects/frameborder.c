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
#include "frameborder.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *frameborder_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    const int max_size = height >> 1;
    const int def_size = clampi(width >> 3, 1, max_size);
    const int soft_hi = clampi(height >> 2, 1, max_size);

    ve->defaults[0] = def_size;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = max_size;

    ve->has_user = 0;
    ve->description = "Frame Border Translation";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Size");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 4, soft_hi, 4, 14, 2800, 7800, 2200, 22
    );

    return ve;
}

void frameborder_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int size = args[0];

    frameborder_yuvdata(frame->data[0], frame->data[1], frame->data[2],
                        frame2->data[0], frame2->data[1], frame2->data[2],
                        frame->width, frame->height, size, size, size, size,
                        frame->shift_h, frame->shift_v);
}
