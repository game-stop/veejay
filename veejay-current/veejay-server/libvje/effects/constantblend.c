/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include "internal.h"
#include "constantblend.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *constantblend_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;           ve->limits[1][0] = VJ_EFFECT_BLEND_COUNT;     ve->defaults[0] = 0;
    ve->limits[0][1] = 1;           ve->limits[1][1] = 500;                       ve->defaults[1] = 110;
    ve->limits[0][2] = pixel_Y_lo_; ve->limits[1][2] = pixel_Y_hi_;               ve->defaults[2] = 16;

    ve->description = "Constant Luminance Blend";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Luma Scale", "Constant");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, VJ_EFFECT_BLEND_STRINGS);

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                  55,                 360,                14, 58,  800, 2800, 0,    84,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                  pixel_Y_lo_,        225,                10, 42, 1100, 3600, 0,    56
    );
    return ve;
}

void constantblend_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int type = args[0];
    const int scale = args[1];
    const int valY = args[2];
    const int len = frame->len;

    const uint8_t y = (uint8_t)valY;
    const int s_fp = (scale * 256) / 100;
    const int n_threads = vje_advise_num_threads(len);

    pix_func_Y blend_y = get_pix_func_Y(type);
    uint8_t *restrict Y = frame->data[0];

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        int tmp_val = ((int)Y[i] * s_fp) >> 8;

        Y[i] = blend_y((uint8_t)clampi(tmp_val, 0, 255), y);
    }
}
