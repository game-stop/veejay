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
#include "colormap.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *colormap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 46;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 109;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 92;

    ve->description = "Color Harmony";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Red", "Green", "Blue");

        
{
    const vj_beat_param_hint_t beat_hints[] = {
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 16, 238, 78, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_MID_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 16, 230, 64, 94, 80, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 16, 238, 78, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0)
    };
    ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
}

    return ve;
}

void colormap_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int r = args[0];
    const int g = args[1];
    const int b = args[2];
    const int uv_len = frame->ssm ? frame->len : frame->uv_len;

    const int n_threads = vje_advise_num_threads(uv_len);

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t u_table[256];
    uint8_t v_table[256];

    for(int i = 0; i < 256; i++)
    {
        u_table[i] = (uint8_t)clampi(i + b - g, 0, 255);
        v_table[i] = (uint8_t)clampi(i + r - g, 0, 255);
    }

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < uv_len; i++)
    {
        Cb[i] = u_table[Cb[i]];
        Cr[i] = v_table[Cr[i]];
    }
}
