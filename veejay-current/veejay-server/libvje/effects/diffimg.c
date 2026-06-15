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
#include "diffimg.h"

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *diffimg_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 6;   ve->defaults[0] = 6;
    ve->limits[0][1] = 1; ve->limits[1][1] = 255; ve->defaults[1] = 15;
    ve->limits[0][2] = 1; ve->limits[1][2] = 255; ve->defaults[2] = 240;

    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Min threshold", "Max threshold");
    ve->description = "Enhanced Magic Blend";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Negation", "Minimum", "Maximum", "Lenhth", "None", "Quantize", "Negation2");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  88,                 10, 38, 1000, 3400, 0,    68,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     145,                255,                10, 38, 1000, 3400, 0,    68
    );

    return ve;
}

void diffimg_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    const int type = args[0];
    int threshold_min = args[1];
    int threshold_max = args[2];
    const int len = frame->len;

    if(threshold_min > threshold_max)
    {
        const int t = threshold_min;
        threshold_min = threshold_max;
        threshold_max = t;
    }

    uint8_t *restrict Y = frame->data[0];
    _pf _pff = _get_pf(type);

    if(!_pff)
        return;

    const int n_threads = vje_advise_num_threads(len);
    const int lo = pixel_Y_lo_ ? pixel_Y_lo_ : 1;
    const int hi = pixel_Y_hi_;
    const int range = threshold_max - threshold_min + 1;
    const int out_range = hi - lo;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const uint8_t y = Y[i];

        if(y >= threshold_min && y <= threshold_max)
        {
            int y_calc = lo + (((int)y - threshold_min) * out_range) / range;

            y_calc = clampi(y_calc, lo, hi);
            Y[i] = _pff((uint8_t)y_calc, y);
        }
    }
}
