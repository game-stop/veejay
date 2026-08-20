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
#include "rawval.h"

#define RAWVAL_PARAMS 4

#define P_OLD_CB 0
#define P_OLD_CR 1
#define P_NEW_CB 2
#define P_NEW_CR 3

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *rawval_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RAWVAL_PARAMS;
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

    ve->defaults[P_OLD_CB] = 232;
    ve->defaults[P_OLD_CR] = 16;
    ve->defaults[P_NEW_CB] = 16;
    ve->defaults[P_NEW_CR] = 16;

    for(int i = 0; i < ve->num_params; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->sub_format = -1;
    ve->description = "Raw Chroma Pixel Replacement";
    ve->has_user = 0;
    ve->extra_frame = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Old Cb",
        "Old Cr",
        "New Cb",
        "New Cr"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 16, 240, 72, 96, 100, 820, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_MID_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 16, 240, 72, 96, 100, 820, 0, 1, 0, VJ_BEAT_COST_CHEAP, 74, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void rawval_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const uint8_t old_cb = (uint8_t)args[P_OLD_CB];
    const uint8_t old_cr = (uint8_t)args[P_OLD_CR];
    const uint8_t new_cb = (uint8_t)args[P_NEW_CB];
    const uint8_t new_cr = (uint8_t)args[P_NEW_CR];

    const int uv_len = frame->ssm ? frame->len : frame->uv_len;
    const int n_threads = vje_advise_num_threads(uv_len);

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < uv_len; i++) {
        const int match = (Cb[i] == old_cb) & (Cr[i] == old_cr);

        if(match) {
            Cb[i] = new_cb;
            Cr[i] = new_cr;
        }
    }
}
