/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "rgbchannel.h"

vj_effect *rgbchannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    for(int i = 0; i < ve->num_params; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 1;
        ve->defaults[i] = 0;
    }

    ve->description = "RGB Channel";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->rgba_only = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Red",
        "Green",
        "Blue"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Keep", "Suppress");
    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1, "Keep", "Suppress");
    vje_build_value_hint_list(ve->hints, ve->limits[1][2], 2, "Keep", "Suppress");
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000, /* Red */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000, /* Green */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000  /* Blue */
    );

    (void) w;
    (void) h;

    return ve;
}

void rgbchannel_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[0])
        return;

    const int kill_r = args[0] ? 1 : 0;
    const int kill_g = args[1] ? 1 : 0;
    const int kill_b = args[2] ? 1 : 0;

    if(!kill_r && !kill_g && !kill_b)
        return;

    const int pixels = frame->width * frame->height;
    if(pixels <= 0)
        return;

    uint8_t *restrict rgba = frame->data[0];
    const int n_threads = vje_advise_num_threads(pixels);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < pixels; i++) {
        uint8_t *p = rgba + (i << 2);

        if(kill_r)
            p[0] = 0;
        if(kill_g)
            p[1] = 0;
        if(kill_b)
            p[2] = 0;
    }
}