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
#include <math.h>

#define RGBKEY_PARAMS 8

#define P_HUE_ANGLE  0
#define P_RED        1
#define P_GREEN      2
#define P_BLUE       3
#define P_THRESHOLD  4
#define P_SOLIDITY   5
#define P_SPILL_KILL 6
#define P_MODE       7

#define RGBKEY_SCALE 4096
#define RGBKEY_DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *rgbkey_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RGBKEY_PARAMS;
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

    ve->defaults[P_HUE_ANGLE] = 4500;
    ve->defaults[P_RED] = 0;
    ve->defaults[P_GREEN] = 255;
    ve->defaults[P_BLUE] = 0;
    ve->defaults[P_THRESHOLD] = 40;
    ve->defaults[P_SOLIDITY] = 160;
    ve->defaults[P_SPILL_KILL] = 200;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_HUE_ANGLE] = 500;  ve->limits[1][P_HUE_ANGLE] = 8500;
    ve->limits[0][P_RED] = 0;          ve->limits[1][P_RED] = 255;
    ve->limits[0][P_GREEN] = 0;        ve->limits[1][P_GREEN] = 255;
    ve->limits[0][P_BLUE] = 0;         ve->limits[1][P_BLUE] = 255;
    ve->limits[0][P_THRESHOLD] = 0;    ve->limits[1][P_THRESHOLD] = 255;
    ve->limits[0][P_SOLIDITY] = 1;     ve->limits[1][P_SOLIDITY] = 255;
    ve->limits[0][P_SPILL_KILL] = 0;   ve->limits[1][P_SPILL_KILL] = 255;
    ve->limits[0][P_MODE] = 0;         ve->limits[1][P_MODE] = 1;

    ve->description = "Advanced Chroma Key";
    ve->has_user = 0;
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Hue Angle",
        "Red",
        "Green",
        "Blue",
        "Threshold",
        "Solidity",
        "Spill Kill",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Composite", "Mask Only");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 40, 600, 72, 100, 0, 240, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 20, 190, 88, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 80, 255, 78, 100, 4, 420, 24, 1, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 72, 255, 68, 96, 120, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}


void rgbkey_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void)ptr;

    const int hue_angle_arg = args[P_HUE_ANGLE];
    const int threshold_arg = args[P_THRESHOLD];
    const int solidity_arg = args[P_SOLIDITY];
    const int spill_kill_arg = args[P_SPILL_KILL];
    const int mode_arg = args[P_MODE];

    int iy, iu, iv;

    _rgb2yuv(args[P_RED], args[P_GREEN], args[P_BLUE], iy, iu, iv);

    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;

    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);

    if(mag_f < 1.0f)
        mag_f = 1.0f;

    const int mag_fp = (int)(mag_f * RGBKEY_SCALE);
    const int cos_q_fp = (int)((ut_f / mag_f) * RGBKEY_SCALE);
    const int sin_q_fp = (int)((vt_f / mag_f) * RGBKEY_SCALE);

    const float angle_rad = ((float)hue_angle_arg / 100.0f) * (3.14159265f / 180.0f);
    const int inv_wedge_slope_fp = (int)((1.0f / tanf(angle_rad)) * RGBKEY_SCALE);

    const int threshold = clampi(threshold_arg, 0, 255);
    const int solidity = clampi(solidity_arg, 1, 255);
    const int spill_kill = clampi(spill_kill_arg, 0, 255);
    const int mode = clampi(mode_arg, 0, 1);

    const float diff = (float)solidity - (float)threshold;
    const int inv_range_fp = (int)((255.0f / (diff < 1.0f ? 1.0f : diff)) * (1 << 8));
    const int black_clip_fp = threshold * RGBKEY_SCALE;
    const int spill_factor_fp = (int)((((float)spill_kill / 255.0f) / mag_f) * RGBKEY_SCALE);

    const int ut = (int)ut_f;
    const int vt = (int)vt_f;
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int pos = 0; pos < len; pos++) {
        const int uc = (int)Cb[pos] - 128;
        const int vc = (int)Cr[pos] - 128;
        const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
        const int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
        const int abs_yy = yy < 0 ? -yy : yy;
        const int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
        int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

        if(mode != 0) {
            Y[pos] = (uint8_t)clampi(alpha, 0, 255);
            Cb[pos] = 128;
            Cr[pos] = 128;
            continue;
        }

        if(LIKELY(alpha <= 0)) {
            Y[pos] = Y2[pos];
            Cb[pos] = Cb2[pos];
            Cr[pos] = Cr2[pos];
            continue;
        }

        if(UNLIKELY(alpha > 255))
            alpha = 255;

        const int invA = 255 - alpha;

        int cb_c = Cb[pos];
        int cr_c = Cr[pos];

        if(xx > 0) {
            int suppress_fp = xx * spill_factor_fp;

            if(suppress_fp > RGBKEY_SCALE)
                suppress_fp = RGBKEY_SCALE;

            cb_c -= (suppress_fp * ut) >> 12;
            cr_c -= (suppress_fp * vt) >> 12;

            cb_c = clampi(cb_c, 0, 255);
            cr_c = clampi(cr_c, 0, 255);
        }

        Y[pos] = RGBKEY_DIV255(Y[pos] * alpha + Y2[pos] * invA);
        Cb[pos] = RGBKEY_DIV255(cb_c * alpha + Cb2[pos] * invA);
        Cr[pos] = RGBKEY_DIV255(cr_c * alpha + Cr2[pos] * invA);
    }
}
