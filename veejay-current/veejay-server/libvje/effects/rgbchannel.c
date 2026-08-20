/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include "rgbchannel.h"

#define RGBCHANNEL_PARAMS 3

#define P_RED   0
#define P_GREEN 1
#define P_BLUE  2

vj_effect *rgbchannel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RGBCHANNEL_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_RED],   P_RED,   "Keep", "Suppress");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_GREEN], P_GREEN, "Keep", "Suppress");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_BLUE],  P_BLUE,  "Keep", "Suppress");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void rgbchannel_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const uint8_t r_mask = args[P_RED]   ? 0x00 : 0xff;
    const uint8_t g_mask = args[P_GREEN] ? 0x00 : 0xff;
    const uint8_t b_mask = args[P_BLUE]  ? 0x00 : 0xff;

    if((r_mask & g_mask & b_mask) == 0xff)
        return;

    const int pixels = frame->width * frame->height;
    const int n_threads = vje_advise_num_threads(pixels);

    uint8_t *restrict rgba = frame->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < pixels; i++) {
        uint8_t *restrict p = rgba + (i << 2);

        p[0] &= r_mask;
        p[1] &= g_mask;
        p[2] &= b_mask;
    }
}
