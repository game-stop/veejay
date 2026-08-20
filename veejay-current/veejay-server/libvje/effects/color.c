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
#include "color.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *color_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 128;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 128;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255; ve->defaults[2] = 128;

    ve->sub_format = -1;
    ve->description = "Color Vibrance";
    ve->param_description = vje_build_param_list(ve->num_params, "Vibrance", "Blue/Yellow Bias", "Red/Green Bias");
    ve->has_user = 0;
    ve->extra_frame = 0;

    
{
    const vj_beat_param_hint_t beat_hints[] = {
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 80, 255, 84, 100, 20, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_BIPOLAR, VJ_BEAT_CURVE_SMOOTHSTEP, 64, 224, 56, 88, 60, 760, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, 64, 224, 68, 96, 0, 620, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0)
    };
    ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
}

    return ve;
}

void color_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;
    const int vibrance = args[0];
    const int bias_u = args[1];
    const int bias_v = args[2];
    const int uv_len = frame->ssm ? frame->len : frame->uv_len;

    const int n_threads = vje_advise_num_threads(uv_len);

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < uv_len; i++)
    {
        int cb = (int)Cb[i] - 128;
        int cr = (int)Cr[i] - 128;
        const int mag = abs(cb) + abs(cr);
        const int norm = mag >> 1;
        const int curve = 255 - ((norm * norm) >> 8);
        const int boost = (vibrance * curve) >> 8;

        cb += (cb * boost) >> 8;
        cr += (cr * boost) >> 8;

        cb = (cb * bias_u) >> 8;
        cr = (cr * bias_v) >> 8;

        Cb[i] = (uint8_t)clampi(cb + 128, 0, 255);
        Cr[i] = (uint8_t)clampi(cr + 128, 0, 255);
    }
}
