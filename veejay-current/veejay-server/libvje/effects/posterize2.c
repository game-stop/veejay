/* 
 * Linux VeeJay
 *
 * Copyright(C)2018 Niels Elburg <nwelburg@gmail.com>
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
#include "posterize2.h"

#define POSTERIZE2_PARAMS 4

#define P_FACTOR 0
#define P_TMIN   1
#define P_TMAX   2
#define P_MODE   3

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *posterize2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = POSTERIZE2_PARAMS;
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

    ve->defaults[P_FACTOR] = 4;
    ve->defaults[P_TMIN] = 16;
    ve->defaults[P_TMAX] = 235;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_FACTOR] = 1; ve->limits[1][P_FACTOR] = 256;
    ve->limits[0][P_TMIN] = 0;   ve->limits[1][P_TMIN] = 256;
    ve->limits[0][P_TMAX] = 0;   ve->limits[1][P_TMAX] = 256;
    ve->limits[0][P_MODE] = 0;   ve->limits[1][P_MODE] = 5;

    ve->description = "Posterize II (Threshold Range)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Factor",
        "Min Threshold",
        "Max Threshold",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Posterize, black outside",
        "Posterize, desaturate inside",
        "Posterize, clamp outside",
        "Alpha low outside",
        "Alpha from inside",
        "Alpha from clamped posterize"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 3, 32, 88, 100, 12, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 118, 84, 100, 10, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 1, 0, VJ_BEAT_GROUP_ASCENDING, 16),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 132, 248, 84, 100, 10, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 1, 1, VJ_BEAT_GROUP_ASCENDING, 16),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static inline void posterize2_build_luts(uint8_t *restrict lut_y,
                                         uint8_t *restrict lut_a,
                                         uint8_t *restrict mask,
                                         int factor_arg,
                                         int tmin,
                                         int tmax,
                                         int mode)
{
    const int step = 256 / factor_arg;

    for(int i = 0; i < 256; i++) {
        const uint8_t q = (uint8_t)((i / step) * step);

        lut_y[i] = q;
        lut_a[i] = q;
        mask[i] = 0;

        switch(mode) {
            case 0:
                if(q < tmin || q > tmax) {
                    lut_y[i] = pixel_Y_lo_;
                    mask[i] = 1;
                }
                break;
            case 1:
                if(q >= tmin && q <= tmax)
                    mask[i] = 1;
                break;
            case 2:
                if(q < tmin) {
                    lut_y[i] = pixel_Y_lo_;
                    mask[i] = 1;
                }
                else if(q > tmax) {
                    lut_y[i] = pixel_Y_hi_;
                    mask[i] = 1;
                }
                break;
            case 3:
                if(q < tmin || q > tmax) {
                    lut_a[i] = pixel_Y_lo_;
                    mask[i] = 1;
                }
                break;
            case 4:
                if(q >= tmin && q <= tmax)
                    mask[i] = 1;
                break;
            case 5:
                if(q < tmin)
                    lut_a[i] = pixel_Y_lo_;
                else if(q > tmax)
                    lut_a[i] = pixel_Y_hi_;
                else
                    lut_a[i] = q;
                mask[i] = 1;
                break;
        }
    }
}

void posterize2_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    int factor = args[P_FACTOR];
    int tmin = args[P_TMIN];
    int tmax = args[P_TMAX];
    const int mode = args[P_MODE];

    if(tmax < tmin) {
        const int tmp = tmin;
        tmin = tmax;
        tmax = tmp;
    }

    uint8_t lut_y[256];
    uint8_t lut_a[256];
    uint8_t mask[256];

    posterize2_build_luts(lut_y, lut_a, mask, factor, tmin, tmax, mode);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

#pragma omp parallel num_threads(n_threads)
    {
        if(mode <= 2) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t y = Y[i];

                Y[i] = lut_y[y];

                if(mask[y]) {
                    Cb[i] = 128;
                    Cr[i] = 128;
                }
            }
        }
        else if(mode == 5) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
                A[i] = lut_a[Y[i]];
        }
        else {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t y = Y[i];

                if(mask[y])
                    A[i] = lut_a[y];
            }
        }
    }
}
